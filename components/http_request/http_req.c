/**
                             *******************
*******************************    C  FILE    ********************************
**                           *******************                            **
**                                                                          **
**  Project     :                                                           **
**  Filename    : http_req.c                                                **
**  Version     : -.- (PCB : )                                              ** 
**  Revised by  :                                                           **
**  Date        : 2025.04.20                                                **
**                                                                          **
******************************************************************************/


/*********************************************************************************************************************/
/*----------------------------------------------------Includes-------------------------------------------------------*/
/*********************************************************************************************************************/

#include "http_req.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sdkconfig.h"

#include "esp_log.h"

/*********************************************************************************************************************/
/*-----------------------------------------------------Macro---------------------------------------------------------*/
/*********************************************************************************************************************/

#define MAX_REC_BUFFER_SIZE 2048

/*********************************************************************************************************************/
/*----------------------------------------------------Interrupt------------------------------------------------------*/
/*********************************************************************************************************************/


/*********************************************************************************************************************/
/*-------------------------------------------------Global Variable---------------------------------------------------*/
/*********************************************************************************************************************/

static const char *HTTP_REQ_TAG = "HttpRequest";

static char g_recvBuf[MAX_REC_BUFFER_SIZE];

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

bool HttpRequest_Req(
    const char *req,      /* INPUT */
    const char *server,   /* INPUT */
    const char *port      /* INPUT */
)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    
    int err = getaddrinfo(server, port, &hints, &res);

    if(err != 0 || res == NULL) {
        ESP_LOGE(HTTP_REQ_TAG, "DNS lookup failed err=%d res=%p", err, res);
        return false;
    }

    /* Code to print the resolved IP.
       Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(HTTP_REQ_TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));
   
    s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
        ESP_LOGE(HTTP_REQ_TAG, "... Failed to allocate socket.");
        freeaddrinfo(res);
        return false;
    }
    ESP_LOGI(HTTP_REQ_TAG, "... allocated socket");
   
    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(HTTP_REQ_TAG, "... socket connect failed errno=%d", errno);
        close(s);
        freeaddrinfo(res);
        return false;
    }
   
    ESP_LOGI(HTTP_REQ_TAG, "... connected");
    freeaddrinfo(res);

    if (write(s, req, strlen(req)) < 0) {
        ESP_LOGE(HTTP_REQ_TAG, "... socket send failed");
        close(s);
        return false;
    }
    ESP_LOGI(HTTP_REQ_TAG, "... socket send success");

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
            sizeof(receiving_timeout)) < 0) {
        ESP_LOGE(HTTP_REQ_TAG, "... failed to set socket receiving timeout");
        close(s);
        return false;
    }
    ESP_LOGI(HTTP_REQ_TAG, "... set socket receiving timeout success");

    /* Read HTTP response */    
    int  g_recvIdx = 0;
    while ((r = read(s, g_recvBuf + g_recvIdx, sizeof(g_recvBuf) - g_recvIdx - 1)) > 0) {
        g_recvIdx += r;
    
        if (g_recvIdx >= sizeof(g_recvBuf) - 1) {
            break; // 더 이상 저장 공간 없음
        }
    }
    g_recvBuf[g_recvIdx] = '\0';

    ESP_LOGI(HTTP_REQ_TAG, "Rec : %s", g_recvBuf);
    ESP_LOGI(HTTP_REQ_TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
    close(s);    

    return true;
}

bool HttpRequest_GetWord(
    char* buf,         /* OUTPUT */
    size_t buf_size    /* INPUT */
)
{
    size_t len = strlen(g_recvBuf);

    if (len >= buf_size) {
        return false;
    }

    strncpy(buf, g_recvBuf, buf_size);
    buf[buf_size - 1] = '\0';  

    g_recvBuf[0] = '\0';  // Clear the buffer for next use
    return true;
}