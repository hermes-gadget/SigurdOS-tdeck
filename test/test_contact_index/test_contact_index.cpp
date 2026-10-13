// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#if !defined(SIGURDOS_MESHCORE_INTEGRATION)

TEST(ContactIndexEnvironmentTest, PlaceholderIsNotProductionCoverage) {
    GTEST_SKIP() << "placeholder only; run pio test -e native_contact_index for "
                    "production-linked contact indexing coverage";
}

#else

#include <cstdio>
#include <cstring>

#include <helpers/BaseChatMesh.h>

#include "mesh/contact_index.h"

// Verifies SigurdOS's public contact indexing (SigurdOS #1193): MeshCore's
// getNumContacts()/ContactsIterator exclude the reserved anon-contact slots,
// and publicContactByIdx() must expose exactly that same zero-based coordinate
// against the real BaseChatMesh backing array. Ported from the former
// core-repo test now that the pin is plain upstream.

namespace {

class FakeMillis final : public mesh::MillisecondClock {
public:
    unsigned long getMillis() override { return 0; }
};

class FakeRtc final : public mesh::RTCClock {
public:
    uint32_t getCurrentTime() override { return now; }
    void setCurrentTime(uint32_t value) override { now = value; }

private:
    uint32_t now = 1;
};

class FakeRng final : public mesh::RNG {
public:
    void random(uint8_t* dest, size_t size) override {
        if (dest) std::memset(dest, 0x5a, size);
    }
};

class FakeRadio final : public mesh::Radio {
public:
    int recvRaw(uint8_t*, int) override { return 0; }
    uint32_t getEstAirtimeFor(int) override { return 1; }
    float packetScore(float, int) override { return 0; }
    bool startSendRaw(const uint8_t*, int) override { return true; }
    bool isSendComplete() override { return true; }
    void onSendFinished() override {}
    bool isInRecvMode() const override { return true; }
};

class FakePacketManager final : public mesh::PacketManager {
public:
    mesh::Packet* allocNew() override { return nullptr; }
    void free(mesh::Packet*) override {}
    void queueOutbound(mesh::Packet*, uint8_t, uint32_t) override {}
    mesh::Packet* getNextOutbound(uint32_t) override { return nullptr; }
    int getOutboundCount(uint32_t) const override { return 0; }
    int getOutboundTotal() const override { return 0; }
    int getFreeCount() const override { return 0; }
    mesh::Packet* getOutboundByIdx(int) override { return nullptr; }
    mesh::Packet* removeOutboundByIdx(int) override { return nullptr; }
    void queueInbound(mesh::Packet*, uint32_t) override {}
    mesh::Packet* getNextInbound(uint32_t) override { return nullptr; }
};

class FakeTables final : public mesh::MeshTables {
public:
    bool wasSeen(const mesh::Packet*) override { return false; }
    void markSeen(const mesh::Packet*) override {}
    void clear(const mesh::Packet*) override {}
};

class ContactMesh final : public BaseChatMesh {
public:
    ContactMesh(mesh::Radio& radio, mesh::MillisecondClock& millis,
                mesh::RNG& rng, mesh::RTCClock& rtc,
                mesh::PacketManager& packets, mesh::MeshTables& tables)
        : BaseChatMesh(radio, millis, rng, rtc, packets, tables) {}

protected:
    void onDiscoveredContact(ContactInfo&, bool, uint8_t, const uint8_t*) override {}
    ContactInfo* processAck(const uint8_t*) override { return nullptr; }
    void onContactPathUpdated(const ContactInfo&) override {}
    void onMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
    void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
    void onCLICommandRecv(const ContactInfo&, mesh::Packet*, uint32_t,
                          const char*, char*) override {}
    void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t,
                             const uint8_t*, const char*) override {}
    uint32_t calcFloodTimeoutMillisFor(uint32_t) const override { return 1; }
    uint32_t calcDirectTimeoutMillisFor(uint32_t, uint8_t) const override { return 1; }
    void onSendTimeout() override {}
    void onChannelMessageRecv(const mesh::GroupChannel&, mesh::Packet*,
                              uint32_t, const char*) override {}
    uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*,
                             uint8_t, uint8_t*) override { return 0; }
    void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}
};

struct Harness {
    FakeRadio radio;
    FakeMillis millis;
    FakeRng rng;
    FakeRtc rtc;
    FakePacketManager packets;
    FakeTables tables;
    ContactMesh mesh{radio, millis, rng, rtc, packets, tables};
};

ContactInfo makeContact(uint16_t marker, uint8_t type = ADV_TYPE_CHAT) {
    ContactInfo contact{};
    std::snprintf(contact.name, sizeof(contact.name), "contact-%u", marker);
    contact.id.pub_key[0] = static_cast<uint8_t>(marker);
    contact.id.pub_key[1] = static_cast<uint8_t>(marker >> 8);
    contact.type = type;
    return contact;
}

void addRegularContacts(ContactMesh& mesh, int count) {
    for (int i = 0; i < count; ++i) {
        ASSERT_TRUE(mesh.addContact(makeContact(static_cast<uint16_t>(i + 1))));
    }
}

void expectPublicSequence(ContactMesh& mesh, int count) {
    ASSERT_EQ(mesh.getNumContacts(), count);
    for (int i = 0; i < count; ++i) {
        ContactInfo actual{};
        ASSERT_TRUE(sigurdos::mesh::publicContactByIdx(
            mesh, static_cast<uint32_t>(i), actual));
        EXPECT_EQ(actual.id.pub_key[0], static_cast<uint8_t>(i + 1));
        EXPECT_EQ(actual.id.pub_key[1], static_cast<uint8_t>((i + 1) >> 8));
    }
    ContactInfo past_end{};
    EXPECT_FALSE(sigurdos::mesh::publicContactByIdx(
        mesh, static_cast<uint32_t>(count), past_end));
}

TEST(BaseChatMeshContactIndex, EnumeratesOneEightNineAndMaximumContacts) {
    for (int count : {1, 8, 9, MAX_CONTACTS}) {
        Harness harness;
        addRegularContacts(harness.mesh, count);
        expectPublicSequence(harness.mesh, count);
    }
}

TEST(BaseChatMeshContactIndex, OccupiedTransientSlotsRemainHidden) {
    Harness harness;
    ASSERT_TRUE(harness.mesh.addContact(makeContact(0xee, ADV_TYPE_NONE)));
    addRegularContacts(harness.mesh, 9);

    EXPECT_EQ(harness.mesh.getTotalContactSlots(), MAX_ANON_CONTACTS + 9);
    expectPublicSequence(harness.mesh, 9);

    auto iterator = harness.mesh.startContactsIterator();
    ContactInfo actual{};
    for (int i = 0; i < 9; ++i) {
        ASSERT_TRUE(iterator.hasNext(&harness.mesh, actual));
        EXPECT_EQ(actual.id.pub_key[0], static_cast<uint8_t>(i + 1));
    }
    EXPECT_FALSE(iterator.hasNext(&harness.mesh, actual));
}

}  // namespace

#endif  // SIGURDOS_MESHCORE_INTEGRATION
