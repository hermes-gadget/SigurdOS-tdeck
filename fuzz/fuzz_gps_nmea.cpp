// GPS NMEA fuzz harness — reads raw bytes from stdin, feeds to GPS parser
// Compile with: g++ -std=c++17 -fsanitize=address,undefined -I src -I src/hal -I test/mocks \
//   fuzz/fuzz_gps_nmea.cpp src/hal/gps.cpp test/mocks/mock_arduino.cpp -o fuzz_gps_nmea
#include "hal/gps.h"
#include "hal/tdeck_pins.h"
#include "Arduino.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <unistd.h>

int main() {
    // Read all of stdin into a buffer
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        data.insert(data.end(), buf, buf + n);
    }
    if (data.empty()) return 0;

    arduino_mock::reset();
    arduino_mock::current_millis = 0;

    // Feed the raw bytes through the GPS serial port one byte at a time
    for (size_t i = 0; i < data.size(); i++) {
        arduino_mock::push_gps_byte(data[i]);
    }

    // Service tick to process
    sigurdos_gps_service(false, 60);

    // Touch output to prevent optimization
    volatile float lat = sigurdos_gps_latitude();
    volatile float lon = sigurdos_gps_longitude();
    volatile uint8_t fix = sigurdos_gps_fix_quality();
    (void)lat; (void)lon; (void)fix;

    return 0;
}
