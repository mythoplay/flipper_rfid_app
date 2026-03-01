#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct UhfUart UhfUart;

bool uhf_uart_open(UhfUart** out_uart, uint32_t baudrate);
void uhf_uart_close(UhfUart* uart);

bool uhf_uart_send(UhfUart* uart, const uint8_t* data, size_t len);
bool uhf_uart_read(UhfUart* uart, uint8_t* out, size_t out_cap, size_t* out_len, uint32_t timeout_ms);
