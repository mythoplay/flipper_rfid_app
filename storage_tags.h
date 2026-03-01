#pragma once

#include "rfid_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    RfidModuleType module;
    RfidScanMode scan_mode;
    int8_t tx_power_db;
    uint16_t read_rate_ms;
    bool use_access_password;
    char access_password[9];
} RfidAppSettings;

bool storage_tags_save(RfidTagRead* tags, size_t count, char* status_msg, size_t status_cap);
bool storage_settings_save(const RfidAppSettings* settings);
bool storage_settings_load(RfidAppSettings* settings);
