/**
 *******************
 *******************************    C  FILE    ********************************
 **                           *******************                            **
 **                                                                          **
 **  Project     :                                                           **
 **  Filename    : audio_stream.c                                            **
 **  Version     : -.- (PCB : )                                              **
 **  Revised by  :                                                           **
 **  Date        : 2025.04.19                                                **
 **                                                                          **
 ******************************************************************************/

/*********************************************************************************************************************/
/*----------------------------------------------------Includes-------------------------------------------------------*/
/*********************************************************************************************************************/

#include "audio_stream.h"

#include "sdkconfig.h"
#include "audio_pipeline.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"

#include "esp_log.h"

/*********************************************************************************************************************/
/*-----------------------------------------------------Macro---------------------------------------------------------*/
/*********************************************************************************************************************/

/*********************************************************************************************************************/
/*----------------------------------------------------Interrupt------------------------------------------------------*/
/*********************************************************************************************************************/

/*********************************************************************************************************************/
/*-------------------------------------------------Global Variable---------------------------------------------------*/
/*********************************************************************************************************************/

static const char *AUDIO_STREAM_TAG = "AudioStream";
static const char *AUDIO_STREAM_URI = "http://172.30.1.13:8080/audio.mp3";

static audio_pipeline_handle_t pipeline;
static audio_element_handle_t http_stream_reader, i2s_stream_writer, volume_controller, mp3_decoder;
static esp_periph_set_handle_t set;
static audio_event_iface_handle_t evt;
static bool s_isInitAudioStream = false;

/*********************************************************************************************************************/
/*-------------------------------------------------Data Structures---------------------------------------------------*/
/*********************************************************************************************************************/

/*********************************************************************************************************************/
/*-----------------------------------------------Function Prototypes-------------------------------------------------*/
/*********************************************************************************************************************/

/*********************************************************************************************************************/
/*-------------------------------------------------Private Function--------------------------------------------------*/
/*********************************************************************************************************************/

/*********************************************************************************************************************/
/*-------------------------------------------------Extern Function---------------------------------------------------*/
/*********************************************************************************************************************/

void AudioStream_Init(void)
{
    ESP_LOGI(AUDIO_STREAM_TAG, "[2.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(AUDIO_STREAM_TAG, "[2.1] Create http stream to read data");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_stream_reader = http_stream_init(&http_cfg);
    
    ESP_LOGI(AUDIO_STREAM_TAG, "[2.2] Create mp3 decoder to decode mp3 file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(AUDIO_STREAM_TAG, "[2.3] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.use_alc = true;
    i2s_cfg.volume = 63;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(AUDIO_STREAM_TAG, "[2.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(AUDIO_STREAM_TAG, "[2.5] Link it together http_stream-->mp3_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"http", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(AUDIO_STREAM_TAG, "[2.6] Set up  uri (http as http_stream, mp3 as mp3 decoder, and default output is i2s)");
    audio_element_set_uri(http_stream_reader, AUDIO_STREAM_URI);

    ESP_LOGI(AUDIO_STREAM_TAG, "[ 3 ] Start and wait for Wi-Fi network");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    // Example of using an audio event -- START
    ESP_LOGI(AUDIO_STREAM_TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(AUDIO_STREAM_TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(AUDIO_STREAM_TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    s_isInitAudioStream = true;
}

void AudioStream_Deinit(void)
{
    if(s_isInitAudioStream == false) return;

    ESP_LOGI(AUDIO_STREAM_TAG, "Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_unregister(pipeline, http_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, mp3_decoder);

    audio_pipeline_remove_listener(pipeline);

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(http_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(mp3_decoder);
    esp_periph_set_destroy(set);

    s_isInitAudioStream = true;
}

void AudioStream_Run(void)
{
    if(s_isInitAudioStream == false) return;

    ESP_LOGI(AUDIO_STREAM_TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);

    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_elements(pipeline);
    audio_pipeline_change_state(pipeline, AEL_STATE_INIT);  // 초기 상태로

    audio_pipeline_run(pipeline);
}

void AudioStream_Task(void)
{
    if(s_isInitAudioStream == false) return;

    audio_event_iface_msg_t msg;
    esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(AUDIO_STREAM_TAG, "[ * ] Event interface error : %d", ret);
        return;
    }

    if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && 
        msg.source == (void *)mp3_decoder && 
        msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            
        audio_element_info_t music_info = {0};
        audio_element_getinfo(mp3_decoder, &music_info);

        ESP_LOGI(AUDIO_STREAM_TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                 music_info.sample_rates, music_info.bits, music_info.channels);
        
        i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
        return;
    }
    else if(msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && 
        msg.source == (void *)volume_controller)
        {
            ESP_LOGI(AUDIO_STREAM_TAG, "[ * ] Message received from volume_controller. CMD: 0x%x", msg.cmd);
        }

    /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
    if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)i2s_stream_writer && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
        ESP_LOGW(AUDIO_STREAM_TAG, "[ * ] Stop event received");
        return;
    }
}