#include "sockets.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"

#include "kd_common.h"

#include "cJSON.h"
#include <esp_partition.h>

#include "device-api.pb-c.h"
#include "kd_global.pb-c.h"
#include "kd_lantern.pb-c.h"

#include <mbedtls/base64.h>
#include "led.h"

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
    case WEBSOCKET_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "connected");

        const esp_app_desc_t* app_desc = esp_app_get_description();

        Kd__Join join = KD__JOIN__INIT;
        join.device_id = kd_common_get_device_name();
        join.device_type = DEVICE_NAME_PREFIX;
        join.firmware_version = strdup(app_desc->version);
        join.firmware_variant = FIRMWARE_VARIANT;
        join.firmware_project = strdup(app_desc->project_name);

        Kd__KDGlobalMessage message = KD__KDGLOBAL_MESSAGE__INIT;
        message.message_case = KD__KDGLOBAL_MESSAGE__MESSAGE_JOIN;
        message.join = &join;

        Kd__DeviceAPIMessage device_api_message = KD__DEVICE_APIMESSAGE__INIT;
        device_api_message.message_case = KD__DEVICE_APIMESSAGE__MESSAGE_KD_GLOBAL_MESSAGE;
        device_api_message.kd_global_message = &message;

        send_device_api_message(&device_api_message);

        xTaskCreate(upload_coredump_task, "upload_coredump_task", 8192, NULL, 5, NULL);
        break;
    }
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

void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        sockets_disconnect();
        led_set_effect(LED_SOLID);
        led_set_color(0, 255, 0);
        led_set_brightness(255);
    }
    if (event_base == IP_EVENT) {
        sockets_connect();
        led_set_effect(LED_OFF);
    }
}

void handle_global_message(Kd__KDGlobalMessage* message)
{
    switch (message->message_case) {
    case KD__KDGLOBAL_MESSAGE__MESSAGE_JOIN_RESPONSE: {
        bool needs_claimed = message->join_response->needs_claimed;
        if (needs_claimed) {
            ESP_LOGI(TAG, "device needs to be claimed");
            char* claim_token = (char*)calloc(2048, sizeof(char));
            size_t claim_token_len = 2048;
            kd_common_get_claim_token(claim_token, &claim_token_len);

            Kd__ClaimDevice claim_device = KD__CLAIM_DEVICE__INIT;
            claim_device.claim_token = claim_token;

            Kd__KDGlobalMessage claim_message = KD__KDGLOBAL_MESSAGE__INIT;
            claim_message.message_case = KD__KDGLOBAL_MESSAGE__MESSAGE_CLAIM_DEVICE;
            claim_message.claim_device = &claim_device;

            Kd__DeviceAPIMessage device_api_message = KD__DEVICE_APIMESSAGE__INIT;
            device_api_message.message_case = KD__DEVICE_APIMESSAGE__MESSAGE_KD_GLOBAL_MESSAGE;
            device_api_message.kd_global_message = &claim_message;

            send_device_api_message(&device_api_message);
        }
        else {
            ESP_LOGI(TAG, "device is already claimed");
        }
        break;
    }
    case KD__KDGLOBAL_MESSAGE__MESSAGE_OK_RESPONSE:
        ESP_LOGI(TAG, "ok response");
        break;
    case KD__KDGLOBAL_MESSAGE__MESSAGE_ERROR_RESPONSE:
        ESP_LOGE(TAG, "error response: %s", message->error_response->error_message);
        break;
    case KD__KDGLOBAL_MESSAGE__MESSAGE_RESTART:
        esp_restart();
    default:
        break;
    }
}

void handle_lantern_message(Kd__KDLanternMessage* message)
{
    switch (message->message_case) {
    case KD__KDLANTERN_MESSAGE__MESSAGE_SET_COLOR:
        led_set_color(message->set_color->red, message->set_color->green, message->set_color->blue);
        led_set_effect((LEDEffect_t)message->set_color->effect);
        led_set_brightness(message->set_color->effect_brightness);
        break;
    case KD__KDLANTERN_MESSAGE__MESSAGE_TOUCH_EVENT_RESPONSE:
        ESP_LOGI(TAG, "touch event response: %i", message->touch_event_response->success);
        break;
    default:
        break;
    }
}

void handle_message(Kd__DeviceAPIMessage* message)
{
    switch (message->message_case) {
    case KD__DEVICE_APIMESSAGE__MESSAGE_KD_GLOBAL_MESSAGE:
        handle_global_message(message->kd_global_message);
        break;
    case KD__DEVICE_APIMESSAGE__MESSAGE_KD_LANTERN_MESSAGE:
        handle_lantern_message(message->kd_lantern_message);
        break;
    default:
        break;
    }

    kd__device_apimessage__free_unpacked(message, NULL);
}

void sockets_task(void* pvParameter)
{
    while (1) {
        if (kd_common_crypto_get_state() != CryptoState_t::CRYPTO_STATE_VALID_CERT && kd_common_crypto_get_state() != CryptoState_t::CRYPTO_STATE_BAD_DS_PARAMS) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        break;
    }

    if (kd_common_crypto_get_state() == CryptoState_t::CRYPTO_STATE_BAD_DS_PARAMS) {
        ESP_LOGE(TAG, "Bad DS params");
        led_set_effect(LED_BLINK);
        led_set_color(255, 0, 0);
        vTaskDelete(NULL);
    }

    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    esp_ds_data_ctx_t* ds_data_ctx = kd_common_crypto_get_ctx();
    char* cert = (char*)calloc(4096, sizeof(char));
    size_t cert_len = 4096;

    kd_common_get_device_cert(cert, &cert_len);

    esp_websocket_client_config_t prod_websocket_cfg = {
        .uri = SOCKETS_URI,
        .port = 443,
        .client_cert = cert,
        .client_cert_len = cert_len + 1,
        .client_ds_data = ds_data_ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = 5000,
    };

    char headers[256];
    snprintf(headers, sizeof(headers), "X-Common-Name: %s\r\n", kd_common_get_device_name());

    esp_websocket_client_config_t dev_websocket_cfg = {
        .host = "192.168.4.138",
        .port = 9091,
        .path = "/",
        .transport = WEBSOCKET_TRANSPORT_OVER_TCP,
        .headers = headers,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = 5000,
    };

    esp_websocket_client_config_t* websocket_cfg = NULL;

    if (strcmp(FIRMWARE_VARIANT, "devel") == 0) {
        websocket_cfg = &dev_websocket_cfg;
    }
    else {
        websocket_cfg = &prod_websocket_cfg;
    }

    client = esp_websocket_client_init(websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void*)client);

    ProcessableMessage_t message;

    while (1)
    {
        if (xQueueReceive(xSocketsQueue, &message, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (message.message == NULL) {
                ESP_LOGE(TAG, "message is NULL");
                continue;
            }

            if (message.is_outbox) {
                esp_websocket_client_send_bin(client, message.message, message.message_len, pdMS_TO_TICKS(1000));
                free(message.message);
                continue;
            }

            Kd__DeviceAPIMessage* device_api_message = kd__device_apimessage__unpack(NULL, message.message_len, (uint8_t*)message.message);
            if (device_api_message == NULL) {
                ESP_LOGE(TAG, "failed to unpack socket message");
                free(message.message);
                continue;
            }

            handle_message(device_api_message);
        }
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

void send_device_api_message(Kd__DeviceAPIMessage* message)
{
    size_t len = kd__device_apimessage__get_packed_size(message);
    uint8_t* buffer = (uint8_t*)heap_caps_calloc(len, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate message buffer");
        return;
    }

    kd__device_apimessage__pack(message, buffer);
    ProcessableMessage_t p_message;
    p_message.message = (char*)buffer;
    p_message.message_len = len;
    p_message.is_outbox = true;

    if (xQueueSend(xSocketsQueue, &p_message, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGE(TAG, "failed to send message to queue");
    }
}

void notify_touch() {
    Kd__TouchEvent event = KD__TOUCH_EVENT__INIT;

    Kd__KDLanternMessage message = KD__KDLANTERN_MESSAGE__INIT;
    message.message_case = KD__KDLANTERN_MESSAGE__MESSAGE_TOUCH_EVENT;
    message.touch_event = &event;

    Kd__DeviceAPIMessage device_api_message = KD__DEVICE_APIMESSAGE__INIT;
    device_api_message.message_case = KD__DEVICE_APIMESSAGE__MESSAGE_KD_LANTERN_MESSAGE;
    device_api_message.kd_lantern_message = &message;

    send_device_api_message(&device_api_message);
}

void upload_coredump_task(void* pvParameter) {
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

    Kd__UploadCoreDump upload = KD__UPLOAD_CORE_DUMP__INIT;
    Kd__DeviceAPIMessage device_api_message = KD__DEVICE_APIMESSAGE__INIT;
    Kd__KDGlobalMessage global_message = KD__KDGLOBAL_MESSAGE__INIT;

    //esp_app_desc
    const esp_app_desc_t* app_desc = esp_app_get_description();

    uint8_t* core_dump_data = (uint8_t*)heap_caps_calloc(core_dump_size + 1, sizeof(uint8_t), MALLOC_CAP_SPIRAM); // used as return, so freed in calling function
    if (esp_partition_read(core_dump_partition, 0, core_dump_data, core_dump_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read core dump data");
        goto exit;
    }

    // Check if coredump is erased (all 0xFF)
    for (size_t i = 0; i < core_dump_size && is_erased; i++) {
        is_erased &= (core_dump_data[i] == 0xFF);
    }

    if (is_erased) {
        ESP_LOGI(TAG, "Core dump partition is empty");
        goto exit;
    }

    // Calculate Base64 encoded size
    mbedtls_base64_encode(NULL, 0, &encoded_size, core_dump_data, core_dump_size);
    encoded_data = (uint8_t*)heap_caps_calloc(encoded_size + 1, sizeof(uint8_t), MALLOC_CAP_SPIRAM); // used as return, so freed in calling function
    if (!encoded_data)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for encoded data");
        goto exit;
    }

    mbedtls_base64_encode(encoded_data, encoded_size, &encoded_size, core_dump_data, core_dump_size);

    upload.core_dump.data = encoded_data;
    upload.core_dump.len = encoded_size;
    upload.firmware_project = strdup(app_desc->project_name);
    upload.firmware_version = strdup(app_desc->version);
    upload.firmware_variant = FIRMWARE_VARIANT;

    device_api_message.message_case = KD__DEVICE_APIMESSAGE__MESSAGE_KD_GLOBAL_MESSAGE;
    global_message.message_case = KD__KDGLOBAL_MESSAGE__MESSAGE_UPLOAD_CORE_DUMP;
    global_message.upload_core_dump = &upload;
    device_api_message.kd_global_message = &global_message;

    send_device_api_message(&device_api_message);

    //clear the coredump partition
    if (esp_partition_erase_range(core_dump_partition, 0, core_dump_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to erase core dump partition");
    }

exit:
    free(core_dump_data);
    free(encoded_data);
    vTaskDelete(NULL);
}