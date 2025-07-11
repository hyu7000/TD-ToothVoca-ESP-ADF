/* Play an MP3 file from HTTP

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"

#include "audio_stream.h"
#include "lcd_touch.h"
#include "task.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

static const char *TAG = "TV_V1.0";

static void init_board_gpio()
{
    {
        gpio_config_t audio_mute_out = {
            .pin_bit_mask = 1ULL << GPIO_NUM_14,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&audio_mute_out);
        gpio_set_level(GPIO_NUM_14, 1);
    }
}

static void app_init(void)
{
    /* Init NVM */
    { 
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
            // NVS partition was truncated and needs to be erased
            // Retry nvs_flash_init
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
    }

    /* Init Network */
    {
        #if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
            ESP_ERROR_CHECK(esp_netif_init());
        #else
            tcpip_adapter_init();
        #endif
    }    

    /* Init Board */
    {
        init_board_gpio();
    }

    /* Init LCD */
    {
        LcdTouch_Init();        
    }

    /* Init Audio */
    {
        AudioStream_Init();
    }

    /* Init Task */
    {
        /* Note: This module should be initialized last during the initialization process */
        Task_Init();
    }
}

void app_main(void)
{
    app_init(); 
}
