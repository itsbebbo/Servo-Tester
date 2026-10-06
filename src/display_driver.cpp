#include "display_driver.h"

#include <M5Dial.h>
#include <lvgl.h>

#include "config.h"

namespace {

lv_disp_draw_buf_t s_draw_buf;
lv_color_t s_buf1[DISPLAY_WIDTH * LVGL_DRAW_BUF_LINES];
lv_color_t s_buf2[DISPLAY_WIDTH * LVGL_DRAW_BUF_LINES];

lv_disp_drv_t s_disp_drv;
lv_indev_drv_t s_touch_drv;

void disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    const uint32_t w = lv_area_get_width(area);
    const uint32_t h = lv_area_get_height(area);

    M5Dial.Display.startWrite();
    M5Dial.Display.setAddrWindow(area->x1, area->y1, w, h);
    // LV_COLOR_16_SWAP is enabled in lv_conf.h, so the pixel data LVGL
    // produced is already in the panel's native (big-endian) byte order.
    M5Dial.Display.writePixels(reinterpret_cast<uint16_t *>(color_p), w * h, false);
    M5Dial.Display.endWrite();

    lv_disp_flush_ready(disp_drv);
}

void touchpad_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    auto &detail = M5Dial.Touch.getDetail();
    if (detail.isPressed()) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = detail.x;
        data->point.y = detail.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

} // namespace

void display_driver_init() {
    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, DISPLAY_WIDTH * LVGL_DRAW_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = DISPLAY_WIDTH;
    s_disp_drv.ver_res = DISPLAY_HEIGHT;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_touch_drv);
    s_touch_drv.type = LV_INDEV_TYPE_POINTER;
    s_touch_drv.read_cb = touchpad_read_cb;
    lv_indev_drv_register(&s_touch_drv);
}

void display_driver_task_handler() {
    lv_timer_handler();
}
