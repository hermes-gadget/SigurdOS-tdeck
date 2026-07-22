#pragma once

#include <lvgl.h>

namespace sigurdos::ui {

// Adopt the current bottom-bar WiFi label and bind its delete lifetime.
void wifi_status_icon_attach(lv_obj_t* icon);

}  // namespace sigurdos::ui
