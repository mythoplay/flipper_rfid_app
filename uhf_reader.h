#pragma once

#include "uhf_protocol.h"
#include "uhf_uart.h"

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    UhfScanModeEpc = 0,
    UhfScanModeTid,
    UhfScanModeUser,
} UhfScanMode;

bool uhf_reader_inventory_once(UhfUart* uart, UhfScanMode mode, UhfInventoryResult* out);
bool uhf_reader_write_epc(UhfUart* uart, const char* epc_hex);
bool uhf_reader_write_epc_ex(UhfUart* uart, const char* epc_hex, char* detail, size_t detail_cap);
bool uhf_reader_write_user_ex(
    UhfUart* uart,
    uint8_t addr_words,
    const char* user_hex,
    char* detail,
    size_t detail_cap);
bool uhf_reader_access_pwd(UhfUart* uart, const char* access_pwd_hex, char* detail, size_t detail_cap);
bool uhf_reader_set_tx_power(UhfUart* uart, int8_t dbm);
