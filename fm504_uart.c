#include "fm504_uart.h"

#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <furi_hal_serial_types.h>
#include <furi/core/stream_buffer.h>
#include <stdlib.h>
#include <string.h>

#define TAG "FM504_UART"
#define FM504_UART_RX_BUFFER_SIZE 512U
#define FM504_UART_INTERBYTE_TIMEOUT_MS 10U

struct Fm504Uart {
    uint32_t baudrate;
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
};

static void fm504_uart_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    UNUSED(handle);
    Fm504Uart* uart = context;
    if(!uart || !uart->rx_stream) return;

    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(uart->serial)) {
            const uint8_t byte = furi_hal_serial_async_rx(uart->serial);
            furi_stream_buffer_send(uart->rx_stream, &byte, 1, 0);
        }
    }
}

bool fm504_uart_open(Fm504Uart** out_uart, uint32_t baudrate) {
    furi_check(out_uart);
    Fm504Uart* uart = malloc(sizeof(Fm504Uart));
    if(!uart) return false;

    memset(uart, 0, sizeof(Fm504Uart));
    uart->baudrate = baudrate;
    uart->rx_stream = furi_stream_buffer_alloc(FM504_UART_RX_BUFFER_SIZE, 1);
    if(!uart->rx_stream) {
        free(uart);
        return false;
    }

    uart->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!uart->serial) {
        FURI_LOG_E(TAG, "USART busy (acquire failed)");
        furi_stream_buffer_free(uart->rx_stream);
        free(uart);
        return false;
    }

    furi_hal_serial_init(uart->serial, uart->baudrate);
    furi_hal_serial_enable_direction(uart->serial, FuriHalSerialDirectionRx);
    furi_hal_serial_enable_direction(uart->serial, FuriHalSerialDirectionTx);
    furi_hal_serial_async_rx_start(uart->serial, fm504_uart_rx_callback, uart, true);

    FURI_LOG_I(TAG, "UART open @ %lu bps", baudrate);
    *out_uart = uart;
    return true;
}

void fm504_uart_close(Fm504Uart* uart) {
    if(!uart) return;

    if(uart->serial) {
        furi_hal_serial_async_rx_stop(uart->serial);
        furi_hal_serial_tx_wait_complete(uart->serial);
        furi_hal_serial_disable_direction(uart->serial, FuriHalSerialDirectionTx);
        furi_hal_serial_disable_direction(uart->serial, FuriHalSerialDirectionRx);
        furi_hal_serial_deinit(uart->serial);
        furi_hal_serial_control_release(uart->serial);
        uart->serial = NULL;
    }

    if(uart->rx_stream) {
        furi_stream_buffer_free(uart->rx_stream);
        uart->rx_stream = NULL;
    }

    FURI_LOG_I(TAG, "UART close");
    free(uart);
}

bool fm504_uart_send(Fm504Uart* uart, const uint8_t* data, size_t len) {
    if(!uart || !uart->serial || !uart->rx_stream || !data || !len) return false;

    /* Drop stale bytes before sending a new command frame. */
    furi_stream_buffer_reset(uart->rx_stream);

    furi_hal_serial_tx(uart->serial, data, len);
    furi_hal_serial_tx_wait_complete(uart->serial);
    FURI_LOG_D(TAG, "UART send len=%u", (unsigned)len);
    return true;
}

bool fm504_uart_read(Fm504Uart* uart, uint8_t* out, size_t out_cap, size_t* out_len, uint32_t timeout_ms) {
    if(!uart || !uart->rx_stream || !out || !out_len || !out_cap) return false;
    *out_len = 0;

    const uint32_t start_tick = furi_get_tick();
    const uint32_t total_timeout_ticks = furi_ms_to_ticks(timeout_ms);
    bool started = false;

    while(*out_len < out_cap) {
        uint32_t wait_ticks = 0;
        if(started) {
            wait_ticks = furi_ms_to_ticks(FM504_UART_INTERBYTE_TIMEOUT_MS);
        } else {
            const uint32_t now = furi_get_tick();
            const uint32_t elapsed = now - start_tick;
            if(elapsed >= total_timeout_ticks) break;
            wait_ticks = total_timeout_ticks - elapsed;
        }

        uint8_t byte = 0;
        size_t got = furi_stream_buffer_receive(uart->rx_stream, &byte, 1, wait_ticks);
        if(got == 0) {
            if(started) break;
            continue;
        }

        out[*out_len] = byte;
        (*out_len)++;
        started = true;
        if(byte == '\r') break;
    }

    if(*out_len == 0) return false;
    FURI_LOG_D(TAG, "UART read len=%u", (unsigned)*out_len);
    return true;
}
