#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RfidModuleFm504 = 0,
    RfidModuleRe40,
} RfidModuleType;

typedef enum {
    RfidScanModeEpc = 0,
    RfidScanModeTid,
    RfidScanModeUser,
    RfidScanModeAll,
} RfidScanMode;

typedef struct {
    char primary_hex[33];
    char raw_hex[65];
    int8_t rssi;
} RfidTagRead;

typedef struct {
    RfidModuleType module;
    uint32_t baudrate;
    RfidScanMode scan_mode;
    int8_t tx_power_db;
    uint16_t read_rate_ms;
} RfidDriverConfig;

typedef struct RfidDriver RfidDriver;

bool rfid_driver_open(RfidDriver** out_driver, const RfidDriverConfig* cfg);
void rfid_driver_close(RfidDriver* driver);

bool rfid_driver_set_mode(RfidDriver* driver, RfidScanMode mode);
bool rfid_driver_set_tx_power(RfidDriver* driver, int8_t dbm);
bool rfid_driver_set_enabled(RfidDriver* driver, bool enabled);
bool rfid_driver_probe(RfidDriver* driver, int8_t tx_power_dbm);

bool rfid_driver_scan_once(RfidDriver* driver, RfidTagRead* out);
bool rfid_driver_write_epc(RfidDriver* driver, const char* epc_hex);
bool rfid_driver_write_epc_ex(
    RfidDriver* driver,
    const char* epc_hex,
    char* detail,
    size_t detail_cap);
bool rfid_driver_access_pwd(
    RfidDriver* driver,
    const char* access_pwd_hex,
    char* detail,
    size_t detail_cap);

bool rfid_driver_normalize_epc(const char* in, char* out, size_t out_cap);
