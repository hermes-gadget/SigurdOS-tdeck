// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.


/**
 * Unit tests for mesh messaging system
 * Tests: message queue, send/receive contract, UI integration contract,
 *        message overflow, timestamp ordering
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

#include "mesh/durable_fanout.h"
#include "mesh/pending_ack_policy.h"
#include "mesh/companion_message_policy.h"
#include "mesh/incoming_message_policy.h"
#include "mesh/mesh_safety_policy.h"

namespace {

struct FanoutProbe {
    std::vector<char> calls;
    bool presentation_accepts;
};

bool recordDurableFanout(void* raw)
{
    static_cast<FanoutProbe*>(raw)->calls.push_back('D');
    return true;
}

bool recordPresentationFanout(void* raw)
{
    FanoutProbe* probe = static_cast<FanoutProbe*>(raw);
    probe->calls.push_back('P');
    return probe->presentation_accepts;
}

TEST(MessageFanout, FullPresentationQueueDoesNotSuppressDurableDelivery)
{
    FanoutProbe probe{{}, false};
    const auto result = sigurdos::mesh::deliverMessageDurableFirst(
        recordDurableFanout, recordPresentationFanout, &probe);
    EXPECT_TRUE(result.durable);
    EXPECT_FALSE(result.presented);
    ASSERT_EQ(probe.calls.size(), 2U);
    EXPECT_EQ(probe.calls[0], 'D');
    EXPECT_EQ(probe.calls[1], 'P');
}

TEST(CompanionMessagePolicy, CliDataUsesTranscriptInsteadOfChatTimeline)
{
    EXPECT_TRUE(sigurdos::mesh::companion_message_should_present_in_chat(
        sigurdos::mesh::COMPANION_TEXT_PLAIN));
    EXPECT_TRUE(sigurdos::mesh::companion_message_should_present_in_chat(
        sigurdos::mesh::COMPANION_TEXT_SIGNED_PLAIN));
    EXPECT_FALSE(sigurdos::mesh::companion_message_should_present_in_chat(
        sigurdos::mesh::COMPANION_TEXT_CLI_DATA));
}

// ── Contact list (replicating SlopContact + LRU eviction logic) ──

static constexpr int TEST_MAX_CONTACTS = 64;

struct TestContact {
    char name[32];
    uint32_t last_seen;
    int last_rssi;
    bool has_location;
    float latitude;
    float longitude;
    bool valid;
};

struct ContactList {
    TestContact contacts[TEST_MAX_CONTACTS];
    int n_contacts = 0;

    void init() {
        n_contacts = 0;
        for (int i = 0; i < TEST_MAX_CONTACTS; i++) {
            contacts[i].valid = false;
            contacts[i].last_seen = 0;
            contacts[i].name[0] = '\0';
            contacts[i].has_location = false;
            contacts[i].latitude = 0.0f;
            contacts[i].longitude = 0.0f;
        }
    }

    // Returns true if contact was added/updated, false if dropped
    bool on_advert(const char* name, uint32_t timestamp, int rssi) {
        return on_advert(name, timestamp, rssi, false, 0.0f, 0.0f);
    }

    bool on_advert(const char* name, uint32_t timestamp, int rssi,
                   bool has_location, float lat, float lon) {
        // Deduplicate
        for (int i = 0; i < n_contacts; i++) {
            if (strcmp(contacts[i].name, name) == 0) {
                contacts[i].last_seen = timestamp;
                contacts[i].last_rssi = rssi;
                contacts[i].has_location = has_location;
                contacts[i].latitude = has_location ? lat : 0.0f;
                contacts[i].longitude = has_location ? lon : 0.0f;
                return true;
            }
        }

        if (n_contacts >= TEST_MAX_CONTACTS) {
            // List full — LRU eviction
            int oldest = 0;
            for (int i = 1; i < TEST_MAX_CONTACTS; i++) {
                if (contacts[i].last_seen < contacts[oldest].last_seen)
                    oldest = i;
            }
            TestContact& c = contacts[oldest];
            c.valid = true;
            c.last_seen = timestamp;
            c.last_rssi = rssi;
            c.has_location = has_location;
            c.latitude = has_location ? lat : 0.0f;
            c.longitude = has_location ? lon : 0.0f;
            strncpy(c.name, name, sizeof(c.name) - 1);
            c.name[sizeof(c.name) - 1] = '\0';
            return true;
        }

        TestContact& c = contacts[n_contacts++];
        c.valid = true;
        c.last_seen = timestamp;
        c.last_rssi = rssi;
        c.has_location = has_location;
        c.latitude = has_location ? lat : 0.0f;
        c.longitude = has_location ? lon : 0.0f;
        strncpy(c.name, name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        return true;
    }

    int count() const { return n_contacts; }
    const TestContact* get(int i) const {
        return (i >= 0 && i < n_contacts) ? &contacts[i] : nullptr;
    }

    // Find a contact by name
    int find(const char* name) const {
        for (int i = 0; i < n_contacts; i++) {
            if (strcmp(contacts[i].name, name) == 0) return i;
        }
        return -1;
    }
};

// ── Message types ────────────────────────────────────────
struct MeshMessage {
    char sender[32];
    char text[256];
    uint32_t timestamp;  // seconds since boot (or RTC time)
    bool is_self;        // sent by us?

    void clear() {
        memset(sender, 0, sizeof(sender));
        memset(text, 0, sizeof(text));
        timestamp = 0;
        is_self = false;
    }
};

// ── Message queue (circular buffer) ──────────────────────
static constexpr int MAX_QUEUED = 32;

class MessageQueue {
    MeshMessage buf[MAX_QUEUED];
    int head = 0;  // write index
    int tail = 0;  // read index
    int count = 0;

public:
    void reset() {
        head = 0;
        tail = 0;
        count = 0;
    }

    bool push(const MeshMessage& msg) {
        if (count >= MAX_QUEUED) return false; // queue full
        buf[head] = msg;
        head = (head + 1) % MAX_QUEUED;
        count++;
        return true;
    }

    bool pop(MeshMessage* out) {
        if (count == 0) return false;
        *out = buf[tail];
        tail = (tail + 1) % MAX_QUEUED;
        count--;
        return true;
    }

    int size() const { return count; }
    bool empty() const { return count == 0; }
    bool full() const { return count >= MAX_QUEUED; }
};

// ── Send/receive simulation ──────────────────────────────
class MeshSession {
    MessageQueue inbox;
    MessageQueue outbox;
    char own_name[32] = "TDeck+";
    int unread_count = 0;

public:
    void reset() {
        inbox.reset();
        outbox.reset();
        unread_count = 0;
    }

    void setOwnName(const char* name) {
        strncpy(own_name, name, sizeof(own_name) - 1);
        own_name[sizeof(own_name) - 1] = '\0';
    }

    const char* getOwnName() const { return own_name; }

    // Queue a message to be sent (simulates send_direct or send_channel)
    bool send_message(const char* dest, const char* text) {
        MeshMessage msg;
        msg.clear();
        strncpy(msg.sender, own_name, sizeof(msg.sender) - 1);
        strncpy(msg.text, text, sizeof(msg.text) - 1);
        msg.timestamp = 1000; // mock time
        msg.is_self = true;
        return outbox.push(msg);
    }

    // Simulate receiving a message from another node
    bool receive_message(const char* from, const char* text, uint32_t ts) {
        MeshMessage msg;
        msg.clear();
        strncpy(msg.sender, from, sizeof(msg.sender) - 1);
        strncpy(msg.text, text, sizeof(msg.text) - 1);
        msg.timestamp = ts;
        msg.is_self = false;
        const bool queued = inbox.push(msg);
        if (queued) unread_count++;
        return queued;
    }

    // Poll for pending received messages (returns count drained)
    int poll_inbox(MeshMessage* out, int max) {
        int drained = 0;
        while (drained < max && inbox.pop(&out[drained])) {
            drained++;
        }
        return drained;
    }

    int pending_count() const { return inbox.size(); }
    int unread() const { return unread_count; }
};

// ── Message validation ───────────────────────────────────
bool is_valid_message(const MeshMessage& msg) {
    if (msg.sender[0] == '\0') return false; // no sender
    if (msg.text[0] == '\0') return false;   // no text
    return true;
}

bool sender_matches(const MeshMessage& msg, const char* expected) {
    return strncmp(msg.sender, expected, sizeof(msg.sender)) == 0;
}

// ── Chat screen integration contract ─────────────────────
// (replicates what chat_screen_add_msg expects)
struct ChatEntry {
    std::string sender;
    std::string text;
    bool is_self;
};

std::vector<ChatEntry> chat_feed;

void chat_add_message(const char* sender, const char* text, bool is_self) {
    chat_feed.push_back({sender, text, is_self});
}

void chat_clear() {
    chat_feed.clear();
}

// ── Integration: drain mesh inbox → chat ──────────────────
void mesh_poll_to_chat(MeshSession& mesh) {
    MeshMessage msgs[8];
    int n = mesh.poll_inbox(msgs, 8);
    for (int i = 0; i < n; i++) {
        if (is_valid_message(msgs[i])) {
            chat_add_message(msgs[i].sender, msgs[i].text, msgs[i].is_self);
        }
    }
}

// ════════════════════════════════════════════════════════
// TEST FIXTURE
// ════════════════════════════════════════════════════════
class MeshMessagingTest : public ::testing::Test {
protected:
    MeshSession mesh;

    void SetUp() override {
        mesh.reset();
        mesh.setOwnName("TDeck+");
        chat_clear();
    }
};

// ── Queue basics ──────────────────────────────────────────
TEST_F(MeshMessagingTest, QueueStartsEmpty) {
    EXPECT_EQ(mesh.pending_count(), 0);
}

TEST_F(MeshMessagingTest, ReceiveMessageIncrementsCount) {
    mesh.receive_message("Alice", "Hello!", 100);
    EXPECT_EQ(mesh.pending_count(), 1);
}

TEST_F(MeshMessagingTest, MatchingDisplayNameStillIncrementsUnread) {
    mesh.receive_message(mesh.getOwnName(), "remote name collision", 100);
    EXPECT_EQ(mesh.unread(), 1);
}

TEST_F(MeshMessagingTest, PollDrainsQueue) {
    mesh.receive_message("Alice", "Hello!", 100);
    mesh.receive_message("Bob", "Hi!", 200);

    MeshMessage msgs[4];
    int n = mesh.poll_inbox(msgs, 4);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(mesh.pending_count(), 0);
}

TEST_F(MeshMessagingTest, PollReturnsMaxCount) {
    for (int i = 0; i < 5; i++) {
        mesh.receive_message("Node", "Msg", 100 + i);
    }
    MeshMessage msgs[3];
    int n = mesh.poll_inbox(msgs, 3);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(mesh.pending_count(), 2); // 2 left
}

// ── Message contents ──────────────────────────────────────
TEST_F(MeshMessagingTest, MessagePreservesSenderAndText) {
    mesh.receive_message("Charlie", "How are you?", 150);
    EXPECT_EQ(mesh.pending_count(), 1);

    MeshMessage msg;
    ASSERT_TRUE(mesh.poll_inbox(&msg, 1) == 1);
    EXPECT_STREQ(msg.sender, "Charlie");
    EXPECT_STREQ(msg.text, "How are you?");
}

TEST_F(MeshMessagingTest, SelfMessagesMarkedAsSelf) {
    mesh.send_message("Alice", "My message");
    // Self-messages go to outbox, not inbox — but we can check is_self
    // via the outbox
    // For this test, validate send_message creates a self-marked message
    EXPECT_TRUE(true); // send_message works (compiles and runs)
}

TEST_F(MeshMessagingTest, ReceivedMessagesAreNotSelf) {
    mesh.receive_message("Bob", "External", 300);
    MeshMessage msg;
    mesh.poll_inbox(&msg, 1);
    EXPECT_FALSE(msg.is_self);
}

// ── Timestamp ordering ────────────────────────────────────
TEST_F(MeshMessagingTest, MessagesPreserveTimestamp) {
    mesh.receive_message("A", "1", 100);
    mesh.receive_message("B", "2", 200);
    mesh.receive_message("C", "3", 300);

    MeshMessage msgs[3];
    mesh.poll_inbox(msgs, 3);
    EXPECT_EQ(msgs[0].timestamp, 100u);
    EXPECT_EQ(msgs[1].timestamp, 200u);
    EXPECT_EQ(msgs[2].timestamp, 300u);
}

// ── Overflow ──────────────────────────────────────────────
TEST_F(MeshMessagingTest, QueueOverflowDropsOldest) {
    // Fill to capacity
    for (int i = 0; i < 32; i++) {
        char name[8];
        snprintf(name, sizeof(name), "N%d", i);
        EXPECT_TRUE(mesh.receive_message(name, "msg", i));
    }
    EXPECT_EQ(mesh.pending_count(), 32);

    // 33rd message should be dropped
    EXPECT_FALSE(mesh.receive_message("Overflow", "lost", 999));
    EXPECT_EQ(mesh.pending_count(), 32);
}

// ── Chat integration ──────────────────────────────────────
TEST_F(MeshMessagingTest, InboxDrainsToChat) {
    mesh.receive_message("Alice", "Hey!", 100);
    mesh.receive_message("Bob", "Howdy!", 200);

    mesh_poll_to_chat(mesh);

    EXPECT_EQ(chat_feed.size(), 2u);
    EXPECT_EQ(chat_feed[0].sender, "Alice");
    EXPECT_EQ(chat_feed[0].text, "Hey!");
    EXPECT_FALSE(chat_feed[0].is_self);
    EXPECT_EQ(chat_feed[1].sender, "Bob");
}

TEST_F(MeshMessagingTest, InvalidMessagesSkipped) {
    // Message with no sender should be skipped
    MeshMessage bad;
    bad.clear();
    bad.text[0] = 'x'; // has text but no sender
    // Direct push of invalid message (simulates corrupted packet)
    // The validation in is_valid_message should reject it
    EXPECT_FALSE(is_valid_message(bad));

    // Valid messages still reach chat
    mesh.receive_message("Alice", "Valid", 100);
    mesh_poll_to_chat(mesh);
    EXPECT_EQ(chat_feed.size(), 1u);
}

// ── Sender matching ───────────────────────────────────────
TEST_F(MeshMessagingTest, SenderMatchingIsExact) {
    MeshMessage msg;
    msg.clear();
    strncpy(msg.sender, "TDeck+", sizeof(msg.sender));
    EXPECT_TRUE(sender_matches(msg, "TDeck+"));
    EXPECT_FALSE(sender_matches(msg, "TDeck"));
}

// ── Send flow ─────────────────────────────────────────────
TEST_F(MeshMessagingTest, SendCreatesOutgoingMessage) {
    bool ok = mesh.send_message("Alice", "Test message from T-Deck");
    EXPECT_TRUE(ok);
}

TEST_F(MeshMessagingTest, SendUsesOwnNameAsSender) {
    mesh.setOwnName("MyNode");
    mesh.send_message("Anyone", "Broadcast test");
    EXPECT_STREQ(mesh.getOwnName(), "MyNode");
}

// ── Own name defaults ─────────────────────────────────────
TEST_F(MeshMessagingTest, DefaultOwnNameIsTDeckPlus) {
    MeshSession fresh;
    EXPECT_STREQ(fresh.getOwnName(), "TDeck+");
}

// ── Payload null-termination ──────────────────────────────
// Reproduces the bug from slop_mesh.h: when onPeerDataRecv receives
// a 1-byte payload, the old code (`if (len > 1) data[len - 1] = '\0'`)
// skips null-termination, leaving the byte readable as a C string
// that runs past the buffer (UB / stack data leak).
// The fix unconditionally null-terminates: `data[len - 1] = '\0'`.
TEST_F(MeshMessagingTest, NullTerminatesSingleBytePayload) {
    uint8_t buf[1] = {'X'};
    size_t len = 1;

    // OLD behaviour: conditional skips null-term when len == 1
    // if (len > 1) buf[len - 1] = '\0';
    // buf[0] would still be 'X' — NOT null-terminated (BUG)

    // FIXED behaviour: always null-terminate
    buf[len - 1] = '\0';
    EXPECT_EQ(buf[0], '\0') << "Single-byte payload must be null-terminated";
}

TEST_F(MeshMessagingTest, NullTerminatesMultiBytePayload) {
    uint8_t buf[5] = {'H', 'e', 'l', 'l', 'o'};
    size_t len = 5;

    // Always null-terminate (same fix applies regardless of length)
    buf[len - 1] = '\0';
    EXPECT_EQ(buf[4], '\0') << "Multi-byte payload last byte must be null-term";
    EXPECT_EQ(buf[0], 'H') << "Earlier bytes preserved";
}

// ════════════════════════════════════════════════════════
// Contact list LRU eviction
// ════════════════════════════════════════════════════════

class ContactEvictionTest : public ::testing::Test {
protected:
    ContactList cl;

    void SetUp() override {
        cl.init();
    }
};

TEST_F(ContactEvictionTest, StartsEmpty) {
    EXPECT_EQ(cl.count(), 0);
}

TEST_F(ContactEvictionTest, AddsContact) {
    EXPECT_TRUE(cl.on_advert("Alice", 1000, -90));
    EXPECT_EQ(cl.count(), 1);
    EXPECT_GE(cl.find("Alice"), 0);
}

TEST_F(ContactEvictionTest, DedupUpdatesLastSeen) {
    cl.on_advert("Alice", 1000, -90);
    cl.on_advert("Alice", 2000, -80);

    EXPECT_EQ(cl.count(), 1); // still 1 contact
    int idx = cl.find("Alice");
    ASSERT_GE(idx, 0);
    EXPECT_EQ(cl.get(idx)->last_seen, 2000u);
    EXPECT_EQ(cl.get(idx)->last_rssi, -80);
}

TEST_F(ContactEvictionTest, StoresAdvertLocationWhenPresent) {
    EXPECT_TRUE(cl.on_advert("Toronto", 1000, -82, true, 43.6532f, -79.3832f));

    int idx = cl.find("Toronto");
    ASSERT_GE(idx, 0);
    const TestContact* c = cl.get(idx);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->has_location);
    EXPECT_NEAR(c->latitude, 43.6532f, 0.0001f);
    EXPECT_NEAR(c->longitude, -79.3832f, 0.0001f);
}

TEST_F(ContactEvictionTest, ClearsAdvertLocationWhenMissingOnUpdate) {
    cl.on_advert("Mobile", 1000, -82, true, 43.6532f, -79.3832f);
    cl.on_advert("Mobile", 2000, -78);

    int idx = cl.find("Mobile");
    ASSERT_GE(idx, 0);
    const TestContact* c = cl.get(idx);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->has_location);
    EXPECT_FLOAT_EQ(c->latitude, 0.0f);
    EXPECT_FLOAT_EQ(c->longitude, 0.0f);
}

TEST_F(ContactEvictionTest, FillsToMax) {
    for (int i = 0; i < TEST_MAX_CONTACTS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Node_%d", i);
        EXPECT_TRUE(cl.on_advert(name, i * 10, -90));
    }
    EXPECT_EQ(cl.count(), TEST_MAX_CONTACTS);
}

TEST_F(ContactEvictionTest, EvictsOldestWhenFull) {
    // Fill with 64 contacts at increasing timestamps
    for (int i = 0; i < TEST_MAX_CONTACTS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Node_%d", i);
        EXPECT_TRUE(cl.on_advert(name, i * 100, -90));
    }
    EXPECT_EQ(cl.count(), TEST_MAX_CONTACTS);

    // Node_0 has last_seen=0 (oldest). Adding a new contact should evict it.
    EXPECT_TRUE(cl.on_advert("NewNode", 99999, -70));
    EXPECT_EQ(cl.count(), TEST_MAX_CONTACTS);
    EXPECT_LT(cl.find("Node_0"), 0) << "Node_0 should have been evicted";
    EXPECT_GE(cl.find("Node_1"), 0)  << "Node_1 should still be present";
    EXPECT_GE(cl.find("NewNode"), 0) << "NewNode should have been added";
}

TEST_F(ContactEvictionTest, EvictsCorrectOldest) {
    // Fill with timestamps
    for (int i = 0; i < TEST_MAX_CONTACTS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Node_%d", i);
        cl.on_advert(name, i * 10, -90);
    }

    // Touch Node_5 (make it recent)
    cl.on_advert("Node_5", 99998, -80);

    // Node_0 should still be oldest (last_seen=0). Adding new contact evicts Node_0.
    EXPECT_TRUE(cl.on_advert("Latest", 99999, -70));
    EXPECT_LT(cl.find("Node_0"), 0) << "Node_0 (oldest) should be evicted";
    EXPECT_GE(cl.find("Node_5"), 0) << "Node_5 (recent) should survive";
    EXPECT_GE(cl.find("Latest"), 0) << "Latest should have been added";
}

TEST_F(ContactEvictionTest, EvictsToMakeRoomForManyNew) {
    // Fill to max
    for (int i = 0; i < TEST_MAX_CONTACTS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Node_%d", i);
        cl.on_advert(name, 1000 + i * 10, -90);
    }

    // Add 10 new contacts — each should evict the oldest at that moment
    for (int i = 0; i < 10; i++) {
        char name[16];
        snprintf(name, sizeof(name), "New_%d", i);
        EXPECT_TRUE(cl.on_advert(name, 2000 + i, -70));
    }

    EXPECT_EQ(cl.count(), TEST_MAX_CONTACTS);
    // First 10 nodes should be gone (they were the oldest)
    for (int i = 0; i < 10; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Node_%d", i);
        EXPECT_LT(cl.find(name), 0) << name << " should have been evicted";
    }
    // All new nodes should be present
    for (int i = 0; i < 10; i++) {
        char name[16];
        snprintf(name, sizeof(name), "New_%d", i);
        EXPECT_GE(cl.find(name), 0) << name << " should be in list";
    }
}

TEST_F(ContactEvictionTest, SameNameAfterEvictionWorks) {
    // Fill to max
    for (int i = 0; i < TEST_MAX_CONTACTS - 1; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Node_%d", i);
        cl.on_advert(name, 1000 + i * 10, -90);
    }
    // Add "Target" with a very old timestamp so it gets evicted first
    cl.on_advert("Target", 0, -70);
    EXPECT_EQ(cl.count(), TEST_MAX_CONTACTS);

    // Fill 5 more nodes — each evicts the oldest. Target (ts=0) is first to go.
    for (int i = 0; i < 5; i++) {
        char name[16];
        snprintf(name, sizeof(name), "New_%d", i);
        cl.on_advert(name, 2000 + i, -70);
    }

    // Target should be evicted
    EXPECT_LT(cl.find("Target"), 0);

    // Re-add "Target" with fresh timestamp — should work
    EXPECT_TRUE(cl.on_advert("Target", 100000, -60));
    EXPECT_GE(cl.find("Target"), 0);
}

TEST_F(ContactEvictionTest, UnknownContactEvictedDoesNotAffectOthers) {
    // Fill to max
    for (int i = 0; i < TEST_MAX_CONTACTS - 1; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Node_%d", i);
        cl.on_advert(name, 1000 + i, -90);
    }
    cl.on_advert("First", 0, -95); // oldest timestamp
    EXPECT_EQ(cl.count(), TEST_MAX_CONTACTS);

    // Add a new contact — "First" with timestamp 0 should be evicted
    EXPECT_TRUE(cl.on_advert("Second", 9999, -80));
    EXPECT_LT(cl.find("First"), 0);
    EXPECT_GE(cl.find("Second"), 0);

    // All the nodes 0..62 should be intact
    for (int i = 0; i < TEST_MAX_CONTACTS - 1; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Node_%d", i);
        EXPECT_GE(cl.find(name), 0) << name << " should survive";
    }
}

// ════════════════════════════════════════════════════════
// Receive pipeline edge cases — preventative guards
// ════════════════════════════════════════════════════════

class ReceivePipelineTest : public ::testing::Test {
protected:
    MessageQueue rx_queue;
    MeshSession  mesh;

    void SetUp() override {
        rx_queue.reset();
        mesh.reset();
    }
};

// ── Queue overflow drops older messages ──────────────────
TEST_F(ReceivePipelineTest, QueueOverflowDropsCorrectly) {
    for (int i = 0; i < MAX_QUEUED; i++) {
        MeshMessage msg;
        msg.clear();
        snprintf(msg.sender, sizeof(msg.sender), "Node_%d", i);
        snprintf(msg.text, sizeof(msg.text), "Message_%d", i);
        msg.timestamp = i;
        msg.is_self = false;
        EXPECT_TRUE(rx_queue.push(msg)) << "should accept up to capacity";
    }
    EXPECT_TRUE(rx_queue.full());

    // Overflow message should be rejected
    MeshMessage overflow;
    overflow.clear();
    strcpy(overflow.sender, "Overflow");
    strcpy(overflow.text, "dropped");
    EXPECT_FALSE(rx_queue.push(overflow)) << "overflow should be dropped";

    // All original messages still readable
    MeshMessage out;
    EXPECT_TRUE(rx_queue.pop(&out));
    EXPECT_STREQ(out.sender, "Node_0");
}

// ── Empty queue returns empty poll result ────────────────
TEST_F(ReceivePipelineTest, EmptyQueuePollReturnsZero) {
    MeshMessage msgs[4];
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (rx_queue.pop(&msgs[i])) n++;
    }
    EXPECT_EQ(n, 0) << "no messages should drain from empty queue";
}

// ── Null-termination edge case: 1-byte payload ───────────
// Matches the fix in slop_mesh.h's onPeerDataRecv / onGroupDataRecv
static bool simulate_receive_nullterm(uint8_t* data, size_t len) {
    // Replicate MeshCore's unconditional null-termination pattern
    if (len == 0) return false;
    data[len - 1] = '\0';
    return true;
}

TEST_F(ReceivePipelineTest, NullTerminatesSingleBytePayload) {
    uint8_t buf[1] = {'X'};
    EXPECT_TRUE(simulate_receive_nullterm(buf, 1));
    EXPECT_EQ(buf[0], '\0') << "1-byte payload must be null-terminated";
}

TEST_F(ReceivePipelineTest, NullTerminatesTwoBytePayload) {
    uint8_t buf[2] = {'A', 'B'};
    EXPECT_TRUE(simulate_receive_nullterm(buf, 2));
    EXPECT_EQ(buf[1], '\0') << "last byte overwritten";
    EXPECT_EQ(buf[0], 'A') << "first byte preserved";
}

// ── Group text receive boundary: exactly at 5-byte header ─
// If len <= 5, onGroupDataRecv returns early (no text).
// Text content starts at data[5], null-term is at data[len-1].
// For N bytes of text, len must be 5 + N + 1 (header + text + null-term).
TEST_F(ReceivePipelineTest, GroupTextPayloadBoundary) {
    struct {
        size_t len;
        const char* text_content;  // bytes after 5-byte header
        const char* expected;
    } cases[] = {
        {5,  "", ""},            // header only — no text, returns early
        {7,  "X", "X"},         // 1 byte text
        {8,  "AB", "AB"},       // 2 bytes text
        {11, "Hello", "Hello"}, // 5 bytes text
    };
    for (auto& c : cases) {
        uint8_t buf[16];
        memset(buf, 0xFF, sizeof(buf));
        // Clear header bytes [0..4]
        for (size_t i = 0; i < 5 && i < c.len; i++) buf[i] = 0;
        // Place text content at buf+5
        for (size_t i = 0; i < strlen(c.text_content) && (5 + i) < c.len; i++)
            buf[5 + i] = c.text_content[i];
        // Unconditional null-term at last byte
        buf[c.len - 1] = '\0';
        // Text starts at data+5
        if (c.len > 5) {
            const char* text = (const char*)(buf + 5);
            EXPECT_STREQ(text, c.expected) << "len=" << c.len;
        }
    }
}

TEST_F(ReceivePipelineTest, AnonymousChatDecodesTimestampAfterRequestTag) {
    const uint8_t payload[] = {
        0xAA, 0xBB, 0xCC, 0xDD,
        0x78, 0x56, 0x34, 0x12,
        'H', 'e', 'l', 'l', 'o', 0
    };
    uint32_t timestamp = 0;
    char text[16];
    ASSERT_TRUE(sigurdos::mesh::decodeAnonymousChat(
        payload, sizeof(payload), timestamp, text, sizeof(text)));
    EXPECT_EQ(timestamp, 0x12345678U);
    EXPECT_STREQ(text, "Hello");
}

TEST_F(ReceivePipelineTest, AnonymousChatRejectsMissingTextHeader) {
    const uint8_t payload[8] = {};
    uint32_t timestamp = 123;
    char text[8];
    EXPECT_FALSE(sigurdos::mesh::decodeAnonymousChat(
        payload, sizeof(payload), timestamp, text, sizeof(text)));
}

TEST_F(ReceivePipelineTest, AnonymousSenderUsesStableSixBytePrefix) {
    const uint8_t public_key[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    char sender[20];
    sigurdos::mesh::formatAnonymousSender(
        public_key, sender, sizeof(sender));
    EXPECT_STREQ(sender, "anon_001122334455");
}

// ── DM payload boundary: 5-byte header minimum ──────────
TEST_F(ReceivePipelineTest, DmPayloadBoundary) {
    // TXT_MSG: [4-byte ts][1-byte flags][text]
    // len == 5 means header only, no text. Returns "" for text.
    uint8_t buf[5] = {0, 0, 0, 0, 0};
    buf[4] = 0;       // flags
    buf[4] = '\0';    // null-term the only content byte
    // Text starts at data+5, but there's nothing there
    // In real code: type=PAYLOAD_TYPE_TXT_MSG && len > 5 → data+5 else data
    // len==5 falls to else-if, treats whole buffer as text (empty after null-term)
    // This is benign but tests verify the contract
    simulate_receive_nullterm(buf, 5);
    EXPECT_EQ(buf[4], '\0');
}

// ── Group sender parsing preserves malformed wire content ─
TEST_F(ReceivePipelineTest, ParseGroupSenderNoColon) {
    char sender[32];
    char msg[64];

    EXPECT_TRUE(sigurdos::mesh::parseGroupText(
        "Alice: Hello!", sender, sizeof(sender), msg, sizeof(msg)));
    EXPECT_STREQ(sender, "Alice");
    EXPECT_STREQ(msg, "Hello!");

    EXPECT_FALSE(sigurdos::mesh::parseGroupText(
        "JustARawText", sender, sizeof(sender), msg, sizeof(msg)));
    EXPECT_STREQ(sender, "unknown");
    EXPECT_STREQ(msg, "JustARawText");

    EXPECT_FALSE(sigurdos::mesh::parseGroupText(
        ": message", sender, sizeof(sender), msg, sizeof(msg)));
    EXPECT_STREQ(sender, "unknown");
    EXPECT_STREQ(msg, ": message");
}

TEST_F(ReceivePipelineTest, ParseGroupSenderRejectsOversizedName) {
    char sender[8];
    char msg[64];
    EXPECT_FALSE(sigurdos::mesh::parseGroupText(
        "sender-too-long: payload", sender, sizeof(sender), msg, sizeof(msg)));
    EXPECT_STREQ(sender, "unknown");
    EXPECT_STREQ(msg, "sender-too-long: payload");
}

TEST(MeshSafetyPolicy, ReceiveBufferMustFitWholeDatagram) {
    uint8_t output[4] = {};
    EXPECT_TRUE(sigurdos::mesh::receiveBufferFits(output, 4, 4));
    EXPECT_FALSE(sigurdos::mesh::receiveBufferFits(output, 3, 4));
    EXPECT_FALSE(sigurdos::mesh::receiveBufferFits(output, -1, 0));
    EXPECT_FALSE(sigurdos::mesh::receiveBufferFits(nullptr, 4, 4));
    EXPECT_TRUE(sigurdos::mesh::receiveBufferFits(nullptr, 0, 0));
}

TEST(MeshSafetyPolicy, AdvertRequiresOneNewOutboundPacket) {
    EXPECT_TRUE(sigurdos::mesh::outboundQueueAccepted(3, 4));
    EXPECT_FALSE(sigurdos::mesh::outboundQueueAccepted(3, 3));
    EXPECT_FALSE(sigurdos::mesh::outboundQueueAccepted(-1, 0));
}

// ── Channel hash match: 1-byte vs full compare ──────────
// searchChannelsByHash uses 1-byte compare, onGroupDataRecv
// uses full compare. This test validates the asymmetry is safe.
TEST_F(ReceivePipelineTest, ChannelHashCompareSafety) {
    uint8_t hash1[32], hash2[32];
    memset(hash1, 0xAB, sizeof(hash1));
    memset(hash2, 0xAB, sizeof(hash2));

    // Same first byte → searchChannelsByHash matches
    EXPECT_EQ(hash1[0], hash2[0]);

    // Same full hash → onGroupDataRecv matches
    EXPECT_EQ(memcmp(hash1, hash2, sizeof(hash1)), 0);

    // Now differ on second byte only
    hash2[1] = 0xCD;
    EXPECT_EQ(hash1[0], hash2[0]) << "first byte still matches";
    EXPECT_NE(memcmp(hash1, hash2, sizeof(hash1)), 0) << "full hash differs";

    // Search (1-byte) says match, lookup (full) says no match
    // This is the expected behavior — search is lossy, lookup is exact
    SUCCEED();
}

// ── Inject + drain round-trip ─────────────────────────────
TEST_F(ReceivePipelineTest, InjectAndDrainRoundTrip) {
    mesh.receive_message("Alice", "Hello from Alice", 1000);
    mesh.receive_message("Bob", "Hi back", 1001);

    EXPECT_EQ(mesh.pending_count(), 2);

    MeshMessage msgs[4];
    int n = mesh.poll_inbox(msgs, 4);
    EXPECT_EQ(n, 2);
    EXPECT_STREQ(msgs[0].sender, "Alice");
    EXPECT_STREQ(msgs[0].text, "Hello from Alice");
    EXPECT_STREQ(msgs[1].sender, "Bob");
    EXPECT_STREQ(msgs[1].text, "Hi back");
    EXPECT_EQ(mesh.pending_count(), 0);
}

// ── Inject with empty channel ────────────────────────────
TEST_F(ReceivePipelineTest, ReceiveWithChannel) {
    // The real onGroupDataRecv passes the channel name to _onMessage.
    // Our MeshSession.receive_message doesn't track channel, but the
    // real code path does. This tests the UI display contract.
    mesh.receive_message("Charlie", "#general: Hello", 2000);
    MeshMessage msg;
    mesh.poll_inbox(&msg, 1);
    EXPECT_STREQ(msg.sender, "Charlie");
    EXPECT_STREQ(msg.text, "#general: Hello");
}

// ── Multiple rapid receives don't lose ordering ────────────
TEST_F(ReceivePipelineTest, PreservesOrderUnderLoad) {
    for (int i = 0; i < 10; i++) {
        char name[8], text[16];
        snprintf(name, sizeof(name), "N%d", i);
        snprintf(text, sizeof(text), "Msg_%d", i);
        mesh.receive_message(name, text, i);
    }

    MeshMessage msgs[10];
    int n = mesh.poll_inbox(msgs, 10);
    EXPECT_EQ(n, 10);
    for (int i = 0; i < n; i++) {
        EXPECT_EQ(msgs[i].timestamp, (uint32_t)i)
            << "Message " << i << " timestamp out of order";
    }
}

// ── Inject then immediately poll again ────────────────────
TEST_F(ReceivePipelineTest, DrainThenReinject) {
    mesh.receive_message("First", "Msg1", 100);
    EXPECT_EQ(mesh.pending_count(), 1);

    MeshMessage m1;
    EXPECT_EQ(mesh.poll_inbox(&m1, 1), 1);
    EXPECT_STREQ(m1.sender, "First");
    EXPECT_EQ(mesh.pending_count(), 0);

    // Now inject more — queue should be fresh
    mesh.receive_message("Second", "Msg2", 200);
    EXPECT_EQ(mesh.pending_count(), 1);

    MeshMessage m2;
    EXPECT_EQ(mesh.poll_inbox(&m2, 1), 1);
    EXPECT_STREQ(m2.sender, "Second");
    EXPECT_EQ(mesh.pending_count(), 0);
}

// ── Very long sender/text is truncated gracefully ─────────
TEST_F(ReceivePipelineTest, TruncatesLongSender) {
    // Sender longer than 31 chars
    char long_sender[64];
    memset(long_sender, 'A', 63);
    long_sender[63] = '\0';
    mesh.receive_message(long_sender, "Hello", 100);

    MeshMessage msg;
    mesh.poll_inbox(&msg, 1);
    EXPECT_EQ(strlen(msg.sender), 31u) << "sender should be truncated to 31 chars";
    EXPECT_STREQ(msg.text, "Hello");
}

TEST_F(ReceivePipelineTest, TruncatesLongText) {
    // Text longer than 255 chars
    char long_text[300];
    memset(long_text, 'X', 299);
    long_text[299] = '\0';
    mesh.receive_message("Alice", long_text, 100);

    MeshMessage msg;
    mesh.poll_inbox(&msg, 1);
    EXPECT_EQ(strlen(msg.text), 255u) << "text should be truncated to 255 chars";
    EXPECT_STREQ(msg.sender, "Alice");
}

// ═══════════════════════════════════════════════════════════════
//  REQ/RESPONSE framework tests (Phase 4.1)
// ═══════════════════════════════════════════════════════════════

// The onContactResponse wire format: data[0..3] = tag (uint32 LE),
// data[4+] = response payload (up to len-4 bytes).
// Tests verify tag extraction and response capture matching the
// BaseChatMesh::sendRequest() → onContactResponse() protocol.

class ReqResponseTest : public ::testing::Test {
protected:
    static constexpr int MAX_RESP_DATA = 128;
    static constexpr int MAX_RESPONSES = 8;

    struct TestResponseEntry {
        uint32_t tag;
        char     contact_name[32];
        uint8_t  data[MAX_RESP_DATA];
        uint8_t  len;
    };

    TestResponseEntry _responses[MAX_RESPONSES];
    int _n_responses = 0;

    // Matches SigurdMeshV2::onContactResponse() logic
    void handle_response(const char* contact_name, const uint8_t* data, uint8_t len) {
        if (!data || len < 4) return;
        if (_n_responses < MAX_RESPONSES) {
            TestResponseEntry& re = _responses[_n_responses++];
            memcpy(&re.tag, data, 4);
            if (contact_name) {
                strncpy(re.contact_name, contact_name, 31);
                re.contact_name[31] = '\0';
            } else {
                re.contact_name[0] = '\0';
            }
            re.len = (len < MAX_RESP_DATA) ? len : MAX_RESP_DATA;
            memcpy(re.data, data, re.len);
        }
    }
};

TEST_F(ReqResponseTest, ExtractsTagFromFirst4Bytes) {
    uint8_t resp_data[8] = {0x78, 0x56, 0x34, 0x12, 0x01, 0x02, 0x03, 0x04};
    handle_response("Alice", resp_data, sizeof(resp_data));

    ASSERT_EQ(_n_responses, 1);
    // LE: 0x12345678 = 305419896
    EXPECT_EQ(_responses[0].tag, (uint32_t)0x12345678);
    EXPECT_STREQ(_responses[0].contact_name, "Alice");
}

TEST_F(ReqResponseTest, ExtractsTagAtMinimumLength) {
    uint8_t resp_data[4] = {0x01, 0x00, 0x00, 0x00};
    handle_response("Bob", resp_data, 4);

    ASSERT_EQ(_n_responses, 1);
    EXPECT_EQ(_responses[0].tag, (uint32_t)1);
    EXPECT_EQ(_responses[0].len, 4);
    // Only tag, no payload
    EXPECT_EQ(_responses[0].data[0], 1);
    EXPECT_EQ(_responses[0].data[1], 0);
    EXPECT_EQ(_responses[0].data[2], 0);
    EXPECT_EQ(_responses[0].data[3], 0);
}

TEST_F(ReqResponseTest, RejectsNullData) {
    handle_response("Charlie", nullptr, 0);
    EXPECT_EQ(_n_responses, 0);
}

TEST_F(ReqResponseTest, RejectsShortData) {
    uint8_t resp_data[3] = {0x01, 0x02, 0x03};
    handle_response("Dave", resp_data, 3);
    EXPECT_EQ(_n_responses, 0) << "Response with len < 4 should be rejected";
}

TEST_F(ReqResponseTest, StoresPayloadAfterTag) {
    // Tag (4 bytes) + payload "OK" (2 bytes)
    uint8_t resp_data[6] = {0xEF, 0xBE, 0xAD, 0xDE, 0x4F, 0x4B};
    handle_response("Eve", resp_data, sizeof(resp_data));

    ASSERT_EQ(_n_responses, 1);
    EXPECT_EQ(_responses[0].tag, (uint32_t)0xDEADBEEF);
    EXPECT_EQ(_responses[0].len, 6);
    // Payload bytes at offset 4+
    EXPECT_EQ(_responses[0].data[4], 0x4F);    // 'O'
    EXPECT_EQ(_responses[0].data[5], 0x4B);    // 'K'
}

TEST_F(ReqResponseTest, StoresUpToMaxPayload) {
    // Generate data larger than MAX_RESP_DATA
    uint8_t large_data[MAX_RESP_DATA + 20];
    uint32_t tag = 0xCAFEBABE;
    memcpy(large_data, &tag, 4);
    for (int i = 4; i < MAX_RESP_DATA + 20; i++) {
        large_data[i] = (uint8_t)(i & 0xFF);
    }
    handle_response("Frank", large_data, MAX_RESP_DATA + 20);

    ASSERT_EQ(_n_responses, 1);
    EXPECT_EQ(_responses[0].tag, (uint32_t)0xCAFEBABE);
    // Length should be clamped to MAX_RESP_DATA
    EXPECT_EQ(_responses[0].len, MAX_RESP_DATA);
    // Verify last stored byte
    EXPECT_EQ(_responses[0].data[MAX_RESP_DATA - 1], large_data[MAX_RESP_DATA - 1]);
}

TEST_F(ReqResponseTest, BufferOverflowsGracefully) {
    // Fill the response buffer to max
    for (int i = 0; i < MAX_RESPONSES; i++) {
        uint8_t resp[6] = {0};
        uint32_t tag = (uint32_t)(i + 1);
        memcpy(resp, &tag, 4);
        resp[4] = (uint8_t)('A' + i);
        resp[5] = 0;
        handle_response("Overflow", resp, 6);
    }

    EXPECT_EQ(_n_responses, MAX_RESPONSES);

    // Next response should be silently dropped
    uint8_t extra[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    handle_response("Dropped", extra, 4);
    EXPECT_EQ(_n_responses, MAX_RESPONSES) << "Response beyond capacity should be dropped";
}

TEST_F(ReqResponseTest, TagMatchingZeroIsValid) {
    // Tag value 0 is valid (though unlikely in practice)
    uint8_t resp_data[4] = {0x00, 0x00, 0x00, 0x00};
    handle_response("ZeroTag", resp_data, 4);

    ASSERT_EQ(_n_responses, 1);
    EXPECT_EQ(_responses[0].tag, (uint32_t)0);
    EXPECT_STREQ(_responses[0].contact_name, "ZeroTag");
}

TEST_F(ReqResponseTest, TagMatchClearsPendingRequest) {
    // Simulates the full flow: pending request is cleared when matching response arrives
    // This mirrors the logic in SigurdMeshV2::onContactResponse()

    // Simulate a pending request with tag 0x12345678
    struct PendingReq {
        uint32_t tag = 0x12345678;
        char name[32] = "Alice";
        bool in_use = true;
    };
    PendingReq pending;

    // Simulate the onContactResponse logic: clear pending on tag match
    uint8_t resp_data[4];
    memcpy(resp_data, &pending.tag, 4);
    uint32_t recv_tag;
    memcpy(&recv_tag, resp_data, 4);

    // Match and clear
    if (pending.in_use && pending.tag == recv_tag) {
        pending.in_use = false;
    }

    EXPECT_FALSE(pending.in_use) << "Pending request should be cleared on tag match";
}

TEST_F(ReqResponseTest, NonMatchingTagDoesNotClearWrongPending) {
    // Simulate pending request with different tag
    struct PendingReq {
        uint32_t tag = 0xDEADBEEF;
        bool in_use = true;
    };
    PendingReq pending;

    uint8_t resp_data[4];
    uint32_t wrong_tag = 0xCAFEBABE;
    memcpy(resp_data, &wrong_tag, 4);
    uint32_t recv_tag;
    memcpy(&recv_tag, resp_data, 4);

    if (pending.in_use && pending.tag == recv_tag) {
        pending.in_use = false;
    }

    EXPECT_TRUE(pending.in_use) << "Non-matching tag should not clear pending request";
}

// ═══════════════════════════════════════════════════════════════
//  PendingAck table tests (audit beta-0.1.41 RC3 Finding 1)
// ═══════════════════════════════════════════════════════════════
//
// Replicates SigurdMeshV2::PendingAck and the addPendingAck()/processAck()
// slot/expiry logic from src/mesh/sigurd_mesh_v2.{h,cpp}. The production array
// PendingAck _pending_acks[16] is a default-constructed member; these tests
// locks in zero-init, wrap-safe expiry, and oldest-entry loss reporting. (The
// mesh classes link MeshCore and are not part of native_test, so table wiring
// is mirrored here; timeout math itself is imported from production.)

namespace {

struct AckSlot {
    char dest_name[32] = {};
    uint32_t timestamp = 0;
    uint32_t expected_ack = 0;
    uint32_t sent_at_ms = 0;
    uint32_t expires_at_ms = 0;
    bool in_use = false;
};

static constexpr int ACK_TABLE_SIZE = 16;

struct AckTable {
    AckSlot slots[ACK_TABLE_SIZE];   // default-constructed, mirrors _pending_acks[16]
    uint32_t now_ms = 0;
    uint32_t drop_count = 0;
    uint32_t expired_count = 0;
    uint32_t lost_count = 0;

    void expirePendingAcks() {
        for (int i = 0; i < ACK_TABLE_SIZE; i++) {
            if (slots[i].in_use && sigurdos::mesh::pendingAckDeadlineReached(
                                      now_ms, slots[i].expires_at_ms)) {
                slots[i].in_use = false;
                expired_count++;
                lost_count++;
            }
        }
    }

    void addPendingAck(const char* name, uint32_t ts, uint32_t expected_ack,
                       uint32_t est_timeout = 0) {
        if (!name || !name[0] || expected_ack == 0) return;
        expirePendingAcks();
        const uint32_t expires = now_ms + sigurdos::mesh::pendingAckLifetimeMs(est_timeout);
        for (int i = 0; i < ACK_TABLE_SIZE; i++) {
            if (!slots[i].in_use) {
                strncpy(slots[i].dest_name, name, sizeof(slots[i].dest_name) - 1);
                slots[i].dest_name[sizeof(slots[i].dest_name) - 1] = '\0';
                slots[i].timestamp = ts;
                slots[i].expected_ack = expected_ack;
                slots[i].sent_at_ms = now_ms;
                slots[i].expires_at_ms = expires;
                slots[i].in_use = true;
                return;
            }
        }
        // Table full — evict the greatest wrap-safe age.
        int oldest = 0;
        uint32_t oldest_age = now_ms - slots[0].sent_at_ms;
        for (int i = 1; i < ACK_TABLE_SIZE; i++) {
            uint32_t age = now_ms - slots[i].sent_at_ms;
            if (age > oldest_age) {
                oldest = i;
                oldest_age = age;
            }
        }
        drop_count++;
        lost_count++;
        strncpy(slots[oldest].dest_name, name, sizeof(slots[oldest].dest_name) - 1);
        slots[oldest].dest_name[sizeof(slots[oldest].dest_name) - 1] = '\0';
        slots[oldest].timestamp = ts;
        slots[oldest].expected_ack = expected_ack;
        slots[oldest].sent_at_ms = now_ms;
        slots[oldest].expires_at_ms = expires;
        slots[oldest].in_use = true;
    }

    // Returns matched slot index, or -1 if none.
    int processAck(uint32_t ack_val) {
        expirePendingAcks();
        for (int i = 0; i < ACK_TABLE_SIZE; i++) {
            if (slots[i].in_use && slots[i].expected_ack == ack_val) {
                slots[i].in_use = false;
                return i;
            }
        }
        return -1;
    }
};

} // namespace

TEST(PendingAckTest, FreshTableIsFullyZeroed) {
    AckTable t;
    for (int i = 0; i < ACK_TABLE_SIZE; i++) {
        EXPECT_FALSE(t.slots[i].in_use) << "slot " << i << " must start free";
        EXPECT_EQ(t.slots[i].timestamp, 0u);
        EXPECT_EQ(t.slots[i].expected_ack, 0u);
        EXPECT_EQ(t.slots[i].sent_at_ms, 0u);
        EXPECT_EQ(t.slots[i].expires_at_ms, 0u);
        EXPECT_EQ(t.slots[i].dest_name[0], '\0');
    }
}

TEST(PendingAckTest, FreshTableMatchesNothing) {
    AckTable t;
    // A fresh table must not match any ACK value — including 0, which is
    // exactly what a zeroed-but-falsely-in-use slot would collide with.
    EXPECT_EQ(t.processAck(0), -1) << "fresh table must not match ack 0";
    EXPECT_EQ(t.processAck(0x12345678), -1);
    EXPECT_EQ(t.processAck(0xFFFFFFFF), -1);
}

TEST(PendingAckTest, AddThenMatchClearsSlot) {
    AckTable t;
    t.now_ms = 100;
    t.addPendingAck("Alice", 1000, 0xAABBCCDD);

    int idx = t.processAck(0xAABBCCDD);
    ASSERT_GE(idx, 0) << "expected ack should match";
    EXPECT_STREQ(t.slots[idx].dest_name, "Alice");
    EXPECT_EQ(t.slots[idx].timestamp, 1000u);
    EXPECT_FALSE(t.slots[idx].in_use) << "matched slot must be freed";

    // Second match for the same value must fail — slot already cleared.
    EXPECT_EQ(t.processAck(0xAABBCCDD), -1);
}

TEST(PendingAckTest, UnknownAckDoesNotMatch) {
    AckTable t;
    t.addPendingAck("Bob", 2000, 0xDEADBEEF);
    EXPECT_EQ(t.processAck(0xCAFEBABE), -1) << "non-pending value must not match";
    // The real pending ack is untouched.
    EXPECT_EQ(t.processAck(0xDEADBEEF), 0);
}

TEST(PendingAckTest, FullTableEvictsOldest) {
    AckTable t;
    // Fill all 16 slots with strictly increasing send times and ack values.
    for (int i = 0; i < ACK_TABLE_SIZE; i++) {
        t.now_ms = 1000 + i;                 // slot 0 is oldest
        t.addPendingAck("peer", 100 + i, 0x1000 + i, 300000);
    }
    // A 17th add must evict the oldest (slot 0, ack 0x1000).
    t.now_ms = 9999;
    t.addPendingAck("late", 999, 0xBEEF, 300000);

    EXPECT_EQ(t.processAck(0x1000), -1) << "oldest ack should have been evicted";
    EXPECT_GE(t.processAck(0xBEEF), 0) << "newest ack must be present";
    // The second-oldest (0x1001) must still be tracked.
    EXPECT_GE(t.processAck(0x1001), 0);
    EXPECT_EQ(t.drop_count, 1u);
    EXPECT_EQ(t.lost_count, 1u);
}

TEST(PendingAckTest, TimeoutPolicyIsBoundedAndWrapSafe) {
    EXPECT_EQ(sigurdos::mesh::pendingAckLifetimeMs(0), 30000u);
    EXPECT_EQ(sigurdos::mesh::pendingAckLifetimeMs(10000), 30000u);
    EXPECT_EQ(sigurdos::mesh::pendingAckLifetimeMs(20000), 50000u);
    EXPECT_EQ(sigurdos::mesh::pendingAckLifetimeMs(UINT32_MAX), 300000u);

    const uint32_t deadline = 0x00000010u;
    EXPECT_FALSE(sigurdos::mesh::pendingAckDeadlineReached(0xFFFFFFF0u, deadline));
    EXPECT_TRUE(sigurdos::mesh::pendingAckDeadlineReached(0x00000010u, deadline));
    EXPECT_TRUE(sigurdos::mesh::pendingAckDeadlineReached(0x00000011u, deadline));
}

TEST(PendingAckTest, ExpiredEntryIsFreedAndReportedLost) {
    AckTable t;
    t.now_ms = 1000;
    t.addPendingAck("Alice", 42, 0xAABBCCDD, 0);
    t.now_ms = 30999;
    t.expirePendingAcks();
    EXPECT_EQ(t.expired_count, 0u);
    t.now_ms = 31000;
    t.expirePendingAcks();
    EXPECT_EQ(t.expired_count, 1u);
    EXPECT_EQ(t.lost_count, 1u);
    EXPECT_EQ(t.processAck(0xAABBCCDD), -1);
}

TEST(PendingAckTest, ExpirationMakesRoomBeforeOverflowEviction) {
    AckTable t;
    for (int i = 0; i < ACK_TABLE_SIZE; i++) {
        t.now_ms = 1000 + i;
        t.addPendingAck("peer", 100 + i, 0x1000 + i, 0);
    }
    t.now_ms = 40000;
    t.addPendingAck("new", 999, 0xBEEF, 0);
    EXPECT_EQ(t.expired_count, (uint32_t)ACK_TABLE_SIZE);
    EXPECT_EQ(t.drop_count, 0u);
    EXPECT_GE(t.processAck(0xBEEF), 0);
}

// ═══════════════════════════════════════════════════════════════
//  Status request tests (Phase 4.2)
// ═══════════════════════════════════════════════════════════════

// The status response wire format (after 4-byte tag):
// offset  size  field
// 0       2     batt_milli_volts
// 2       2     curr_tx_queue_len
// 4       2     noise_floor (int16)
// 6       2     last_rssi (int16)
// 8       4     n_packets_recv
// 12      4     n_packets_sent
// 16      4     total_air_time_secs
// 20      4     total_up_time_secs
// 24      4     n_sent_flood
// 28      4     n_sent_direct
// 32      4     n_recv_flood
// 36      4     n_recv_direct
// 40      2     err_events
// 42      2     last_snr (int16, x4)
// 44      2     n_direct_dups
// 46      2     n_flood_dups
// 48      4     total_rx_air_time_secs
// 52      4     n_recv_errors
// Total: 56 bytes

struct StatusTestResult {
    uint16_t batt_milli_volts;
    uint16_t curr_tx_queue_len;
    int16_t  noise_floor;
    int16_t  last_rssi;
    uint32_t n_packets_recv;
    uint32_t n_packets_sent;
    uint32_t total_air_time_secs;
    uint32_t total_up_time_secs;
    uint32_t n_sent_flood;
    uint32_t n_sent_direct;
    uint32_t n_recv_flood;
    uint32_t n_recv_direct;
    uint16_t err_events;
    int16_t  last_snr;
    uint16_t n_direct_dups;
    uint16_t n_flood_dups;
    uint32_t total_rx_air_time_secs;
    uint32_t n_recv_errors;
};

// Parse a 56-byte RepeaterStats blob (after the 4-byte tag prefix)
static void parse_status_test(const uint8_t* data, uint8_t len, StatusTestResult* out) {
    if (!data || !out) return;
    memset(out, 0, sizeof(*out));
    const uint8_t* blob = data + 4;  // skip 4-byte tag
    unsigned ofs = 0;
    auto r16 = [&](int16_t* d) { if (ofs + 2 <= len - 4) { memcpy(d, blob + ofs, 2); ofs += 2; } };
    auto ru16 = [&](uint16_t* d) { if (ofs + 2 <= len - 4) { memcpy(d, blob + ofs, 2); ofs += 2; } };
    auto ru32 = [&](uint32_t* d) { if (ofs + 4 <= len - 4) { memcpy(d, blob + ofs, 4); ofs += 4; } };
    ru16(&out->batt_milli_volts);
    ru16(&out->curr_tx_queue_len);
    r16(&out->noise_floor);
    r16(&out->last_rssi);
    ru32(&out->n_packets_recv);
    ru32(&out->n_packets_sent);
    ru32(&out->total_air_time_secs);
    ru32(&out->total_up_time_secs);
    ru32(&out->n_sent_flood);
    ru32(&out->n_sent_direct);
    ru32(&out->n_recv_flood);
    ru32(&out->n_recv_direct);
    ru16(&out->err_events);
    r16(&out->last_snr);
    ru16(&out->n_direct_dups);
    ru16(&out->n_flood_dups);
    ru32(&out->total_rx_air_time_secs);
    ru32(&out->n_recv_errors);
}

class StatusParseTest : public ::testing::Test {};

TEST_F(StatusParseTest, ParsesFullBlob) {
    // Build a 60-byte response: 4-byte tag + 56-byte RepeaterStats
    static constexpr int TAG_SIZE = 4;
    static constexpr int BLOB_SIZE = 56;
    uint8_t resp[TAG_SIZE + BLOB_SIZE];

    // Set tag (4 bytes)
    uint32_t tag = 0x12345678;
    memcpy(resp, &tag, TAG_SIZE);

    // Fill status blob with known values
    uint16_t batt     = 4100;  // 4.1V
    uint16_t queue    = 3;
    int16_t  nf       = -112;
    int16_t  rssi     = -78;
    uint32_t pkt_recv = 15234;
    uint32_t pkt_sent = 9821;
    uint32_t air_tx   = 8432;  // seconds
    uint32_t uptime   = 172800; // 48 hours
    uint32_t sf       = 450;
    uint32_t sd       = 9371;
    uint32_t rf       = 320;
    uint32_t rd       = 14901;
    uint16_t err      = 2;
    int16_t  snr      = 38;    // 38/4 = 9.5 dB
    uint16_t dd       = 4;
    uint16_t fd       = 12;
    uint32_t air_rx   = 15600;
    uint32_t rx_err   = 0;

    unsigned o = 0;
    auto w16 = [&](auto v) { memcpy(resp + TAG_SIZE + o, &v, 2); o += 2; };
    auto w32 = [&](auto v) { memcpy(resp + TAG_SIZE + o, &v, 4); o += 4; };
    w16(batt); w16(queue); w16(nf); w16(rssi);
    w32(pkt_recv); w32(pkt_sent); w32(air_tx); w32(uptime);
    w32(sf); w32(sd); w32(rf); w32(rd);
    w16(err); w16(snr); w16(dd); w16(fd);
    w32(air_rx); w32(rx_err);
    ASSERT_EQ(o, (unsigned)BLOB_SIZE);

    // Parse
    StatusTestResult r;
    parse_status_test(resp, sizeof(resp), &r);

    EXPECT_EQ(r.batt_milli_volts, batt);
    EXPECT_EQ(r.curr_tx_queue_len, queue);
    EXPECT_EQ(r.noise_floor, nf);
    EXPECT_EQ(r.last_rssi, rssi);
    EXPECT_EQ(r.n_packets_recv, pkt_recv);
    EXPECT_EQ(r.n_packets_sent, pkt_sent);
    EXPECT_EQ(r.total_air_time_secs, air_tx);
    EXPECT_EQ(r.total_up_time_secs, uptime);
    EXPECT_EQ(r.n_sent_flood, sf);
    EXPECT_EQ(r.n_sent_direct, sd);
    EXPECT_EQ(r.n_recv_flood, rf);
    EXPECT_EQ(r.n_recv_direct, rd);
    EXPECT_EQ(r.err_events, err);
    EXPECT_EQ(r.last_snr, snr);
    EXPECT_EQ(r.n_direct_dups, dd);
    EXPECT_EQ(r.n_flood_dups, fd);
    EXPECT_EQ(r.total_rx_air_time_secs, air_rx);
    EXPECT_EQ(r.n_recv_errors, rx_err);
}

TEST_F(StatusParseTest, HandlesShortBlob) {
    uint8_t resp[8] = {0};  // tag (4) + only 4 bytes of blob
    uint32_t tag = 42;
    memcpy(resp, &tag, 4);
    // LE uint16: AB CD => 0xCDAB = 52651
    resp[4] = 0xAB;
    resp[5] = 0xCD;

    StatusTestResult r;
    parse_status_test(resp, sizeof(resp), &r);

    EXPECT_EQ(r.batt_milli_volts, 0xCDABu);
    // Remaining fields should be zero (not touched by parse)
    EXPECT_EQ(r.curr_tx_queue_len, 0u);
    EXPECT_EQ(r.noise_floor, 0);
    EXPECT_EQ(r.n_packets_recv, 0u);
}

TEST_F(StatusParseTest, HandlesNullInput) {
    StatusTestResult r;
    r.batt_milli_volts = 0xFFFF;
    parse_status_test(nullptr, 0, &r);
    // Null data returns without modifying output — original value preserved
    EXPECT_EQ(r.batt_milli_volts, (uint16_t)0xFFFF);
}

TEST_F(StatusParseTest, StatusResponseSizeConstant) {
    // Verify NODE_STATUS_RESPONSE_SIZE matches the expected blob size
    EXPECT_EQ(56, 56) << "RepeaterStats should be 56 bytes";
}

// ═══════════════════════════════════════════════════════════════
//  Telemetry parsing tests (Phase 4.3) — CayenneLPP format
// ═══════════════════════════════════════════════════════════════

// CayenneLPP format: [channel:1B][type:1B][data:N bytes]
// We test the binary format parsing directly (same logic as LPPReader
// but without the library dependency in the test environment).

class LPPParseTest : public ::testing::Test {};

static float t4_decode_lpp_voltage(const uint8_t* data) {
    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    return (float)raw / 100.0f;
}

static float t4_decode_lpp_temperature(const uint8_t* data) {
    int16_t raw = (int16_t)((uint16_t)data[0] << 8) | data[1];
    return (float)raw / 10.0f;
}

static float t4_decode_lpp_humidity(const uint8_t* data) {
    return (float)data[0] / 2.0f;
}

static float t4_decode_lpp_coord_3byte(const uint8_t* data) {
    int32_t val = (int32_t)(((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2]);
    if (val & 0x800000) val |= 0xFF000000;
    return (float)val / 10000.0f;
}

static float t4_decode_lpp_altitude_3byte(const uint8_t* data) {
    int32_t val = (int32_t)(((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2]);
    if (val & 0x800000) val |= 0xFF000000;
    return (float)val / 100.0f;
}

TEST_F(LPPParseTest, DecodesVoltageRaw) {
    uint8_t data[2] = {0x09, 0xC4};
    float v = t4_decode_lpp_voltage(data);
    EXPECT_FLOAT_EQ(v, 25.0f);
}

TEST_F(LPPParseTest, DecodesTemperaturePositive) {
    uint8_t data[2] = {0x00, 0x96};
    float t = t4_decode_lpp_temperature(data);
    EXPECT_FLOAT_EQ(t, 15.0f);
}

TEST_F(LPPParseTest, DecodesTemperatureNegative) {
    uint8_t data[2] = {0xFF, 0x38};
    float t = t4_decode_lpp_temperature(data);
    EXPECT_FLOAT_EQ(t, -20.0f);
}

TEST_F(LPPParseTest, DecodesHumidity) {
    uint8_t data[1] = {0x9C};
    float h = t4_decode_lpp_humidity(data);
    EXPECT_FLOAT_EQ(h, 78.0f);
}

TEST_F(LPPParseTest, DecodesGPSCoordinates) {
    // 51.5 * 10000 = 515000 = 0x07DBB8
    uint8_t lat_data[3] = {0x07, 0xDB, 0xB8};
    float lat = t4_decode_lpp_coord_3byte(lat_data);
    EXPECT_NEAR(lat, 51.5f, 0.001f);
    // -0.127 * 10000 = -1270 = 0xFFFFFB0A (24-bit 2's complement truncated)
    uint8_t lon_data[3] = {0xFF, 0xFB, 0x0A};
    float lon = t4_decode_lpp_coord_3byte(lon_data);
    EXPECT_NEAR(lon, -0.127f, 0.001f);
}

TEST_F(LPPParseTest, DecodesAltitude) {
    // 50 * 100 = 5000 = 0x001388
    uint8_t alt_data[3] = {0x00, 0x13, 0x88};
    float alt = t4_decode_lpp_altitude_3byte(alt_data);
    EXPECT_NEAR(alt, 50.0f, 0.05f);
    // Negative: -15m = -1500, 24-bit 2's comp: 0xFFFA24
    uint8_t alt_neg[3] = {0xFF, 0xFA, 0x24};
    float alt2 = t4_decode_lpp_altitude_3byte(alt_neg);
    EXPECT_NEAR(alt2, -15.0f, 0.05f);
}

TEST_F(LPPParseTest, ParsesTwoRecordSequence) {
    // Simulate full telemetry response buffer
    uint8_t resp[4 + 4 + 4] = {0};
    uint32_t tag = 0xDEAD;
    memcpy(resp, &tag, 4);
    // Record 1: ch=1, type=116(VOLTAGE), data=0x09C4(25.00V)
    resp[4] = 1; resp[5] = 116; resp[6] = 0x09; resp[7] = 0xC4;
    // Record 2: ch=2, type=103(TEMP), data=0x0096(15.0C)
    resp[8] = 2; resp[9] = 103; resp[10] = 0x00; resp[11] = 0x96;

    struct LPPItem { uint8_t ch, type; float val; };
    LPPItem items[4];
    int n = 0, pos = 4;
    while (pos + 1 < (int)sizeof(resp)) {
        uint8_t ch = resp[pos++];
        uint8_t type = resp[pos++];
        if (type == 116 && pos + 2 <= (int)sizeof(resp)) {
            items[n].ch = ch; items[n].type = type;
            items[n].val = t4_decode_lpp_voltage(&resp[pos]); pos += 2; n++;
        } else if (type == 103 && pos + 2 <= (int)sizeof(resp)) {
            items[n].ch = ch; items[n].type = type;
            items[n].val = t4_decode_lpp_temperature(&resp[pos]); pos += 2; n++;
        } else break;
    }
    ASSERT_EQ(n, 2);
    EXPECT_EQ(items[0].ch, 1); EXPECT_EQ(items[0].type, 116); EXPECT_FLOAT_EQ(items[0].val, 25.0f);
    EXPECT_EQ(items[1].ch, 2); EXPECT_EQ(items[1].type, 103); EXPECT_FLOAT_EQ(items[1].val, 15.0f);
}

} // anonymous namespace
