#pragma once

#include "fm504_protocol.h"
#include "fm504_uart.h"

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    Fm504ScanModeEpc = 0,
    Fm504ScanModeTid,
} Fm504ScanMode;

bool fm504_reader_inventory_once(Fm504Uart* uart, Fm504ScanMode mode, Fm504InventoryResult* out);
bool fm504_reader_write_epc(Fm504Uart* uart, const char* epc_hex);
bool fm504_reader_write_epc_ex(Fm504Uart* uart, const char* epc_hex, char* detail, size_t detail_cap);
bool fm504_reader_access_pwd(Fm504Uart* uart, const char* access_pwd_hex, char* detail, size_t detail_cap);
bool fm504_reader_set_tx_power(Fm504Uart* uart, int8_t dbm);
