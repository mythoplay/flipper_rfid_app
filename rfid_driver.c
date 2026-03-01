#include "rfid_driver.h"

#include "fm504_reader.h"
#include "fm504_uart.h"

#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Fm504Uart* uart;
    Fm504ScanMode mode;
    const GpioPin* en_gpio;
    bool enabled;
} DriverFm504;

typedef struct {
    RfidScanMode mode;
} DriverRe40;

struct RfidDriver {
    RfidModuleType module;
    void* impl;
};

static bool is_hex_char(char c) {
    return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'));
}

bool rfid_driver_normalize_epc(const char* in, char* out, size_t out_cap) {
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
    return j > 0 && (j % 2 == 0) && (j <= 32);
}

static bool fm504_open_impl(void** out_impl, const RfidDriverConfig* cfg) {
    if(!out_impl || !cfg) return false;

    DriverFm504* impl = malloc(sizeof(DriverFm504));
    if(!impl) return false;
    memset(impl, 0, sizeof(DriverFm504));

    if(!fm504_uart_open(&impl->uart, cfg->baudrate)) {
        free(impl);
        return false;
    }

    if(cfg->scan_mode == RfidScanModeTid) {
        impl->mode = Fm504ScanModeTid;
    } else if(cfg->scan_mode == RfidScanModeUser) {
        impl->mode = Fm504ScanModeUser;
    } else {
        impl->mode = Fm504ScanModeEpc;
    }
    impl->en_gpio = &gpio_ext_pa7;
    impl->enabled = false;

    /* EN control: default OFF to avoid keeping reader powered all the time */
    furi_hal_gpio_init(impl->en_gpio, GpioModeOutputPushPull, GpioPullDown, GpioSpeedLow);
    furi_hal_gpio_write(impl->en_gpio, false);
    *out_impl = impl;
    return true;
}

static void fm504_close_impl(void* impl_ptr) {
    if(!impl_ptr) return;
    DriverFm504* impl = impl_ptr;
    if(impl->en_gpio) furi_hal_gpio_write(impl->en_gpio, false);
    fm504_uart_close(impl->uart);
    free(impl);
}

static bool re40_open_impl(void** out_impl, const RfidDriverConfig* cfg) {
    if(!out_impl || !cfg) return false;
    DriverRe40* impl = malloc(sizeof(DriverRe40));
    if(!impl) return false;
    memset(impl, 0, sizeof(DriverRe40));
    impl->mode = cfg->scan_mode;
    *out_impl = impl;
    return true;
}

static void re40_close_impl(void* impl_ptr) {
    if(!impl_ptr) return;
    free(impl_ptr);
}

bool rfid_driver_open(RfidDriver** out_driver, const RfidDriverConfig* cfg) {
    if(!out_driver || !cfg) return false;

    RfidDriver* driver = malloc(sizeof(RfidDriver));
    if(!driver) return false;
    memset(driver, 0, sizeof(RfidDriver));

    driver->module = cfg->module;

    bool ok = false;
    switch(cfg->module) {
    case RfidModuleFm504:
        ok = fm504_open_impl(&driver->impl, cfg);
        break;
    case RfidModuleRe40:
        ok = re40_open_impl(&driver->impl, cfg);
        break;
    default:
        ok = false;
        break;
    }

    if(!ok) {
        free(driver);
        return false;
    }

    *out_driver = driver;
    return true;
}

void rfid_driver_close(RfidDriver* driver) {
    if(!driver) return;

    switch(driver->module) {
    case RfidModuleFm504:
        fm504_close_impl(driver->impl);
        break;
    case RfidModuleRe40:
        re40_close_impl(driver->impl);
        break;
    default:
        break;
    }

    free(driver);
}

bool rfid_driver_set_mode(RfidDriver* driver, RfidScanMode mode) {
    if(!driver || !driver->impl) return false;

    switch(driver->module) {
    case RfidModuleFm504: {
        DriverFm504* impl = driver->impl;
        if(mode == RfidScanModeTid) {
            impl->mode = Fm504ScanModeTid;
        } else if(mode == RfidScanModeUser) {
            impl->mode = Fm504ScanModeUser;
        } else {
            impl->mode = Fm504ScanModeEpc;
        }
        return true;
    }
    case RfidModuleRe40: {
        DriverRe40* impl = driver->impl;
        impl->mode = mode;
        return true;
    }
    default:
        return false;
    }
}

bool rfid_driver_set_tx_power(RfidDriver* driver, int8_t dbm) {
    if(!driver || !driver->impl) return false;

    switch(driver->module) {
    case RfidModuleFm504: {
        DriverFm504* impl = driver->impl;
        return fm504_reader_set_tx_power(impl->uart, dbm);
    }
    case RfidModuleRe40:
        return false;
    default:
        return false;
    }
}

bool rfid_driver_set_enabled(RfidDriver* driver, bool enabled) {
    if(!driver || !driver->impl) return false;

    switch(driver->module) {
    case RfidModuleFm504: {
        DriverFm504* impl = driver->impl;
        furi_hal_gpio_write(impl->en_gpio, enabled);
        impl->enabled = enabled;
        if(enabled) furi_delay_ms(180);
        return true;
    }
    case RfidModuleRe40:
        return true;
    default:
        return false;
    }
}

bool rfid_driver_probe(RfidDriver* driver, int8_t tx_power_dbm) {
    if(!driver || !driver->impl) return false;

    switch(driver->module) {
    case RfidModuleFm504: {
        DriverFm504* impl = driver->impl;
        bool was_enabled = impl->enabled;

        if(!was_enabled) {
            if(!rfid_driver_set_enabled(driver, true)) return false;
        }

        bool ok = false;
        for(size_t i = 0; i < 3; i++) {
            if(fm504_reader_set_tx_power(impl->uart, tx_power_dbm)) {
                ok = true;
                break;
            }
            furi_delay_ms(90);
        }

        if(!was_enabled) {
            rfid_driver_set_enabled(driver, false);
        }
        return ok;
    }
    case RfidModuleRe40:
        return false;
    default:
        return false;
    }
}

bool rfid_driver_scan_once(RfidDriver* driver, RfidTagRead* out) {
    if(!driver || !driver->impl || !out) return false;

    switch(driver->module) {
    case RfidModuleFm504: {
        DriverFm504* impl = driver->impl;
        if(!impl->enabled) return false;
        Fm504InventoryResult tmp = {0};
        if(!fm504_reader_inventory_once(impl->uart, impl->mode, &tmp)) return false;

        strncpy(out->primary_hex, tmp.epc_hex, sizeof(out->primary_hex) - 1);
        out->primary_hex[sizeof(out->primary_hex) - 1] = '\0';
        strncpy(out->raw_hex, tmp.raw_hex, sizeof(out->raw_hex) - 1);
        out->raw_hex[sizeof(out->raw_hex) - 1] = '\0';
        out->rssi = tmp.rssi;
        return true;
    }
    case RfidModuleRe40:
        return false;
    default:
        return false;
    }
}

bool rfid_driver_write_epc_ex(
    RfidDriver* driver,
    const char* epc_hex,
    char* detail,
    size_t detail_cap) {
    if(!driver || !driver->impl || !epc_hex) return false;
    if(detail && detail_cap > 0) detail[0] = '\0';

    switch(driver->module) {
    case RfidModuleFm504: {
        DriverFm504* impl = driver->impl;
        bool was_enabled = impl->enabled;
        if(!was_enabled) {
            if(!rfid_driver_set_enabled(driver, true)) return false;
        }
        bool ok = fm504_reader_write_epc_ex(impl->uart, epc_hex, detail, detail_cap);
        if(!was_enabled) {
            rfid_driver_set_enabled(driver, false);
        }
        return ok;
    }
    case RfidModuleRe40:
        return false;
    default:
        return false;
    }
}

bool rfid_driver_write_epc(RfidDriver* driver, const char* epc_hex) {
    return rfid_driver_write_epc_ex(driver, epc_hex, NULL, 0);
}

bool rfid_driver_access_pwd(
    RfidDriver* driver,
    const char* access_pwd_hex,
    char* detail,
    size_t detail_cap) {
    if(!driver || !driver->impl || !access_pwd_hex) return false;
    if(detail && detail_cap > 0) detail[0] = '\0';

    switch(driver->module) {
    case RfidModuleFm504: {
        DriverFm504* impl = driver->impl;
        bool was_enabled = impl->enabled;
        if(!was_enabled) {
            if(!rfid_driver_set_enabled(driver, true)) return false;
        }
        bool ok = fm504_reader_access_pwd(impl->uart, access_pwd_hex, detail, detail_cap);
        if(!was_enabled) rfid_driver_set_enabled(driver, false);
        return ok;
    }
    case RfidModuleRe40:
        if(detail && detail_cap > 0) snprintf(detail, detail_cap, "RE40 access not implemented");
        return false;
    default:
        return false;
    }
}
