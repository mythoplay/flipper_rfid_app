#include "storage_tags.h"

#include <furi.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAGS_FILE "/ext/apps_data/fm504_rfid/tags.txt"
#define SETTINGS_FILE "/ext/apps_data/fm504_rfid/settings.txt"

static bool is_hex8(const char* s) {
    if(!s || strlen(s) != 8) return false;
    for(size_t i = 0; i < 8; i++) {
        char c = s[i];
        bool hex = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if(!hex) return false;
    }
    return true;
}

bool storage_tags_save(
    RfidTagRead* tags,
    size_t count,
    char* status_msg,
    size_t status_cap) {
    if(!tags || !status_msg || status_cap == 0) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    bool ok = false;
    do {
        if(!storage_simply_mkdir(storage, "/ext/apps_data/fm504_rfid")) break;
        if(!storage_file_open(file, TAGS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;

        char line[64];
        for(size_t i = 0; i < count; i++) {
            int len = snprintf(line, sizeof(line), "%s,%d\n", tags[i].primary_hex, tags[i].rssi);
            if(len <= 0) continue;
            if(storage_file_write(file, line, (size_t)len) != (size_t)len) {
                ok = false;
                goto end;
            }
        }
        ok = true;
    } while(false);

end:
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(ok) {
        snprintf((char*)status_msg, status_cap, "Saved: %u tags", (unsigned)count);
    } else {
        snprintf((char*)status_msg, status_cap, "Save error");
    }

    return ok;
}

bool storage_settings_save(const RfidAppSettings* settings) {
    if(!settings) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    do {
        if(!storage_simply_mkdir(storage, "/ext/apps_data/fm504_rfid")) break;
        if(!storage_file_open(file, SETTINGS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;

        char buff[128];
        int n = snprintf(
            buff,
            sizeof(buff),
            "module=%u\nscan_mode=%u\ntx_power_db=%d\nread_rate_ms=%u\nuse_access_password=%u\naccess_password=%s\n",
            (unsigned)settings->module,
            (unsigned)settings->scan_mode,
            settings->tx_power_db,
            (unsigned)settings->read_rate_ms,
            settings->use_access_password ? 1U : 0U,
            settings->access_password);
        if(n <= 0 || (size_t)n >= sizeof(buff)) break;
        if(storage_file_write(file, buff, (size_t)n) != (size_t)n) break;
        ok = true;
    } while(false);

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool storage_settings_load(RfidAppSettings* settings) {
    if(!settings) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    do {
        if(!storage_file_open(file, SETTINGS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) break;

        char buff[512];
        size_t read = storage_file_read(file, buff, sizeof(buff) - 1);
        if(read == 0) break;
        buff[read] = '\0';

        char* cursor = buff;
        while(*cursor) {
            char* line = cursor;
            while(*cursor && *cursor != '\n' && *cursor != '\r') cursor++;
            if(*cursor) {
                *cursor = '\0';
                cursor++;
                while(*cursor == '\n' || *cursor == '\r') cursor++;
            }
            if(line[0] == '\0') continue;

            char* eq = strchr(line, '=');
            if(eq) {
                *eq = '\0';
                const char* key = line;
                const char* val = eq + 1;

                if(strcmp(key, "module") == 0) {
                    long v = strtol(val, NULL, 10);
                    if(v >= (long)RfidModuleFm504 && v <= (long)RfidModuleRe40) settings->module = (RfidModuleType)v;
                } else if(strcmp(key, "scan_mode") == 0) {
                    long v = strtol(val, NULL, 10);
                    if(v >= (long)RfidScanModeEpc && v <= (long)RfidScanModeAll) settings->scan_mode = (RfidScanMode)v;
                } else if(strcmp(key, "tx_power_db") == 0) {
                    long v = strtol(val, NULL, 10);
                    if(v >= -2 && v <= 25) settings->tx_power_db = (int8_t)v;
                } else if(strcmp(key, "read_rate_ms") == 0) {
                    long v = strtol(val, NULL, 10);
                    if(v >= 10 && v <= 2000) settings->read_rate_ms = (uint16_t)v;
                } else if(strcmp(key, "use_access_password") == 0) {
                    settings->use_access_password = (strtol(val, NULL, 10) != 0);
                } else if(strcmp(key, "access_password") == 0) {
                    if(is_hex8(val)) {
                        strncpy(settings->access_password, val, sizeof(settings->access_password) - 1);
                        settings->access_password[sizeof(settings->access_password) - 1] = '\0';
                    }
                }
            }
        }
        ok = true;
    } while(false);

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
