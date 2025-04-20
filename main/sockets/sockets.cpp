#include "sockets.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_wifi.h"

#include "kd_common.h"

#include "cJSON.h"
#include <esp_partition.h>
#include <mbedtls/base64.h>

static const char* TAG = "sockets";
TaskHandle_t xSocketsTask = NULL;

QueueHandle_t xSocketsQueue = NULL;
esp_websocket_client_handle_t client = NULL;

typedef struct ProcessableMessage_t {
    char* message;
    size_t message_len;
    bool is_outbox;
} ProcessableMessage_t;

static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;
    static char* dbuf = NULL;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        attempt_coredump_upload();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->payload_offset == 0) {
            free(dbuf);
            dbuf = (char*)heap_caps_calloc(data->payload_len + 1, sizeof(char), MALLOC_CAP_SPIRAM);
            if (dbuf == NULL) {
                ESP_LOGE(TAG, "malloc failed: dbuf (%d)", data->payload_len);
                return;
            }
        }

        if (dbuf) {
            memcpy(dbuf + data->payload_offset, data->data_ptr, data->data_len);
        }

        if (data->payload_len + data->payload_offset >= data->data_len) {
            ProcessableMessage_t message;
            message.message = dbuf;
            message.message_len = data->payload_len;
            message.is_outbox = false;

            if (xQueueSend(xSocketsQueue, &message, pdMS_TO_TICKS(50)) != pdTRUE) {
                ESP_LOGE(TAG, "failed to send message to queue");
                free(dbuf);
            }
        }

        break;
    default:
        break;
    }
}

void sockets_task(void* pvParameter)
{
    while (1) {
        if (kd_common_crypto_get_state() != CryptoState_t::CRYPTO_STATE_VALID_CERT) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        break;
    }

    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    esp_ds_data_ctx_t* ds_data_ctx = kd_common_crypto_get_ctx();
    char* cert = (char*)calloc(4096, sizeof(char));
    size_t cert_len = 4096;

    kd_common_get_device_cert(cert, &cert_len);

    esp_websocket_client_config_t websocket_cfg = {
        .uri = SOCKETS_URI,
        .port = 443,
        .client_cert = cert,
        .client_cert_len = cert_len + 1,
        .client_ds_data = ds_data_ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = 5000,
    };

    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void*)client);

    ProcessableMessage_t message;

    while (1)
    {
        if (xQueueReceive(xSocketsQueue, &message, pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (message.message == NULL) {
                continue;
            }

            if (message.is_outbox) {
                esp_websocket_client_send_bin(client, message.message, message.message_len, pdMS_TO_TICKS(1000));
                free(message.message);
                continue;
            }

            /*
            Matrx__SocketMessage* socket_message = matrx__socket_message__unpack(NULL, message.message_len, (uint8_t*)message.message);
            if (socket_message == NULL) {
                ESP_LOGE(TAG, "failed to unpack socket message");
                free(message.message);
                continue;
            }
            handle_message(socket_message);
            */
        }
    }
}

void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        sockets_disconnect();
    }
    if (event_base == IP_EVENT) {
        sockets_connect();
    }
}

void sockets_init()
{
    xSocketsQueue = xQueueCreate(10, sizeof(ProcessableMessage_t));

    xTaskCreatePinnedToCore(sockets_task, "sockets", 4096, NULL, 5, &xSocketsTask, 1);
}

void sockets_connect()
{
    if (client == NULL) {
        return;
    }

    esp_websocket_client_start(client);
}

void sockets_disconnect()
{
    if (client != NULL) {
        return;
    }

    esp_websocket_client_close(client, pdMS_TO_TICKS(1000));
}

void upload_coredump(uint8_t* core_dump, size_t core_dump_len) {
    /*
    Matrx__UploadCoreDump upload = MATRX__UPLOAD_CORE_DUMP__INIT;
    upload.core_dump.data = core_dump;
    upload.core_dump.len = core_dump_len;

    Matrx__SocketMessage message = MATRX__SOCKET_MESSAGE__INIT;
    message.message_case = MATRX__SOCKET_MESSAGE__MESSAGE_UPLOAD_CORE_DUMP;
    message.upload_core_dump = &upload;

    send_socket_message(&message);
    */
}

void attempt_coredump_upload() {
    ESP_LOGI(TAG, "attempting coredump upload");

    // Find the coredump partition
    const esp_partition_t* core_dump_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "coredump");

    if (!core_dump_partition)
    {
        return;
    }

    // Read the core dump size
    size_t core_dump_size = core_dump_partition->size;

    size_t encoded_size = 0;
    uint8_t* encoded_data = 0;
    bool is_erased = true;

    uint8_t* core_dump_data = (uint8_t*)malloc(core_dump_size + 1); // used as return, so freed in calling function
    if (!core_dump_data)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for core dump");
        goto exit;
    }
    if (esp_partition_read(core_dump_partition, 0, core_dump_data, core_dump_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read core dump data");
        goto exit;
    }

    //if coredump is all 0xFF, then it's erased
    for (size_t i = 0; i < core_dump_size; i++)
    {
        if (core_dump_data[i] != 0xFF)
        {
            is_erased = false;
            break;
        }
    }

    if (is_erased)
    {
        ESP_LOGI(TAG, "Core dump partition is empty");
        goto exit;
    }

    // Calculate Base64 encoded size
    mbedtls_base64_encode(NULL, 0, &encoded_size, core_dump_data, core_dump_size);
    encoded_data = (uint8_t*)malloc(encoded_size + 1); // used as return, so freed in calling function
    if (!encoded_data)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for encoded data");
        goto exit;
    }

    // Encode core dump to Base64
    if (mbedtls_base64_encode(encoded_data, encoded_size, &encoded_size, core_dump_data, core_dump_size) != 0)
    {
        goto exit;
    }

    encoded_data[encoded_size] = '\0'; // Null-terminate the Base64 string
    upload_coredump(encoded_data, encoded_size);

    //clear the coredump partition
    if (esp_partition_erase_range(core_dump_partition, 0, core_dump_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to erase core dump partition");
    }

exit:
    free(core_dump_data);
    free(encoded_data);
}