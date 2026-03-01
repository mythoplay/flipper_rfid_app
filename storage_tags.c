#include "storage_tags.h"

#include <furi.h>
#include <storage/storage.h>

#include <stdio.h>
#include <string.h>

#define TAGS_FILE "/ext/apps_data/fm504_rfid/tags.txt"

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
