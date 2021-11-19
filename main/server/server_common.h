#ifndef _SERVER_COMMON_H
#define _SERVER_COMMON_H

#include <stdint.h>
#include <stddef.h>

void tcp_server_task();
void tcp_data_in_handle(uint8_t *buffer, size_t len);
void mdns_setup();


#endif