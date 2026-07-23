// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "telemetry_lpp_parser.h"

#include <stdio.h>

#include <helpers/sensors/LPPDataHelpers.h>

namespace sigurdos::mesh::detail {
namespace {

bool fixedValueSize(uint8_t type, size_t* size)
{
    if (!size) return false;

    switch (type) {
        case LPP_DIGITAL_INPUT:
        case LPP_DIGITAL_OUTPUT:
        case LPP_PRESENCE:
        case LPP_RELATIVE_HUMIDITY:
        case LPP_PERCENTAGE:
        case LPP_SWITCH:
            *size = 1;
            return true;

        case LPP_ANALOG_INPUT:
        case LPP_ANALOG_OUTPUT:
        case LPP_LUMINOSITY:
        case LPP_TEMPERATURE:
        case LPP_BAROMETRIC_PRESSURE:
        case LPP_VOLTAGE:
        case LPP_CURRENT:
        case LPP_ALTITUDE:
        case LPP_CONCENTRATION:
        case LPP_POWER:
        case LPP_DIRECTION:
            *size = 2;
            return true;

        case LPP_COLOUR:
            *size = 3;
            return true;

        case LPP_GENERIC_SENSOR:
        case LPP_FREQUENCY:
        case LPP_DISTANCE:
        case LPP_ENERGY:
        case LPP_UNIXTIME:
            *size = 4;
            return true;

        case LPP_ACCELEROMETER:
        case LPP_GYROMETER:
            *size = 6;
            return true;

        case LPP_GPS:
            *size = 9;
            return true;

        default:
            return false;
    }
}

bool encodedValueSize(uint8_t type, const uint8_t* value, size_t remaining,
                      size_t* size)
{
    if (fixedValueSize(type, size)) return true;
    if (type != LPP_POLYLINE || !value || remaining < 1 || !size) return false;

    // MeshCore's extension stores the total value length in its first byte:
    // size, delta factor, 3-byte latitude, 3-byte longitude, then deltas.
    const size_t declared = value[0];
    if (declared < 8) return false;
    *size = declared;
    return true;
}

uint32_t readUnsigned(const uint8_t* value, size_t size)
{
    uint32_t decoded = 0;
    for (size_t i = 0; i < size; ++i) {
        decoded = (decoded << 8) | value[i];
    }
    return decoded;
}

float readFloat(const uint8_t* value, size_t size, uint32_t multiplier,
                bool is_signed)
{
    const uint32_t decoded = readUnsigned(value, size);
    int64_t scaled = decoded;
    if (is_signed) {
        const uint64_t sign_bit = UINT64_C(1) << ((size * 8) - 1);
        if ((decoded & sign_bit) != 0) {
            scaled -= static_cast<int64_t>(sign_bit << 1);
        }
    }
    return static_cast<float>(scaled) / static_cast<float>(multiplier);
}

void formatItem(TelemetryItem* item, const uint8_t* value)
{
    if (!item || !value) return;

    switch (item->type) {
        case LPP_VOLTAGE:
            item->value_float = readFloat(value, 2, LPP_VOLTAGE_MULT, false);
            snprintf(item->value_str, sizeof(item->value_str), "%.2fV",
                     item->value_float);
            break;

        case LPP_TEMPERATURE:
            item->value_float = readFloat(value, 2, LPP_TEMPERATURE_MULT, true);
            snprintf(item->value_str, sizeof(item->value_str), "%.1fC",
                     item->value_float);
            break;

        case LPP_RELATIVE_HUMIDITY:
            item->value_float =
                readFloat(value, 1, LPP_RELATIVE_HUMIDITY_MULT, false);
            snprintf(item->value_str, sizeof(item->value_str), "%.0f%%",
                     item->value_float);
            break;

        case LPP_BAROMETRIC_PRESSURE:
            item->value_float =
                readFloat(value, 2, LPP_BAROMETRIC_PRESSURE_MULT, false);
            snprintf(item->value_str, sizeof(item->value_str), "%.1f hPa",
                     item->value_float);
            break;

        case LPP_GPS: {
            const float latitude =
                readFloat(value, 3, LPP_GPS_LAT_LON_MULT, true);
            const float longitude =
                readFloat(value + 3, 3, LPP_GPS_LAT_LON_MULT, true);
            const float altitude = readFloat(value + 6, 3, LPP_GPS_ALT_MULT, true);
            item->value_float = latitude;
            snprintf(item->value_str, sizeof(item->value_str), "%.4f, %.4f %.0fm",
                     latitude, longitude, altitude);
            break;
        }

        case LPP_CURRENT:
            // Match MeshCore's reader, which treats the two-byte current field
            // as signed even though the protocol comment calls it unsigned.
            item->value_float = readFloat(value, 2, LPP_CURRENT_MULT, true);
            snprintf(item->value_str, sizeof(item->value_str), "%.3fA",
                     item->value_float);
            break;

        default:
            snprintf(item->value_str, sizeof(item->value_str), "type=%u",
                     static_cast<unsigned>(item->type));
            break;
    }
}

} // namespace

bool parseTelemetryLpp(const uint8_t* data, size_t len, TelemetryResult* out)
{
    if (!out || (len != 0 && !data)) return false;

    TelemetryResult parsed = {};
    size_t offset = 0;
    while (offset < len) {
        if (len - offset < 2 || parsed.n_items >= MAX_TELEMETRY_ITEMS) {
            return false;
        }

        const uint8_t channel = data[offset++];
        const uint8_t type = data[offset++];
        if (channel == 0) return false;

        size_t value_size = 0;
        const size_t remaining = len - offset;
        if (!encodedValueSize(type, data + offset, remaining, &value_size) ||
            value_size > remaining) {
            return false;
        }

        TelemetryItem& item = parsed.items[parsed.n_items];
        item.channel = channel;
        item.type = type;
        formatItem(&item, data + offset);
        ++parsed.n_items;
        offset += value_size;
    }

    *out = parsed;
    return true;
}

} // namespace sigurdos::mesh::detail
