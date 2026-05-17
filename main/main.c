#include "bsp/display.h"
#include "bsp/esp32_s3_matrix.h"
#include "bsp/config.h"
#include "common_ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "lvgl.h"
#include <stdbool.h>

// Tab modules
#include "03_Matrix_QMI/matrix_qmi.h"
#include "04_Matrix_RTC/matrix_rtc.h"
#include "05_Matrix_SDCard/matrix_sdcard.h"
#include "06_Matrix_SHTC3/matrix_shtc3.h"
#include "07_Matrix_WiFi/matrix_wifi.h"
#include "08_Matrix_Audio/matrix_audio.h"

static const char *TAG = "Tab-UI";

#define NUM_TABS 6

typedef struct {
  const char *name;
  void (*start_func)(void);
} tab_config_t;

static const tab_config_t tabs[NUM_TABS] = {
  {"RTC",    rtc_start},
  {"SHTC3",  shtc3_start},
  {"QMI",    qmi_start},
  {"WiFi",   wifi_start},
  {"SDCard", sdcard_start},
  {"Audio",  audio_start},
};

static QueueHandle_t     tab_switch_queue = NULL;
static TaskHandle_t      module_task_handle = NULL;
static button_handle_t   boot_button = NULL;
static volatile int      current_tab = 0;

/* -----------------------------------------------------------------------
 * Module task wrapper – each module blocks in its own while(true) loop.
 * ----------------------------------------------------------------------- */
typedef struct { int idx; } module_task_arg_t;
static module_task_arg_t module_arg;   /* single instance, re-used per switch */

static void module_task_fn(void *arg) {
  int idx = ((module_task_arg_t *)arg)->idx;
  ESP_LOGI(TAG, "Starting module: %s", tabs[idx].name);
  tabs[idx].start_func();   /* blocks until task is deleted */
  vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * Boot button callback – runs from esp_timer task, must not call LVGL.
 * Sends the *next* tab index into the queue and returns immediately.
 * ----------------------------------------------------------------------- */
static void boot_button_callback(void *button_handle, void *usr_data) {
  int next = (current_tab + 1) % NUM_TABS;
  xQueueSend(tab_switch_queue, &next, 0);
}

/* -----------------------------------------------------------------------
 * Launch module idx: kill any running module task, wipe LVGL state, spawn
 * a new task.  Must be called from app_main (not from ISR / timer context).
 * ----------------------------------------------------------------------- */
static void switch_to_tab(int idx) {
  /* 1. Kill the running module task */
  if (module_task_handle != NULL) {
    vTaskDelete(module_task_handle);
    module_task_handle = NULL;
  }

  /* 2. Clean up LVGL: delete timers then wipe the screen */
  bool locked = bsp_display_lock(1000);
  if (locked) {
    ui_delete_all_timers();
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    bsp_display_unlock();
  }

  /* 3. Update state and spawn the new module task */
  current_tab = idx;
  module_arg.idx = idx;
  xTaskCreate(module_task_fn, tabs[idx].name, 4096, &module_arg,
              tskIDLE_PRIORITY + 1, &module_task_handle);
}

/* -----------------------------------------------------------------------
 * Button initialisation
 * ----------------------------------------------------------------------- */
static void button_init(void) {
  const button_gpio_config_t gpio_cfg = {
      .gpio_num    = BSP_BUTTON_MAIN_IO,
      .active_level = 0,
  };
  const button_config_t btn_cfg = {0};

  esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &boot_button);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Button create failed: %s", esp_err_to_name(ret));
    return;
  }
  ret = iot_button_register_cb(boot_button, BUTTON_SINGLE_CLICK, NULL,
                               boot_button_callback, NULL);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Boot button ready – single click to switch tab");
  } else {
    ESP_LOGE(TAG, "Button CB register failed: %s", esp_err_to_name(ret));
  }
}

void app_main(void) {
  init_display();

  tab_switch_queue = xQueueCreate(4, sizeof(int));

  button_init();

  /* Start first module */
  switch_to_tab(0);

  /* Manager loop: wait for button presses and switch modules */
  int next_tab;
  while (true) {
    if (xQueueReceive(tab_switch_queue, &next_tab, portMAX_DELAY) == pdTRUE) {
      ESP_LOGI(TAG, "Switching to tab: %s", tabs[next_tab].name);
      switch_to_tab(next_tab);
    }
  }
}
