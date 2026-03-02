#pragma once

#include "rfid_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RfidRegionEu = 0,
    RfidRegionUs,
} RfidRegion;

typedef struct {
    RfidModuleType module;
    RfidRegion region;
    RfidScanMode scan_mode;
    int8_t tx_power_db;
    uint16_t read_rate_ms;
    bool use_access_password;
    char access_password[9];
} RfidAppSettings;

typedef struct {
    char epc[96];
    char tid[96];
    char user[96];
} SavedUhfTag;

bool storage_tags_save(RfidTagRead* tags, size_t count, char* status_msg, size_t status_cap);
bool storage_saved_tags_save(const SavedUhfTag* tags, size_t count);
size_t storage_saved_tags_load(SavedUhfTag* tags, size_t max_tags);
bool storage_saved_tags_delete(size_t index);
bool storage_saved_tags_clear(void);
bool storage_settings_save(const RfidAppSettings* settings);
bool storage_settings_load(RfidAppSettings* settings);
