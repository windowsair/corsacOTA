/**
 * Copyright (c) 2021 windowsair <msdn_02 at sina.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

#include "corsacOTA.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "sdkconfig.h"
#if (defined CONFIG_IDF_TARGET_ESP8266) && (CONFIG_IDF_TARGET_ESP8266 == 1)
#define CO_TARGET_ESP8266   1
#define CO_DEVICE_TYPE_NAME "esp8266"
#include "esp8266/eagle_soc.h"
#endif

#if (defined CONFIG_IDF_TARGET_ESP32) && (CONFIG_IDF_TARGET_ESP32 == 1)
#define CO_TARGET_ESP32     1
#define CO_DEVICE_TYPE_NAME "esp32"
#include "esp32/rom/uart.h"
#include "hal/wdt_hal.h"
#include "hal/wdt_types.h"
#include "soc/rtc.h"
#endif

#if (defined CONFIG_IDF_TARGET_ESP32S2) && (CONFIG_IDF_TARGET_ESP32S2 == 1)
#define CO_DEVICE_TYPE_NAME "esp32s2"
#define CO_TARGET_ESP32     1
#include "esp32s2/rom/uart.h"
#include "hal/wdt_hal.h"
#include "hal/wdt_types.h"
#include "soc/rtc.h"
#endif

#if (defined CONFIG_IDF_TARGET_ESP32C3) && (CONFIG_IDF_TARGET_ESP32C3 == 1)
#define CO_DEVICE_TYPE_NAME "esp32c3"
#define CO_TARGET_ESP32C3     1
#include "esp32c3/rom/uart.h"
#include "hal/wdt_hal.h"
#include "hal/wdt_types.h"
#include "soc/rtc.h"
#endif

#if (defined CONFIG_IDF_TARGET_ESP32S3) && (CONFIG_IDF_TARGET_ESP32S3 == 1)
#define CO_DEVICE_TYPE_NAME "esp32s3"
#define CO_TARGET_ESP32S3     1
#include "esp32s3/rom/uart.h"
#include "hal/wdt_hal.h"
#include "hal/wdt_types.h"
#include "soc/rtc.h"
#endif

#if (!defined CO_TARGET_ESP8266) && (!defined CO_TARGET_ESP32) && (!defined CONFIG_IDF_TARGET_ESP32S2) && (!defined CONFIG_IDF_TARGET_ESP32C3) && (!defined CONFIG_IDF_TARGET_ESP32S3)
#error Unknown hardware platform
#endif

#if (defined CONFIG_PARTITION_TABLE_SINGLE_APP) && (CONFIG_PARTITION_TABLE_SINGLE_APP == 1)
#warning No OTA partition configured. corsacOTA may not work!
#endif

static const char *CO_TAG = "corsacOTA";

#define CONFIG_CO_SOCKET_BUFFER_SIZE  1500
#define CONFIG_CO_WS_TEXT_BUFFER_SIZE 100

#define LOG_FMT(x)                    "%s: " x, __func__

#define min(a, b)                     ((a) < (b) ? (a) : (b))

#define CO_NO_RETURN                  __attribute__((noreturn))
#define CO_INLINE                     __attribute__((always_inline))

#define CO_TEST_MODE                  0

#if (CO_TEST_MODE == 1)
#warning corsacOTA test mode is in use
#endif

/**
 * @brief corsacOTA websocket control block
 *
 */
typedef struct co_websocket_cb {
    uint8_t FIN;
    uint8_t OPCODE;

    uint8_t MASK;
    size_t payload_len; // it is used not only for the 7-bit payload len,
                        // but also for the total length of the payload after the extended payload length is included.

    size_t payload_read_len; // the number of payload bytes already read

    union {
        uint32_t val;
        uint8_t data[4];
    } mask;

    bool skip_frame; // skip too long text frames
} co_websocket_cb_t;

/**
 * @brief corsacOTA socket control block
 *
 */
typedef struct co_socket_cb {
    int fd; // The file descriptor for this socket
    enum co_socket_status {
        CO_SOCKET_ACCEPT = 0,
        CO_SOCKET_HANDSHAKE,               // not handshake, or in progress
        CO_SOCKET_WEBSOCKET_HEADER,        // already handshake, now reading the header of websocket frame
        CO_SOCKET_WEBSOCKET_EXTEND_LENGTH, // reading the extended length of websocket header
        CO_SOCKET_WEBSOCKET_MASK,          // reading the mask part of websocket header
        CO_SOCKET_WEBSOCKET_PAYLOAD,       // reading the payload of websocket frame
        CO_SOCKET_CLOSING                  // waiting to close
    } status;

    char *buf;            // data from raw socket
    size_t remaining_len; // the number of available bytes remaining in buf
    size_t read_len;      // the number of bytes that have been processed

    co_websocket_cb_t wcb; // websocket control block

} co_socket_cb_t;

/**
 * @brief corsacOTA OTA control block
 *
 */
typedef struct co_ota_cb {
    enum co_ota_status {
        CO_OTA_INIT = 0,
        CO_OTA_LOAD,
        CO_OTA_DONE,
        CO_OTA_STOP,
        CO_OTA_ERROR,
        CO_OTA_FATAL_ERROR,
    } status;
    int32_t error_code; //// TODO: ?

    const esp_partition_t *update_ptn;
    const esp_partition_t *running_ptn;
    esp_ota_handle_t update_handle;

    int32_t total_size;        // Total firmware size
    int32_t offset;            // Current processed size
    int32_t chunk_size;        // The response will be made every time the chunk size is reached
    int32_t last_index_offset; // The offset recorded in the last response

} co_ota_cb_t;

/**
 * @brief corsacOTA http control block
 *
 */
typedef struct co_cb {
    int listen_fd;        // server listener FD
    int websocket_fd;     // only one websocket is allowed.
    uint8_t *recv_data;   // recv buffer at websocket stage (text mode)
    int recv_data_offset; // (text mode)
    int max_listen_num;   // maxium number of connections. In fact, after the handshake is complete, there is only one connection to provide services

    int wait_timeout_sec;  // timeout (in seconds)
    int wait_timeout_usec; // timeout (in microseconds)

    co_socket_cb_t **socket_list; // socket control block list
    co_socket_cb_t *websocket;    // the only valid socket in the list

    int accept_num; // current number of established connections

    int closing_num; // current number of closing socket

    co_ota_cb_t ota; // ota control block

} co_cb_t;

static co_cb_t *global_cb = NULL;

typedef void (*co_process_fn_t)(void *);

typedef struct co_process_entry {
    const char *str;
    co_process_fn_t fn;
} co_process_entry_t;

static void co_ota_start(void *data);
static void co_ota_stop(void *data);

#define CO_ENTRY_DICT_LEN      (sizeof(co_entry_dict) / sizeof(co_entry_dict[0]))
#define CO_ENTRT_DICT_ITEM_LEN (sizeof(co_entry_dict[0]))

// must be sorted alphabetically
static const co_process_entry_t co_entry_dict[] = {
    {"start", co_ota_start},
    {"stop", co_ota_stop},
};

static int co_compare(const void *s1, const void *s2) {
    const co_process_entry_t *e1 = s1;
    const co_process_entry_t *e2 = s2;

    return strcmp(e1->str, e2->str);
}

static co_process_fn_t co_get_process_entry(char *str) {
    co_process_entry_t *res;
    co_process_entry_t key = {str, NULL};

    res = bsearch(&key, co_entry_dict, CO_ENTRY_DICT_LEN, CO_ENTRT_DICT_ITEM_LEN, co_compare);
    if (res) {
        return res->fn;
    }

    return NULL;
}

#if (CO_TARGET_ESP8266)
extern void uart_tx_wait_idle(uint8_t uart_no);
extern void esp_reset_reason_set_hint(esp_reset_reason_t hint);

/**
 * @brief In some cases, reboot operation cannot be completed properly, so we take a forced reboot.
 *
 */
static void CO_NO_RETURN co_hardware_restart() {

    vPortEnterCritical(); // avoid entering the panicHandler

    uart_tx_wait_idle(0);
    uart_tx_wait_idle(1);

    esp_reset_reason_set_hint(ESP_RST_SW); // software restart

    // force restart
    CLEAR_WDT_REG_MASK(WDT_CTL_ADDRESS, BIT0);
    WDT_REG_WRITE(WDT_OP_ADDRESS, 1);
    WDT_REG_WRITE(WDT_OP_ND_ADDRESS, 1);
    SET_PERI_REG_BITS(PERIPHS_WDT_BASEADDR + WDT_CTL_ADDRESS,
                      WDT_CTL_RSTLEN_MASK,
                      7 << WDT_CTL_RSTLEN_LSB,
                      0);
    SET_PERI_REG_BITS(PERIPHS_WDT_BASEADDR + WDT_CTL_ADDRESS,
                      WDT_CTL_RSPMOD_MASK,
                      0 << WDT_CTL_RSPMOD_LSB,
                      0);
    SET_PERI_REG_BITS(PERIPHS_WDT_BASEADDR + WDT_CTL_ADDRESS,
                      WDT_CTL_EN_MASK,
                      1 << WDT_CTL_EN_LSB,
                      0);

    while (1) {
    }
}
#endif // CO_TARGET_ESP8266

#if (CO_TARGET_ESP32)
extern void uart_tx_wait_idle(uint8_t uart_no);
extern void esp_reset_reason_set_hint(esp_reset_reason_t hint);
extern void xt_ints_off(unsigned int mask);

/**
 * @brief In some cases, reboot operation cannot be completed properly, so we take a forced reboot.
 *
 */
void CO_NO_RETURN co_hardware_restart() {
    wdt_hal_context_t rtc_wdt_ctx;
    uint32_t stage_timeout_ticks;

    xt_ints_off(0xFFFFFFFF); // disable all interrupts

    uart_tx_wait_idle(0);
    uart_tx_wait_idle(1);
    uart_tx_wait_idle(2);

    wdt_hal_init(&rtc_wdt_ctx, WDT_RWDT, 0, false);
    stage_timeout_ticks = (uint32_t)(1000ULL * rtc_clk_slow_freq_get_hz() / 1000ULL);
    wdt_hal_write_protect_disable(&rtc_wdt_ctx);
    wdt_hal_config_stage(&rtc_wdt_ctx, WDT_STAGE0, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_config_stage(&rtc_wdt_ctx, WDT_STAGE1, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_RTC);
    wdt_hal_set_flashboot_en(&rtc_wdt_ctx, true);
    wdt_hal_write_protect_enable(&rtc_wdt_ctx);

    esp_reset_reason_set_hint(ESP_RST_SW); // software restart

    while (1) {
    }
}
#elif (CO_TARGET_ESP32S3)
extern void uart_tx_wait_idle(uint8_t uart_no);
extern void esp_reset_reason_set_hint(esp_reset_reason_t hint);
extern void xt_ints_off(unsigned int mask);

void CO_NO_RETURN co_hardware_restart() {
    wdt_hal_context_t rtc_wdt_ctx;
    uint32_t stage_timeout_ticks;

    xt_ints_off(0xFFFFFFFF); // disable all interrupts

    uart_tx_wait_idle(0);
    uart_tx_wait_idle(1);
    uart_tx_wait_idle(2);

    wdt_hal_init(&rtc_wdt_ctx, WDT_RWDT, 0, false);
    stage_timeout_ticks = (uint32_t)(1000ULL * rtc_clk_slow_freq_get_hz() / 1000ULL);
    wdt_hal_write_protect_disable(&rtc_wdt_ctx);
    wdt_hal_config_stage(&rtc_wdt_ctx, WDT_STAGE0, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_config_stage(&rtc_wdt_ctx, WDT_STAGE1, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_RTC);
    wdt_hal_set_flashboot_en(&rtc_wdt_ctx, true);
    wdt_hal_write_protect_enable(&rtc_wdt_ctx);

    esp_reset_reason_set_hint(ESP_RST_SW); // software restart

    while (1) {
    }
}
#elif (CO_TARGET_ESP32C3)
extern void uart_tx_wait_idle(uint8_t uart_no);
extern void esp_reset_reason_set_hint(esp_reset_reason_t hint);
extern void riscv_global_interrupts_disable(void);
/**
 * @brief In some cases, reboot operation cannot be completed properly, so we take a forced reboot.
 *
 */
void CO_NO_RETURN co_hardware_restart() {
    wdt_hal_context_t rtc_wdt_ctx;
    uint32_t stage_timeout_ticks;

    riscv_global_interrupts_disable(); // disable all interrupts

    uart_tx_wait_idle(0);
    uart_tx_wait_idle(1);

    wdt_hal_init(&rtc_wdt_ctx, WDT_RWDT, 0, false);
    stage_timeout_ticks = (uint32_t)(1000ULL * rtc_clk_slow_freq_get_hz() / 1000ULL);
    wdt_hal_write_protect_disable(&rtc_wdt_ctx);
    wdt_hal_config_stage(&rtc_wdt_ctx, WDT_STAGE0, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_config_stage(&rtc_wdt_ctx, WDT_STAGE1, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_RTC);
    wdt_hal_set_flashboot_en(&rtc_wdt_ctx, true);
    wdt_hal_write_protect_enable(&rtc_wdt_ctx);

    esp_reset_reason_set_hint(ESP_RST_SW); // software restart

    while (1) {
    }
}

#endif // CO_TARGET_ESP32

/**
 * @brief Parse the request text and populate the op and data.
 *
 * @param text Original request text
 * @param[in] op operation field
 * @param[in] data data field
 * @return co_err_t
 * - CO_OK
 * - CO_FAIL
 */
static co_err_t co_parse_request_text(const char *text, char *op, char *data) {
    int ret, n;

    n = 0;
    // "op=auth&data=password"
    // "op=start&data=12345"
    // "op=stop&data="
    ret = sscanf(text, "op=%10[^&]&data=%n%10s", op, &n, data);
    if (ret == 2) {
        return CO_OK;
    } else if (ret == 1 && text[n] == '\0') { // data is empty
        data[0] = '\0';
        return CO_OK;
    } else {
        return CO_FAIL;
    }
}

/*  RFC 6455: The WebSocket Protocol

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-------+-+-------------+-------------------------------+
    |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
    |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
    |N|V|V|V|       |S|             |   (if payload len==126/127)   |
    | |1|2|3|       |K|             |                               |
    +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
    |     Extended payload length continued, if payload len == 127  |
    + - - - - - - - - - - - - - - - +-------------------------------+
    |                               |Masking-key, if MASK set to 1  |
    +-------------------------------+-------------------------------+
    | Masking-key (continued)       |          Payload Data         |
    +-------------------------------- - - - - - - - - - - - - - - - +
    :                     Payload Data continued ...                :
    + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
    |                     Payload Data continued ...                |
    +---------------------------------------------------------------+
*/

#define WS_FIN                 0x80
#define WS_RSV1                0x40
#define WS_RSV2                0x20
#define WS_RSV3                0x10
#define WS_OPCODE_CONTINUTAION 0x00
#define WS_OPCODE_TEXT         0x01
#define WS_OPCODE_BINARY       0x02
#define WS_OPCODE_CLOSE        0x08
#define WS_OPCODE_PING         0x09
#define WS_OPCODE_PONG         0x0A

#define WS_MASK                0x80

static inline int co_websocket_get_res_payload_offset(int payload_len) {
    //  promise: payload_len <= 65535
    return 2 + (payload_len >= 126 ? 2 : 0);
}

static co_err_t co_websocket_process_header(co_cb_t *cb, co_socket_cb_t *scb) {
    uint8_t opcode, fin, mask;
    uint64_t payload_len;
    uint8_t *data;

    data = (uint8_t *)scb->buf;

    if (scb->status == CO_SOCKET_WEBSOCKET_HEADER) {
        if (scb->remaining_len < 2) {
            return CO_OK;
        }

        // check RSV
        if (data[0] & 0b1110000) {
            return CO_FAIL; // no extension defining RSV
        }

        // first byte
        fin = (data[0] & WS_FIN) == WS_FIN;
        opcode = data[0] & 0b1111;
        // second byte
        mask = (data[1] & WS_MASK) == WS_MASK;
        payload_len = data[1] & 0x7F;

        switch (opcode) {
        case WS_OPCODE_CONTINUTAION:
            // nothing to do
            break;
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
            scb->wcb.OPCODE = opcode;
            break;
        case WS_OPCODE_PING:
        case WS_OPCODE_PONG:
            scb->wcb.OPCODE = opcode;
            break;
        case WS_OPCODE_CLOSE:
            scb->wcb.OPCODE = opcode;
            break;
        default:
            return CO_FAIL;
            break;
        }

        scb->wcb.FIN = fin;
        scb->wcb.MASK = mask;
        scb->wcb.payload_len = payload_len;

        // extended payload length should be read
        if (payload_len == 126 || payload_len == 127) {
            scb->status = CO_SOCKET_WEBSOCKET_EXTEND_LENGTH;
        } else if (mask == 1) {
            scb->status = CO_SOCKET_WEBSOCKET_MASK;
        }

        scb->read_len = 2; // first 2 byte header
    }

    if (scb->status == CO_SOCKET_WEBSOCKET_EXTEND_LENGTH) {
        if (scb->wcb.payload_len == 126) {
            if (scb->remaining_len < scb->read_len + 2) { // 2 byte extended length
                return CO_OK;
            }

            payload_len = data[2] << 8 | data[3]; // 0 + scb->read_len == 2

            scb->read_len += 2;
        } else if (scb->wcb.payload_len == 127) { // 8 byte extended length
            if (scb->remaining_len < scb->read_len + 8) {
                return CO_OK;
            }

            payload_len = ((uint64_t)(data[9]) << 0);
            payload_len |= ((uint64_t)(data[8]) << 8);
            payload_len |= ((uint64_t)(data[7]) << 16);
            payload_len |= ((uint64_t)(data[6]) << 24);
            payload_len |= ((uint64_t)(data[5]) << 32);
            payload_len |= ((uint64_t)(data[4]) << 40);
            payload_len |= ((uint64_t)(data[3]) << 48);
            payload_len |= ((uint64_t)(data[2]) << 56);

            // most significant bit MUST be 0
            if (((payload_len >> 63) & 0b1) == 0x1) {
                ESP_LOGE(CO_TAG, "wrong payload length");
                return CO_FAIL;
            }

            scb->read_len += 8;
        } else {
            payload_len = scb->wcb.payload_len;
        }

        scb->wcb.payload_len = payload_len;

        scb->status = scb->wcb.MASK == 1 ? CO_SOCKET_WEBSOCKET_MASK : CO_SOCKET_WEBSOCKET_PAYLOAD;
    }

    if (scb->status == CO_SOCKET_WEBSOCKET_MASK) {
        if (scb->remaining_len < scb->read_len + 4) { // 4 byte mask
            return CO_OK;
        }

        memcpy(&scb->wcb.mask.data[0], &data[scb->read_len], 4);
        scb->read_len += 4;
        scb->status = CO_SOCKET_WEBSOCKET_PAYLOAD;
    } else {
        scb->status = CO_SOCKET_WEBSOCKET_PAYLOAD;
    }

    return CO_OK;
}

// We promise that the length of the payload should not exceed 65535
static co_err_t co_websocket_send_frame(void *frame_buffer, size_t payload_len, int frame_type) {
    int sz;
    uint16_t payload_length;
    uint8_t *p;

    payload_length = payload_len;
    sz = co_websocket_get_res_payload_offset(payload_len) + payload_len;

    p = frame_buffer;
    // 2 bytes
    *p++ = WS_FIN | frame_type; // frame_type
    *p++ = (payload_length >= 126 ? 126 : payload_length);

    // extended length
    if (payload_length >= 126) {
        payload_length = htons(payload_length);
        memcpy(p, &payload_length, 2);
        p += 2;
    }

    // no mask

    send(global_cb->websocket->fd, frame_buffer, sz, 0);

    return CO_OK;
}

// Create a new frame buffer, construct text and send frame.
static co_err_t co_websocket_send_msg_with_code(int code, const char *msg) {
    char *buffer;
    int len, ret;
    int offset;

    len = strlen(msg);
    offset = co_websocket_get_res_payload_offset(len);
    buffer = malloc(offset + len + 25);
    if (buffer == NULL) {
        ret = CO_ERROR_NO_MEM;
        goto cleanup;
    }

    if (code == CO_RES_SUCCESS) {
        ret = snprintf(buffer + offset, len + 24, "code=%d&data=\"%s\"", code, msg);
    } else {
        ret = snprintf(buffer + offset, len + 24, "code=%d&data=\"msg=%s\"", code, msg);
    }

    if (ret < 0) {
        ESP_LOGE(CO_TAG, "invalid arg");
        ret = CO_ERROR_INVALID_ARG;
        goto cleanup;
    }

    ret = co_websocket_send_frame(buffer, ret, WS_OPCODE_TEXT);

cleanup:
    free(buffer);
    return ret;
}

#if (CO_TEST_MODE == 1)
// use for test
static co_err_t co_websocket_send_echo(void *data, size_t len, int frame_type) {
    char *buffer;
    int ret;
    int offset;

    offset = co_websocket_get_res_payload_offset(len);
    buffer = malloc(offset + len);
    if (buffer == NULL) {
        ret = CO_ERROR_NO_MEM;
        goto cleanup;
    }
    memcpy(buffer + offset, data, len);

    ret = co_websocket_send_frame(buffer, len, frame_type);

cleanup:
    free(buffer);
    return ret;
}
#endif // (CO_TEST_MODE == 1)

static const char *co_ota_error_to_msg(esp_err_t err) {
    switch (err) {
    case ESP_OK:
        return NULL;
    case ESP_ERR_NO_MEM:
        return "No Mem";
    case ESP_ERR_INVALID_ARG:
        return "Invalid handle";
    case ESP_ERR_OTA_VALIDATE_FAILED:
        return "Invalid firmware";
    case ESP_ERR_INVALID_SIZE:
        return "Firmware size too large";
    case ESP_ERR_OTA_SELECT_INFO_INVALID:
        return "Invalid partition info";
    case ESP_ERR_NOT_FOUND:
        return "OTA partition not found";
    case ESP_ERR_FLASH_OP_TIMEOUT:
    case ESP_ERR_FLASH_OP_FAIL:
        return "Flash write failed";
    case ESP_ERR_INVALID_STATE:
        return "Flash encryption is enabled";
    default:
        return "OTA Failed";
    }
}

/**
 * @brief OTA init
 *
 * @param size Total firmware size
 * @return const char* Error message, returns NULL indicating that no error occurred
 */
static const char *co_ota_init(int32_t size) {
    const esp_partition_t *boot_ptn, *running_ptn, *update_ptn;
    esp_err_t ret;

    boot_ptn = esp_ota_get_boot_partition();
    running_ptn = esp_ota_get_running_partition();
    if (boot_ptn != running_ptn) {
        //// TODO:  boot image become corrupted
    }

    update_ptn = esp_ota_get_next_update_partition(NULL);
    if (update_ptn == NULL) {
        global_cb->ota.status = CO_OTA_FATAL_ERROR;
        global_cb->ota.error_code = CO_ERROR_INVALID_OTA_PTN;
        return "Invalid OTA data partition";
    }

    // Start erase flash
    //// TODO: full chip erase
    ret = esp_ota_begin(update_ptn, size, &global_cb->ota.update_handle);

    global_cb->ota.update_ptn = update_ptn;

    return co_ota_error_to_msg(ret);
}

// write ota data
static const char *co_ota_write(void *data, size_t len) {
    esp_err_t ret = esp_ota_write(global_cb->ota.update_handle, data, len);
    return co_ota_error_to_msg(ret);
}

static const char *co_ota_end() {
    esp_err_t ret = esp_ota_end(global_cb->ota.update_handle);

    if (ret != ESP_OK) {
        return co_ota_error_to_msg(ret);
    }

    ret = esp_ota_set_boot_partition(global_cb->ota.update_ptn);
    return co_ota_error_to_msg(ret);
}

/**
 * @brief Process OTA start request
 *
 * @param data Pointer to a string indicating the size of the firmware
 */
static void co_ota_start(void *data) { // TODO: return value -> status
    const char *res_msg = "deviceType=" CO_DEVICE_TYPE_NAME "&state=ready&offset=0";
    const char *err_msg;
    int size;

    // may be we should ignore status...
    // if (global_cb->ota.status != CO_OTA_INIT && global_cb->ota.status != CO_OTA_STOP) {
    //     co_websocket_send_msg_with_code(CO_RES_INVALID_STATUS, "OTA has not started");
    //     return;
    // }

    size = atoi(data);
    if (size < 1) {
        co_websocket_send_msg_with_code(CO_RES_INVALID_SIZE, "Invalid size");
        return;
    }

    err_msg = co_ota_init(size);
    if (err_msg != NULL) {
        co_websocket_send_msg_with_code(CO_RES_SYSTEM_ERROR, err_msg);
        return;
    }

    global_cb->ota.status = CO_OTA_LOAD;
    global_cb->ota.total_size = size;

    size = min(global_cb->ota.total_size / 10, 1024 * 10); // 10KB default
    if (size == 0) {
        size = 1; // Firmware too small...
    }

    global_cb->ota.chunk_size = size;
    global_cb->ota.offset = 0;
    global_cb->ota.last_index_offset = 0;

    co_websocket_send_msg_with_code(CO_RES_SUCCESS, res_msg);
}

static void co_ota_stop(void *data) {
    if (global_cb->ota.status != CO_OTA_FATAL_ERROR) {
        memset(&global_cb->ota, 0, sizeof(global_cb->ota));
        global_cb->ota.status = CO_OTA_STOP;
        co_websocket_send_msg_with_code(CO_RES_SUCCESS, "");
    } else {
        co_websocket_send_msg_with_code(CO_RES_SYSTEM_ERROR, "Fatal error");
    }
}

static void co_websocket_process_binary(uint8_t *data, size_t len) {
    char res[32]; // state=ready&offset=2147483647
    const char *err_msg;
    bool is_done;

    if (global_cb->ota.status == CO_OTA_LOAD) {
        global_cb->ota.offset += (int)len;
        is_done = global_cb->ota.total_size == global_cb->ota.offset;
        if (is_done) {
            // If everything is fine, then we will restart chip afterwards, which does not require the use of the status
            global_cb->ota.status = CO_OTA_INIT;
        }

        err_msg = co_ota_write(data, len);
        if (err_msg != NULL) {
            memset(&global_cb->ota, 0, sizeof(global_cb->ota));
            global_cb->ota.status = CO_OTA_STOP;
            co_websocket_send_msg_with_code(CO_RES_SYSTEM_ERROR, err_msg);
            return;
        }

        // response
        if (!is_done && global_cb->ota.offset - global_cb->ota.last_index_offset < global_cb->ota.chunk_size) {
            return;
        }

        global_cb->ota.last_index_offset = global_cb->ota.offset;

        snprintf(res, 32, "state=%s&offset=%d", is_done ? "done" : "ready", global_cb->ota.offset);

        if (is_done) {
            err_msg = co_ota_end();
            if (err_msg != NULL) {
                co_websocket_send_msg_with_code(CO_RES_SYSTEM_ERROR, err_msg);
                return;
            }

            co_websocket_send_msg_with_code(CO_RES_SUCCESS, res);

            ESP_LOGD(CO_TAG, "prepare to restart");
            vTaskDelay(pdMS_TO_TICKS(5000));
            co_hardware_restart();
        }

        co_websocket_send_msg_with_code(CO_RES_SUCCESS, res);
    } else if (global_cb->ota.status != CO_OTA_STOP) {
        // skip the rest of the frame when a stop command is received
        co_websocket_send_msg_with_code(CO_RES_INVALID_STATUS, "OTA has not started");
    }
}

static void co_websocket_process_text(uint8_t *data, size_t len) {
    char op_field[10 + 1], data_field[10 + 1];
    char *text;
    co_process_fn_t fn;

    // Note that the text may not have a terminator
    // TODO: add terminator
    text = strndup((char *)data, len);
    if (text == NULL) {
        // TODO: memory leak
        goto clean;
    }

    if (co_parse_request_text(text, op_field, data_field) != CO_OK) {
        co_websocket_send_msg_with_code(CO_RES_INVALID_ARG, "parse error");
        goto clean;
    }

    if ((fn = co_get_process_entry(op_field)) == NULL) {
        co_websocket_send_msg_with_code(CO_RES_INVALID_ARG, "invalid op");
        goto clean;
    }

    // start process!
    fn(data_field);

clean:
    free(text);
}

// send pong response
// TODO: too long ping frame
static void co_websocket_process_ping(co_cb_t *cb, co_socket_cb_t *scb) {
    int len;

    // control frame max payload length: 125 -> 0 byte extended length
    len = 2 + scb->wcb.payload_len + (scb->wcb.MASK ? 4 : 0);

    scb->buf[0] = WS_FIN | WS_OPCODE_PONG;

    send(scb->fd, scb->buf, len, 0);
}

// close handshake
// TODO: array
static void co_websocket_process_close(co_cb_t *cb, co_socket_cb_t *scb) {
    uint8_t buf[4];
    uint8_t *p = buf;

    *p++ = WS_FIN | WS_OPCODE_CLOSE;
    *p++ = 0x02; // 2 byte status code
    // normal closure
    *p++ = 0x03;
    *p = 0xe8;

    send(scb->fd, buf, 4, 0);
}

static inline CO_INLINE uint32_t co_rotr32(uint32_t n, unsigned int c) {
    const unsigned int mask = (CHAR_BIT * sizeof(n) - 1);
    c &= mask;
    return (n >> c) | (n << ((-c) & mask));
}

/**
 * @brief Quick calculation WebSocket. The process of calculating the mask is one of the performance bottlenecks
 * of the entire websocket. The performance between the optimized version and the version without mask is not significant.
 *
 * We assume: the natural machine word length is 4byte (32bits) and the endianness is little-endian
 * For xtensa: single fetch: 4 byte(32bit)
 *
 * @param data data buffer ptr
 * @param mask websocket mask. Little-endian 32bis mask.
 * @param len data length
 */
void co_websocket_fast_mask(uint8_t *data, uint32_t mask, size_t len) {
    uint32_t new_mask;
    int align_len;
    size_t i;

    const uint8_t *p_mask = (uint8_t *)&mask;

    unsigned long int dst = (long int)data;

    if (len >= 8) {
        // copy just a few bytes to make dst aligned.
        align_len = (-dst) % 4;
        len -= align_len;

        for (i = 0; i < align_len; i++) {
            data[i] ^= p_mask[i];
        }

        // use the new mask on the aligned address
        switch (align_len) {
        case 1:
            new_mask = co_rotr32(mask, 8U);
            break;
        case 2:
            new_mask = co_rotr32(mask, 16U);
            break;
        case 3:
            new_mask = co_rotr32(mask, 24U);
            break;
        default: // 0
            new_mask = mask;
            break;
        }

        p_mask = (uint8_t *)&new_mask;

        dst += align_len;

        for (i = 0; i < len / 4; i++) {
            *((uint32_t *)dst) ^= new_mask;
            dst += 4;
        }

        len %= 4;
    }

    // There are just a few bytes to process
    for (i = 0; i < len; i++) {
        *((uint8_t *)dst) ^= p_mask[i % 4];
        dst += 1;
    }
}

static inline uint32_t co_websocket_get_new_mask(uint32_t mask, size_t len) {
    switch (len & 0b11) {
    case 1:
        return co_rotr32(mask, 8U);
    case 2:
        return co_rotr32(mask, 16U);
    case 3:
        return co_rotr32(mask, 24U);
    default:
        return mask;
    }
}

/**
 * @brief Process websocket payload
 *
 * @param cb corsacOTA control block
 * @param scb corsacOTA socket control block
 * @return co_err_t
 * - CO_OK: Successful processing of all payloads
 * - CO_ERROR_IO_PENDING: There are still new frames to be processed
 */
static co_err_t co_websocket_process_payload(co_cb_t *cb, co_socket_cb_t *scb) {
    int len, new_len;
    uint8_t *data;
    uint32_t mask;

    data = (uint8_t *)scb->buf + scb->read_len;
    // May be possible to read the complete frame and maybe a new frame rate afterwards
    len = min(scb->remaining_len - scb->read_len, scb->wcb.payload_len);

    // For ping frames, we will directly change their opcode and send.
    if (scb->wcb.MASK == 1 && scb->wcb.OPCODE != WS_OPCODE_PING) {
        mask = scb->wcb.mask.val;
        co_websocket_fast_mask(data, mask, len);

        scb->wcb.mask.val = co_websocket_get_new_mask(mask, len);
    }

    // In the previous processing, we can ensure that each new frame can begin in a place where the Buffer offset is 0.
    switch (scb->wcb.OPCODE) {
    case WS_OPCODE_TEXT:
#if (CO_TEST_MODE == 1)
        co_websocket_send_echo(data, len, WS_OPCODE_TEXT);
        break;
#endif
        // case 0: This frame should be skip
        if (scb->wcb.skip_frame) {
            if (len == scb->wcb.payload_len) { // The last part of the data in this frame has been received
                scb->wcb.skip_frame = false;
            }
            break;
        }

        // case 1: Receive the entire payload
        if (len == scb->wcb.payload_len && cb->recv_data_offset == 0) {
            co_websocket_process_text(data, len);
            break;
        }

        // case 2: Part of the payload has been received before
        if (len > CONFIG_CO_WS_TEXT_BUFFER_SIZE - cb->recv_data_offset) { // overflow
            if (len < scb->wcb.payload_len) {                             // This frame has not yet been received
                scb->wcb.skip_frame = true;
            }

            co_websocket_send_msg_with_code(CO_RES_INVALID_SIZE, "request too long");
            cb->recv_data_offset = 0;
            break;
        }

        memcpy(cb->recv_data + cb->recv_data_offset, data, len);
        cb->recv_data_offset += len;

        if (len == scb->wcb.payload_len) {
            co_websocket_process_text(cb->recv_data, len);
            cb->recv_data_offset = 0;
        }
        break;
    case WS_OPCODE_BINARY:
#if (CO_TEST_MODE == 1)
        co_websocket_send_echo(data, len, WS_OPCODE_BINARY);
        break;
#endif
        //// TODO: check return val
        co_websocket_process_binary(data, len);
        break;
    case WS_OPCODE_PING:
        co_websocket_process_ping(cb, scb);
        break;
    case WS_OPCODE_PONG:
        break;
    case WS_OPCODE_CLOSE:
        co_websocket_process_close(cb, scb);
        return CO_FAIL; // close by server
    default:
        ESP_LOGE(CO_TAG, "unknow opcode: %d", scb->wcb.OPCODE);
        break;
    }

    new_len = scb->remaining_len - scb->read_len - len;
    // case 0: New frames still exist
    if (new_len > 0) {
        // For simplicity, we make sure that the websocket header is always at the beginning of the buf.
        memcpy(scb->buf, data + len, new_len);

        scb->read_len = 0;
        scb->remaining_len = new_len;

        scb->status = CO_SOCKET_WEBSOCKET_HEADER;
        scb->wcb.payload_len = 0;
        scb->wcb.payload_read_len = 0;
        return CO_ERROR_IO_PENDING;
    }
    // case 1: The payload part is not yet fully read.
    else if (scb->wcb.payload_len > len) {
        scb->wcb.payload_len -= len;

        scb->read_len = 0;
        scb->remaining_len = 0;

        return CO_OK;
    }
    // case 2: Exactly one complete frame is read and there is no remaining available data in buf.
    else {
        scb->read_len = 0;
        scb->remaining_len = 0;

        scb->status = CO_SOCKET_WEBSOCKET_HEADER;
        scb->wcb.payload_len = 0;
        scb->wcb.payload_read_len = 0;
        return CO_OK;
    }
}

static esp_err_t co_websocket_process(co_cb_t *cb, co_socket_cb_t *scb) {
    if (cb->websocket != scb) {
        return ESP_FAIL;
    }
    int fd, ret, offset;

    fd = scb->fd;
    offset = scb->remaining_len;

    ret = recv(fd, scb->buf + offset, CONFIG_CO_SOCKET_BUFFER_SIZE - offset, 0);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    scb->remaining_len += ret;

    do {
        // After we process a partial or complete payload,
        // we always receive a new payload or header starting from the head of the buf.
        if (scb->status != CO_SOCKET_WEBSOCKET_PAYLOAD) {
            ret = co_websocket_process_header(cb, scb);
            if (ret != CO_OK) {
                return CO_FAIL;
            }
        }

        // Perhaps we have already read the header section, and if there are extra bytes left over,
        // we continue reading the payload section.
        if (scb->status == CO_SOCKET_WEBSOCKET_PAYLOAD) {
            ret = co_websocket_process_payload(cb, scb);
            if (ret == CO_FAIL) {
                return CO_FAIL;
            }
        }
    } while (ret == CO_ERROR_IO_PENDING);

    return ESP_OK;
}

/**
 * @brief parse HTTP header lines of format:
 *        \r\nfield_name: value1, value2, ... \r\n
 *
 * @param header_start
 * @param header_end
 * @param field_name
 * @param value Optional value
 * @return const char* The specific value starts, or the beginning of value in field.
 */
static const char *co_http_header_find_field_value(const char *header_start, const char *header_end, const char *field_name, const char *value) {
    const char *field_start, *field_end, *next_crlf, *value_start;
    int field_name_len;

    field_name_len = (int)strlen(field_name);

    field_start = header_start;
    do {
        field_start = strcasestr(field_start + 1, field_name);
        field_end = field_start + field_name_len - 1;

        if (field_start != NULL && field_start - header_start >= 2 && field_start[-2] == '\r' && field_start[-1] == '\n') { // is start with "\r\n" ?
            if (header_end - field_end >= 1 && field_end[1] == ':') {                                                       // is end with ':' ?
                break;
            }
        }
    } while (field_start != NULL);

    if (field_start == NULL) {
        return NULL;
    }

    // find the field terminator
    next_crlf = strcasestr(field_start, "\r\n");
    if (next_crlf == NULL) {
        return NULL; // Malformed HTTP header!
    }

    // If not looking for a value, then return a pointer to the start of values string
    if (value == NULL) {
        return field_end + 2; // 2 for ':'  ' '(blank)
    }

    value_start = strcasestr(field_start, value);
    if (value_start == NULL) {
        return NULL;
    }

    if (value_start > next_crlf) {
        return NULL; // encounter with unanticipated CRLF
    }

    // The value we found should be properly delineated from the other tokens
    if (isalnum((unsigned char)value_start[-1]) || isalnum((unsigned char)value_start[strlen(value)])) {
        // "field_name: value1, value2,"
        // Consecutive tokens will be considered as errors.
        return NULL;
    }

    return value_start;
}

static void co_http_error_400_response(co_cb_t *cb, co_socket_cb_t *scb) {
    const char *error = "HTTP/1.1 400 Bad Request\r\n\r\n";
    send(scb->fd, error, strlen(error), 0);
}

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

int co_sha1(const unsigned char *src, size_t src_len, unsigned char *dst) {
    return mbedtls_sha1_ret(src, src_len, dst);
}

int co_base64_encode(unsigned char *dst, size_t dst_len, size_t *written_len, unsigned char *src, size_t src_len) {
    return mbedtls_base64_encode(dst, dst_len, written_len, src, src_len);
}

static esp_err_t co_websocket_create_accept_key(char *dst, size_t dst_len, const char *client_key) {
    uint8_t sha1buf[20], key_src[60];

    memcpy(key_src, client_key, 24);
    memcpy(key_src + 24, WS_GUID, 36);

    if (co_sha1(key_src, sizeof(key_src), sha1buf) != 0) {
        return ESP_FAIL;
    }

    size_t base64_encode_len;
    if (co_base64_encode((unsigned char *)dst, dst_len, &base64_encode_len, sha1buf, sizeof(sha1buf)) != 0) {
        return ESP_FAIL;
    }

    // add terminator
    dst[base64_encode_len] = '\0';

    return ESP_OK;
}

static esp_err_t co_websocket_handshake_send_key(int fd, const char *client_key) {
    char res_header[256], accept_key[29];
    int res_header_length;

    if (co_websocket_create_accept_key(accept_key, sizeof(accept_key), client_key) != ESP_OK) {
        ESP_LOGE(CO_TAG, LOG_FMT("fail to create accept key"));
        return ESP_FAIL;
    }

    snprintf(res_header, sizeof(res_header),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Server: corsacOTA server\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n"
             "\r\n",
             accept_key);

    res_header_length = strlen(res_header);
    send(fd, res_header, res_header_length, 0);

    return ESP_OK;
}

static esp_err_t co_websocket_handshake_process(co_cb_t *cb, co_socket_cb_t *scb) {
    if (scb->remaining_len == 0) {
        memset(scb->buf, 0, CONFIG_CO_SOCKET_BUFFER_SIZE);
    }

    int offset = scb->remaining_len;
    int fd = scb->fd;

    int ret = recv(fd, scb->buf + offset, CONFIG_CO_SOCKET_BUFFER_SIZE - offset, 0);
    if (ret <= 0) {
        co_http_error_400_response(cb, scb);
        return ESP_FAIL;
    }

    scb->remaining_len += ret;

    // Already received the entire http header?
    if (scb->remaining_len < 4 || memcmp(scb->buf + scb->remaining_len - 4, "\r\n\r\n", 4) != 0) {
        return ESP_OK; // Not yet received
    }

    const char *header_start = scb->buf, *header_end = scb->buf + scb->remaining_len - 1;
    const char *ws_key_start, *ws_key_end;

    if (co_http_header_find_field_value(header_start, header_end, "Upgrade", "websocket") == NULL ||
        co_http_header_find_field_value(header_start, header_end, "Connection", "Upgrade") == NULL ||
        (ws_key_start = co_http_header_find_field_value(header_start, header_end, "Sec-WebSocket-Key", NULL)) == NULL) {
        co_http_error_400_response(cb, scb);
        return ESP_FAIL;
    }

    /* example:
     Sec-WebSocket-Key: c2REMVVpRXJRQWJ0Q1dKeQ==\r\n
                       |
                    ws_key_start
    */

    // skip the extra blank
    for (; *ws_key_start == ' '; ws_key_start++) {
        ;
    }

    // find the end of ws key
    for (ws_key_end = ws_key_start; *ws_key_end != '\r' && *ws_key_end != ' '; ws_key_end++) {
        ;
    }

    /* example:
     Sec-WebSocket-Key: c2REMVVpRXJRQWJ0Q1dKeQ==\r\n
                        |                       ||
                    ws_key_start            ws_key_end
    */
    if (ws_key_end - ws_key_start != 24) {
        co_http_error_400_response(cb, scb);
        return ESP_FAIL;
    }

    if (co_websocket_handshake_send_key(scb->fd, ws_key_start) != ESP_OK) {
        co_http_error_400_response(cb, scb);
        return ESP_FAIL;
    }

    ESP_LOGD(CO_TAG, "websocket handshake success");

    cb->websocket = scb;
    scb->status = CO_SOCKET_WEBSOCKET_HEADER;
    scb->remaining_len = 0;

    memset(scb->buf, 0, CONFIG_CO_SOCKET_BUFFER_SIZE);

    return ESP_OK;
}

static esp_err_t co_socket_data_process(co_cb_t *cb, co_socket_cb_t *scb) {
    if (cb == NULL || scb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (scb->status) {
    case CO_SOCKET_ACCEPT:
        ESP_LOGW(CO_TAG, LOG_FMT("This state should not occur"));
        return ESP_FAIL; //// TODO: remove this?
    case CO_SOCKET_HANDSHAKE:
        return co_websocket_handshake_process(cb, scb);
    case CO_SOCKET_WEBSOCKET_HEADER:
    case CO_SOCKET_WEBSOCKET_EXTEND_LENGTH:
    case CO_SOCKET_WEBSOCKET_MASK:
    case CO_SOCKET_WEBSOCKET_PAYLOAD:
        return co_websocket_process(cb, scb);
    default:
        ESP_LOGW(CO_TAG, LOG_FMT("This state should not occur"));
        return ESP_OK;
    }
}

static esp_err_t co_socket_list_alloc(co_cb_t *cb) {
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int i;
    cb->socket_list = calloc(cb->max_listen_num, sizeof(co_socket_cb_t *)); // pointer list
    if (cb->socket_list == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (i = 0; i < cb->max_listen_num; i++) {
        if ((cb->socket_list[i] = calloc(1, sizeof(struct co_socket_cb))) == NULL) {
            goto fail;
        }
    }
    return ESP_OK;

fail:
    for (i = 0; i < cb->max_listen_num; i++) {
        if (cb->socket_list[i] != NULL) {
            free(cb->socket_list[i]);
        }
    }
    free(cb->socket_list);
    cb->socket_list = NULL;
    return ESP_ERR_NO_MEM;
}

/**
 * @brief add a new fd to socket list
 *
 * @param cb corsacOTA control block
 * @param fd socket file descriptor
 * @return err_t
 */
static err_t co_socket_list_insert(co_cb_t *cb, int fd) {
    int i;
    for (i = 0; i < cb->max_listen_num; i++) {
        if (cb->socket_list[i]->fd == -1) {
            cb->socket_list[i]->fd = fd;
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

/**
 * @brief Iterate to get each socket control blck in socket list
 *
 * @param cb corsacOTA control block
 * @param begin The fd obtained in the previous iteration. Should be `NULL` for the initial iteration.
 * @param ignore_closing If true, all fd's that are closing will be ignored.
 * @return int The current socket control block obtained by the iterator, or `NULL` for not found.
 */
static co_socket_cb_t *co_socket_list_iterate(co_cb_t *cb, co_socket_cb_t *begin, bool ignore_closing) {
    int start_index = 0;
    int i;

    if (begin != NULL) {
        for (i = 0; i < cb->max_listen_num; i++) {
            if (cb->socket_list[i] == begin) {
                start_index = i + 1;
                break;
            }
        }
    }

    for (i = start_index; i < cb->max_listen_num; i++) {
        if (cb->socket_list[i]->fd != -1) {
            if (ignore_closing && cb->socket_list[i]->status == CO_SOCKET_CLOSING) {
                continue;
            }
            return cb->socket_list[i];
        }
    }

    return NULL;
}

/**
 * @brief Invalidate all sockets in the list in the accept stage
 *
 * @param cb corsacOTA cotrol block pointer
 */
static void co_socket_list_init(co_cb_t *cb) {
    int i;
    for (i = 0; i < cb->max_listen_num; i++) {
        cb->socket_list[i]->fd = -1;
        cb->socket_list[i]->status = CO_SOCKET_ACCEPT;
    }
}

static co_cb_t *co_control_block_create(co_config_t *config) {
    co_cb_t *cb = calloc(1, sizeof(co_cb_t));
    if (cb == NULL) {
        return NULL;
    }

    cb->recv_data = malloc(CONFIG_CO_WS_TEXT_BUFFER_SIZE + 1);
    if (cb->recv_data == NULL) {
        free(cb);
        return NULL;
    }

    // setting
    cb->max_listen_num = config->max_listen_num;
    cb->wait_timeout_sec = config->wait_timeout_sec;
    cb->wait_timeout_usec = config->wait_timeout_usec;

    cb->listen_fd = -1;
    cb->websocket_fd = -1;

    cb->recv_data_offset = 0;

    if (co_socket_list_alloc(cb) != ESP_OK) {
        free(cb);
        return NULL;
    }
    return cb;
}

static void co_free_all(co_cb_t *cb) {
    if (cb == NULL) {
        return;
    }

    int i;
    if (cb->socket_list != NULL) {
        for (i = 0; i < cb->max_listen_num; i++) {
            if (cb->socket_list[i] != NULL) {
                if (cb->socket_list[i]->fd != -1) {
                    close(cb->socket_list[i]->fd);
                }
                free(cb->socket_list[i]->buf);
            }
            free(cb->socket_list[i]);
        }
        free(cb->socket_list);
    }

    if (cb->listen_fd != -1) {
        close(cb->listen_fd);
    }

    free(cb->recv_data);

    free(cb);
    global_cb = NULL;
}

/**
 * @brief free the specified socket control block
 *
 * @param scb Socket control block included in cb
 */
static void co_socket_buf_free(co_socket_cb_t *scb) {
    free(scb->buf);
    scb->buf = NULL;
    scb->remaining_len = 0;
    scb->read_len = 0;
}

static int co_socket_init(co_cb_t *cb, co_config_t *config) {
#if (defined CONFIG_LWIP_IPV6) && (CONFIG_LWIP_IPV6 == 1)
    int fd = socket(PF_INET6, SOCK_STREAM, 0);
#else
    int fd = socket(PF_INET, SOCK_STREAM, 0);
#endif // (defined CONFIG_LWIP_IPV6) && (CONFIG_LWIP_IPV6 == 1)
    if (fd < 0) {
        ESP_LOGE(CO_TAG, LOG_FMT("error in socket (%d)"), errno);
        return ESP_FAIL;
    }

#if (defined CONFIG_LWIP_IPV6) && (CONFIG_LWIP_IPV6 == 1)
    struct in6_addr inaddr_any = IN6ADDR_ANY_INIT;
    struct sockaddr_in6 serv_addr = {
        .sin6_family = PF_INET6,
        .sin6_addr = inaddr_any,
        .sin6_port = htons(config->listen_port),
    };
#else
    struct sockaddr_in serv_addr = {
        .sin_family = PF_INET,
        .sin_addr = {
            .s_addr = htonl(INADDR_ANY)},
        .sin_port = htons(config->listen_port),
    };
#endif // (defined CONFIG_LWIP_IPV6) && (CONFIG_LWIP_IPV6 == 1)
    int ret = bind(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        ESP_LOGE(CO_TAG, LOG_FMT("error in bind (%d)"), errno);
        close(fd);
        return ESP_FAIL;
    }

    ret = listen(fd, config->max_listen_num);
    if (ret < 0) {
        ESP_LOGE(CO_TAG, LOG_FMT("error in listen (%d)"), errno);
        close(fd);
        return ESP_FAIL;
    }

    cb->listen_fd = fd;
    return ESP_OK;
}

/**
 * @brief Accept a new connection, set the timeout, insert it into socket list,
 *        allocate the memory needed for the connection
 * @param cb corsacOTA control block
 * @return esp_err_t
 * - ESP_OK accept successfully
 */
static esp_err_t co_socket_accept(co_cb_t *cb) {
    struct sockaddr_in addr_from;
    socklen_t addr_from_len = sizeof(addr_from);
    int new_fd = accept(cb->listen_fd, (struct sockaddr *)&addr_from, &addr_from_len);
    if (new_fd < 0) {
        ESP_LOGW(CO_TAG, LOG_FMT("error in accept (%d)"), errno);
        return ESP_FAIL;
    }

    struct timeval tv;
    // set recv timrout
    tv.tv_sec = cb->wait_timeout_usec;
    tv.tv_usec = cb->wait_timeout_usec;
    setsockopt(new_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    // set send timeout
    tv.tv_sec = cb->wait_timeout_usec;
    tv.tv_usec = cb->wait_timeout_usec;
    setsockopt(new_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

    // try to add to the socket list
    if (co_socket_list_insert(cb, new_fd) != ESP_OK) {
        ESP_LOGW(CO_TAG, LOG_FMT("Unable to add to socket list"));
        close(new_fd);
        return ESP_FAIL;
    }

    // create new buffer
    int i;
    co_socket_cb_t *scb = NULL;
    for (i = 0; i < cb->max_listen_num; i++) { // find the corresponding SCB
        if (cb->socket_list[i]->fd == new_fd) {
            scb = cb->socket_list[i];
            break;
        }
    }
    assert(scb != NULL);
    scb->buf = malloc(CONFIG_CO_SOCKET_BUFFER_SIZE + 1);
    scb->remaining_len = 0;
    if (scb->buf == NULL) {
        close(new_fd);
        return ESP_ERR_NO_MEM;
    }

    scb->status = CO_SOCKET_HANDSHAKE;

    return ESP_OK;
}

static void co_socket_set_non_block(int fd) {
    int flag;
    if ((flag = fcntl(fd, F_GETFL, 0)) < 0) {
        return;
    }

    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

/**
 * @brief Close the socket and set its state
 *
 * @param cb corsacOTA control block
 * @param scb corsacOTA socket control block
 */
static void co_socket_close(co_cb_t *cb, co_socket_cb_t *scb) {
    cb->closing_num++;
    scb->status = CO_SOCKET_CLOSING;

    if (cb->websocket == scb) {
        cb->websocket = NULL;
    }

    co_socket_set_non_block(scb->fd);
    shutdown(scb->fd, SHUT_WR); // wait client to close socket
}

static void co_socket_close_cleanup(co_cb_t *cb) {
    int ret;
    co_socket_cb_t *iter;

    iter = NULL;

    while ((iter = co_socket_list_iterate(cb, iter, false)) != NULL) {
        if (iter->status != CO_SOCKET_CLOSING) {
            continue;
        }

        ret = recv(iter->fd, iter->buf, CONFIG_CO_SOCKET_BUFFER_SIZE, 0);
        if (ret == 0 || errno == ENOTCONN) { // client gracefully closed connection
            close(iter->fd);
            co_socket_buf_free(iter);

            iter->status = CO_SOCKET_ACCEPT;
            iter->fd = -1;

            cb->closing_num--;
        }
    }
}

/**
 * @brief Set and find the largest fd in socket list only
 *
 */
static int co_select_update_maxfd(co_cb_t *cb, fd_set *fdset) {
    int i;
    int maxfd = -1;
    for (i = 0; i < cb->max_listen_num; i++) {
        if (cb->socket_list[i] != NULL && cb->socket_list[i]->fd != -1) {
            FD_SET(cb->socket_list[i]->fd, fdset);
            maxfd = MAX(maxfd, cb->socket_list[i]->fd);
        }
    }
    return maxfd;
}

static int co_select_fd_is_valid(int fd) {
    return fcntl(fd, F_GETFD, 0) != -1 || errno != EBADF;
}

static void co_select_clean_invalid(co_cb_t *cb) {
    int i;
    for (i = 0; i < cb->max_listen_num; i++) {
        if (cb->socket_list[i] != NULL && cb->socket_list[i]->fd != -1 &&
            !co_select_fd_is_valid(cb->socket_list[i]->fd)) {
            co_socket_buf_free(cb->socket_list[i]);
        }
    }
}

/**
 * @brief The select function will be processed here.
 *
 * @param cb corsacOTA control block
 * @return esp_err_t
 * - ESP_OK success
 * - others: need to close server.
 */
static esp_err_t co_select_process(co_cb_t *cb) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(cb->listen_fd, &read_set);

    int maxfd = co_select_update_maxfd(cb, &read_set);
    maxfd = MAX(maxfd, cb->listen_fd);

    struct timeval tv;
    // Set recv timeout of this fd as per config
    tv.tv_sec = cb->wait_timeout_sec;
    tv.tv_usec = cb->wait_timeout_usec;

    int ret = select(maxfd + 1, &read_set, NULL, NULL, &tv);
    if (ret < 0) {
        ESP_LOGE(CO_TAG, LOG_FMT("error in select (%d)"), errno);
        co_select_clean_invalid(cb);
        return ESP_OK;
    } else if (ret == 0) {
        return ESP_ERR_TIMEOUT;
    }

    // 1. Find out if there is any data available on the socket list
    co_socket_cb_t *iter = NULL;
    while ((iter = co_socket_list_iterate(cb, iter, true)) != NULL) {
        if (FD_ISSET(iter->fd, &read_set)) {
            if (co_socket_data_process(cb, iter) != ESP_OK) {
                co_socket_close(cb, iter);
            }
        }
    }

    // 2. There are new connections waiting to be accepted
    if (FD_ISSET(cb->listen_fd, &read_set)) {
        if (co_socket_accept(cb) != ESP_OK) {
            ESP_LOGW(CO_TAG, LOG_FMT("can not accept a new connnection"));
        }
    }

    // 3. clean up
    if (cb->closing_num > 0) {
        co_socket_close_cleanup(cb);
    }

    // TODO: control port?

    return ESP_OK;
}

static void co_main_thread(void *pvParameter) {
    // co_config_t *server_config = (co_config_t *)pvParameter;
    ESP_LOGI(CO_TAG, "start corsacOTA thread..."); // TODO: debug

    do {
        ;
    } while (co_select_process(global_cb) == ESP_OK);

    co_free_all(global_cb);
    vTaskDelete(NULL);
}

static inline int co_thread_create(co_config_t *config) {
    int ret = xTaskCreate(co_main_thread, config->thread_name, config->stack_size, config,
                          config->thread_prio, NULL);
    if (ret == pdPASS) {
        return ESP_OK;
    }
    ESP_LOGE(CO_TAG, "can not create corsacOTA thread:%d", ret);
    return ESP_FAIL;
}

int corsacOTA_init(co_handle_t *handle, co_config_t *config) {
    if (handle == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (global_cb != NULL) {
        ESP_LOGE(CO_TAG, "already init");
        return ESP_FAIL;
    }

    co_cb_t *cb = co_control_block_create(config);
    if (cb == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int ret = co_socket_init(cb, config);
    if (ret != ESP_OK) {
        co_free_all(cb);
        return ESP_FAIL;
    }

    co_socket_list_init(cb);
    global_cb = cb;
    // start new corsacOTA thread
    if (co_thread_create(config) != ESP_OK) {
        co_free_all(cb);
        return ESP_FAIL;
    }

    *handle = (co_handle_t *)cb;
    return ESP_OK;
}
