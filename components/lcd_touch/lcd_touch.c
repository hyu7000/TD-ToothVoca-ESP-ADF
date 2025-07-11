/**
                             *******************
*******************************    C  FILE    ********************************
**                           *******************                            **
**                                                                          **
**  Project     :                                                           **
**  Filename    : lcd_touch.c                                               **
**  Version     : -.- (PCB : )                                              ** 
**  Revised by  :                                                           **
**  Date        : 2025.04.23                                                **
**                                                                          **
******************************************************************************/


/*********************************************************************************************************************/
/*----------------------------------------------------Includes-------------------------------------------------------*/
/*********************************************************************************************************************/

#include "lcd_touch.h"

#include <sys/param.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch.h"
#include "parser_char.h"
#include "Font.h"

#include "esp_log.h"

/*********************************************************************************************************************/
/*-----------------------------------------------------Macro---------------------------------------------------------*/
/*********************************************************************************************************************/

/* LCD Pin */
#define LT_PIN_NUM_LCD_RST        (6)
#define LT_PIN_NUM_BK_LIGHT       (7) 
#define LT_PIN_NUM_LCD_DC         (9) 
#define LT_PIN_NUM_LCD_CS         (10) 
#define LT_PIN_NUM_MOSI           (11) 
#define LT_PIN_NUM_SCLK           (12) 
#define LT_PIN_NUM_MISO           (13)  

/* Touch */
#define LT_PIN_NUM_TOUCH_DIN      (35)
#define LT_PIN_NUM_TOUCH_CLK      (36)
#define LT_PIN_NUM_TOUCH_DO       (37)
#define LT_PIN_NUM_TOUCH_CS       (39)
#define LT_PIN_NUM_TOUCH_IRQ      (8)

/* Color */
#define LT_BLACK                  (0x0000)
#define LT_WHITE                  (0xFFFF)
#define LT_RED                    (0xF800)
#define LT_GREEN                  (0x07E0)
#define LT_BLUE                   (0x001F)
#define LT_CYAN                   (0x07FF)
#define LT_MAGENTA                (0xF81F)
#define LT_YELLOW                 (0xFFE0)
#define LT_ORANGE                 (0xFD20)

/* LCD Config */
#define LT_LCD_V_RES              (240)
#define LT_LCD_H_RES              (320)

#define LT_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)
#define LT_LCD_BK_LIGHT_ON_LEVEL  (1)
#define LT_LCD_BK_LIGHT_OFF_LEVEL (0)

#define LT_LCD_CMD_BITS           (8)
#define LT_LCD_PARAM_BITS         (8)
#define LT_LVGL_DRAW_BUF_LINES    (20) // number of display lines in each draw buffer
#define LT_LCD_BIT_PER_PIXEL      (16)

#define LT_LCD_HOST               SPI2_HOST
#define LT_LCD_TOUCH_HOST         SPI3_HOST

#define LT_FONT_COLOR             LT_WHITE
#define LT_BACKGROUND_COLOR       LT_BLACK

/* Font */
#define LT_CONSONANTS_VOWELS_INDEX_IN_FONT       (95)
#define LT_COMPLETE_TYPE_TEXT_INDEX_IN_FONT      (189)
#define LT_X_SPACE_TEXT_PIXEL                    (1)
#define LT_Y_SPACE_TEXT_PIXEL                    (5)
#define LT_HORIZONTAL_MARGIN                     (20)

#define LT_GAP_UNICODE_FONTFILE                  (32)
#define LT_CALI_VALUE_OF_CV_INDEX                (PC_CONSONANTS_VOWELS_INDEX_IN_UNICODE  - LT_CONSONANTS_VOWELS_INDEX_IN_FONT)
#define LT_CALI_VALUE_OF_CT_INDEX                (PC_COMPLETE_TYPE_TEXT_INDEX_IN_UNICODE - LT_COMPLETE_TYPE_TEXT_INDEX_IN_FONT)

/*********************************************************************************************************************/
/*-------------------------------------------------Data Structures---------------------------------------------------*/
/*********************************************************************************************************************/

typedef struct{
    uint16_t color;
    uint8_t* data_buf;
    uint32_t buf_len;
    uint16_t  start_x_pos;
    uint16_t  start_y_pos;
    uint16_t width_px;
    uint16_t height_px;
}image_data_t;

typedef struct{
    uint16_t color;
    uint16_t  start_x_pos;
    uint16_t  start_y_pos;
    uint32_t fontIndex;
    uint32_t textWidth;   /* Output */
    uint32_t textHeight;  /* Output */
}text_data_t;
 
/*********************************************************************************************************************/
/*-------------------------------------------------Global Variable---------------------------------------------------*/
/*********************************************************************************************************************/

static const char *LCD_TOUCH_TAG = "LcdTouch";

static SemaphoreHandle_t refresh_finish = NULL;
static esp_lcd_panel_handle_t s_panelHandle = NULL;
static uint16_t s_backgroud_color = 0x0000; // black
static touch_callback s_touch_callback_f = NULL;
static bool isTouchOccurred = false;
static bool isInitDisplay = false;

/*********************************************************************************************************************/
/*----------------------------------------------------Interrupt------------------------------------------------------*/
/*********************************************************************************************************************/

IRAM_ATTR static bool LcdTouch_NotifyRefreshReady(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;

    xSemaphoreGiveFromISR(refresh_finish, &need_yield);
    return (need_yield == pdTRUE);
}

static void LcdTouch_TouchISR(esp_lcd_touch_handle_t tp)
{
    isTouchOccurred = true;
}

/*********************************************************************************************************************/
/*-----------------------------------------------Function Prototypes-------------------------------------------------*/
/*********************************************************************************************************************/

/*********************************************************************************************************************/
/*-------------------------------------------------Private Function--------------------------------------------------*/
/*********************************************************************************************************************/

static void LcdToudh_FillBackground(esp_lcd_panel_handle_t panel_handle, uint16_t color)
{
    refresh_finish = xSemaphoreCreateBinary();
    if(refresh_finish == NULL) {
        ESP_LOGE(LCD_TOUCH_TAG, "Failed to create semaphore for refresh finish notification");
        return;
    }

    uint16_t row_line = LT_LCD_V_RES / LT_LCD_BIT_PER_PIXEL;
    uint8_t byte_per_pixel = LT_LCD_BIT_PER_PIXEL / 8;
    uint32_t buf_size = row_line * LT_LCD_H_RES * byte_per_pixel;
    uint8_t *display_buf = (uint8_t *)heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA);
    if(display_buf == NULL) {
        ESP_LOGE(LCD_TOUCH_TAG, "Failed to allocate memory for color buffer");
        vSemaphoreDelete(refresh_finish);
        return;
    }

    s_backgroud_color = color;

    for(uint32_t i = 0; i < buf_size; i += 2)
    {
        display_buf[i]   = (uint8_t)(color >> 8);
        display_buf[i+1] = (uint8_t)(color);
    }

    for (int j = 0; j < LT_LCD_BIT_PER_PIXEL; j++) {    
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
            panel_handle,             // handler
            0,                        // start x pos
            j * row_line,             // start y pos
            LT_LCD_H_RES,      // end x pos
            (j + 1) * row_line,       // end y pos
            display_buf               // image buf
        ));
        xSemaphoreTake(refresh_finish, portMAX_DELAY);
    }

    free(display_buf);
    vSemaphoreDelete(refresh_finish);
}

static void LcdToudh_DrawImageData(esp_lcd_panel_handle_t panel_handle, const image_data_t* image_data)
{
    refresh_finish = xSemaphoreCreateBinary();
    if(refresh_finish == NULL) {
        ESP_LOGE(LCD_TOUCH_TAG, "Failed to create semaphore for refresh finish notification");
        return;
    }

    uint8_t byte_per_pixel = LT_LCD_BIT_PER_PIXEL / 8;
    uint32_t image_data_buf_idx = 0;
    for (int i = image_data->start_y_pos; i < (image_data->start_y_pos + image_data->height_px); i++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
            panel_handle,                                      // handler
            image_data->start_x_pos,                           // start x pos
            i,                                                 // start y pos
            image_data->start_x_pos + image_data->width_px,    // end x pos
            i + 1,                                             // end y pos
            &image_data->data_buf[image_data_buf_idx]          // image buf
        ));

        image_data_buf_idx += (image_data->width_px * byte_per_pixel);
        xSemaphoreTake(refresh_finish, portMAX_DELAY);
    }

    vSemaphoreDelete(refresh_finish);
}

static void LcdTouch_GetFontImageData(esp_lcd_panel_handle_t panel_handle, image_data_t* image_data, uint32_t fontIndex)
{
    if(Font.chars[fontIndex].image == NULL)
    {
        ESP_LOGE(LCD_TOUCH_TAG, "Font.chars[fontIndex].image is NULL");
        return;
    }    
    
    image_data->width_px = Font.chars[fontIndex].image->width;
    image_data->height_px = Font.chars[fontIndex].image->height;
    image_data->data_buf = NULL;
    image_data->buf_len = 0;

    uint8_t byte_per_pixel = LT_LCD_BIT_PER_PIXEL / 8;
    image_data->buf_len = image_data->width_px * image_data->height_px * byte_per_pixel;
    image_data->data_buf = (uint8_t *)heap_caps_calloc(1, image_data->buf_len , MALLOC_CAP_DMA);
    if(image_data->data_buf == NULL) {
        ESP_LOGE(LCD_TOUCH_TAG, "Failed to allocate memory for image data buffer");
        return;
    }

    uint32_t image_idx = 0;
    uint32_t width_idx = 0;
    uint32_t buf_idx = 0;
    while(buf_idx + 1 < image_data->buf_len)
    {    
        for(int j = 7; j >= 0; j--) // 1 byte
        {
            if(width_idx >= image_data->width_px)
            {
                width_idx = 0;
                break;
            }
            width_idx++;

            if((Font.chars[fontIndex].image->data[image_idx] & (1 << j)) == (1 << j))
            {
                image_data->data_buf[buf_idx++] = (uint8_t)(image_data->color >> 8);
                image_data->data_buf[buf_idx++] = (uint8_t)(image_data->color);
            }
            else
            {
                image_data->data_buf[buf_idx++] = (uint8_t)(s_backgroud_color >> 8);
                image_data->data_buf[buf_idx++] = (uint8_t)(s_backgroud_color);
            }
        }

        image_idx++;
    }
}

static void LcdTouch_DrawText(esp_lcd_panel_handle_t panel_handle, text_data_t* text_data)
{
    image_data_t image_data = { 
        .color = text_data->color,
        .start_x_pos = text_data->start_x_pos,
        .start_y_pos = text_data->start_y_pos,
    };

    LcdTouch_GetFontImageData(panel_handle, &image_data, text_data->fontIndex);
    LcdToudh_DrawImageData(panel_handle, &image_data);

    text_data->textWidth  = image_data.width_px;
    text_data->textHeight = image_data.height_px;

    if(image_data.data_buf != NULL) {
        free(image_data.data_buf);
    }
}

static uint32_t LcdTouch_GetFontIndexOfUnicode(uint32_t unicode)
{
    uint32_t code;

    if(unicode < 127)
    {
        code = unicode - LT_GAP_UNICODE_FONTFILE;
    }
    else if(unicode >= PC_COMPLETE_TYPE_TEXT_INDEX_IN_UNICODE)
    {
        code = unicode - LT_CALI_VALUE_OF_CT_INDEX; //유니코드와 Font.h 파일간의 배열 인덱스 차이 계산
    }
    else
    {
        code = unicode - LT_CALI_VALUE_OF_CV_INDEX; //유니코드와 Font.h 파일간의 배열 인덱스 차이 계산
    }

    return code;
}

static void LcdTouch_InitDisplay(void)
{
    ESP_LOGI(LCD_TOUCH_TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LT_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    ESP_LOGI(LCD_TOUCH_TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = LT_PIN_NUM_SCLK,
        .mosi_io_num = LT_PIN_NUM_MOSI,
        .miso_io_num = LT_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LT_LCD_V_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LT_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));    

    ESP_LOGI(LCD_TOUCH_TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LT_PIN_NUM_LCD_DC,
        .cs_gpio_num = LT_PIN_NUM_LCD_CS,
        .pclk_hz = LT_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LT_LCD_CMD_BITS,
        .lcd_param_bits = LT_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = LcdTouch_NotifyRefreshReady,
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LT_LCD_HOST, &io_config, &io_handle));
    
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LT_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    ESP_LOGI(LCD_TOUCH_TAG, "Install ILI9341 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &s_panelHandle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panelHandle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panelHandle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panelHandle, true, false));
    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panelHandle, false));

    ESP_LOGI(LCD_TOUCH_TAG, "Turn on LCD backlight");
    gpio_set_level(LT_PIN_NUM_BK_LIGHT, LT_LCD_BK_LIGHT_OFF_LEVEL);

    LcdToudh_FillBackground(s_panelHandle, LT_BACKGROUND_COLOR);  

    isInitDisplay = true;
    ESP_LOGI(LCD_TOUCH_TAG, "Initialized LCD Touch : %d", isInitDisplay);
}

static void LcdTouch_InitTouchPanel(void)
{
    ESP_LOGI(LCD_TOUCH_TAG, "Initialize SPI bus");
    spi_bus_config_t touchBuscfg = {
        .sclk_io_num = LT_PIN_NUM_TOUCH_CLK,
        .mosi_io_num = LT_PIN_NUM_TOUCH_DIN,
        .miso_io_num = LT_PIN_NUM_TOUCH_DO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LT_LCD_V_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LT_LCD_TOUCH_HOST, &touchBuscfg, SPI_DMA_CH_AUTO)); 

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;    
    esp_lcd_panel_io_spi_config_t tp_io_config = {
        .dc_gpio_num = -1,
        .cs_gpio_num = LT_PIN_NUM_TOUCH_CS,
        .pclk_hz = (2 * 1000 * 1000),
        .lcd_cmd_bits = LT_LCD_CMD_BITS,
        .lcd_param_bits = LT_LCD_PARAM_BITS,
        .trans_queue_depth = 1,
        .spi_mode = 0,
    };
    // Attach the TOUCH to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LT_LCD_TOUCH_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LT_LCD_H_RES,
        .y_max = LT_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = LT_PIN_NUM_TOUCH_IRQ,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .interrupt_callback = LcdTouch_TouchISR,
    };

    esp_lcd_touch_handle_t tp = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    tp->io = tp_io_handle;
    memcpy(&tp->config, &tp_cfg, sizeof(esp_lcd_touch_config_t));

    ESP_LOGI(LCD_TOUCH_TAG, "Start Register Isr");
    if (tp_cfg.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = (tp_cfg.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(tp_cfg.int_gpio_num)
        };

        ESP_ERROR_CHECK(gpio_config(&int_gpio_config));

        /* Register interrupt callback */
        if (tp_cfg.interrupt_callback) {
            ESP_LOGI(LCD_TOUCH_TAG, "Register Isr");
            ESP_ERROR_CHECK(esp_lcd_touch_register_interrupt_callback(tp, tp_cfg.interrupt_callback));
        }
    }
}

/*********************************************************************************************************************/
/*-------------------------------------------------Extern Function---------------------------------------------------*/
/*********************************************************************************************************************/

void LcdTouch_Init(void)
{
    LcdTouch_InitDisplay();
    LcdTouch_InitTouchPanel();
}

void LcdTouch_DrawString(const char* str, uint32_t x, uint32_t y)
{
    if(isInitDisplay == false) return;

    uint32_t str_len = strlen(str);
    
    uint32_t str_index = 0;
    uint8_t out_len = 0;
    uint32_t unicode = 0; 
    text_data_t text_data = { 
        .color = LT_FONT_COLOR,
        .start_x_pos = x,
        .start_y_pos = y,
        .fontIndex = 0,
    };

    while(str_index < str_len)
    {
        out_len = 0;
        unicode = ParserChar_GetUnicode((const uint8_t*)&str[str_index], &out_len);
        text_data.fontIndex = LcdTouch_GetFontIndexOfUnicode(unicode);

        LcdTouch_DrawText(s_panelHandle, &text_data);

        text_data.start_x_pos += (text_data.textWidth + LT_X_SPACE_TEXT_PIXEL);

        if(text_data.start_x_pos >= (LT_LCD_H_RES - LT_HORIZONTAL_MARGIN))
        {
            text_data.start_x_pos = LT_HORIZONTAL_MARGIN;
            text_data.start_y_pos = text_data.start_y_pos + text_data.textHeight + LT_Y_SPACE_TEXT_PIXEL;
        }

        str_index += out_len;
    }
}

void LcdTouch_TurnOff(void)
{
    if(isInitDisplay == false) return;

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panelHandle, false));
    gpio_set_level(LT_PIN_NUM_BK_LIGHT, LT_LCD_BK_LIGHT_OFF_LEVEL);
}

void LcdTouch_TurnOn(void)
{
    if(isInitDisplay == false) return;

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panelHandle, true));
    gpio_set_level(LT_PIN_NUM_BK_LIGHT, LT_LCD_BK_LIGHT_ON_LEVEL);
}

void LcdTouch_ResetBackground(void)
{
    if(isInitDisplay == false) return;

    LcdToudh_FillBackground(s_panelHandle, s_backgroud_color);
}

void LcdTouch_SetTouchCallback(touch_callback callback)
{
    s_touch_callback_f = callback;
}

void LcdTouch_Task(void)
{
    if(isTouchOccurred)
    {
        if(s_touch_callback_f != NULL)
        {
            s_touch_callback_f();
        }

        isTouchOccurred = false;
    }
}