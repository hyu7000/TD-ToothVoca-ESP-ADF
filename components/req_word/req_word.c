/**
                             *******************
*******************************    C  FILE    ********************************
**                           *******************                            **
**                                                                          **
**  Project     :                                                           **
**  Filename    : req_word.c                                                **
**  Version     : -.- (PCB : )                                              ** 
**  Revised by  :                                                           **
**  Date        : 2025.04.20                                                **
**                                                                          **
******************************************************************************/


/*********************************************************************************************************************/
/*----------------------------------------------------Includes-------------------------------------------------------*/
/*********************************************************************************************************************/

#include "req_word.h"

#include <stddef.h>

#include "http_req.h"
#include "esp_log.h"

/*********************************************************************************************************************/
/*-----------------------------------------------------Macro---------------------------------------------------------*/
/*********************************************************************************************************************/

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER      "172.30.1.13"
#define WEB_PORT        "8080"
#define WEB_PATH        "/word"
#define MAX_BUFFER_SIZE 2048

/*********************************************************************************************************************/
/*----------------------------------------------------Interrupt------------------------------------------------------*/
/*********************************************************************************************************************/


/*********************************************************************************************************************/
/*-------------------------------------------------Global Variable---------------------------------------------------*/
/*********************************************************************************************************************/

static const char *REQ_WORD_TAG = "RequestWord";
static const char *REQUEST = "GET " WEB_PATH " HTTP/1.0\r\n"
    "Host: "WEB_SERVER":"WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

static char s_reqBodyBuf[MAX_BUFFER_SIZE] = {0};

static char word[128] = {0};
static char sentence[256] = {0};

/*********************************************************************************************************************/
/*-------------------------------------------------Data Structures---------------------------------------------------*/
/*********************************************************************************************************************/


/*********************************************************************************************************************/
/*-----------------------------------------------Function Prototypes-------------------------------------------------*/
/*********************************************************************************************************************/


/*********************************************************************************************************************/
/*-------------------------------------------------Private Function--------------------------------------------------*/
/*********************************************************************************************************************/

static void ReqWord_ParseResponse(
    char* buf,         /* INPUT */
    size_t buf_size    /* INPUT */
)
{
    char* body_start = strstr(buf, "\r\n\r\n");
    if (!body_start) {
        ESP_LOGE(REQ_WORD_TAG, "Failed to locate body start.");
        return;
    }

    body_start += 4;  // Move past the "\r\n\r\n"

    char* colon = strchr(body_start, ':');
    if (!colon) {
        ESP_LOGE(REQ_WORD_TAG, "Colon ':' not found in response body.");
        return;
    }

    // Extract word (before colon)
    size_t word_len = colon - body_start;
    memset(word, 0, sizeof(word));  // Clear the buffer
    if (word_len >= sizeof(word)) {
        ESP_LOGE(REQ_WORD_TAG, "Word is too long to store.");
        return;
    }
    strncpy(word, body_start, word_len);
    word[word_len] = '\0';

    // Extract sentence (after colon)
    const char* sentence_start = colon + 1;
    while (*sentence_start == ' ') sentence_start++;  // Skip leading spaces

    memset(sentence, 0, sizeof(sentence));  // Clear the buffer
    strncpy(sentence, sentence_start, sizeof(sentence) - 1);
    sentence[sizeof(sentence) - 1] = '\0';

    // Log results
    ESP_LOGI(REQ_WORD_TAG, "Parsed word: \"%s\"", word);
    ESP_LOGI(REQ_WORD_TAG, "Parsed sentence: \"%s\"", sentence);
}

/*********************************************************************************************************************/
/*-------------------------------------------------Extern Function---------------------------------------------------*/
/*********************************************************************************************************************/

bool ReqWord_Requset(void)
{
    if(HttpRequest_Req(REQUEST, WEB_SERVER, WEB_PORT) == false)
    {
        ESP_LOGE(REQ_WORD_TAG, "Request Failed");
        return false;
    }
    
    if(HttpRequest_GetWord(s_reqBodyBuf, sizeof(s_reqBodyBuf)) == false)
    {
        ESP_LOGE(REQ_WORD_TAG, "Get Word Failed");
        return false;
    }

    ReqWord_ParseResponse(s_reqBodyBuf, sizeof(s_reqBodyBuf));

    s_reqBodyBuf[0] = '\0';  // Clear the buffer for next request

    return true;
}

void ReqWord_GetWord(char* str)
{
    uint32_t len = (uint32_t)strlen(word);
    strncpy(str, word, (size_t)len);
    str[len] = '\0';  // 복사된 문자열 끝에 '\0' 추가
}

void ReqWord_GetSentence(char* str)
{
    uint32_t len = (uint32_t)strlen(sentence);
    strncpy(str, sentence, (size_t)len);
    str[len] = '\0';  // 복사된 문자열 끝에 '\0' 추가
}