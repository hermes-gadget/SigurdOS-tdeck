#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "mesh/esp32_hardware_rng.h"

namespace {

size_t fill_calls = 0;
size_t fill_bytes = 0;
uint8_t next_byte = 1;

void resetFillState() {
  fill_calls = 0;
  fill_bytes = 0;
  next_byte = 1;
}

}  // namespace

extern "C" void esp_fill_random(void* buffer, size_t length) {
  ++fill_calls;
  fill_bytes += length;
  auto* bytes = static_cast<uint8_t*>(buffer);
  for (size_t i = 0; i < length; ++i) {
    bytes[i] = next_byte++;
  }
}

class HardwareRngTest : public testing::Test {
 protected:
  void SetUp() override { resetFillState(); }
};

TEST_F(HardwareRngTest, FillsEveryRequestedIdentitySeedByte) {
  sigurdos::mesh::Esp32HardwareRng rng;
  std::array<uint8_t, 32> seed{};

  rng.random(seed.data(), seed.size());

  EXPECT_EQ(fill_calls, 1U);
  EXPECT_EQ(fill_bytes, seed.size());
  for (size_t i = 0; i < seed.size(); ++i) {
    EXPECT_EQ(seed[i], static_cast<uint8_t>(i + 1));
  }
}

TEST_F(HardwareRngTest, RepeatedRequestsUseFreshHardwareFills) {
  sigurdos::mesh::Esp32HardwareRng rng;
  std::array<uint8_t, 16> first{};
  std::array<uint8_t, 16> second{};

  rng.random(first.data(), first.size());
  rng.random(second.data(), second.size());

  EXPECT_EQ(fill_calls, 2U);
  EXPECT_EQ(fill_bytes, first.size() + second.size());
  EXPECT_NE(first, second);
}

TEST_F(HardwareRngTest, EmptyAndNullRequestsDoNotCallPlatformRng) {
  sigurdos::mesh::Esp32HardwareRng rng;
  uint8_t byte = 0;

  rng.random(nullptr, 16);
  rng.random(&byte, 0);

  EXPECT_EQ(fill_calls, 0U);
  EXPECT_EQ(fill_bytes, 0U);
  EXPECT_EQ(byte, 0U);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
