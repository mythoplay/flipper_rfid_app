#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Fm504Uart Fm504Uart;

bool fm504_uart_open(Fm504Uart** out_uart, uint32_t baudrate);
void fm504_uart_close(Fm504Uart* uart);

bool fm504_uart_send(Fm504Uart* uart, const uint8_t* data, size_t len);
bool fm504_uart_read(Fm504Uart* uart, uint8_t* out, size_t out_cap, size_t* out_len, uint32_t timeout_ms);
