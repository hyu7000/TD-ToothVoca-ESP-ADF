/**
                             *******************
*******************************    C  FILE    ********************************
**                           *******************                            **
**                                                                          **
**  Project     :                                                           **
**  Filename    : task.c                                                    **
**  Version     : -.- (PCB : )                                              ** 
**  Revised by  :                                                           **
**  Date        : 2025.04.20                                                **
**                                                                          **
******************************************************************************/


/*********************************************************************************************************************/
/*----------------------------------------------------Includes-------------------------------------------------------*/
/*********************************************************************************************************************/

#include "task.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_wifi.h"
#include "esp_sleep.h"

#include "audio_stream.h"
#include "req_word.h"
#include "lcd_touch.h"

#include "esp_log.h"

/*********************************************************************************************************************/
/*-----------------------------------------------------Macro---------------------------------------------------------*/
/*********************************************************************************************************************/

#define TV_TASK_TIME_MIN        (2)
#define TV_TASK_TIME_SEC        (0)
#define TV_TASK_REPEAT_TIME_SEC (15)

/*********************************************************************************************************************/
/*----------------------------------------------------Interrupt------------------------------------------------------*/
/*********************************************************************************************************************/


/*********************************************************************************************************************/
/*-------------------------------------------------Data Structures---------------------------------------------------*/
/*********************************************************************************************************************/

typedef struct
{
    bool isNewWord;
    char word[100];
    uint32_t word_x;
    uint32_t word_y;

    bool isNewSentence;
    char sentence[256];
    uint32_t sentence_x;
    uint32_t sentence_y;

    bool isNewTime;
    char time[20];
    uint32_t time_x;
    uint32_t time_y;
}ScreenData;

/*********************************************************************************************************************/
/*-------------------------------------------------Global Variable---------------------------------------------------*/
/*********************************************************************************************************************/

static const char *TASK_TAG = "Task";
static bool s_isWordRequest = false;
static bool s_isStartTimer  = false;
static uint32_t s_timerTick = 0;

static ScreenData s_wordData = {
    .isNewWord = false,
    .word = {0},
    .word_x = 10,
    .word_y = 10,

    .isNewSentence = false,
    .sentence = {0},
    .sentence_x = 10,
    .sentence_y = 60,

    .isNewTime = false,
    .time = {0},
    .time_x = 150,
    .time_y = 200
};

/*********************************************************************************************************************/
/*-----------------------------------------------Function Prototypes-------------------------------------------------*/
/*********************************************************************************************************************/


/*********************************************************************************************************************/
/*-------------------------------------------------Private Function--------------------------------------------------*/
/*********************************************************************************************************************/

static void Task_TouchCallback(void)
{
    ESP_LOGI(TASK_TAG, "Touch occurred!");

    s_isStartTimer = true;

    LcdTouch_TurnOn();
}

static void Task_AudioStream_Wrapper(void *pvParameters)
{
    while (1)
    {
        AudioStream_Task();  

        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay
    }
}

static void Task_UpdateScreen(ScreenData* screeenData)
{
    if(screeenData->isNewWord)
    {
        LcdTouch_DrawString(screeenData->word, screeenData->word_x, screeenData->word_y);
        screeenData->isNewWord = false;
    }

    if(screeenData->isNewSentence)
    {
        LcdTouch_DrawString(screeenData->sentence, screeenData->sentence_x, screeenData->sentence_y);
        screeenData->isNewSentence = false;
    }

    if(screeenData->isNewTime)
    {
        LcdTouch_DrawString(screeenData->time, screeenData->time_x, screeenData->time_y);
        screeenData->isNewTime = false;
    }
}

static void Task_LcdTouch(void *pvParameters)
{
    ScreenData* screeenData = (ScreenData*)pvParameters;

    while (1)
    {
        LcdTouch_Task(); /* check touched state */

        Task_UpdateScreen(screeenData);

        vTaskDelay(pdMS_TO_TICKS(500)); // 500ms delay
    }
}

static void Task_TimerTask(void *pvParameters)
{
    ScreenData* screeenData = (ScreenData*)pvParameters;
    uint8_t min = TV_TASK_TIME_MIN, sec = TV_TASK_TIME_SEC;

    while (1)
    {
        s_timerTick++; // 1초마다 증가

        if(s_isStartTimer)
        {
            sprintf(screeenData->time, "%02d : %02d", min, sec);
            ESP_LOGI(TASK_TAG, "timeStr : %s", screeenData->time);
            screeenData->isNewTime = true;
            
            sec--;
            if(sec > 60)
            {
                s_isWordRequest = true; 
                sec = 59;
                min--;
                if(min > 60)
                {
                    min = TV_TASK_TIME_MIN;
                }           
            }
           
            if((sec == 0) && (min == 0))
            {
                s_isStartTimer = false;
                min = TV_TASK_TIME_MIN;
                sec = TV_TASK_TIME_SEC;
                ESP_LOGI(TASK_TAG, "Start Sleep");
                
                LcdTouch_TurnOff();
                esp_light_sleep_start();
            }
            else if(((sec % TV_TASK_REPEAT_TIME_SEC) == 0) && (sec != 0))
            {
                AudioStream_Run();
            }            
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // 1초 대기
    }
}

static void Task_MainTask(void *pvParameters)
{
    ScreenData* screeenData = (ScreenData*)pvParameters;

    while (1)
    {        
        if(s_isWordRequest)
        {
            if(ReqWord_Requset())
            {
                LcdTouch_ResetBackground();

                ReqWord_GetWord(screeenData->word);
                ESP_LOGI(TASK_TAG, "Request Word : %s", screeenData->word);
                screeenData->isNewWord = true;
                
                ReqWord_GetSentence(screeenData->sentence);
                ESP_LOGI(TASK_TAG, "Request Sentence : %s", screeenData->sentence);
                screeenData->isNewSentence = true;

                AudioStream_Run();
            }

            s_isWordRequest = false; // Reset touch flag
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // 50ms 대기
    }
}

/*********************************************************************************************************************/
/*-------------------------------------------------Extern Function---------------------------------------------------*/
/*********************************************************************************************************************/

void Task_Init(void)
{   
    esp_sleep_enable_ext0_wakeup(8, 0);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    LcdTouch_SetTouchCallback(Task_TouchCallback);

    
    xTaskCreatePinnedToCore(
        Task_AudioStream_Wrapper,  // Task 함수
        "AudioStreamTask",         // 이름
        4096,                      // 스택 크기 (적절히 조정)
        NULL,                      // 파라미터
        5,                         // 우선순위
        NULL,                      // 핸들 (필요하면 저장)
        tskNO_AFFINITY             // 코어 지정 (0, 1, or tskNO_AFFINITY)
    );
    
    xTaskCreatePinnedToCore(
        Task_LcdTouch,     // Task 함수
        "LcdTouchTask",            // 이름
        4096,                      // 스택 크기 (적절히 조정)
        (void*)&s_wordData,        // 파라미터
        5,                         // 우선순위
        NULL,                      // 핸들 (필요하면 저장)
        tskNO_AFFINITY             // 코어 지정 (0, 1, or tskNO_AFFINITY)
    );

    xTaskCreatePinnedToCore(
        Task_TimerTask,            // Task 함수
        "TimerTask",               // 이름
        4096,                      // 스택 크기 (적절히 조정)
        (void*)&s_wordData,        // 파라미터
        5,                         // 우선순위
        NULL,                      // 핸들 (필요하면 저장)
        tskNO_AFFINITY             // 코어 지정 (0, 1, or tskNO_AFFINITY)
    );

    
    xTaskCreatePinnedToCore(
        Task_MainTask,             // Task 함수
        "MainTask",                // 이름
        4096,                      // 스택 크기 (적절히 조정)
        (void*)&s_wordData,        // 파라미터
        5,                         // 우선순위
        NULL,                      // 핸들 (필요하면 저장)
        tskNO_AFFINITY             // 코어 지정 (0, 1, or tskNO_AFFINITY)
    );    

    esp_light_sleep_start();
}