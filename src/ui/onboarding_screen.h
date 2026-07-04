#pragma once

#include "../hal/trackball.h"
#include <cstdint>

namespace sigurdos::ui {

inline bool onboarding_is_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline uint8_t onboarding_days_in_month(int year, int month)
{
    static constexpr uint8_t DAYS_IN_MONTH[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && onboarding_is_leap_year(year)) return 29;
    return DAYS_IN_MONTH[month - 1];
}

inline bool onboarding_date_valid(int year, int month, int day)
{
    if (year < 2020 || day < 1) return false;
    const uint8_t max_days = onboarding_days_in_month(year, month);
    return max_days > 0 && day <= max_days;
}

inline bool onboarding_time_valid(int hour, int minute)
{
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

inline int onboarding_clamp_day(int year, int month, int day)
{
    const uint8_t max_days = onboarding_days_in_month(year, month);
    if (max_days == 0) return 1;
    if (day < 1) return 1;
    if (day > max_days) return max_days;
    return day;
}

inline int onboarding_wrap_range(int value, int min_value, int max_value)
{
    if (min_value > max_value) return value;
    if (value < min_value) return max_value;
    if (value > max_value) return min_value;
    return value;
}

void onboarding_screen_show();
}
