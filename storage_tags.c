#include "storage_tags.h"

#include <furi.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_DATA_DIR_NEW "/ext/apps_data/flipperrfid"
#define APP_DATA_DIR_OLD "/ext/apps_data/fm504_rfid"

#define TAGS_FILE_NEW APP_DATA_DIR_NEW "/tags.txt"
#define SETTINGS_FILE_NEW APP_DATA_DIR_NEW "/settings.txt"
#define SAVED_TAGS_FILE_NEW APP_DATA_DIR_NEW "/saved_tags.csv"

#define TAGS_FILE_OLD APP_DATA_DIR_OLD "/tags.txt"
#define SETTINGS_FILE_OLD APP_DATA_DIR_OLD "/settings.txt"
#define SAVED_TAGS_FILE_OLD APP_DATA_DIR_OLD "/saved_tags.csv"

#define SAVED_TAGS_MAX_LINE 320U
#define SAVED_TAGS_MAX_SCAN_BYTES (32U * 1024U)

static bool is_hex8(const char* s) {
    if(!s || strlen(s) != 8) return false;
    for(size_t i = 0; i < 8; i++) {
        char c = s[i];
        bool hex = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if(!hex) return false;
    }
    return true;
}

static bool storage_open_read_with_fallback(Storage* storage, File* file, const char* primary, const char* fallback) {
    if(!storage || !file || !primary) return false;

    if(storage_file_exists(storage, primary)) {
        if(storage_file_open(file, primary, FSAM_READ, FSOM_OPEN_EXISTING)) return true;
    }

    if(fallback && storage_file_exists(storage, fallback)) {
        if(storage_file_open(file, fallback, FSAM_READ, FSOM_OPEN_EXISTING)) return true;
    }

    return false;
}

static void sanitize_saved_field(char* s) {
    if(!s) return;
    size_t w = 0;
    for(size_t r = 0; s[r] != '\0'; r++) {
        char c = s[r];
        /* Keep visible ASCII only, drop controls and separators. */
        if(c == ',' || c == '\r' || c == '\n') continue;
        if((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) continue;
        s[w++] = c;
    }
    s[w] = '\0';
}

static bool parse_saved_tag_line(const char* line_in, SavedUhfTag* out_tag) {
    if(!line_in || !out_tag) return false;

    char line[SAVED_TAGS_MAX_LINE + 1];
    strncpy(line, line_in, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    char* c1 = strchr(line, ',');
    if(!c1) return false;
    *c1 = '\0';
    char* c2 = strchr(c1 + 1, ',');
    if(!c2) return false;
    *c2 = '\0';

    strncpy(out_tag->epc, line, sizeof(out_tag->epc) - 1);
    out_tag->epc[sizeof(out_tag->epc) - 1] = '\0';
    strncpy(out_tag->tid, c1 + 1, sizeof(out_tag->tid) - 1);
    out_tag->tid[sizeof(out_tag->tid) - 1] = '\0';
    strncpy(out_tag->user, c2 + 1, sizeof(out_tag->user) - 1);
    out_tag->user[sizeof(out_tag->user) - 1] = '\0';

    sanitize_saved_field(out_tag->epc);
    sanitize_saved_field(out_tag->tid);
    sanitize_saved_field(out_tag->user);
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
        if(!storage_simply_mkdir(storage, APP_DATA_DIR_NEW)) break;
        if(!storage_file_open(file, TAGS_FILE_NEW, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;

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

static void clean_csv_field(char* s) {
    if(!s) return;
    for(size_t i = 0; s[i] != '\0'; i++) {
        if(s[i] == ',' || s[i] == '\n' || s[i] == '\r') s[i] = ' ';
    }
}

bool storage_saved_tags_save(const SavedUhfTag* tags, size_t count) {
    if(!tags && count > 0) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    do {
        if(!storage_simply_mkdir(storage, APP_DATA_DIR_NEW)) break;
        if(!storage_file_open(file, SAVED_TAGS_FILE_NEW, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;

        char line[360];
        for(size_t i = 0; i < count; i++) {
            char epc[96];
            char tid[96];
            char user[96];
            strncpy(epc, tags[i].epc, sizeof(epc) - 1);
            epc[sizeof(epc) - 1] = '\0';
            strncpy(tid, tags[i].tid, sizeof(tid) - 1);
            tid[sizeof(tid) - 1] = '\0';
            strncpy(user, tags[i].user, sizeof(user) - 1);
            user[sizeof(user) - 1] = '\0';
            clean_csv_field(epc);
            clean_csv_field(tid);
            clean_csv_field(user);

            int n = snprintf(line, sizeof(line), "%s,%s,%s\n", epc, tid, user);
            if(n <= 0 || (size_t)n >= sizeof(line)) continue;
            if(storage_file_write(file, line, (size_t)n) != (size_t)n) {
                ok = false;
                goto done_saved_save;
            }
        }
        ok = true;
    } while(false);

done_saved_save:
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

size_t storage_saved_tags_load(SavedUhfTag* tags, size_t max_tags) {
    if(!tags || max_tags == 0) return 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    size_t count = 0;

    do {
        if(!storage_open_read_with_fallback(storage, file, SAVED_TAGS_FILE_NEW, SAVED_TAGS_FILE_OLD))
            break;

        char line[SAVED_TAGS_MAX_LINE + 1];
        size_t line_len = 0;
        size_t scanned = 0;
        uint8_t ch = 0;

        while(count < max_tags && scanned < SAVED_TAGS_MAX_SCAN_BYTES) {
            size_t r = storage_file_read(file, &ch, 1);
            if(r != 1) break;
            scanned++;

            if(ch == '\r' || ch == '\n') {
                if(line_len == 0) continue;
                line[line_len] = '\0';
                if(parse_saved_tag_line(line, &tags[count])) count++;
                line_len = 0;
                continue;
            }

            if(line_len < SAVED_TAGS_MAX_LINE) {
                line[line_len++] = (char)ch;
            }
        }

        /* Last line without trailing newline */
        if(count < max_tags && line_len > 0) {
            line[line_len] = '\0';
            if(parse_saved_tag_line(line, &tags[count])) count++;
        }
    } while(false);

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return count;
}

bool storage_saved_tags_delete(size_t index) {
    SavedUhfTag tmp[64];
    size_t count = storage_saved_tags_load(tmp, 64);
    if(index >= count) return false;
    for(size_t i = index; i + 1 < count; i++) {
        tmp[i] = tmp[i + 1];
    }
    count--;
    return storage_saved_tags_save(tmp, count);
}

bool storage_saved_tags_clear(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok_new = storage_simply_remove(storage, SAVED_TAGS_FILE_NEW);
    bool ok_old = storage_simply_remove(storage, SAVED_TAGS_FILE_OLD);
    furi_record_close(RECORD_STORAGE);
    return ok_new || ok_old;
}

bool storage_settings_save(const RfidAppSettings* settings) {
    if(!settings) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    do {
        if(!storage_simply_mkdir(storage, APP_DATA_DIR_NEW)) break;
        if(!storage_file_open(file, SETTINGS_FILE_NEW, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;

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
        if(!storage_open_read_with_fallback(storage, file, SETTINGS_FILE_NEW, SETTINGS_FILE_OLD))
            break;

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
                    if(v >= -2 && v <= 27) settings->tx_power_db = (int8_t)v;
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
