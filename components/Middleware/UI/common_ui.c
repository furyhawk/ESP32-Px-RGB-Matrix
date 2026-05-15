#include "common_ui.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static common_ui ui;

common_ui *common_ui_get(void) { return &ui; }


void middle_fmt_fixed1(char *dest, size_t size, int32_t val_x10) {
    if (!dest || size == 0) return;
    bool neg = (val_x10 < 0);
    uint32_t u = (uint32_t)(neg ? -val_x10 : val_x10);
    snprintf(dest, size, "%s%u.%u", neg ? "-" : "", (unsigned)(u / 10), (unsigned)(u % 10));
}

/**
 * Create and layout common LVGL widgets on the active screen.
 */
void common_ui_init(void) {
    common_ui *ui = common_ui_get();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    const lv_color_t text_color = lv_color_hex(0xFFFFFF);
    ui->line1_label = lv_label_create(scr);
    lv_obj_set_width(ui->line1_label, lv_pct(100));
    lv_obj_set_style_text_align(ui->line1_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui->line1_label, text_color, 0);
    lv_obj_align(ui->line1_label, LV_ALIGN_TOP_MID, 0, 0);
    ui->line2_label = lv_label_create(scr);
    lv_obj_set_width(ui->line2_label, lv_pct(100));
    lv_obj_set_style_text_align(ui->line2_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui->line2_label, text_color, 0);
    lv_obj_align_to(ui->line2_label, ui->line1_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    ui->line3_label = lv_label_create(scr);
    lv_obj_set_width(ui->line3_label, lv_pct(100));
    lv_obj_set_style_text_align(ui->line3_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui->line3_label, text_color, 0);
    lv_obj_align_to(ui->line3_label, ui->line2_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    ui->line4_label = lv_label_create(scr);
    lv_obj_set_width(ui->line4_label, lv_pct(100));
    lv_obj_set_style_text_align(ui->line4_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui->line4_label, text_color, 0);
    lv_obj_align_to(ui->line4_label, ui->line3_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}

/**
 * Install a periodic LVGL timer.
 */
void ui_create_timer(uint32_t period_ms, lv_timer_cb_t cb) {
    lv_timer_create(cb, period_ms, NULL);
}
