#pragma once

#include "rfid_driver.h"

#include <stdbool.h>
#include <stddef.h>

bool storage_tags_save(RfidTagRead* tags, size_t count, char* status_msg, size_t status_cap);
