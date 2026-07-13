// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "home_routes.h"

#include <cstring>

namespace sigurdos::ui {
namespace {

constexpr HomeRoute ROUTES[] = {
    {"CHATS",     true,  Screen::Chat,       HomeRouteFilter::Channels},
    {"DMs",       false, Screen::Chat,       HomeRouteFilter::DirectMessages},
    {"ROOMS",     false, Screen::Contacts,   HomeRouteFilter::Rooms},
    {"CONTACTS",  false, Screen::Contacts,   HomeRouteFilter::Default},
    {"REPEATERS", false, Screen::Repeaters,  HomeRouteFilter::Default},
    {"ADVERTISE", false, Screen::Advertise,  HomeRouteFilter::Default},
    {"MAP",       false, Screen::Map,        HomeRouteFilter::Default},
    {"TERMINAL",  false, Screen::Terminal,   HomeRouteFilter::Default},
    {"PACKETS",   false, Screen::Heard,      HomeRouteFilter::Default},
    {"SETTINGS",  false, Screen::Settings,   HomeRouteFilter::Default},
    {"SETUP",     false, Screen::Onboarding, HomeRouteFilter::Default},
    {"SIGNAL",    false, Screen::Signal,     HomeRouteFilter::Default},
};
static_assert(sizeof(ROUTES) / sizeof(ROUTES[0]) == HOME_ROUTE_COUNT);

} // namespace

std::size_t homeRouteCount() { return HOME_ROUTE_COUNT; }

const HomeRoute* homeRouteAt(std::size_t index)
{
    return index < homeRouteCount() ? &ROUTES[index] : nullptr;
}

const HomeRoute* findHomeRoute(const char* label)
{
    if (!label) return nullptr;
    for (const auto& route : ROUTES) {
        if (std::strcmp(route.label, label) == 0) return &route;
    }
    return nullptr;
}

} // namespace sigurdos::ui
