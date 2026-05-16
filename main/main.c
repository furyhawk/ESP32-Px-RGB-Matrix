#include "bsp/display.h"
#include "bsp/esp32_s3_matrix.h"
#include "bsp/config.h"
#include "common_ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "lvgl.h"
#include "font/font_5x7.h"
#include <stdbool.h>

// Tab modules
#include "03_Matrix_QMI/matrix_qmi.h"
#include "04_Matrix_RTC/matrix_rtc.h"
#include "05_Matrix_SDCard/matrix_sdcard.h"
#include "06_Matrix_SHTC3/matrix_shtc3.h"
#include "07_Matrix_WiFi/matrix_wifi.h"
#include "08_Matrix_Audio/matrix_audio.h"

static const char *TAG = "Tab-UI";

#define STEP_DELAY_MS 2000
#define NUM_TABS 6

// Tab configuration
typedef struct {
  const char *name;
  void (*start_func)(void);
} tab_config_t;

static const tab_config_t tabs[NUM_TABS] = {
    {"QMI", qmi_start},
    {"RTC", rtc_start},
    {"SDCard", sdcard_start},
    {"SHTC3", shtc3_start},
    {"WiFi", wifi_start},
    {"Audio", audio_start},
};

// Global state
static lv_obj_t *tabview = NULL;
static int current_tab = 0;
static button_handle_t boot_button = NULL;

/**
 * Boot button callback - switch to next tab on single click
 */
static void boot_button_callback(void *button_handle, void *usr_data) {
  current_tab = (current_tab + 1) % NUM_TABS;
  ESP_LOGI(TAG, "Switching to tab: %s", tabs[current_tab].name);
  
  if (tabview) {
    bool locked = bsp_display_lock(1000);
    if (locked) {
      lv_tabview_set_act(tabview, current_tab, LV_ANIM_ON);
      bsp_display_unlock();
    }
  }
}

/**
 * Initialize tab view with all modules
 */
static void tabview_init(void) {
  bool locked = bsp_display_lock(1000);
  if (!locked) return;

  lv_obj_t *scr = lv_scr_act();
  if (!scr) {
    bsp_display_unlock();
    return;
  }

  // Create tabview
  tabview = lv_tabview_create(scr);
  lv_obj_set_size(tabview, lv_pct(100), lv_pct(100));

  // Create tabs for each module
  for (int i = 0; i < NUM_TABS; i++) {
    lv_obj_t *tab = lv_tabview_add_tab(tabview, tabs[i].name);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x000000), 0);
  }

  bsp_display_unlock();
}

/**
 * Start current tab module
 */
static void start_current_tab(void) {
  if (current_tab < NUM_TABS && tabs[current_tab].start_func) {
    ESP_LOGI(TAG, "Starting tab: %s", tabs[current_tab].name);
    tabs[current_tab].start_func();
  }
}

/**
 * Initialize boot button for tab switching
 */
static void button_init(void) {
  // Configure boot button (GPIO 0, active low)
  const button_gpio_config_t gpio_cfg = {
      .gpio_num = BSP_BUTTON_MAIN_IO,
      .active_level = 0,
  };
  
  const button_config_t btn_cfg = {0};
  
  esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &boot_button);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create button: %s", esp_err_to_name(ret));
    return;
  }
  
  // Register single-click event to switch tabs
  ret = iot_button_register_cb(boot_button, BUTTON_SINGLE_CLICK, NULL, boot_button_callback, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register button callback: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Boot button configured for tab switching");
  }
}

void app_main(void) {
  init_display();
  
  // Initialize button for tab switching
  button_init();
  
  // Create tab view
  tabview_init();
  
  // Start the first tab module
  start_current_tab();
  
  // Keep the app running
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));
  }
}
