// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include "Arduino.h"
#include "hal/tdeck_pins.h"
#include "hal/trackball.h"

namespace {

class TrackballTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        sigurdos_trackball_init();
        set_pin(PIN_TRACKBALL_UP, false);
        set_pin(PIN_TRACKBALL_DOWN, false);
        set_pin(PIN_TRACKBALL_LEFT, false);
        set_pin(PIN_TRACKBALL_RIGHT, false);
        set_pin(PIN_TRACKBALL_BTN, false);
        sigurdos_trackball_reset_scan_state();
        advance(300);
    }

    void set_pin(uint8_t pin, bool active) {
        arduino_mock::pin_states[pin] = active ? LOW : HIGH;
    }

    bool next(SigurdOSTrackballEvent* event) {
        return sigurdos_trackball_next_event(event);
    }

    void advance(uint32_t ms) {
        arduino_mock::current_millis += ms;
    }

    void reset_scan_state_after_settle() {
        sigurdos_trackball_reset_scan_state();
        advance(300);
    }
};

TEST_F(TrackballTest, NoInputProducesNoEvent) {
    sigurdos_trackball_scan();

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    EXPECT_FALSE(next(&event));
}

TEST_F(TrackballTest, DirectionPulseProducesEventImmediately) {
    set_pin(PIN_TRACKBALL_UP, true);
    sigurdos_trackball_scan();

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    EXPECT_TRUE(next(&event));
    EXPECT_EQ(event, SigurdOSTrackballEvent::Up);
}

TEST_F(TrackballTest, DirectionsMapToExpectedEvents) {
    struct Case {
        uint8_t pin;
        SigurdOSTrackballEvent event;
    };

    const Case cases[] = {
        {PIN_TRACKBALL_UP, SigurdOSTrackballEvent::Up},
        {PIN_TRACKBALL_DOWN, SigurdOSTrackballEvent::Down},
        {PIN_TRACKBALL_LEFT, SigurdOSTrackballEvent::Left},
        {PIN_TRACKBALL_RIGHT, SigurdOSTrackballEvent::Right},
    };

    for (const Case& c : cases) {
        reset_scan_state_after_settle();
        set_pin(c.pin, true);
        sigurdos_trackball_scan();

        SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
        ASSERT_TRUE(next(&event));
        EXPECT_EQ(event, c.event);
        EXPECT_FALSE(next(&event));

        set_pin(c.pin, false);
        sigurdos_trackball_scan();
        advance(20);
        sigurdos_trackball_scan();
    }
}

TEST_F(TrackballTest, PulsesAfterDeadtimeProduceSeparateEvents) {
    set_pin(PIN_TRACKBALL_DOWN, true);
    sigurdos_trackball_scan();

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    ASSERT_TRUE(next(&event));
    EXPECT_EQ(event, SigurdOSTrackballEvent::Down);

    // Return to idle and pulse again after deadtime expires
    set_pin(PIN_TRACKBALL_DOWN, false);
    advance(151);
    sigurdos_trackball_scan();

    set_pin(PIN_TRACKBALL_DOWN, true);
    advance(1);
    sigurdos_trackball_scan();

    ASSERT_TRUE(next(&event));
    EXPECT_EQ(event, SigurdOSTrackballEvent::Down);
}

TEST_F(TrackballTest, DirectionBounceInsideDeadtimeIsSuppressed) {
    set_pin(PIN_TRACKBALL_LEFT, true);
    sigurdos_trackball_scan();

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    ASSERT_TRUE(next(&event));

    set_pin(PIN_TRACKBALL_LEFT, false);
    advance(20);
    sigurdos_trackball_scan();

    set_pin(PIN_TRACKBALL_LEFT, true);
    advance(20);
    sigurdos_trackball_scan();
    EXPECT_FALSE(next(&event));
}

TEST_F(TrackballTest, HeldDirectionDoesNotRepeat) {
    set_pin(PIN_TRACKBALL_RIGHT, true);
    sigurdos_trackball_scan();

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    ASSERT_TRUE(next(&event));
    EXPECT_EQ(event, SigurdOSTrackballEvent::Right);

    advance(1000);
    sigurdos_trackball_scan();
    EXPECT_FALSE(next(&event));
}

TEST_F(TrackballTest, DirectionIdleLevelIsCalibratedAtInit) {
    // Calibrate with LEFT already active (mid-detent at boot)
    set_pin(PIN_TRACKBALL_LEFT, true);
    reset_scan_state_after_settle();

    // No event for steady state
    sigurdos_trackball_scan();
    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    EXPECT_FALSE(next(&event));

    // Release (rising edge) → no event (falling-edge-only applies to all directions)
    set_pin(PIN_TRACKBALL_LEFT, false);
    sigurdos_trackball_scan();
    EXPECT_FALSE(next(&event));

    // Re-activate (falling edge) → should fire
    set_pin(PIN_TRACKBALL_LEFT, true);
    sigurdos_trackball_scan();
    ASSERT_TRUE(next(&event));
    EXPECT_EQ(event, SigurdOSTrackballEvent::Left);
}

TEST_F(TrackballTest, ClickProducesOneSelectEventWithoutRepeat) {
    set_pin(PIN_TRACKBALL_BTN, true);
    sigurdos_trackball_scan();
    advance(20);
    sigurdos_trackball_scan();

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    ASSERT_TRUE(next(&event));
    EXPECT_EQ(event, SigurdOSTrackballEvent::Click);

    advance(1000);
    sigurdos_trackball_scan();
    EXPECT_FALSE(next(&event));
}

TEST_F(TrackballTest, NullEventPointerIsRejected) {
    EXPECT_FALSE(sigurdos_trackball_next_event(nullptr));
}

TEST_F(TrackballTest, InjectedInvalidEventsAreIgnored) {
    sigurdos_trackball_inject(SigurdOSTrackballEvent::None);
    sigurdos_trackball_inject(static_cast<SigurdOSTrackballEvent>(0xFF));

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    EXPECT_FALSE(next(&event));
}

TEST_F(TrackballTest, InjectedInvalidEventsDoNotDisruptValidOrdering) {
    sigurdos_trackball_inject(static_cast<SigurdOSTrackballEvent>(0xFE));
    sigurdos_trackball_inject(SigurdOSTrackballEvent::Up);
    sigurdos_trackball_inject(SigurdOSTrackballEvent::None);
    sigurdos_trackball_inject(SigurdOSTrackballEvent::Click);

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    ASSERT_TRUE(next(&event));
    EXPECT_EQ(event, SigurdOSTrackballEvent::Up);
    ASSERT_TRUE(next(&event));
    EXPECT_EQ(event, SigurdOSTrackballEvent::Click);
    EXPECT_FALSE(next(&event));
}

} // namespace
