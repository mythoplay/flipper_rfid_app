#include "uhf_reader.h"

#include <stdio.h>
#include <string.h>

static void uhf_reader_decode_write_result(const uint8_t* rx, size_t rx_len, char* detail, size_t detail_cap) {
    if(!detail || detail_cap == 0) return;
    detail[0] = '\0';

    if(!rx || rx_len == 0) {
        snprintf(detail, detail_cap, "No UART response");
        return;
    }

    char line[96];
    size_t n = (rx_len < sizeof(line) - 1) ? rx_len : (sizeof(line) - 1);
    memcpy(line, rx, n);
    line[n] = '\0';

    if(strstr(line, "W<OK>") || strstr(line, "<OK>")) {
        snprintf(detail, detail_cap, "Write OK");
        return;
    }

    const char* payload = line;
    while(*payload == '\n' || *payload == '\r' || *payload == ' ') payload++;
    if(*payload == 'W' && payload[1] != '<') payload++;
    while(*payload == ',' || *payload == ' ') payload++;

    if(payload[0] == 'W' && payload[1] == '\0') snprintf(detail, detail_cap, "No tag in range");
    else if(payload[0] == '3' && payload[1] == '\0') snprintf(detail, detail_cap, "Addr/length not supported");
    else if(payload[0] == '4' && payload[1] == '\0')
        snprintf(detail, detail_cap, "RAW 4 (Locked): access pwd/permalock");
    else if(payload[0] == 'B' && payload[1] == '\0') snprintf(detail, detail_cap, "Tag power too low");
    else if(payload[0] == 'E' && payload[1] == '\0') snprintf(detail, detail_cap, "Tag replied error");
    else if(payload[0] == 'X' && payload[1] == '\0') snprintf(detail, detail_cap, "Command format not supported");
    else if(payload[0] == 'F' && payload[1] == '\0') snprintf(detail, detail_cap, "Non-specific error");
    else if(payload[0] == '0' && payload[1] == '\0') snprintf(detail, detail_cap, "Other error");
    else snprintf(detail, detail_cap, "Raw: %s", payload);
}

bool uhf_reader_inventory_once(UhfUart* uart, UhfScanMode mode, UhfInventoryResult* out) {
    if(!uart || !out) return false;

    uint8_t cmd[32];
    size_t cmd_len = 0;
    bool ok_cmd = false;
    if(mode == UhfScanModeTid) {
        ok_cmd = uhf_protocol_make_read_tid_cmd(0, 6, cmd, sizeof(cmd), &cmd_len);
    } else if(mode == UhfScanModeUser) {
        ok_cmd = uhf_protocol_make_read_user_cmd(0, 4, cmd, sizeof(cmd), &cmd_len);
    } else {
        ok_cmd = uhf_protocol_make_read_epc_cmd(8, cmd, sizeof(cmd), &cmd_len);
    }
    if(!ok_cmd) return false;
    if(!uhf_uart_send(uart, cmd, cmd_len)) return false;

    uint8_t rx[128];
    size_t rx_len = 0;
    if(!uhf_uart_read(uart, rx, sizeof(rx), &rx_len, 100)) return false;
    if(rx_len == 0) return false;

    if(mode == UhfScanModeTid) {
        return uhf_protocol_parse_tid_read(rx, rx_len, out);
    } else if(mode == UhfScanModeUser) {
        return uhf_protocol_parse_user_read(rx, rx_len, out);
    } else {
        return uhf_protocol_parse_epc_read(rx, rx_len, out);
    }
}

bool uhf_reader_write_epc_ex(UhfUart* uart, const char* epc_hex, char* detail, size_t detail_cap) {
    if(!uart || !epc_hex) return false;
    if(detail && detail_cap > 0) detail[0] = '\0';

    uint8_t cmd[96];
    size_t cmd_len = 0;
    if(!uhf_protocol_make_write_epc_cmd(epc_hex, cmd, sizeof(cmd), &cmd_len)) return false;
    if(!uhf_uart_send(uart, cmd, cmd_len)) return false;

    uint8_t rx[96] = {0};
    size_t rx_len = 0;
    bool got = false;
    for(size_t i = 0; i < 3; i++) {
        if(uhf_uart_read(uart, rx, sizeof(rx), &rx_len, 220) && rx_len > 0) {
            got = true;
            break;
        }
    }
    if(!got) {
        uhf_reader_decode_write_result(NULL, 0, detail, detail_cap);
        return false;
    }

    uhf_reader_decode_write_result(rx, rx_len, detail, detail_cap);
    return uhf_protocol_response_is_ok(rx, rx_len);
}

bool uhf_reader_write_epc(UhfUart* uart, const char* epc_hex) {
    return uhf_reader_write_epc_ex(uart, epc_hex, NULL, 0);
}

bool uhf_reader_write_user_ex(
    UhfUart* uart,
    uint8_t addr_words,
    const char* user_hex,
    char* detail,
    size_t detail_cap) {
    if(!uart || !user_hex) return false;
    if(detail && detail_cap > 0) detail[0] = '\0';

    uint8_t cmd[128];
    size_t cmd_len = 0;
    if(!uhf_protocol_make_write_user_cmd(addr_words, user_hex, cmd, sizeof(cmd), &cmd_len)) return false;
    if(!uhf_uart_send(uart, cmd, cmd_len)) return false;

    uint8_t rx[96] = {0};
    size_t rx_len = 0;
    bool got = false;
    for(size_t i = 0; i < 3; i++) {
        if(uhf_uart_read(uart, rx, sizeof(rx), &rx_len, 220) && rx_len > 0) {
            got = true;
            break;
        }
    }
    if(!got) {
        uhf_reader_decode_write_result(NULL, 0, detail, detail_cap);
        return false;
    }

    uhf_reader_decode_write_result(rx, rx_len, detail, detail_cap);
    return uhf_protocol_response_is_ok(rx, rx_len);
}

bool uhf_reader_access_pwd(UhfUart* uart, const char* access_pwd_hex, char* detail, size_t detail_cap) {
    if(!uart || !access_pwd_hex) return false;
    if(detail && detail_cap > 0) detail[0] = '\0';

    size_t pwd_len = strlen(access_pwd_hex);
    if(pwd_len != 8) {
        if(detail && detail_cap > 0) snprintf(detail, detail_cap, "Access pwd must be 8 HEX");
        return false;
    }

    uint8_t cmd[32];
    int cmd_len = snprintf((char*)cmd, sizeof(cmd), "\nP%s\r", access_pwd_hex);
    if(cmd_len <= 0 || (size_t)cmd_len >= sizeof(cmd)) return false;
    if(!uhf_uart_send(uart, cmd, (size_t)cmd_len)) return false;

    uint8_t rx[96] = {0};
    size_t rx_len = 0;
    bool got = false;
    for(size_t i = 0; i < 3; i++) {
        if(uhf_uart_read(uart, rx, sizeof(rx), &rx_len, 220) && rx_len > 0) {
            got = true;
            break;
        }
    }
    if(!got) {
        if(detail && detail_cap > 0) snprintf(detail, detail_cap, "Access: no UART response");
        return false;
    }

    if(uhf_protocol_response_is_ok(rx, rx_len)) {
        if(detail && detail_cap > 0) snprintf(detail, detail_cap, "Access OK");
        return true;
    }

    if(detail && detail_cap > 0) {
        char raw[64];
        size_t n = (rx_len < sizeof(raw) - 1) ? rx_len : (sizeof(raw) - 1);
        memcpy(raw, rx, n);
        raw[n] = '\0';
        snprintf(detail, detail_cap, "Access failed: %.48s", raw);
    }
    return false;
}

bool uhf_reader_set_tx_power(UhfUart* uart, int8_t dbm) {
    if(!uart) return false;

    uint8_t cmd[32];
    size_t cmd_len = 0;
    if(!uhf_protocol_make_set_tx_power_cmd(dbm, cmd, sizeof(cmd), &cmd_len)) return false;
    if(!uhf_uart_send(uart, cmd, cmd_len)) return false;

    uint8_t rx[96];
    size_t rx_len = 0;
    if(!uhf_uart_read(uart, rx, sizeof(rx), &rx_len, 120)) return false;
    if(rx_len == 0) return false;

    return uhf_protocol_response_is_ok(rx, rx_len);
}
