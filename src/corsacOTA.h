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
#ifndef _CORSACOTA_H_
#define _CORSACOTA_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef signed int co_err_t;

#define CO_OK                    0
#define CO_FAIL                  -1
#define CO_ERROR_IO_PENDING      -2
#define CO_ERROR_NO_MEM          0x101
#define CO_ERROR_INVALID_ARG     0x102
#define CO_ERROR_INVALID_SIZE    0x104
#define CO_ERROR_INVALID_OTA_PTN -3

#define CO_RES_SUCCESS           0
#define CO_RES_SYSTEM_ERROR      1
#define CO_RES_INVALID_ARG       2
#define CO_RES_INVALID_SIZE      3
#define CO_RES_INVALID_STATUS    4

/**
 * @brief corsacOTA instance handle. Only one instance is allowed.
 *
 */
typedef void *co_handle_t;

typedef struct co_config {
    char *thread_name; // corsacOTA thread name
    int stack_size;    // corsacOTA thread stack size
    int thread_prio;   // corsacOTA thread priority

    int listen_port;    // corsacOTA server listen port
    int max_listen_num; // Maximum number of connections. In fact, after the handshake is complete, there is only one connection to provide services.

    int wait_timeout_sec; // Timeout (in seconds)
    int wait_timeout_usec; // Timeout (in microseconds)

} co_config_t;

/**
 * @brief Start the corsacOTA server
 *
 * @param config Configuration for new instance of the server
 * @return
 *  - ESP_OK                   : Server Init successfully
 *  - ESP_ERR_INVALID_ARG      : Null argument
 *  - ESP_ERR_NO_MEM           : Failed to allocate memory for instance
 */
int corsacOTA_init(co_handle_t *handle, co_config_t *config);

#ifdef __cplusplus
}
#endif

#endif