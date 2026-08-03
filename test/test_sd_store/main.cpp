#include <gtest/gtest.h>

// platformio.ini intentionally keeps the production source filter unchanged;
// compile the backend adapter into this native-only test binary.
#include "../../src/mesh/sd_message_store.cpp"

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
