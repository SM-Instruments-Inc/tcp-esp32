#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/socket.h"
#include "netdb.h"
#include "errno.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "protocol_examples_common.h"

#define INVALID_SOCK (-1)
#define YIELD_TO_ALL_MS 50

#define DEFAULT_VREF 1100
#define MAX_SIZE 1000

static esp_adc_cal_characteristics_t * adc_chars;
static const adc_channel_t gas_channel = ADC_CHANNEL_6; // GPIO34 if gas, GPIO35 if mic
static const adc_channel_t mic_channel = ADC_CHANNEL_7;
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

SemaphoreHandle_t xmutex;

typedef struct _item_t {
    uint16_t mic;
    uint16_t gas;
}item_t;


typedef struct __circleQueue {
    int rear;
    int front;
    item_t * data;
    item_t * array_to_send;
}Queue;

Queue q;

void InitQueue(Queue * q) 
{
    q->front = 0;
    q->rear = 0;
    q->data = (item_t *)pvPortMalloc(sizeof(item_t) * MAX_SIZE);
    q->array_to_send = (item_t *)pvPortMalloc(sizeof(item_t) * (MAX_SIZE));
}
int IsEmpty(Queue * q) 
{
    if (q -> front == q -> rear)  // front와 rear가 같으면 큐는 비어있는 상태
        return true;
    else 
        return false;
    
}
int IsFull(Queue * q) 
{
    if ((q -> rear + 1) % MAX_SIZE == q -> front) 
    {
        return true;
    }
    
    return false;
}
void AddQ(Queue * q, item_t value) 
{
    if (IsFull(q)) 
    {
        printf("Queue is Full.\n");
        return;
    } 
    else 
    {
        q -> rear = (q -> rear + 1) % MAX_SIZE;
        q -> data[q -> rear] = value;
    }
    return;
}
item_t DeleteQ(Queue * q) 
{
    q -> front = (q -> front + 1) % MAX_SIZE;
    return q -> data[q -> front];
}
void AllPopQ(Queue * q) 
{
    for (int i = 0; i < MAX_SIZE - 1; i ++) 
        q -> array_to_send[i] = DeleteQ(q);
}

static void CheckEfuse(void) 
{
    // Check if TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) 
        printf("eFuse Two Point: Supported\n");
    else 
        printf("eFuse Two Point: NOT supported\n");
    // Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) 
        printf("eFuse Vref: Supported\n");
    else
        printf("eFuse Vref: NOT supported\n");
}

static void PrintCharValType(esp_adc_cal_value_t val_type) {
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
        printf("Characterized using Two Point Value\n");
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) 
        printf("Characterized using eFuse Vref\n");
    else
        printf("Characterized using Default Vref\n");
}

void AdcRead() 
{
    while (1) 
    {
        if (xmutex != NULL) 
        {
            if (IsEmpty(&q)) 
            {
                while(!IsFull(&q))
                {
                    uint16_t gas_value, mic_value;
                    // Multisampling
                    gas_value = adc1_get_raw((adc1_channel_t)gas_channel);
                    mic_value = adc1_get_raw((adc1_channel_t)mic_channel);
                    item_t item;
                    item.gas = gas_value;
                    item.mic = mic_value;
                    if (xSemaphoreTake(xmutex, portMAX_DELAY) == pdTRUE) 
                    {
                        AddQ(&q, item);
                        xSemaphoreGive(xmutex);
                    }

                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

static void LogSocketError(const char *tag, const int sock, const int err, const char *message)
{
    ESP_LOGE(tag, "[sock=%d]: %s\n"
                  "error=%d: %s", sock, message, err, strerror(err));
}

static int TryReceive(const char *tag, const int sock, char * data, size_t max_len)
{
    int len = recv(sock, data, max_len, 0);
    if (len < 0) 
    {
        if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;   // Not an error
        if (errno == ENOTCONN) 
        {
            ESP_LOGW(tag, "[sock=%d]: Connection closed", sock);
            return -2;  // Socket has been disconnected
        }
        LogSocketError(tag, sock, errno, "Error occurred during receiving");
        return -1;
    }

    return len;
}

static int SocketSend(const char *tag, const int sock, Queue *q, int len)
{
    int to_write = len;
    while (to_write > 0) 
    {
        int written = send(sock, q->array_to_send + (len - to_write), to_write, 0);
        if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) 
        {
            LogSocketError(tag, sock, errno, "Error occurred during sending");
            return -1;
        }
        to_write -= written;
    }
    return len;
}

static int MacAddressSend(const char *tag, const int sock, char *str, int len)
{
    int to_write = len;
    while (to_write > 0) 
    {
        int written = send(sock, str + (len - to_write), to_write, 0);
        if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            LogSocketError(tag, sock, errno, "Error occurred during sending");
            return -1;
        }
        to_write -= written;
    }
    return len;
}

static void DataSent(void *pv_parameters)
{
    static const char *TAG = "nonblocking-socket-client";
    static char rx_buffer[128];

    struct addrinfo hints = { .ai_socktype = SOCK_STREAM };
    struct addrinfo *address_info;
    int sock = INVALID_SOCK;

    int res = getaddrinfo(CONFIG_EXAMPLE_TCP_CLIENT_CONNECT_ADDRESS, CONFIG_EXAMPLE_TCP_CLIENT_CONNECT_PORT, &hints, &address_info);
    if (res != 0 || address_info == NULL)
        ESP_LOGE(TAG, "couldn't get hostname for `%s` "
                      "getaddrinfo() returns %d, addrinfo=%p", CONFIG_EXAMPLE_TCP_CLIENT_CONNECT_ADDRESS, res, address_info);

    while(1)
    {
        // Creating client's socket
        sock = socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);
        if (sock < 0) 
            LogSocketError(TAG, sock, errno, "Unable to create socket");
        ESP_LOGI(TAG, "Socket created, connecting to %s:%s", CONFIG_EXAMPLE_TCP_CLIENT_CONNECT_ADDRESS, CONFIG_EXAMPLE_TCP_CLIENT_CONNECT_PORT);

        // Marking the socket as non-blocking
        int flags = fcntl(sock, F_GETFL);
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
            LogSocketError(TAG, sock, errno, "Unable to set socket non blocking");

        if (connect(sock, address_info->ai_addr, address_info->ai_addrlen) != 0) 
        {
            if (errno == EINPROGRESS) 
            {
                ESP_LOGD(TAG, "connection in progress");
                fd_set fdset;
                FD_ZERO(&fdset);
                FD_SET(sock, &fdset);

                // Connection in progress -> have to wait until the connecting socket is marked as writable, i.e. connection completes
                res = select(sock+1, NULL, &fdset, NULL, NULL);
                if (res < 0) 
                    LogSocketError(TAG, sock, errno, "Error during connection: select for socket to be writable");
                else if (res == 0)
                    LogSocketError(TAG, sock, errno, "Connection timeout: select for socket to be writable");
                else {
                    int sockerr;
                    socklen_t len = (socklen_t)sizeof(int);

                    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (void*)(&sockerr), &len) < 0)
                        LogSocketError(TAG, sock, errno, "Error when getting socket error using getsockopt()");
                    if (sockerr)
                        LogSocketError(TAG, sock, sockerr, "Connection error");
                }
            } 
            else
                LogSocketError(TAG, sock, errno, "Socket is unable to connect");
        }

        int optval = 2;

        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof (optval));

        uint8_t mac[6];

        esp_efuse_mac_get_default(mac);

        char macStr[18] = { 0 };

        sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        printf("%s\n", macStr);

        int len = MacAddressSend(TAG, sock, macStr, sizeof(macStr));
        if (len < 0) 
        {
            ESP_LOGE(TAG, "Error occurred during SocketSend");
            break;
        }

        while(1)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            if(xmutex != NULL)
            {
                if(IsFull(&q))
                {
                    if(xSemaphoreTake(xmutex, portMAX_DELAY) == pdTRUE)
                    {
                        AllPopQ(&q);
                        xSemaphoreGive(xmutex);
                    }
                    int len = SocketSend(TAG, sock, &q, sizeof(item_t) * (MAX_SIZE-1));
                    if (len < 0) 
                    {
                        ESP_LOGE(TAG, "Error occurred during SocketSend");
                        break;
                    }
                    //Keep receiving until we have a reply
                    do 
                    {
                        len = TryReceive(TAG, sock, rx_buffer, 9);
                        if (len < 0) 
                        {
                            ESP_LOGE(TAG, "Error occurred during TryReceive");
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(YIELD_TO_ALL_MS));
                    } 
                    while (len == 0);
                        ESP_LOGI(TAG, "Received: %.*s", len, rx_buffer);
                }
                
            }
        }
        close(sock);
    }

    free(address_info);
    vTaskDelete(NULL);
}

void app_main(void)
{
    CheckEfuse();
    //Configure ADC
    adc1_config_width(width);
    adc1_config_channel_atten(gas_channel, atten);
    adc1_config_channel_atten(mic_channel, atten);

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
        unit,
        atten,
        width,
        DEFAULT_VREF,
        adc_chars
    );

    PrintCharValType(val_type);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    InitQueue(&q);

    xmutex = xSemaphoreCreateMutex();

    xTaskCreate(AdcRead, "AdcRead", 4096, NULL, 5, NULL);
    xTaskCreate(DataSent, "DataSent", 4096, NULL, 5, NULL);

}
