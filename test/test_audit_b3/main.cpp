// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/prefs.h"
#include "ui/contact_qr_policy.h"
#include "ui/prefs_ui.h"
#include "ui/repeater_password_policy.h"
#include "ui/wifi_credentials_policy.h"

#include <fstream>
#include <iterator>
#include <string>
#include <cstring>

namespace sigurdos {
void prefs_mock_reset();
void prefs_mock_set_save_result(bool result);
} // namespace sigurdos

namespace {

using sigurdos::ui::NotificationEvent;

NotificationEvent g_last_error_event = NotificationEvent::DirectMessage;
std::string g_last_error_text;
int g_error_count = 0;

void capture_prefs_error(NotificationEvent event, const char* text)
{
    g_last_error_event = event;
    g_last_error_text = text ? text : "";
    ++g_error_count;
}

void reset_error_capture()
{
    g_last_error_event = NotificationEvent::DirectMessage;
    g_last_error_text.clear();
    g_error_count = 0;
}

std::string read_project_file(const char* path)
{
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream in(std::string(prefix) + path);
        if (in.good()) {
            return std::string(std::istreambuf_iterator<char>(in), {});
        }
    }
    return {};
}

std::string source_line_containing(const std::string& source,
                                   const char* needle)
{
    const size_t start = source.find(needle);
    if (start == std::string::npos) return {};
    const size_t end = source.find('\n', start);
    return source.substr(start, end == std::string::npos
                                  ? std::string::npos : end - start);
}

TEST(AuditB3Qr, StableIdLookupUsesResolvedDisplayName)
{
    const std::string stable_id(64, 'a');
    sigurdos::ui::ContactQrPayload payload{};
    ASSERT_TRUE(sigurdos::ui::contact_qr_payload_init(
        payload, stable_id.c_str(), "Alice & Bob", 3));

    char uri[512]{};
    ASSERT_TRUE(sigurdos::ui::contact_qr_build_uri(
        payload, stable_id.c_str(), uri, sizeof(uri)));

    EXPECT_STREQ(payload.stable_id, stable_id.c_str());
    EXPECT_STREQ(payload.display_name, "Alice & Bob");
    EXPECT_NE(std::string(uri).find("name=Alice%20%26%20Bob"),
              std::string::npos);
    EXPECT_NE(std::string(uri).find("public_key=" + stable_id),
              std::string::npos);
    EXPECT_NE(std::string(uri).find("type=3"), std::string::npos);
}

TEST(AuditB3Repeater, CredentialActionsAreMaskedAndDoNotEcho)
{
    EXPECT_TRUE(sigurdos::ui::repeater_password_action("password "));
    EXPECT_TRUE(sigurdos::ui::repeater_password_action(
        "set guest.password "));
    EXPECT_FALSE(sigurdos::ui::repeater_password_action("set sf "));
    EXPECT_STREQ(sigurdos::ui::repeater_password_feedback(true),
                 "Credential update sent");
    EXPECT_STREQ(sigurdos::ui::repeater_password_feedback(false),
                 "Credential update failed");

    const std::string source = read_project_file(
        "src/ui/screens/screen_repeaters.cpp");
    ASSERT_FALSE(source.empty());
    EXPECT_NE(source.find("lv_textarea_set_password_mode(ta, is_credential)"),
              std::string::npos);
    EXPECT_NE(source.find("repeater_password_feedback(sent)"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "repeater_send(d->name, cmd, \"Sent: %s\", d->is_credential)"),
              std::string::npos);

    const std::string admin_line = source_line_containing(
        source, "Admin Password");
    const std::string guest_line = source_line_containing(
        source, "Guest Password");
    ASSERT_FALSE(admin_line.empty());
    ASSERT_FALSE(guest_line.empty());
    EXPECT_NE(admin_line.find("true"), std::string::npos);
    EXPECT_NE(guest_line.find("true"), std::string::npos);
    EXPECT_EQ(admin_line.find("false"), std::string::npos);
    EXPECT_EQ(guest_line.find("false"), std::string::npos);
}

class PrefsUiFailureTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        sigurdos::prefs_mock_set_save_result(true);
        sigurdos::prefs_mock_reset();
        sigurdos::NodePrefs defaults;
        defaults.set_defaults();
        ASSERT_TRUE(sigurdos::prefs_set(defaults));
        reset_error_capture();
    }

    void TearDown() override
    {
        sigurdos::prefs_mock_set_save_result(true);
    }

    void expect_commit_rejected(const sigurdos::NodePrefs& candidate)
    {
        sigurdos::prefs_mock_set_save_result(false);
        EXPECT_FALSE(sigurdos::ui::prefs_ui_commit(
            candidate, capture_prefs_error));
        EXPECT_EQ(g_error_count, 1);
        EXPECT_EQ(g_last_error_event, NotificationEvent::UiError);
        EXPECT_STREQ(g_last_error_text.c_str(),
                     sigurdos::ui::PREFS_UI_SAVE_ERROR);
    }
};

TEST_F(PrefsUiFailureTest, RadioSettingsStayUnchanged)
{
    const sigurdos::NodePrefs before = sigurdos::prefs_get();
    sigurdos::NodePrefs candidate = before;
    candidate.duty_cycle = 25;
    candidate.airtime_factor = 3.0f;
    expect_commit_rejected(candidate);
    EXPECT_EQ(sigurdos::prefs_get().duty_cycle, before.duty_cycle);
    EXPECT_FLOAT_EQ(sigurdos::prefs_get().airtime_factor,
                    before.airtime_factor);
}

TEST_F(PrefsUiFailureTest, GpsSettingsStayUnchanged)
{
    const sigurdos::NodePrefs before = sigurdos::prefs_get();
    sigurdos::NodePrefs candidate = before;
    candidate.gps_enabled = !before.gps_enabled;
    candidate.gps_interval = 60;
    expect_commit_rejected(candidate);
    EXPECT_EQ(sigurdos::prefs_get().gps_enabled, before.gps_enabled);
    EXPECT_EQ(sigurdos::prefs_get().gps_interval, before.gps_interval);
}

TEST_F(PrefsUiFailureTest, DisplaySettingsStayUnchanged)
{
    const sigurdos::NodePrefs before = sigurdos::prefs_get();
    sigurdos::NodePrefs candidate = before;
    candidate.display_brightness = 200;
    candidate.theme_id = 4;
    expect_commit_rejected(candidate);
    EXPECT_EQ(sigurdos::prefs_get().display_brightness,
              before.display_brightness);
    EXPECT_EQ(sigurdos::prefs_get().theme_id, before.theme_id);
}

TEST_F(PrefsUiFailureTest, SystemSettingsStayUnchanged)
{
    const sigurdos::NodePrefs before = sigurdos::prefs_get();
    sigurdos::NodePrefs candidate = before;
    candidate.device_pin = 1234;
    expect_commit_rejected(candidate);
    EXPECT_EQ(sigurdos::prefs_get().device_pin, before.device_pin);
}

TEST_F(PrefsUiFailureTest, OnboardingSettingsStayUnchanged)
{
    const sigurdos::NodePrefs before = sigurdos::prefs_get();
    sigurdos::NodePrefs candidate = before;
    std::strncpy(candidate.node_name, "new-node",
                 sizeof(candidate.node_name) - 1);
    candidate.node_name[sizeof(candidate.node_name) - 1] = '\0';
    candidate.configured = true;
    expect_commit_rejected(candidate);
    EXPECT_STREQ(sigurdos::prefs_get().node_name, before.node_name);
    EXPECT_EQ(sigurdos::prefs_get().configured, before.configured);
}

TEST_F(PrefsUiFailureTest, WifiCredentialsNeedDurableCommit)
{
    sigurdos::ui::WifiCredentialStage staged{};
    ASSERT_TRUE(sigurdos::ui::wifi_credentials_stage(
        staged, "audit-network", "new-secret"));

    const sigurdos::NodePrefs before = sigurdos::prefs_get();
    sigurdos::NodePrefs candidate = before;
    ASSERT_TRUE(sigurdos::ui::wifi_credentials_commit(
        staged, true, candidate.wifi_ssid, sizeof(candidate.wifi_ssid),
        candidate.wifi_password, sizeof(candidate.wifi_password)));
    expect_commit_rejected(candidate);
    EXPECT_STREQ(sigurdos::prefs_get().wifi_ssid, before.wifi_ssid);
    EXPECT_STREQ(sigurdos::prefs_get().wifi_password, before.wifi_password);
    EXPECT_FALSE(sigurdos::ui::wifi_credentials_save_succeeded(true, true, false));
    EXPECT_TRUE(sigurdos::ui::wifi_credentials_save_succeeded(true, true, true));
    EXPECT_FALSE(sigurdos::ui::wifi_credentials_save_succeeded(false, true, true));
}

TEST(AuditB3SourceContract, PreferenceScreensUseDurableCommitHelper)
{
    const char* paths[] = {
        "src/ui/screens/screen_wifi_networks.cpp",
        "src/ui/screens/screen_settings_radio.cpp",
        "src/ui/screens/screen_settings_gps.cpp",
        "src/ui/screens/screen_settings_display.cpp",
        "src/ui/screens/screen_settings_system.cpp",
        "src/ui/onboarding_screen.cpp",
        "src/ui/chat_screen.cpp",
        "src/ui/screens/screen_map.cpp",
    };
    for (const char* path : paths) {
        const std::string source = read_project_file(path);
        ASSERT_FALSE(source.empty()) << path;
        EXPECT_NE(source.find("prefs_ui_commit"), std::string::npos) << path;
        EXPECT_EQ(source.find("prefs_set("), std::string::npos) << path;
    }
}

TEST(AuditB3SourceContract, OnboardingCommitsBeforeMeshSideEffects)
{
    const std::string source = read_project_file("src/ui/onboarding_screen.cpp");
    ASSERT_FALSE(source.empty());
    const size_t commit = source.find("if (!prefs_ui_commit(np)) return;");
    const size_t set_name = source.find("setOwnName(s_name)");
    const size_t join_public = source.find("joinPublicChannel()");
    const size_t restart = source.find("ESP.restart()");
    ASSERT_NE(commit, std::string::npos);
    ASSERT_NE(set_name, std::string::npos);
    ASSERT_NE(join_public, std::string::npos);
    ASSERT_NE(restart, std::string::npos);
    EXPECT_LT(commit, set_name);
    EXPECT_LT(commit, join_public);
    EXPECT_LT(commit, restart);
}

TEST(AuditB3SourceContract, WifiRequiresDurableCredentialsBeforeSuccess)
{
    const std::string source = read_project_file(
        "src/ui/screens/screen_wifi_networks.cpp");
    ASSERT_FALSE(source.empty());
    EXPECT_NE(source.find("wifi_credentials_save_succeeded"),
              std::string::npos);
    EXPECT_NE(source.find("Connected, settings not saved"),
              std::string::npos);
    EXPECT_EQ(source.find("prefs_set("), std::string::npos);
}

TEST(AuditB3SourceContract, BrightnessPreviewIsRestoredAfterSaveFailure)
{
    const std::string source = read_project_file(
        "src/ui/screens/screen_settings_display.cpp");
    ASSERT_FALSE(source.empty());
    EXPECT_NE(source.find(
                  "sigurdos_keyboard_set_brightness(c->original_brightness)"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "sigurdos_display_set_brightness(c->original_brightness)"),
              std::string::npos);
}

TEST(AuditB3SourceContract, ContactQrCopiesResolvedIdentityAndName)
{
    const std::string source = read_project_file(
        "src/ui/screens/screen_contacts.cpp");
    ASSERT_FALSE(source.empty());
    EXPECT_NE(source.find("contact_qr_payload_init"), std::string::npos);
    EXPECT_NE(source.find("target->id, target->name, target->type"),
              std::string::npos);
    EXPECT_NE(source.find("payload->stable_id"), std::string::npos);
    EXPECT_NE(source.find("contact_qr_build_uri"), std::string::npos);
    EXPECT_EQ(source.find("lv_obj_set_user_data(qr_row"), std::string::npos);
}

} // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
