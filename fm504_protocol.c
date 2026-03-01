#include "fm504_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool is_hex_char(char c) {
    return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'));
}

static size_t extract_longest_hex_token(const char* in, size_t in_len, char* out, size_t out_cap) {
    size_t best_start = 0;
    size_t best_len = 0;

    for(size_t i = 0; i < in_len; i++) {
        if(!is_hex_char((char)toupper((unsigned char)in[i]))) continue;
        size_t j = i;
        while(j < in_len && is_hex_char((char)toupper((unsigned char)in[j]))) j++;
        size_t token_len = j - i;
        if(token_len > best_len) {
            best_start = i;
            best_len = token_len;
        }
        i = j;
    }

    if(best_len == 0 || best_len + 1 > out_cap) return 0;
    for(size_t i = 0; i < best_len; i++) {
        out[i] = (char)toupper((unsigned char)in[best_start + i]);
    }
    out[best_len] = '\0';
    return best_len;
}

bool fm504_protocol_normalize_epc(const char* in, char* out, size_t out_cap) {
    if(!in || !out || out_cap < 2) return false;

    size_t j = 0;
    for(size_t i = 0; in[i] != '\0'; i++) {
        if(in[i] == ' ' || in[i] == '\t' || in[i] == '-') continue;
        if(j + 1 >= out_cap) return false;

        char c = (char)toupper((unsigned char)in[i]);
        if(!is_hex_char(c)) return false;
        out[j++] = c;
    }

    out[j] = '\0';
    return j > 0 && (j % 2 == 0);
}

bool fm504_protocol_is_valid_epc(const char* epc_hex) {
    if(!epc_hex) return false;
    size_t len = strlen(epc_hex);
    if(len == 0 || len > 32 || (len % 2 != 0)) return false;
    for(size_t i = 0; i < len; i++) {
        if(!is_hex_char(epc_hex[i])) return false;
    }
    return true;
}

bool fm504_protocol_make_inventory_cmd(uint8_t* out, size_t out_cap, size_t* out_len) {
    if(!out || !out_len || out_cap < 3) return false;

    /* FM505 manual:
       Inventory single tag: <LF>Q<CR> */
    out[0] = '\n';
    out[1] = 'Q';
    out[2] = '\r';
    *out_len = 3;
    return true;
}

bool fm504_protocol_make_read_tid_cmd(
    uint8_t addr_words,
    uint8_t words,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!out || !out_len) return false;
    int n = snprintf((char*)out, out_cap, "\nR2,%X,%X\r", addr_words, words);
    if(n <= 0 || (size_t)n >= out_cap) return false;
    *out_len = (size_t)n;
    return true;
}

bool fm504_protocol_make_read_epc_cmd(uint8_t words, uint8_t* out, size_t out_cap, size_t* out_len) {
    if(!out || !out_len) return false;
    int n = snprintf((char*)out, out_cap, "\nR1,0,%X\r", words);
    if(n <= 0 || (size_t)n >= out_cap) return false;
    *out_len = (size_t)n;
    return true;
}

bool fm504_protocol_make_read_user_cmd(
    uint8_t addr_words,
    uint8_t words,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!out || !out_len) return false;
    int n = snprintf((char*)out, out_cap, "\nR3,%X,%X\r", addr_words, words);
    if(n <= 0 || (size_t)n >= out_cap) return false;
    *out_len = (size_t)n;
    return true;
}

bool fm504_protocol_make_set_tx_power_cmd(
    int8_t dbm,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!out || !out_len) return false;
    if(dbm < -2 || dbm > 25) return false;

    /* FM503 python: hex_power_level = dbm + 2, command N1,XX */
    uint8_t level = (uint8_t)(dbm + 2);
    int n = snprintf((char*)out, out_cap, "\nN1,%02X\r", level);
    if(n <= 0 || (size_t)n >= out_cap) return false;
    *out_len = (size_t)n;
    return true;
}

bool fm504_protocol_make_write_epc_cmd(
    const char* epc_hex,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(!epc_hex || !out || !out_len) return false;
    if(!fm504_protocol_is_valid_epc(epc_hex)) return false;

    /* Inference from FM505 manual:
       R example is R2,0,4 (bank,address,length).
       For EPC write we use W1,2,<len_words>,<epc_hex> with <LF>/<CR>. */
    size_t epc_hex_len = strlen(epc_hex);
    size_t len_words = epc_hex_len / 4;
    if(len_words == 0) return false;
    int n = snprintf((char*)out, out_cap, "\nW1,2,%X,%s\r", (unsigned)len_words, epc_hex);
    if(n <= 0 || (size_t)n >= out_cap) return false;

    *out_len = (size_t)n;
    return true;
}

bool fm504_protocol_parse_inventory(const uint8_t* frame, size_t frame_len, Fm504InventoryResult* out) {
    return fm504_protocol_parse_epc_read(frame, frame_len, out);
}

bool fm504_protocol_parse_tid_read(const uint8_t* frame, size_t frame_len, Fm504InventoryResult* out) {
    if(!frame || !out || frame_len == 0) return false;

    char buff[128];
    char hex[65];
    if(frame_len >= sizeof(buff)) return false;

    memcpy(buff, frame, frame_len);
    buff[frame_len] = '\0';
    size_t hex_len = extract_longest_hex_token(buff, frame_len, hex, sizeof(hex));
    if(hex_len < 8 || hex_len > 64 || (hex_len % 2 != 0)) return false;

    strncpy(out->raw_hex, hex, sizeof(out->raw_hex) - 1);
    out->raw_hex[sizeof(out->raw_hex) - 1] = '\0';

    strncpy(out->epc_hex, hex, sizeof(out->epc_hex) - 1);
    out->epc_hex[sizeof(out->epc_hex) - 1] = '\0';
    out->rssi = 0;
    return true;
}

bool fm504_protocol_parse_epc_read(const uint8_t* frame, size_t frame_len, Fm504InventoryResult* out) {
    if(!frame || !out || frame_len == 0) return false;

    char buff[128];
    char hex[65];
    if(frame_len >= sizeof(buff)) return false;

    memcpy(buff, frame, frame_len);
    buff[frame_len] = '\0';
    size_t hex_len = extract_longest_hex_token(buff, frame_len, hex, sizeof(hex));
    if(hex_len < 8 || (hex_len % 2 != 0)) return false;

    strncpy(out->raw_hex, hex, sizeof(out->raw_hex) - 1);
    out->raw_hex[sizeof(out->raw_hex) - 1] = '\0';

    /* FM503 python indicates R1 returns CRC16 + PC + EPC */
    if(hex_len >= 8) {
        unsigned pc = 0;
        if(sscanf(hex + 4, "%4X", &pc) == 1) {
            size_t epc_words = (pc >> 11) & 0x1F;
            size_t epc_len = epc_words * 4;
            if(epc_len > 0 && (8 + epc_len) <= hex_len && epc_len < sizeof(out->epc_hex)) {
                memcpy(out->epc_hex, hex + 8, epc_len);
                out->epc_hex[epc_len] = '\0';
            } else {
                size_t fallback = hex_len - 8;
                if(fallback >= sizeof(out->epc_hex)) fallback = sizeof(out->epc_hex) - 1;
                memcpy(out->epc_hex, hex + 8, fallback);
                out->epc_hex[fallback] = '\0';
            }
        } else {
            return false;
        }
    } else {
        return false;
    }

    out->rssi = 0;
    return true;
}

bool fm504_protocol_parse_user_read(const uint8_t* frame, size_t frame_len, Fm504InventoryResult* out) {
    if(!frame || !out || frame_len == 0) return false;

    char buff[128];
    char hex[65];
    if(frame_len >= sizeof(buff)) return false;

    memcpy(buff, frame, frame_len);
    buff[frame_len] = '\0';
    size_t hex_len = extract_longest_hex_token(buff, frame_len, hex, sizeof(hex));
    if(hex_len < 4 || (hex_len % 2 != 0)) return false;

    strncpy(out->raw_hex, hex, sizeof(out->raw_hex) - 1);
    out->raw_hex[sizeof(out->raw_hex) - 1] = '\0';

    size_t copy_len = hex_len;
    if(copy_len >= sizeof(out->epc_hex)) copy_len = sizeof(out->epc_hex) - 1;
    memcpy(out->epc_hex, hex, copy_len);
    out->epc_hex[copy_len] = '\0';
    out->rssi = 0;
    return true;
}

bool fm504_protocol_response_is_ok(const uint8_t* frame, size_t frame_len) {
    if(!frame || frame_len == 0) return false;

    char buff[96];
    if(frame_len >= sizeof(buff)) return false;
    memcpy(buff, frame, frame_len);
    buff[frame_len] = '\0';

    return strstr(buff, "<OK>") || strstr(buff, "OK");
}
