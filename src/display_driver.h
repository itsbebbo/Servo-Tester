#pragma once

// Wires up LVGL's display + touchpad input devices to the M5Dial's
// M5GFX display driver. Call display_driver_init() once from setup(),
// then display_driver_task_handler() periodically from loop().
void display_driver_init();
void display_driver_task_handler();
