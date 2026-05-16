#include "bsp/display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common_ui.h"
#include "font/font_5x7.h"
#include "middle_wifi.h"
#include <stdio.h>
#include <string.h>

typedef struct {
  esp_err_t wifi_init_ret;
  bool wifi_sta_configured;
  bool wifi_sta_connected;
  int wifi_sta_rssi;
  int wifi_ap_clients;
} wifi_state_t;

typedef struct {
  common_ui *base;
} WiFi_UI;

static WiFi_UI ui;
static wifi_state_t wifi_state;
#if LV_USE_QRCODE
static lv_obj_t *prov_qr = NULL;
static char qr_last_payload[160] = {0};
#endif

static void wifi_qr_hide(void) {
#if LV_USE_QRCODE
  if (prov_qr) {
    lv_obj_del(prov_qr);
    prov_qr = NULL;
  }
  qr_last_payload[0] = '\0';
#endif
}

static bool wifi_qr_show(const char *service_name) {
#if LV_USE_QRCODE
  char payload[160] = {0};
  if (middle_wifi_get_prov_qr_payload(payload, sizeof(payload)) != ESP_OK) {
    if (!service_name || service_name[0] == '\0') {
      return false;
    }
    snprintf(payload,
             sizeof(payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"transport\":\"ble\",\"network\":\"wifi\"}",
             service_name);
  }

  if (!prov_qr) {
    prov_qr = lv_qrcode_create(lv_scr_act());
    if (!prov_qr) {
      return false;
    }

    lv_coord_t size = LV_MIN(lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act())) - 2;
    if (size < 18) {
      size = 18;
    }
    lv_qrcode_set_size(prov_qr, size);
    lv_qrcode_set_dark_color(prov_qr, lv_color_black());
    lv_qrcode_set_light_color(prov_qr, lv_color_white());
    lv_obj_set_style_border_width(prov_qr, 1, 0);
    lv_obj_set_style_border_color(prov_qr, lv_color_white(), 0);
  }

  if (strcmp(qr_last_payload, payload) != 0) {
    if (lv_qrcode_update(prov_qr, payload, strlen(payload)) != LV_RESULT_OK) {
      lv_obj_add_flag(prov_qr, LV_OBJ_FLAG_HIDDEN);
      return false;
    }
    strncpy(qr_last_payload, payload, sizeof(qr_last_payload) - 1);
    qr_last_payload[sizeof(qr_last_payload) - 1] = '\0';
  }

  lv_obj_clear_flag(prov_qr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_center(prov_qr);
  return true;
#else
  (void)service_name;
  return false;
#endif
}

static void wifi_ui_init(void) {
  /* =======================
   * 1. Create Base UI
   * ======================= */
  ui.base = common_ui_get();
  common_ui_init();
  common_ui *b = ui.base;
  /* =======================
   * 2. Layout & Fonts
   * ======================= */
  lv_obj_set_style_text_font(b->line1_label, &lv_font_5x7, 0);
  lv_obj_set_style_text_font(b->line2_label, &lv_font_5x7, 0);
  lv_obj_set_style_text_font(b->line3_label, &lv_font_5x7, 0);
  lv_obj_set_style_text_font(b->line4_label, &lv_font_5x7, 0);
  /* =======================
   * 3. line1 (Align Top Left)
   * ======================= */
  lv_obj_set_width(b->line1_label, lv_pct(100));
  lv_obj_set_style_text_align(b->line1_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(b->line1_label, LV_ALIGN_TOP_MID, 0, -1);
  /* =======================
   * 4. line2 (Align Mid)
   * ======================= */
  lv_obj_clear_flag(b->line2_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_width(b->line2_label, lv_pct(100));
  lv_obj_set_style_text_align(b->line2_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(b->line2_label, b->line1_label, LV_ALIGN_OUT_BOTTOM_MID, 0,-1);
  /* =======================
   * 5. line3 (Align Mid)
   * ======================= */
  lv_obj_clear_flag(b->line3_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_width(b->line3_label, lv_pct(100));
  lv_obj_set_style_text_align(b->line3_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(b->line3_label, b->line2_label, LV_ALIGN_OUT_BOTTOM_MID, 0,-1);
  /* =======================
   * 6. line4 (Align Mid)
   * ======================= */
  lv_obj_clear_flag(b->line4_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_width(b->line4_label, lv_pct(100));
  lv_obj_set_style_text_align(b->line4_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(b->line4_label, b->line3_label, LV_ALIGN_OUT_BOTTOM_MID, 0,-1);
}

static void wifi_ui_apply(const wifi_state_t *st) {
  /* =======================
   * 1. Title & Status Color
   * ======================= */
  example_ui_t *b = ui.base;
  snprintf(b->line1_text, sizeof(b->line1_text), "WIFI");
  lv_label_set_text(b->line1_label, b->line1_text);
  switch (st->wifi_init_ret) {
    case ESP_OK:
        lv_obj_set_style_text_color(b->line1_label, lv_color_hex(0x00FF00), 0);
        break;
    case ESP_ERR_NOT_SUPPORTED:
        lv_obj_set_style_text_color(b->line1_label, lv_color_hex(0x808080), 0);
        break;
    default:
        lv_obj_set_style_text_color(b->line1_label, lv_color_hex(0xFF0000), 0);
        break;
  }
  /* =======================
   * 2. Format Status Text
   * ======================= */
  if (st->wifi_init_ret == ESP_OK) {
    if (!st->wifi_sta_configured) {
      char prov_name[32] = {0};
      if (middle_wifi_get_prov_service_name(prov_name, sizeof(prov_name)) == ESP_OK) {
        snprintf(b->line2_text, sizeof(b->line2_text), "%s", prov_name);
      } else {
        snprintf(b->line2_text, sizeof(b->line2_text), "PROV_??????");
      }
      if (middle_wifi_is_provisioning_running()) {
        bool qr_ok = wifi_qr_show(prov_name);
        if (qr_ok) {
          snprintf(b->line3_text, sizeof(b->line3_text), "Provisioning: ACTIVE");
          snprintf(b->line4_text, sizeof(b->line4_text), "Scan QR with ESP app");
        } else {
          snprintf(b->line3_text, sizeof(b->line3_text), "Provisioning: ACTIVE");
          snprintf(b->line4_text, sizeof(b->line4_text), "QR unavailable on panel");
        }
      } else {
        wifi_qr_hide();
        snprintf(b->line3_text, sizeof(b->line3_text), "Provisioning: idle");
        snprintf(b->line4_text, sizeof(b->line4_text), "Open ESP BLE app");
      }
    } else {
      wifi_qr_hide();
      snprintf(b->line2_text, sizeof(b->line2_text), "AP:%u",
               (unsigned)st->wifi_ap_clients);
      snprintf(b->line3_text, sizeof(b->line3_text), "STA:%u",
               (unsigned)(st->wifi_sta_connected != 0));
      snprintf(b->line4_text, sizeof(b->line4_text), "RSSI:%d",
               st->wifi_sta_rssi);
    }
  } else {
    wifi_qr_hide();
    snprintf(b->line2_text, sizeof(b->line2_text), "Error");
    snprintf(b->line3_text, sizeof(b->line3_text), "R:%d", st->wifi_init_ret);
    b->line4_text[0] = '\0';
  }
  lv_label_set_text(b->line2_label, b->line2_text);
  lv_label_set_text(b->line3_label, b->line3_text);
  lv_label_set_text(b->line4_label, b->line4_text);
}

static void wifi_data_update(lv_timer_t *t) {
  /* =======================
   * 1. Query WiFi Status & Refresh UI
   * ======================= */
  middle_wifi_status_t status;
  esp_err_t r = middle_wifi_get_status(&status);
  wifi_state.wifi_init_ret = r;
  if (r == ESP_OK) {
    wifi_state.wifi_sta_configured = status.sta_configured;
    wifi_state.wifi_sta_connected = status.sta_connected;
    wifi_state.wifi_sta_rssi = status.sta_rssi;
    wifi_state.wifi_ap_clients = status.ap_clients;
  }
  wifi_ui_apply(&wifi_state);
}

void wifi_start(void) {
  /* =======================
   * 1. Start Display & UI
   * ======================= */
  bool locked = bsp_display_lock(0);
  if (locked) {
    wifi_ui_init();
    bsp_display_unlock();
  }

  /* =======================
   * 2. Configure & Enable WiFi
   * ======================= */
  middle_wifi_set_sta_config(NULL, NULL);
  wifi_state.wifi_init_ret = middle_wifi_init();

  /* =======================
   * 3. Periodic Refresh
   * ======================= */
  locked = bsp_display_lock(0);
  if (locked) {
    ui_create_timer(1000, wifi_data_update);
    bsp_display_unlock();
  }

  /* =======================
   * 4. Idle Loop
   * ======================= */
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
