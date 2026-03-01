#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char epc_hex[33];
    char raw_hex[65];
    int8_t rssi;
} UhfInventoryResult;

bool uhf_protocol_normalize_epc(const char* in, char* out, size_t out_cap);
bool uhf_protocol_is_valid_epc(const char* epc_hex);

bool uhf_protocol_make_inventory_cmd(uint8_t* out, size_t out_cap, size_t* out_len);
bool uhf_protocol_make_read_tid_cmd(
    uint8_t addr_words,
    uint8_t words,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
bool uhf_protocol_make_read_epc_cmd(uint8_t words, uint8_t* out, size_t out_cap, size_t* out_len);
bool uhf_protocol_make_read_user_cmd(
    uint8_t addr_words,
    uint8_t words,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
bool uhf_protocol_make_set_tx_power_cmd(
    int8_t dbm,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
bool uhf_protocol_make_write_epc_cmd(
    const char* epc_hex,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
bool uhf_protocol_make_write_user_cmd(
    uint8_t addr_words,
    const char* user_hex,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);

bool uhf_protocol_parse_inventory(const uint8_t* frame, size_t frame_len, UhfInventoryResult* out);
bool uhf_protocol_parse_tid_read(const uint8_t* frame, size_t frame_len, UhfInventoryResult* out);
bool uhf_protocol_parse_epc_read(const uint8_t* frame, size_t frame_len, UhfInventoryResult* out);
bool uhf_protocol_parse_user_read(const uint8_t* frame, size_t frame_len, UhfInventoryResult* out);
bool uhf_protocol_response_is_ok(const uint8_t* frame, size_t frame_len);
