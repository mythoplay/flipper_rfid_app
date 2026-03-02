#include "rfid_driver.h"
#include "storage_tags.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define TAG "FLIPPER_RFID"
#define MAX_TAGS 64
#define MAX_SCAN_LINES 4

typedef enum {
    ViewIdMainMenu = 1,
    ViewIdConfig,
    ViewIdScan,
    ViewIdWriteSelect,
    ViewIdWriteInput,
    ViewIdAccessPassword,
    ViewIdWriteConfirm,
    ViewIdWriteResult,
    ViewIdCloneSource,
    ViewIdCloneTarget,
    ViewIdCloneResult,
    ViewIdProtection,
    ViewIdModule,
    ViewIdRegion,
    ViewIdReadMode,
    ViewIdTxPower,
    ViewIdReadRate,
    ViewIdSavedTags,
    ViewIdSavedTagDetail,
    ViewIdAbout,
} ViewId;

typedef enum {
    MainActionScan = 100,
    MainActionWriteTag,
    MainActionWriteUser,
    MainActionSavedTags,
    MainActionAccessPassword,
    MainActionClone,
    MainActionCheckProtection,
    MainActionConfig,
    MainActionModule,
    MainActionRegion,
    MainActionReadMode,
    MainActionTxPower,
    MainActionReadRate,
    MainActionAbout,
} MainAction;

typedef enum {
    ModuleActionFm504 = 150,
    ModuleActionFm505,
    ModuleActionFm507,
    ModuleActionFm509,
    ModuleActionFm505A,
    ModuleActionRe40,
} ModuleAction;

typedef enum {
    RegionActionEu = 180,
    RegionActionUs,
} RegionAction;

typedef enum {
    ModeActionEpc = 200,
    ModeActionTid,
    ModeActionUser,
    ModeActionAll,
} ModeAction;

typedef enum {
    RateAction10 = 300,
    RateAction25,
    RateAction50,
    RateAction100,
    RateAction200,
    RateAction300,
    RateAction400,
    RateAction500,
    RateAction750,
} RateAction;

typedef enum {
    EventScanTick = 1000,
    EventScanToggle,
    EventScanSave,
    EventScanClear,
    EventScanTagPrev,
    EventScanTagNext,
    EventWriteCommit,
    EventWriteUserCommit,
    EventAccessPwdCommit,
    EventWriteDo,
    EventWriteToggleAccess,
    EventWriteConfirmBack,
    EventWriteRetry,
    EventWriteBack,
    EventCloneSourceRead,
    EventCloneSourceRetry,
    EventCloneTargetWrite,
    EventCloneTargetRetry,
    EventCloneToggleAccess,
    EventCloneBackToSource,
    EventCloneResultBack,
    EventProtectionRun,
    EventProtectionDo,
    EventProtectionBack,
    EventWriteSelectBase = 2000,
    EventTxPowerBase = 3000,
    EventSavedSelectBase = 4000,
    EventSavedTagBack = 5000,
    EventSavedTagDelete,
    EventSavedTagClearAll,
} CustomEvent;

typedef struct {
    char epc[96];
    char tid[96];
    char user[96];
} ScanAllEntry;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    Submenu* config_menu;
    Widget* scan_widget;
    Submenu* write_select_menu;
    Submenu* saved_tags_menu;
    TextInput* write_input;
    TextInput* access_input;
    Widget* write_confirm_widget;
    Widget* write_result_widget;
    Widget* saved_tag_widget;
    Widget* clone_source_widget;
    Widget* clone_target_widget;
    Widget* clone_result_widget;
    Widget* protection_widget;
    Submenu* module_menu;
    Submenu* region_menu;
    Submenu* read_mode_menu;
    Submenu* tx_power_menu;
    Submenu* read_rate_menu;
    Widget* about_widget;
    FuriTimer* scan_timer;
    RfidDriver* driver;
    NotificationApp* notification;

    bool closing;
    bool scan_running;
    bool scan_tick_pending;
    RfidModuleType current_module;
    RfidRegion region;
    RfidScanMode scan_mode;
    uint8_t scan_all_step;
    int8_t tx_power_db;
    uint16_t read_rate_ms;

    ViewId current_view;

    RfidTagRead tags[MAX_TAGS];
    size_t tags_count;
    size_t selected_tag_idx;

    char status_msg[64];
    char scan_mode_line[40];
    char scan_last_line[96];
    char scan_last_epc[96];
    char scan_last_tid[96];
    char scan_last_user[96];
    char scan_preview_line[96];
    uint8_t scan_view_tab;
    ScanAllEntry scan_all_entries[MAX_TAGS];
    size_t scan_all_count;
    size_t scan_all_selected;
    char write_buffer[40];
    char write_user_buffer[65];
    char access_password[9];
    char access_input_buffer[9];
    bool use_access_password;
    bool write_user_mode;
    char last_write_epc[33];
    char last_write_user[65];
    char last_write_detail[64];
    bool last_write_ok;
    bool last_write_is_user;
    char clone_source_epc[33];
    char clone_info_line[96];
    char clone_result_detail[64];
    bool clone_result_ok;
    char protection_epc[33];
    char protection_status[24];
    char protection_detail[64];
    SavedUhfTag saved_tags[MAX_TAGS];
    size_t saved_tags_count;
    size_t saved_selected_idx;
    char saved_tag_text[320];
} FlipperRfidApp;

static void flipper_rfid_app_free(FlipperRfidApp* app);
static bool flipper_rfid_add_or_update_tag(FlipperRfidApp* app, const RfidTagRead* tag);
static void flipper_rfid_persist_settings(FlipperRfidApp* app);
static void flipper_rfid_autosave_scan_captures(FlipperRfidApp* app);

static const char* flipper_rfid_scan_mode_name(RfidScanMode mode) {
    switch(mode) {
    case RfidScanModeTid:
        return "TID";
    case RfidScanModeUser:
        return "USER";
    case RfidScanModeAll:
        return "ALL";
    case RfidScanModeEpc:
    default:
        return "EPC";
    }
}

static const char* flipper_rfid_module_name(RfidModuleType module) {
    switch(module) {
    case RfidModuleFm504:
        return "Fonkan FM504";
    case RfidModuleFm505:
        return "Fonkan FM505";
    case RfidModuleFm507:
        return "Fonkan FM507";
    case RfidModuleFm509:
        return "Fonkan FM509";
    case RfidModuleFm505A:
        return "Fonkan FM505A";
    case RfidModuleRe40:
    default:
        return "Zebra RE40";
    }
}

static const char* flipper_rfid_region_short_name(RfidRegion region) {
    return (region == RfidRegionUs) ? "US" : "EU";
}

static void flipper_rfid_scan_all_reset(FlipperRfidApp* app) {
    if(!app) return;
    app->scan_all_count = 0;
    app->scan_all_selected = 0;
    memset(app->scan_all_entries, 0, sizeof(app->scan_all_entries));
}

static size_t flipper_rfid_scan_all_find_by_epc(FlipperRfidApp* app, const char* epc) {
    if(!app || !epc || epc[0] == '\0') return SIZE_MAX;
    for(size_t i = 0; i < app->scan_all_count; i++) {
        if(strcmp(app->scan_all_entries[i].epc, epc) == 0) return i;
    }
    return SIZE_MAX;
}

static void flipper_rfid_scan_all_record(FlipperRfidApp* app, const char* bank, const char* value) {
    if(!app || !bank || !value || value[0] == '\0') return;
    if(app->scan_all_selected >= MAX_TAGS) app->scan_all_selected = 0;

    if(strcmp(bank, "EPC") == 0) {
        size_t idx = flipper_rfid_scan_all_find_by_epc(app, value);
        if(idx == SIZE_MAX) {
            if(app->scan_all_count >= MAX_TAGS) return;
            idx = app->scan_all_count++;
        }
        snprintf(app->scan_all_entries[idx].epc, sizeof(app->scan_all_entries[idx].epc), "%s", value);
        app->scan_all_selected = idx;
        return;
    }

    /* Do not create phantom entries from TID/USER without a known EPC anchor. */
    if(app->scan_all_count == 0) return;
    if(app->scan_all_selected >= app->scan_all_count) app->scan_all_selected = app->scan_all_count - 1;

    ScanAllEntry* e = &app->scan_all_entries[app->scan_all_selected];
    if(strcmp(bank, "TID") == 0) {
        snprintf(e->tid, sizeof(e->tid), "%s", value);
    } else if(strcmp(bank, "USER") == 0) {
        snprintf(e->user, sizeof(e->user), "%s", value);
    }
}

static void flipper_rfid_scan_all_build_preview(const FlipperRfidApp* app, char* out, size_t out_cap) {
    if(!app || !out || out_cap < 2) return;
    if(app->scan_all_count == 0 || app->scan_all_selected >= app->scan_all_count) {
        snprintf(out, out_cap, "E:No EPC\nT:No TID\nU:No USER");
        return;
    }
    const ScanAllEntry* e = &app->scan_all_entries[app->scan_all_selected];
    snprintf(
        out,
        out_cap,
        "E:%s\nT:%s\nU:%s",
        (e->epc[0] != '\0') ? e->epc : "No EPC",
        (e->tid[0] != '\0') ? e->tid : "No TID",
        (e->user[0] != '\0') ? e->user : "No USER");
}

static void flipper_rfid_autosave_scan_captures(FlipperRfidApp* app) {
    if(!app) return;

    if(app->scan_mode == RfidScanModeAll) {
        size_t n = (app->scan_all_count < MAX_TAGS) ? app->scan_all_count : MAX_TAGS;
        for(size_t i = 0; i < n; i++) {
            snprintf(app->saved_tags[i].epc, sizeof(app->saved_tags[i].epc), "%s", app->scan_all_entries[i].epc);
            snprintf(app->saved_tags[i].tid, sizeof(app->saved_tags[i].tid), "%s", app->scan_all_entries[i].tid);
            snprintf(app->saved_tags[i].user, sizeof(app->saved_tags[i].user), "%s", app->scan_all_entries[i].user);
        }
        if(n > 0) (void)storage_saved_tags_save(app->saved_tags, n);
    } else {
        size_t n = (app->tags_count < MAX_TAGS) ? app->tags_count : MAX_TAGS;
        for(size_t i = 0; i < n; i++) {
            snprintf(app->saved_tags[i].epc, sizeof(app->saved_tags[i].epc), "%s", app->tags[i].primary_hex);
            app->saved_tags[i].tid[0] = '\0';
            app->saved_tags[i].user[0] = '\0';
        }
        if(n > 0) (void)storage_saved_tags_save(app->saved_tags, n);
    }
}

static RfidScanMode flipper_rfid_effective_driver_mode(RfidScanMode mode, uint8_t all_step) {
    if(mode == RfidScanModeTid) return RfidScanModeTid;
    if(mode == RfidScanModeUser) return RfidScanModeUser;
    if(mode == RfidScanModeAll) {
        if((all_step % 3) == 1) return RfidScanModeTid;
        if((all_step % 3) == 2) return RfidScanModeUser;
        return RfidScanModeEpc;
    }
    return RfidScanModeEpc;
}

static void flipper_rfid_apply_scan_mode_to_driver(FlipperRfidApp* app) {
    if(!app || !app->driver) return;
    rfid_driver_set_mode(app->driver, flipper_rfid_effective_driver_mode(app->scan_mode, app->scan_all_step));
}

static void flipper_rfid_persist_settings(FlipperRfidApp* app) {
    if(!app) return;
    RfidAppSettings settings = {
        .module = app->current_module,
        .region = app->region,
        .scan_mode = app->scan_mode,
        .tx_power_db = app->tx_power_db,
        .read_rate_ms = app->read_rate_ms,
        .use_access_password = app->use_access_password,
    };
    strncpy(settings.access_password, app->access_password, sizeof(settings.access_password) - 1);
    settings.access_password[sizeof(settings.access_password) - 1] = '\0';
    (void)storage_settings_save(&settings);
}

static void flipper_rfid_switch_view(FlipperRfidApp* app, ViewId view_id) {
    if(!app || app->closing || !app->view_dispatcher) return;
    app->current_view = view_id;
    view_dispatcher_switch_to_view(app->view_dispatcher, view_id);
}

static void flipper_rfid_scan_button_callback(GuiButtonType button, InputType type, void* context) {
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;

    uint32_t event = EventScanToggle;

    if(type == InputTypeLong) {
        if(button == GuiButtonTypeLeft) event = EventScanSave;
        else if(button == GuiButtonTypeRight) event = EventScanClear;
        else return;
    } else if(type == InputTypeShort) {
        if(button == GuiButtonTypeLeft) {
            event = (app->scan_mode == RfidScanModeAll) ? EventScanTagPrev : EventScanSave;
        } else if(button == GuiButtonTypeRight) {
            event = (app->scan_mode == RfidScanModeAll) ? EventScanTagNext : EventScanClear;
        }
    } else {
        return;
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void flipper_rfid_write_result_button_callback(GuiButtonType button, InputType type, void* context) {
    if(type != InputTypeShort) return;
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;

    uint32_t event = EventWriteBack;
    if(button == GuiButtonTypeCenter) event = EventWriteRetry;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void flipper_rfid_write_confirm_button_callback(GuiButtonType button, InputType type, void* context) {
    if(type != InputTypeShort) return;
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;

    uint32_t event = EventWriteDo;
    if(button == GuiButtonTypeLeft) event = EventWriteConfirmBack;
    else if(button == GuiButtonTypeRight) event = EventWriteToggleAccess;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static bool flipper_rfid_attempt_write_epc(FlipperRfidApp* app, const char* epc_hex, char* detail, size_t detail_cap) {
    if(!app || !app->driver || !epc_hex) return false;
    if(detail && detail_cap) detail[0] = '\0';

    (void)rfid_driver_set_enabled(app->driver, true);
    furi_delay_ms(120);
    (void)rfid_driver_set_tx_power(app->driver, app->tx_power_db);

    bool access_failed = false;
    char access_note[64] = {0};
    if(app->use_access_password) {
        char access_detail[64];
        if(!rfid_driver_access_pwd(
               app->driver, app->access_password, access_detail, sizeof(access_detail))) {
            access_failed = true;
            snprintf(
                access_note,
                sizeof(access_note),
                "Access failed -> fallback (%.34s)",
                access_detail[0] ? access_detail : "no detail");
        }
    }

    bool ok = false;
    char local_detail[64];
    for(size_t i = 0; i < 3; i++) {
        if(rfid_driver_write_epc_ex(app->driver, epc_hex, local_detail, sizeof(local_detail))) {
            ok = true;
            if(detail && detail_cap) {
                if(access_failed) {
                    snprintf(detail, detail_cap, "%.34s; %.26s", access_note, local_detail);
                } else {
                    strncpy(detail, local_detail, detail_cap - 1);
                    detail[detail_cap - 1] = '\0';
                }
            }
            break;
        }
        if(detail && detail_cap) {
            if(access_failed) {
                snprintf(detail, detail_cap, "%.26s; write: %.24s", access_note, local_detail);
            } else {
                strncpy(detail, local_detail, detail_cap - 1);
                detail[detail_cap - 1] = '\0';
            }
        }
        furi_delay_ms(90);
    }

    rfid_driver_set_enabled(app->driver, false);
    return ok;
}

static bool flipper_rfid_is_even_hex_max64(const char* s) {
    if(!s) return false;
    size_t n = strlen(s);
    if(n == 0 || n > 64 || (n % 2) != 0) return false;
    for(size_t i = 0; i < n; i++) {
        char c = s[i];
        bool hex = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if(!hex) return false;
    }
    return true;
}

static void flipper_rfid_uppercase_hex(char* s) {
    if(!s) return;
    for(size_t i = 0; s[i] != '\0'; i++) {
        if(s[i] >= 'a' && s[i] <= 'f') s[i] = (char)(s[i] - 'a' + 'A');
    }
}

static bool flipper_rfid_attempt_write_user(
    FlipperRfidApp* app,
    const char* user_hex,
    uint8_t addr_words,
    char* detail,
    size_t detail_cap) {
    if(!app || !app->driver || !user_hex) return false;
    if(detail && detail_cap) detail[0] = '\0';

    (void)rfid_driver_set_enabled(app->driver, true);
    furi_delay_ms(120);
    (void)rfid_driver_set_tx_power(app->driver, app->tx_power_db);

    bool access_failed = false;
    char access_note[64] = {0};
    if(app->use_access_password) {
        char access_detail[64];
        if(!rfid_driver_access_pwd(
               app->driver, app->access_password, access_detail, sizeof(access_detail))) {
            access_failed = true;
            snprintf(
                access_note,
                sizeof(access_note),
                "Access failed -> fallback (%.34s)",
                access_detail[0] ? access_detail : "no detail");
        }
    }

    bool ok = false;
    char local_detail[64];
    for(size_t i = 0; i < 3; i++) {
        if(rfid_driver_write_user_ex(
               app->driver, addr_words, user_hex, local_detail, sizeof(local_detail))) {
            ok = true;
            if(detail && detail_cap) {
                if(access_failed) {
                    snprintf(detail, detail_cap, "%.34s; %.26s", access_note, local_detail);
                } else {
                    strncpy(detail, local_detail, detail_cap - 1);
                    detail[detail_cap - 1] = '\0';
                }
            }
            break;
        }
        if(detail && detail_cap) {
            if(access_failed) {
                snprintf(detail, detail_cap, "%.26s; write: %.24s", access_note, local_detail);
            } else {
                strncpy(detail, local_detail, detail_cap - 1);
                detail[detail_cap - 1] = '\0';
            }
        }
        furi_delay_ms(90);
    }

    rfid_driver_set_enabled(app->driver, false);
    return ok;
}

static void flipper_rfid_saved_tags_load(FlipperRfidApp* app) {
    if(!app) return;
    app->saved_tags_count = storage_saved_tags_load(app->saved_tags, MAX_TAGS);
    if(app->saved_selected_idx >= app->saved_tags_count) app->saved_selected_idx = 0;
}

static void flipper_rfid_draw_lock_icon(Widget* widget, bool locked) {
    if(!widget) return;

    const uint8_t x = 116;
    const uint8_t y = 2;

    /* Body of lock */
    widget_add_rect_element(widget, x, y + 6, 9, 6, 1, false);

    if(locked) {
        /* Closed shackle */
        widget_add_line_element(widget, x + 2, y + 6, x + 2, y + 2);
        widget_add_line_element(widget, x + 7, y + 6, x + 7, y + 2);
        widget_add_line_element(widget, x + 2, y + 2, x + 7, y + 2);
    } else {
        /* Open shackle */
        widget_add_line_element(widget, x + 7, y + 6, x + 7, y + 2);
        widget_add_line_element(widget, x + 3, y + 2, x + 7, y + 2);
        widget_add_line_element(widget, x + 1, y + 6, x + 1, y + 4);
        widget_add_line_element(widget, x + 1, y + 4, x + 3, y + 4);
    }
}

static void flipper_rfid_refresh_write_confirm_view(FlipperRfidApp* app) {
    if(!app || !app->write_confirm_widget) return;
    widget_reset(app->write_confirm_widget);

    flipper_rfid_draw_lock_icon(app->write_confirm_widget, app->use_access_password);
    widget_add_string_element(
        app->write_confirm_widget, 2, 2, AlignLeft, AlignTop, FontPrimary, "Write Confirm");
    widget_add_string_element(app->write_confirm_widget, 2, 16, AlignLeft, AlignTop, FontSecondary, "EPC:");
    widget_add_text_box_element(
        app->write_confirm_widget, 2, 24, 124, 22, AlignLeft, AlignTop, app->last_write_epc, true);
    widget_add_button_element(
        app->write_confirm_widget, GuiButtonTypeLeft, "Back", flipper_rfid_write_confirm_button_callback, app);
    widget_add_button_element(
        app->write_confirm_widget, GuiButtonTypeCenter, "Write", flipper_rfid_write_confirm_button_callback, app);
    widget_add_button_element(
        app->write_confirm_widget, GuiButtonTypeRight, "Lock", flipper_rfid_write_confirm_button_callback, app);
}

static void flipper_rfid_vibro_read(FlipperRfidApp* app) {
    if(!app || !app->notification) return;
    notification_message(app->notification, &sequence_single_vibro);
}

static void flipper_rfid_vibro_write_ok(FlipperRfidApp* app) {
    if(!app || !app->notification) return;
    notification_message(app->notification, &sequence_double_vibro);
}

static void flipper_rfid_draw_result_widget(
    Widget* widget,
    const char* title,
    bool ok,
    const char* epc_line,
    const char* detail_line,
    const char* left_label,
    const char* center_label,
    const char* right_label,
    ButtonCallback callback,
    void* ctx) {
    if(!widget) return;
    widget_reset(widget);

    widget_add_string_element(widget, 2, 2, AlignLeft, AlignTop, FontPrimary, title);
    widget_add_string_element(
        widget, 2, 16, AlignLeft, AlignTop, FontSecondary, ok ? "SUCCESS" : "FAILED");
    widget_add_text_box_element(widget, 2, 26, 124, 14, AlignLeft, AlignTop, epc_line, true);
    widget_add_text_box_element(widget, 2, 40, 124, 16, AlignLeft, AlignTop, detail_line, true);
    if(left_label) widget_add_button_element(widget, GuiButtonTypeLeft, left_label, callback, ctx);
    if(center_label) widget_add_button_element(widget, GuiButtonTypeCenter, center_label, callback, ctx);
    if(right_label) widget_add_button_element(widget, GuiButtonTypeRight, right_label, callback, ctx);
}

static void flipper_rfid_refresh_write_result_view(FlipperRfidApp* app) {
    if(!app || !app->write_result_widget) return;
    flipper_rfid_draw_result_widget(
        app->write_result_widget,
        "Write Result",
        app->last_write_ok,
        app->last_write_epc,
        app->last_write_detail,
        "Back",
        "Retry",
        "Menu",
        flipper_rfid_write_result_button_callback,
        app);
}

static void flipper_rfid_clone_source_button_callback(GuiButtonType button, InputType type, void* context) {
    if(type != InputTypeShort) return;
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;

    uint32_t event = EventCloneSourceRead;
    if(button == GuiButtonTypeLeft) event = EventCloneSourceRetry;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void flipper_rfid_clone_target_button_callback(GuiButtonType button, InputType type, void* context) {
    if(type != InputTypeShort) return;
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;

    uint32_t event = EventCloneTargetWrite;
    if(button == GuiButtonTypeLeft) event = EventCloneBackToSource;
    else if(button == GuiButtonTypeRight) event = EventCloneToggleAccess;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void flipper_rfid_clone_result_button_callback(GuiButtonType button, InputType type, void* context) {
    if(type != InputTypeShort) return;
    UNUSED(button);
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;

    uint32_t event = EventCloneTargetRetry;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void flipper_rfid_protection_button_callback(GuiButtonType button, InputType type, void* context) {
    if(type != InputTypeShort) return;
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;

    uint32_t event = EventProtectionRun;
    if(button == GuiButtonTypeLeft) event = EventProtectionBack;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void flipper_rfid_saved_tag_button_callback(GuiButtonType button, InputType type, void* context) {
    if(type != InputTypeShort) return;
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;

    uint32_t event = EventSavedTagBack;
    if(button == GuiButtonTypeLeft) event = EventSavedTagDelete;
    else if(button == GuiButtonTypeRight) event = EventSavedTagClearAll;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static bool flipper_rfid_clone_capture_source(FlipperRfidApp* app) {
    if(!app || !app->driver) return false;

    if(!rfid_driver_set_enabled(app->driver, true)) return false;
    RfidScanMode previous_mode = app->scan_mode;
    uint8_t prev_all_step = app->scan_all_step;
    app->scan_mode = RfidScanModeEpc;
    app->scan_all_step = 0;
    flipper_rfid_apply_scan_mode_to_driver(app);

    /* Warm up and retry reads to avoid missing the source tag in a single-shot call. */
    furi_delay_ms(120);
    (void)rfid_driver_set_tx_power(app->driver, app->tx_power_db);

    RfidTagRead tag = {0};
    bool ok = false;
    for(size_t i = 0; i < 12; i++) {
        if(rfid_driver_scan_once(app->driver, &tag) && tag.primary_hex[0] != '\0') {
            ok = true;
            break;
        }
        furi_delay_ms(60);
    }

    app->scan_mode = previous_mode;
    app->scan_all_step = prev_all_step;
    flipper_rfid_apply_scan_mode_to_driver(app);
    rfid_driver_set_enabled(app->driver, false);
    if(!ok) return false;

    strncpy(app->clone_source_epc, tag.primary_hex, sizeof(app->clone_source_epc) - 1);
    app->clone_source_epc[sizeof(app->clone_source_epc) - 1] = '\0';
    (void)flipper_rfid_add_or_update_tag(app, &tag);
    flipper_rfid_vibro_read(app);
    return true;
}

static bool flipper_rfid_capture_single_epc_with_params(
    FlipperRfidApp* app,
    char* out_epc,
    size_t out_cap,
    size_t tries,
    uint32_t warmup_ms,
    uint32_t step_ms) {
    if(!app || !app->driver || !out_epc || out_cap < 2) return false;

    RfidScanMode prev_mode = app->scan_mode;
    uint8_t prev_all_step = app->scan_all_step;
    app->scan_mode = RfidScanModeEpc;
    app->scan_all_step = 0;
    flipper_rfid_apply_scan_mode_to_driver(app);

    if(!rfid_driver_set_enabled(app->driver, true)) {
        app->scan_mode = prev_mode;
        app->scan_all_step = prev_all_step;
        flipper_rfid_apply_scan_mode_to_driver(app);
        return false;
    }

    furi_delay_ms(warmup_ms);
    (void)rfid_driver_set_tx_power(app->driver, app->tx_power_db);

    bool ok = false;
    RfidTagRead tag = {0};
    for(size_t i = 0; i < tries; i++) {
        if(rfid_driver_scan_once(app->driver, &tag) && tag.primary_hex[0] != '\0') {
            strncpy(out_epc, tag.primary_hex, out_cap - 1);
            out_epc[out_cap - 1] = '\0';
            (void)flipper_rfid_add_or_update_tag(app, &tag);
            flipper_rfid_vibro_read(app);
            ok = true;
            break;
        }
        furi_delay_ms(step_ms);
    }

    rfid_driver_set_enabled(app->driver, false);
    app->scan_mode = prev_mode;
    app->scan_all_step = prev_all_step;
    flipper_rfid_apply_scan_mode_to_driver(app);
    return ok;
}

static void flipper_rfid_run_protection_check(FlipperRfidApp* app) {
    if(!app) return;

    app->protection_epc[0] = '\0';
    if(!flipper_rfid_capture_single_epc_with_params(app, app->protection_epc, sizeof(app->protection_epc), 30, 180, 80)) {
        snprintf(app->protection_status, sizeof(app->protection_status), "NO TAG");
        snprintf(app->protection_detail, sizeof(app->protection_detail), "No EPC detected");
        return;
    }

    char write_detail[64];
    bool write_ok = flipper_rfid_attempt_write_epc(app, app->protection_epc, write_detail, sizeof(write_detail));
    if(write_ok) {
        snprintf(app->protection_status, sizeof(app->protection_status), "NOT PROTECTED");
        snprintf(
            app->protection_detail,
            sizeof(app->protection_detail),
            "Write same EPC: %.47s",
            write_detail);
        return;
    }

    if(strstr(write_detail, "RAW 4") || strstr(write_detail, "Locked")) {
        snprintf(app->protection_status, sizeof(app->protection_status), "PROTECTED");
        snprintf(app->protection_detail, sizeof(app->protection_detail), "%s", write_detail);
    } else if(strstr(write_detail, "No tag")) {
        snprintf(app->protection_status, sizeof(app->protection_status), "NO TAG");
        snprintf(app->protection_detail, sizeof(app->protection_detail), "%s", write_detail);
    } else {
        snprintf(app->protection_status, sizeof(app->protection_status), "UNKNOWN");
        snprintf(app->protection_detail, sizeof(app->protection_detail), "%s", write_detail);
    }
}

static void flipper_rfid_refresh_clone_source_view(FlipperRfidApp* app) {
    if(!app || !app->clone_source_widget) return;

    widget_reset(app->clone_source_widget);

    if(app->clone_source_epc[0] == '\0') {
        strncpy(app->clone_info_line, "No source captured", sizeof(app->clone_info_line) - 1);
        app->clone_info_line[sizeof(app->clone_info_line) - 1] = '\0';
    } else {
        snprintf(app->clone_info_line, sizeof(app->clone_info_line), "%s", app->clone_source_epc);
    }

    widget_add_string_element(
        app->clone_source_widget, 2, 2, AlignLeft, AlignTop, FontPrimary, "Clone > Source");
    widget_add_text_box_element(
        app->clone_source_widget,
        2,
        16,
        124,
        30,
        AlignLeft,
        AlignTop,
        "Place source tag, then press Read",
        true);
    widget_add_text_box_element(
        app->clone_source_widget, 2, 34, 124, 20, AlignLeft, AlignTop, app->clone_info_line, true);
    widget_add_button_element(
        app->clone_source_widget, GuiButtonTypeLeft, "Retry", flipper_rfid_clone_source_button_callback, app);
    widget_add_button_element(
        app->clone_source_widget, GuiButtonTypeCenter, "Read", flipper_rfid_clone_source_button_callback, app);
}

static void flipper_rfid_refresh_clone_target_view(FlipperRfidApp* app) {
    if(!app || !app->clone_target_widget) return;
    widget_reset(app->clone_target_widget);

    widget_add_string_element(
        app->clone_target_widget, 2, 2, AlignLeft, AlignTop, FontPrimary, "Clone > Target");
    widget_add_string_element(
        app->clone_target_widget,
        2,
        14,
        AlignLeft,
        AlignTop,
        FontSecondary,
        app->use_access_password ? "Lock: ON" : "Lock: OFF");
    widget_add_string_element(app->clone_target_widget, 2, 24, AlignLeft, AlignTop, FontSecondary, "Source EPC:");
    widget_add_text_box_element(
        app->clone_target_widget,
        2,
        34,
        124,
        14,
        AlignLeft,
        AlignTop,
        app->clone_source_epc,
        true);
    widget_add_button_element(
        app->clone_target_widget, GuiButtonTypeLeft, "Retry", flipper_rfid_clone_target_button_callback, app);
    widget_add_button_element(
        app->clone_target_widget, GuiButtonTypeCenter, "Write", flipper_rfid_clone_target_button_callback, app);
    widget_add_button_element(
        app->clone_target_widget, GuiButtonTypeRight, "Lock", flipper_rfid_clone_target_button_callback, app);
}

static void flipper_rfid_refresh_clone_result_view(FlipperRfidApp* app) {
    if(!app || !app->clone_result_widget) return;
    flipper_rfid_draw_result_widget(
        app->clone_result_widget,
        "Clone Result",
        app->clone_result_ok,
        app->clone_source_epc,
        app->clone_result_detail,
        NULL,
        "Retry",
        NULL,
        flipper_rfid_clone_result_button_callback,
        app);
}

static bool flipper_rfid_is_checking_status(const char* s) {
    if(!s) return false;
    return strncmp(s, "CHECKING", 8) == 0;
}

static void flipper_rfid_add_detecting_art(Widget* w) {
    if(!w) return;
    /* Head contour (closer to Flipper dolphin profile) */
    widget_add_line_element(w, 4, 34, 10, 26);
    widget_add_line_element(w, 10, 26, 18, 20);
    widget_add_line_element(w, 18, 20, 30, 18);
    widget_add_line_element(w, 30, 18, 44, 22);
    widget_add_line_element(w, 44, 22, 54, 30);
    widget_add_line_element(w, 54, 30, 52, 36);
    widget_add_line_element(w, 52, 36, 44, 40);
    widget_add_line_element(w, 44, 40, 28, 42);
    widget_add_line_element(w, 28, 42, 14, 40);
    widget_add_line_element(w, 14, 40, 4, 34);

    /* Snout / jaw */
    widget_add_line_element(w, 38, 34, 70, 34);
    widget_add_line_element(w, 70, 34, 78, 30);
    widget_add_line_element(w, 78, 30, 74, 26);
    widget_add_line_element(w, 74, 26, 66, 24);
    widget_add_line_element(w, 66, 24, 52, 24);
    widget_add_line_element(w, 52, 24, 42, 28);

    /* Lower arc */
    widget_add_line_element(w, 30, 42, 44, 48);
    widget_add_line_element(w, 44, 48, 60, 49);
    widget_add_line_element(w, 60, 49, 74, 46);

    /* Eye */
    widget_add_circle_element(w, 24, 30, 6, false);
    widget_add_rect_element(w, 23, 29, 2, 2, 1, true);

    /* Top fin / back */
    widget_add_line_element(w, 16, 22, 22, 14);
    widget_add_line_element(w, 22, 14, 34, 14);
    widget_add_line_element(w, 34, 14, 28, 20);

    /* RFID chip + waves */
    widget_add_rect_element(w, 96, 16, 11, 11, 1, false);
    widget_add_line_element(w, 99, 14, 99, 16);
    widget_add_line_element(w, 103, 14, 103, 16);
    widget_add_line_element(w, 107, 14, 107, 16);
    widget_add_line_element(w, 96, 28, 96, 30);
    widget_add_line_element(w, 101, 28, 101, 30);
    widget_add_line_element(w, 106, 28, 106, 30);
    widget_add_line_element(w, 88, 16, 92, 19);
    widget_add_line_element(w, 85, 21, 91, 21);
    widget_add_line_element(w, 88, 26, 92, 23);
}

static void flipper_rfid_refresh_protection_view(FlipperRfidApp* app) {
    if(!app || !app->protection_widget) return;
    widget_reset(app->protection_widget);

    if(flipper_rfid_is_checking_status(app->protection_status)) {
        flipper_rfid_add_detecting_art(app->protection_widget);
        widget_add_string_element(
            app->protection_widget, 62, 34, AlignLeft, AlignTop, FontPrimary, "Detecting");
        widget_add_string_element(
            app->protection_widget, 62, 46, AlignLeft, AlignTop, FontPrimary, "[UHF] RFID");
        widget_add_button_element(
            app->protection_widget, GuiButtonTypeLeft, "Back", flipper_rfid_protection_button_callback, app);
    } else {
        widget_add_string_element(
            app->protection_widget, 2, 2, AlignLeft, AlignTop, FontPrimary, "Check Protection");
        widget_add_string_element(
            app->protection_widget, 2, 16, AlignLeft, AlignTop, FontSecondary, app->protection_status);
        widget_add_text_box_element(
            app->protection_widget, 2, 26, 124, 14, AlignLeft, AlignTop, app->protection_epc, true);
        widget_add_text_box_element(
            app->protection_widget, 2, 42, 124, 14, AlignLeft, AlignTop, app->protection_detail, true);
        widget_add_button_element(
            app->protection_widget, GuiButtonTypeLeft, "Back", flipper_rfid_protection_button_callback, app);
        widget_add_button_element(
            app->protection_widget, GuiButtonTypeCenter, "Check", flipper_rfid_protection_button_callback, app);
    }
}

static void flipper_rfid_build_scan_preview_line(const char* in, char* out, size_t out_cap) {
    if(!in || !out || out_cap < 2) return;

    size_t in_len = strlen(in);
    if(in_len == 0) {
        strncpy(out, "No reads", out_cap - 1);
        out[out_cap - 1] = '\0';
        return;
    }

    size_t j = 0;
    size_t max_chars = in_len;
    if(max_chars > 48) max_chars = 48;
    for(size_t i = 0; i < max_chars && j + 1 < out_cap; i++) {
        out[j++] = in[i];
        if(((i + 1) % 24 == 0) && (i + 1 < max_chars) && j + 1 < out_cap) {
            out[j++] = '\n';
        }
    }
    if(in_len > max_chars && j + 4 < out_cap) {
        out[j++] = '.';
        out[j++] = '.';
        out[j++] = '.';
    }
    out[j] = '\0';
}

static void flipper_rfid_refresh_scan_view(FlipperRfidApp* app) {
    if(!app || !app->scan_widget) return;
    char qty_line[28];
    const char* line_value = app->scan_last_line;
    const char* left_label = "Save";
    const char* right_label = "Clear";

    widget_reset(app->scan_widget);
    if(app->scan_last_line[0] == '\0') snprintf(app->scan_last_line, sizeof(app->scan_last_line), "No reads");
    if(app->scan_last_epc[0] == '\0') snprintf(app->scan_last_epc, sizeof(app->scan_last_epc), "No reads");
    if(app->scan_last_tid[0] == '\0') snprintf(app->scan_last_tid, sizeof(app->scan_last_tid), "No reads");
    if(app->scan_last_user[0] == '\0') snprintf(app->scan_last_user, sizeof(app->scan_last_user), "No reads");

    if(app->scan_mode == RfidScanModeAll) {
        snprintf(
            app->scan_mode_line,
            sizeof(app->scan_mode_line),
            "Scan > ALL %s %ddB %ums",
            flipper_rfid_region_short_name(app->region),
            app->tx_power_db,
            app->read_rate_ms);
        flipper_rfid_scan_all_build_preview(app, app->scan_preview_line, sizeof(app->scan_preview_line));
        line_value = app->scan_preview_line;
        left_label = "Tag-";
        right_label = "Tag+";
        snprintf(
            qty_line,
            sizeof(qty_line),
            "Qty:%u Tag:%u/%u",
            (unsigned)app->scan_all_count,
            (unsigned)((app->scan_all_count == 0) ? 0 : (app->scan_all_selected + 1)),
            (unsigned)app->scan_all_count);
    } else {
        snprintf(
            app->scan_mode_line,
            sizeof(app->scan_mode_line),
            "Scan > %s %s %ddB %ums",
            flipper_rfid_scan_mode_name(app->scan_mode),
            flipper_rfid_region_short_name(app->region),
            app->tx_power_db,
            app->read_rate_ms);
        snprintf(qty_line, sizeof(qty_line), "Qty: %u", (unsigned)app->tags_count);
    }
    if(app->scan_mode != RfidScanModeAll) {
        flipper_rfid_build_scan_preview_line(line_value, app->scan_preview_line, sizeof(app->scan_preview_line));
        widget_add_text_box_element(
            app->scan_widget, 2, 26, 124, 28, AlignLeft, AlignTop, app->scan_preview_line, true);
    } else {
        widget_add_text_scroll_element(app->scan_widget, 2, 26, 124, 28, app->scan_preview_line);
    }

    widget_add_string_element(app->scan_widget, 2, 4, AlignLeft, AlignTop, FontPrimary, app->scan_mode_line);
    widget_add_string_element(app->scan_widget, 2, 16, AlignLeft, AlignTop, FontSecondary, qty_line);

    widget_add_button_element(app->scan_widget, GuiButtonTypeLeft, left_label, flipper_rfid_scan_button_callback, app);
    widget_add_button_element(
        app->scan_widget,
        GuiButtonTypeCenter,
        app->scan_running ? "Stop" : "Start",
        flipper_rfid_scan_button_callback,
        app);
    widget_add_button_element(app->scan_widget, GuiButtonTypeRight, right_label, flipper_rfid_scan_button_callback, app);
}

static void flipper_rfid_refresh_about(FlipperRfidApp* app) {
    widget_reset(app->about_widget);
    char about_text[192];
    snprintf(
        about_text,
        sizeof(about_text),
        "FlippeRFID for Flipper Zero\n"
        "Version: %s\n"
        "Project: EPC/TID R/W\n"
        "Author:\n Fernando Garcia Villarroel",
        APP_VERSION);
    widget_add_text_box_element(
        app->about_widget,
        2,
        2,
        124,
        60,
        AlignLeft,
        AlignTop,
        about_text,
        true);
}

static bool flipper_rfid_reopen_driver(FlipperRfidApp* app, RfidModuleType module) {
    if(!app) return false;

    RfidDriverConfig cfg = {
        .module = module,
        .baudrate = 38400,
        .scan_mode = app->scan_mode,
        .tx_power_db = app->tx_power_db,
        .read_rate_ms = app->read_rate_ms,
    };

    RfidDriver* new_driver = NULL;
    if(!rfid_driver_open(&new_driver, &cfg)) return false;

    if(app->scan_timer) furi_timer_stop(app->scan_timer);
    app->scan_running = false;

    rfid_driver_set_enabled(app->driver, false);
    rfid_driver_close(app->driver);
    app->driver = new_driver;
    app->current_module = module;
    return true;
}

static uint32_t flipper_rfid_prev_to_main_callback(void* context) {
    UNUSED(context);
    return ViewIdMainMenu;
}

static uint32_t flipper_rfid_prev_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void flipper_rfid_submenu_event(void* context, uint32_t index) {
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void flipper_rfid_write_done_callback(void* context) {
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, EventWriteCommit);
}

static void flipper_rfid_write_user_done_callback(void* context) {
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, EventWriteUserCommit);
}

static void flipper_rfid_access_pwd_done_callback(void* context) {
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, EventAccessPwdCommit);
}

static bool flipper_rfid_is_hex8(const char* s) {
    if(!s) return false;
    size_t len = strlen(s);
    if(len != 8) return false;
    for(size_t i = 0; i < len; i++) {
        char c = s[i];
        bool hex = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if(!hex) return false;
    }
    return true;
}

static void flipper_rfid_uppercase_str(char* s) {
    if(!s) return;
    for(size_t i = 0; s[i] != '\0'; i++) {
        if(s[i] >= 'a' && s[i] <= 'f') s[i] = (char)(s[i] - 'a' + 'A');
    }
}

static void flipper_rfid_scan_timer_callback(void* context) {
    FlipperRfidApp* app = context;
    if(!app || app->closing || !app->view_dispatcher || !app->scan_running) return;
    if(app->scan_tick_pending) return;
    app->scan_tick_pending = true;
    view_dispatcher_send_custom_event(app->view_dispatcher, EventScanTick);
}

static bool flipper_rfid_add_or_update_tag(FlipperRfidApp* app, const RfidTagRead* tag) {
    for(size_t i = 0; i < app->tags_count; i++) {
        if(strcmp(app->tags[i].primary_hex, tag->primary_hex) == 0) {
            app->tags[i].rssi = tag->rssi;
            return false;
        }
    }

    if(app->tags_count < MAX_TAGS) {
        app->tags[app->tags_count] = *tag;
        app->tags_count++;
        return true;
    }
    return false;
}

static void flipper_rfid_build_write_select_menu(FlipperRfidApp* app) {
    submenu_reset(app->write_select_menu);
    submenu_set_header(app->write_select_menu, "Select Tag");

    if(app->tags_count == 0) {
        submenu_add_item(app->write_select_menu, "No captures", EventWriteSelectBase, flipper_rfid_submenu_event, app);
        return;
    }

    char label[48];
    size_t max_items = (app->tags_count < MAX_TAGS) ? app->tags_count : MAX_TAGS;
    for(size_t i = 0; i < max_items; i++) {
        snprintf(label, sizeof(label), "%u) %.32s", (unsigned)(i + 1), app->tags[i].primary_hex);
        submenu_add_item(
            app->write_select_menu,
            label,
            EventWriteSelectBase + (uint32_t)i,
            flipper_rfid_submenu_event,
            app);
    }
}

static void flipper_rfid_build_saved_tags_menu(FlipperRfidApp* app) {
    if(!app || !app->saved_tags_menu) return;
    flipper_rfid_saved_tags_load(app);
    submenu_reset(app->saved_tags_menu);
    submenu_set_header(app->saved_tags_menu, "Saved Tags");

    if(app->saved_tags_count == 0) {
        submenu_add_item(app->saved_tags_menu, "No saved tags", EventSavedSelectBase, flipper_rfid_submenu_event, app);
        return;
    }

    char label[56];
    for(size_t i = 0; i < app->saved_tags_count; i++) {
        const char* epc = app->saved_tags[i].epc[0] ? app->saved_tags[i].epc : "NO EPC";
        snprintf(label, sizeof(label), "%u) %.32s", (unsigned)(i + 1), epc);
        submenu_add_item(
            app->saved_tags_menu, label, EventSavedSelectBase + (uint32_t)i, flipper_rfid_submenu_event, app);
    }
    submenu_set_selected_item(app->saved_tags_menu, EventSavedSelectBase + (uint32_t)app->saved_selected_idx);
}

static void flipper_rfid_refresh_saved_tag_view(FlipperRfidApp* app) {
    if(!app || !app->saved_tag_widget) return;
    widget_reset(app->saved_tag_widget);
    widget_add_string_element(app->saved_tag_widget, 2, 2, AlignLeft, AlignTop, FontPrimary, "Saved Tag");

    if(app->saved_tags_count == 0 || app->saved_selected_idx >= app->saved_tags_count) {
        widget_add_text_box_element(
            app->saved_tag_widget, 2, 16, 124, 30, AlignLeft, AlignTop, "No saved tags", true);
    } else {
        const SavedUhfTag* t = &app->saved_tags[app->saved_selected_idx];
        snprintf(
            app->saved_tag_text,
            sizeof(app->saved_tag_text),
            "E:%s\nT:%s\nU:%s",
            t->epc[0] ? t->epc : "NO EPC",
            t->tid[0] ? t->tid : "NO TID",
            t->user[0] ? t->user : "NO USER");
        widget_add_text_scroll_element(app->saved_tag_widget, 2, 16, 124, 34, app->saved_tag_text);
    }

    widget_add_button_element(
        app->saved_tag_widget, GuiButtonTypeLeft, "Delete", flipper_rfid_saved_tag_button_callback, app);
    widget_add_button_element(
        app->saved_tag_widget, GuiButtonTypeCenter, "Back", flipper_rfid_saved_tag_button_callback, app);
    widget_add_button_element(
        app->saved_tag_widget, GuiButtonTypeRight, "Clear", flipper_rfid_saved_tag_button_callback, app);
}

static void flipper_rfid_build_read_mode_menu(FlipperRfidApp* app) {
    submenu_reset(app->read_mode_menu);
    submenu_set_header(app->read_mode_menu, "Read Mode");
    submenu_add_item(app->read_mode_menu, "EPC", ModeActionEpc, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_mode_menu, "TID", ModeActionTid, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_mode_menu, "USER", ModeActionUser, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_mode_menu, "ALL", ModeActionAll, flipper_rfid_submenu_event, app);
    uint32_t selected = ModeActionEpc;
    if(app->scan_mode == RfidScanModeTid) selected = ModeActionTid;
    else if(app->scan_mode == RfidScanModeUser) selected = ModeActionUser;
    else if(app->scan_mode == RfidScanModeAll) selected = ModeActionAll;
    submenu_set_selected_item(app->read_mode_menu, selected);
}

static void flipper_rfid_build_module_menu(FlipperRfidApp* app) {
    submenu_reset(app->module_menu);
    submenu_set_header(app->module_menu, "Module");
    submenu_add_item(app->module_menu, "Fonkan FM504 (1dB)", ModuleActionFm504, flipper_rfid_submenu_event, app);
    submenu_add_item(app->module_menu, "Fonkan FM505 (3dB)", ModuleActionFm505, flipper_rfid_submenu_event, app);
    submenu_add_item(app->module_menu, "Fonkan FM507 (4dB)", ModuleActionFm507, flipper_rfid_submenu_event, app);
    submenu_add_item(app->module_menu, "Fonkan FM509 (5dB)", ModuleActionFm509, flipper_rfid_submenu_event, app);
    submenu_add_item(app->module_menu, "Fonkan FM505A (5.5dB)", ModuleActionFm505A, flipper_rfid_submenu_event, app);
    submenu_add_item(app->module_menu, "RE40 (Zebra)", ModuleActionRe40, flipper_rfid_submenu_event, app);

    uint32_t selected = ModuleActionFm504;
    switch(app->current_module) {
    case RfidModuleFm505:
        selected = ModuleActionFm505;
        break;
    case RfidModuleFm507:
        selected = ModuleActionFm507;
        break;
    case RfidModuleFm509:
        selected = ModuleActionFm509;
        break;
    case RfidModuleFm505A:
        selected = ModuleActionFm505A;
        break;
    case RfidModuleRe40:
        selected = ModuleActionRe40;
        break;
    case RfidModuleFm504:
    default:
        selected = ModuleActionFm504;
        break;
    }
    submenu_set_selected_item(app->module_menu, selected);
}

static void flipper_rfid_build_region_menu(FlipperRfidApp* app) {
    submenu_reset(app->region_menu);
    submenu_set_header(app->region_menu, "Region");
    submenu_add_item(app->region_menu, "EU", RegionActionEu, flipper_rfid_submenu_event, app);
    submenu_add_item(app->region_menu, "US", RegionActionUs, flipper_rfid_submenu_event, app);
    submenu_set_selected_item(app->region_menu, (app->region == RfidRegionUs) ? RegionActionUs : RegionActionEu);
}

static void flipper_rfid_build_tx_power_menu(FlipperRfidApp* app) {
    submenu_reset(app->tx_power_menu);
    submenu_set_header(app->tx_power_menu, "TX Power (dB)");

    char label[16];
    for(int db = -2; db <= 27; db++) {
        snprintf(label, sizeof(label), "%ddB", db);
        submenu_add_item(
            app->tx_power_menu,
            label,
            EventTxPowerBase + (uint32_t)(db + 2),
            flipper_rfid_submenu_event,
            app);
    }
    submenu_set_selected_item(app->tx_power_menu, EventTxPowerBase + (uint32_t)(app->tx_power_db + 2));
}

static void flipper_rfid_build_read_rate_menu(FlipperRfidApp* app) {
    submenu_reset(app->read_rate_menu);
    submenu_set_header(app->read_rate_menu, "Read Rate (ms)");

    submenu_add_item(app->read_rate_menu, "10", RateAction10, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_rate_menu, "25", RateAction25, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_rate_menu, "50", RateAction50, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_rate_menu, "100", RateAction100, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_rate_menu, "200", RateAction200, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_rate_menu, "300", RateAction300, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_rate_menu, "400", RateAction400, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_rate_menu, "500", RateAction500, flipper_rfid_submenu_event, app);
    submenu_add_item(app->read_rate_menu, "750", RateAction750, flipper_rfid_submenu_event, app);

    uint32_t selected = RateAction100;
    if(app->read_rate_ms == 10) selected = RateAction10;
    else if(app->read_rate_ms == 25) selected = RateAction25;
    else if(app->read_rate_ms == 50) selected = RateAction50;
    else if(app->read_rate_ms == 200) selected = RateAction200;
    else if(app->read_rate_ms == 300) selected = RateAction300;
    else if(app->read_rate_ms == 400) selected = RateAction400;
    else if(app->read_rate_ms == 500) selected = RateAction500;
    else if(app->read_rate_ms == 750) selected = RateAction750;
    submenu_set_selected_item(app->read_rate_menu, selected);
}

static void flipper_rfid_build_config_menu(FlipperRfidApp* app) {
    submenu_reset(app->config_menu);
    submenu_set_header(app->config_menu, "Config");
    submenu_add_item(app->config_menu, "Module", MainActionModule, flipper_rfid_submenu_event, app);
    submenu_add_item(app->config_menu, "Region", MainActionRegion, flipper_rfid_submenu_event, app);
    submenu_add_item(app->config_menu, "Read Mode", MainActionReadMode, flipper_rfid_submenu_event, app);
    submenu_add_item(app->config_menu, "TX Power", MainActionTxPower, flipper_rfid_submenu_event, app);
    submenu_add_item(app->config_menu, "Read Rate (ms)", MainActionReadRate, flipper_rfid_submenu_event, app);
    submenu_add_item(
        app->config_menu, "Access Password", MainActionAccessPassword, flipper_rfid_submenu_event, app);
}

static void flipper_rfid_apply_read_rate(FlipperRfidApp* app, uint16_t new_rate) {
    app->read_rate_ms = new_rate;
    if(app->scan_running && app->scan_timer) {
        furi_timer_stop(app->scan_timer);
        furi_timer_start(app->scan_timer, furi_ms_to_ticks(app->read_rate_ms));
    }
}

static bool flipper_rfid_custom_event_callback(void* context, uint32_t event) {
    FlipperRfidApp* app = context;
    if(!app || app->closing) return false;

    if(event >= EventTxPowerBase && event < EventTxPowerBase + 64) {
        int8_t db = (int8_t)(event - EventTxPowerBase) - 2;
        if(db > 27) db = 27;
        if(db < -2) db = -2;
        app->tx_power_db = db;
        if(rfid_driver_set_tx_power(app->driver, db)) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Power set: %ddB", db);
        } else {
            snprintf(app->status_msg, sizeof(app->status_msg), "No ACK power (%ddB)", db);
        }
        flipper_rfid_persist_settings(app);
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;
    }

    if(event >= EventWriteSelectBase && event < EventWriteSelectBase + MAX_TAGS) {
        size_t idx = (size_t)(event - EventWriteSelectBase);
        if(idx >= app->tags_count) {
            snprintf(app->status_msg, sizeof(app->status_msg), "No tags to write");
            flipper_rfid_switch_view(app, ViewIdMainMenu);
            return true;
        }

        app->selected_tag_idx = idx;
        strncpy(app->write_buffer, app->tags[idx].primary_hex, sizeof(app->write_buffer) - 1);
        app->write_buffer[sizeof(app->write_buffer) - 1] = '\0';

        text_input_reset(app->write_input);
        text_input_set_header_text(app->write_input, "Edit EPC");
        text_input_set_result_callback(
            app->write_input,
            flipper_rfid_write_done_callback,
            app,
            app->write_buffer,
            sizeof(app->write_buffer),
            false);
        flipper_rfid_switch_view(app, ViewIdWriteInput);
        return true;
    }

    if(event >= EventSavedSelectBase && event < EventSavedSelectBase + MAX_TAGS) {
        size_t idx = (size_t)(event - EventSavedSelectBase);
        if(idx >= app->saved_tags_count) {
            flipper_rfid_switch_view(app, ViewIdSavedTags);
            return true;
        }
        app->saved_selected_idx = idx;
        flipper_rfid_refresh_saved_tag_view(app);
        flipper_rfid_switch_view(app, ViewIdSavedTagDetail);
        return true;
    }

    switch(event) {
    case MainActionScan:
        flipper_rfid_refresh_scan_view(app);
        flipper_rfid_switch_view(app, ViewIdScan);
        return true;

    case MainActionWriteTag:
        app->write_user_mode = false;
        flipper_rfid_build_write_select_menu(app);
        flipper_rfid_switch_view(app, ViewIdWriteSelect);
        return true;

    case MainActionWriteUser:
        app->write_user_mode = true;
        flipper_rfid_saved_tags_load(app);
        app->write_user_buffer[0] = '\0';
        if(app->saved_tags_count > 0 && app->saved_selected_idx < app->saved_tags_count) {
            strncpy(
                app->write_user_buffer,
                app->saved_tags[app->saved_selected_idx].user,
                sizeof(app->write_user_buffer) - 1);
            app->write_user_buffer[sizeof(app->write_user_buffer) - 1] = '\0';
        }
        if(app->write_user_buffer[0] == '\0') snprintf(app->write_user_buffer, sizeof(app->write_user_buffer), "0000");
        text_input_reset(app->write_input);
        text_input_set_header_text(app->write_input, "Edit USER HEX");
        text_input_set_result_callback(
            app->write_input,
            flipper_rfid_write_user_done_callback,
            app,
            app->write_user_buffer,
            sizeof(app->write_user_buffer),
            false);
        flipper_rfid_switch_view(app, ViewIdWriteInput);
        return true;

    case MainActionSavedTags:
        flipper_rfid_build_saved_tags_menu(app);
        flipper_rfid_switch_view(app, ViewIdSavedTags);
        return true;

    case MainActionAccessPassword:
        strncpy(app->access_input_buffer, app->access_password, sizeof(app->access_input_buffer) - 1);
        app->access_input_buffer[sizeof(app->access_input_buffer) - 1] = '\0';
        text_input_reset(app->access_input);
        text_input_set_header_text(app->access_input, "Access Password (8 HEX)");
        text_input_set_result_callback(
            app->access_input,
            flipper_rfid_access_pwd_done_callback,
            app,
            app->access_input_buffer,
            sizeof(app->access_input_buffer),
            false);
        flipper_rfid_switch_view(app, ViewIdAccessPassword);
        return true;

    case MainActionClone:
        if(app->scan_timer) furi_timer_stop(app->scan_timer);
        app->scan_running = false;
        rfid_driver_set_enabled(app->driver, false);
        app->clone_source_epc[0] = '\0';
        snprintf(app->clone_result_detail, sizeof(app->clone_result_detail), "Ready");
        app->clone_result_ok = false;
        flipper_rfid_refresh_clone_source_view(app);
        flipper_rfid_switch_view(app, ViewIdCloneSource);
        return true;

    case MainActionCheckProtection:
        snprintf(app->protection_status, sizeof(app->protection_status), "READY");
        app->protection_epc[0] = '\0';
        snprintf(
            app->protection_detail, sizeof(app->protection_detail), "Place tag and press Check");
        flipper_rfid_refresh_protection_view(app);
        flipper_rfid_switch_view(app, ViewIdProtection);
        return true;

    case MainActionConfig:
        flipper_rfid_build_config_menu(app);
        flipper_rfid_switch_view(app, ViewIdConfig);
        return true;

    case MainActionModule:
        flipper_rfid_build_module_menu(app);
        flipper_rfid_switch_view(app, ViewIdModule);
        return true;

    case MainActionRegion:
        flipper_rfid_build_region_menu(app);
        flipper_rfid_switch_view(app, ViewIdRegion);
        return true;

    case ModuleActionFm504:
    case ModuleActionFm505:
    case ModuleActionFm507:
    case ModuleActionFm509:
    case ModuleActionFm505A:
    case ModuleActionRe40: {
        RfidModuleType selected = RfidModuleFm504;
        switch(event) {
        case ModuleActionFm505:
            selected = RfidModuleFm505;
            break;
        case ModuleActionFm507:
            selected = RfidModuleFm507;
            break;
        case ModuleActionFm509:
            selected = RfidModuleFm509;
            break;
        case ModuleActionFm505A:
            selected = RfidModuleFm505A;
            break;
        case ModuleActionRe40:
            selected = RfidModuleRe40;
            break;
        case ModuleActionFm504:
        default:
            selected = RfidModuleFm504;
            break;
        }

        if(flipper_rfid_reopen_driver(app, selected)) {
            snprintf(
                app->status_msg,
                sizeof(app->status_msg),
                "%s selected",
                flipper_rfid_module_name(selected));
            flipper_rfid_persist_settings(app);
        } else {
            snprintf(
                app->status_msg,
                sizeof(app->status_msg),
                "Error opening %s",
                flipper_rfid_module_name(selected));
        }
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;
    }

    case MainActionReadMode:
        flipper_rfid_build_read_mode_menu(app);
        flipper_rfid_switch_view(app, ViewIdReadMode);
        return true;

    case RegionActionEu:
        app->region = RfidRegionEu;
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Region: EU");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RegionActionUs:
        app->region = RfidRegionUs;
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Region: US");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case MainActionTxPower:
        flipper_rfid_build_tx_power_menu(app);
        flipper_rfid_switch_view(app, ViewIdTxPower);
        return true;

    case MainActionReadRate:
        flipper_rfid_build_read_rate_menu(app);
        flipper_rfid_switch_view(app, ViewIdReadRate);
        return true;

    case MainActionAbout:
        flipper_rfid_refresh_about(app);
        flipper_rfid_switch_view(app, ViewIdAbout);
        return true;

    case ModeActionEpc:
        app->scan_mode = RfidScanModeEpc;
        app->scan_all_step = 0;
        flipper_rfid_apply_scan_mode_to_driver(app);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read mode: EPC");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case ModeActionTid:
        app->scan_mode = RfidScanModeTid;
        app->scan_all_step = 0;
        flipper_rfid_apply_scan_mode_to_driver(app);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read mode: TID");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case ModeActionUser:
        app->scan_mode = RfidScanModeUser;
        app->scan_all_step = 0;
        flipper_rfid_apply_scan_mode_to_driver(app);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read mode: USER");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case ModeActionAll:
        app->scan_mode = RfidScanModeAll;
        app->scan_all_step = 0;
        app->scan_view_tab = 0;
        flipper_rfid_scan_all_reset(app);
        flipper_rfid_apply_scan_mode_to_driver(app);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read mode: ALL");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction10:
        flipper_rfid_apply_read_rate(app, 10);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 10ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction25:
        flipper_rfid_apply_read_rate(app, 25);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 25ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction50:
        flipper_rfid_apply_read_rate(app, 50);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 50ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction100:
        flipper_rfid_apply_read_rate(app, 100);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 100ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction200:
        flipper_rfid_apply_read_rate(app, 200);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 200ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction300:
        flipper_rfid_apply_read_rate(app, 300);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 300ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction400:
        flipper_rfid_apply_read_rate(app, 400);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 400ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction500:
        flipper_rfid_apply_read_rate(app, 500);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 500ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case RateAction750:
        flipper_rfid_apply_read_rate(app, 750);
        flipper_rfid_persist_settings(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Read rate: 750ms");
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case EventScanToggle:
        app->scan_running = !app->scan_running;
        if(app->scan_running) {
            app->scan_tick_pending = false;
            bool probe_ok = true;
            snprintf(app->scan_last_line, sizeof(app->scan_last_line), "No reads");
            snprintf(app->scan_last_epc, sizeof(app->scan_last_epc), "No reads");
            snprintf(app->scan_last_tid, sizeof(app->scan_last_tid), "No reads");
            snprintf(app->scan_last_user, sizeof(app->scan_last_user), "No reads");
            app->scan_all_step = 0;
            app->scan_view_tab = 0;
            flipper_rfid_scan_all_reset(app);
            flipper_rfid_apply_scan_mode_to_driver(app);
            if(!rfid_driver_set_enabled(app->driver, true)) {
                app->scan_running = false;
                snprintf(app->status_msg, sizeof(app->status_msg), "Error EN=HIGH");
                flipper_rfid_refresh_scan_view(app);
                return true;
            }
            if(!rfid_driver_probe(app->driver, app->tx_power_db)) {
                probe_ok = false;
            }
            if(app->scan_timer) furi_timer_start(app->scan_timer, furi_ms_to_ticks(app->read_rate_ms));
            snprintf(
                app->status_msg,
                sizeof(app->status_msg),
                probe_ok ? "Scan started" : "Probe no response (scan active)");
        } else {
            if(app->scan_timer) furi_timer_stop(app->scan_timer);
            rfid_driver_set_enabled(app->driver, false);
            app->scan_tick_pending = false;
            snprintf(app->status_msg, sizeof(app->status_msg), "Scan stopped");
        }
        flipper_rfid_refresh_scan_view(app);
        return true;

    case EventScanSave:
        if(app->scan_mode == RfidScanModeAll && app->scan_all_count > 0) {
            size_t n = (app->scan_all_count < MAX_TAGS) ? app->scan_all_count : MAX_TAGS;
            for(size_t i = 0; i < n; i++) {
                snprintf(app->saved_tags[i].epc, sizeof(app->saved_tags[i].epc), "%s", app->scan_all_entries[i].epc);
                snprintf(app->saved_tags[i].tid, sizeof(app->saved_tags[i].tid), "%s", app->scan_all_entries[i].tid);
                snprintf(app->saved_tags[i].user, sizeof(app->saved_tags[i].user), "%s", app->scan_all_entries[i].user);
            }
            if(storage_saved_tags_save(app->saved_tags, n)) {
                snprintf(app->status_msg, sizeof(app->status_msg), "Saved: %u tags", (unsigned)n);
                snprintf(app->scan_last_line, sizeof(app->scan_last_line), "Saved: %u tags", (unsigned)n);
            } else {
                snprintf(app->status_msg, sizeof(app->status_msg), "Save error");
                snprintf(app->scan_last_line, sizeof(app->scan_last_line), "Save error");
            }
        } else {
            size_t n = (app->tags_count < MAX_TAGS) ? app->tags_count : MAX_TAGS;
            for(size_t i = 0; i < n; i++) {
                snprintf(app->saved_tags[i].epc, sizeof(app->saved_tags[i].epc), "%s", app->tags[i].primary_hex);
                app->saved_tags[i].tid[0] = '\0';
                app->saved_tags[i].user[0] = '\0';
            }
            if(storage_saved_tags_save(app->saved_tags, n)) {
                snprintf(app->status_msg, sizeof(app->status_msg), "Saved: %u tags", (unsigned)n);
                snprintf(app->scan_last_line, sizeof(app->scan_last_line), "Saved: %u tags", (unsigned)n);
            } else {
                snprintf(app->status_msg, sizeof(app->status_msg), "Save error");
                snprintf(app->scan_last_line, sizeof(app->scan_last_line), "Save error");
            }
        }
        flipper_rfid_refresh_scan_view(app);
        return true;

    case EventScanClear:
        app->tags_count = 0;
        flipper_rfid_scan_all_reset(app);
        snprintf(app->status_msg, sizeof(app->status_msg), "Captures cleared");
        snprintf(app->scan_last_line, sizeof(app->scan_last_line), "No reads");
        snprintf(app->scan_last_epc, sizeof(app->scan_last_epc), "No reads");
        snprintf(app->scan_last_tid, sizeof(app->scan_last_tid), "No reads");
        snprintf(app->scan_last_user, sizeof(app->scan_last_user), "No reads");
        flipper_rfid_refresh_scan_view(app);
        return true;

    case EventScanTagPrev:
        if(app->scan_mode == RfidScanModeAll) {
            if(app->scan_all_count > 0) {
                app->scan_all_selected =
                    (app->scan_all_selected == 0) ? (app->scan_all_count - 1) : (app->scan_all_selected - 1);
            }
            flipper_rfid_refresh_scan_view(app);
        }
        return true;

    case EventScanTagNext:
        if(app->scan_mode == RfidScanModeAll) {
            if(app->scan_all_count > 0) {
                app->scan_all_selected = (app->scan_all_selected + 1U) % app->scan_all_count;
            }
            flipper_rfid_refresh_scan_view(app);
        }
        return true;

    case EventCloneSourceRead:
    case EventCloneSourceRetry:
        if(flipper_rfid_clone_capture_source(app)) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Source captured");
            flipper_rfid_refresh_clone_target_view(app);
            flipper_rfid_switch_view(app, ViewIdCloneTarget);
        } else {
            snprintf(app->status_msg, sizeof(app->status_msg), "Source read failed");
            flipper_rfid_refresh_clone_source_view(app);
        }
        return true;

    case EventCloneTargetWrite:
        if(app->clone_source_epc[0] == '\0') {
            snprintf(app->status_msg, sizeof(app->status_msg), "No source EPC");
            snprintf(app->clone_result_detail, sizeof(app->clone_result_detail), "No source EPC");
            flipper_rfid_refresh_clone_source_view(app);
            flipper_rfid_switch_view(app, ViewIdCloneSource);
            return true;
        }
        char write_detail[48];
        if(flipper_rfid_attempt_write_epc(app, app->clone_source_epc, write_detail, sizeof(write_detail))) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Clone write OK");
            snprintf(app->clone_result_detail, sizeof(app->clone_result_detail), "%s", write_detail);
            app->clone_result_ok = true;
            flipper_rfid_vibro_write_ok(app);
        } else {
            snprintf(
                app->status_msg,
                sizeof(app->status_msg),
                "Clone write failed: %.38s",
                write_detail[0] ? write_detail : "No detail");
            snprintf(
                app->clone_result_detail,
                sizeof(app->clone_result_detail),
                "%s",
                write_detail[0] ? write_detail : "No detail");
            app->clone_result_ok = false;
        }
        flipper_rfid_refresh_clone_result_view(app);
        flipper_rfid_switch_view(app, ViewIdCloneResult);
        return true;

    case EventCloneToggleAccess:
        app->use_access_password = !app->use_access_password;
        flipper_rfid_persist_settings(app);
        snprintf(
            app->status_msg,
            sizeof(app->status_msg),
            app->use_access_password ? "Lock enabled for write/clone" : "Lock disabled for write/clone");
        flipper_rfid_refresh_clone_target_view(app);
        return true;

    case EventCloneTargetRetry: {
        char write_detail[48];
        if(app->clone_source_epc[0] == '\0') {
            snprintf(app->clone_result_detail, sizeof(app->clone_result_detail), "No source EPC");
            app->clone_result_ok = false;
            flipper_rfid_refresh_clone_result_view(app);
            return true;
        }
        if(flipper_rfid_attempt_write_epc(app, app->clone_source_epc, write_detail, sizeof(write_detail))) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Clone write OK");
            snprintf(app->clone_result_detail, sizeof(app->clone_result_detail), "%s", write_detail);
            app->clone_result_ok = true;
            flipper_rfid_vibro_write_ok(app);
        } else {
            snprintf(
                app->status_msg,
                sizeof(app->status_msg),
                "Clone write failed: %.38s",
                write_detail[0] ? write_detail : "No detail");
            snprintf(
                app->clone_result_detail,
                sizeof(app->clone_result_detail),
                "%s",
                write_detail[0] ? write_detail : "No detail");
            app->clone_result_ok = false;
        }
        flipper_rfid_refresh_clone_result_view(app);
        return true;
    }

    case EventCloneBackToSource:
        flipper_rfid_refresh_clone_source_view(app);
        flipper_rfid_switch_view(app, ViewIdCloneSource);
        return true;

    case EventCloneResultBack:
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case EventProtectionRun:
        snprintf(app->protection_status, sizeof(app->protection_status), "CHECKING...");
        app->protection_epc[0] = '\0';
        snprintf(app->protection_detail, sizeof(app->protection_detail), "Reading EPC...");
        flipper_rfid_refresh_protection_view(app);
        view_dispatcher_send_custom_event(app->view_dispatcher, EventProtectionDo);
        return true;

    case EventProtectionDo:
        flipper_rfid_run_protection_check(app);
        flipper_rfid_refresh_protection_view(app);
        return true;

    case EventProtectionBack:
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case EventScanTick:
        if(app->scan_running) {
            if(app->scan_mode == RfidScanModeAll) {
                RfidTagRead epc_tag = {0};
                rfid_driver_set_mode(app->driver, RfidScanModeEpc);
                if(rfid_driver_scan_once(app->driver, &epc_tag) && epc_tag.primary_hex[0] != '\0') {
                    bool is_new = flipper_rfid_add_or_update_tag(app, &epc_tag);
                    flipper_rfid_scan_all_record(app, "EPC", epc_tag.primary_hex);
                    snprintf(app->scan_last_epc, sizeof(app->scan_last_epc), "%s", epc_tag.primary_hex);
                    snprintf(app->scan_last_line, sizeof(app->scan_last_line), "%s", epc_tag.primary_hex);
                    if(is_new) flipper_rfid_vibro_read(app);

                    RfidTagRead tid_tag = {0};
                    rfid_driver_set_mode(app->driver, RfidScanModeTid);
                    if(rfid_driver_scan_once(app->driver, &tid_tag) && tid_tag.primary_hex[0] != '\0') {
                        flipper_rfid_scan_all_record(app, "TID", tid_tag.primary_hex);
                        snprintf(app->scan_last_tid, sizeof(app->scan_last_tid), "%s", tid_tag.primary_hex);
                    }

                    RfidTagRead user_tag = {0};
                    rfid_driver_set_mode(app->driver, RfidScanModeUser);
                    if(rfid_driver_scan_once(app->driver, &user_tag) && user_tag.primary_hex[0] != '\0') {
                        flipper_rfid_scan_all_record(app, "USER", user_tag.primary_hex);
                        snprintf(app->scan_last_user, sizeof(app->scan_last_user), "%s", user_tag.primary_hex);
                    }

                    snprintf(app->status_msg, sizeof(app->status_msg), "ALL: %s", epc_tag.primary_hex);
                    if(is_new) {
                        flipper_rfid_autosave_scan_captures(app);
                    }
                }
                rfid_driver_set_mode(app->driver, RfidScanModeEpc);
                if(app->current_view == ViewIdScan) flipper_rfid_refresh_scan_view(app);
            } else {
                if(app->scan_mode == RfidScanModeUser) flipper_rfid_apply_scan_mode_to_driver(app);
                RfidTagRead tag = {0};
                if(rfid_driver_scan_once(app->driver, &tag)) {
                    bool is_new = flipper_rfid_add_or_update_tag(app, &tag);
                    if(is_new) flipper_rfid_vibro_read(app);
                    const char* read_label = flipper_rfid_scan_mode_name(app->scan_mode);
                    snprintf(app->status_msg, sizeof(app->status_msg), "%s: %s", read_label, tag.primary_hex);
                    snprintf(app->scan_last_line, sizeof(app->scan_last_line), "%s", tag.primary_hex);
                    if(strcmp(read_label, "EPC") == 0) {
                        snprintf(app->scan_last_epc, sizeof(app->scan_last_epc), "%s", tag.primary_hex);
                    } else if(strcmp(read_label, "TID") == 0) {
                        snprintf(app->scan_last_tid, sizeof(app->scan_last_tid), "%s", tag.primary_hex);
                    } else if(strcmp(read_label, "USER") == 0) {
                        snprintf(app->scan_last_user, sizeof(app->scan_last_user), "%s", tag.primary_hex);
                    }
                    if(is_new) {
                        flipper_rfid_autosave_scan_captures(app);
                    }
                    if(app->current_view == ViewIdScan) flipper_rfid_refresh_scan_view(app);
                }
            }
        }
        app->scan_tick_pending = false;
        return true;

    case EventWriteCommit: {
        app->last_write_is_user = false;
        char normalized[33];
        if(!rfid_driver_normalize_epc(app->write_buffer, normalized, sizeof(normalized))) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Invalid EPC");
            strncpy(app->last_write_epc, app->write_buffer, sizeof(app->last_write_epc) - 1);
            app->last_write_epc[sizeof(app->last_write_epc) - 1] = '\0';
            snprintf(app->last_write_detail, sizeof(app->last_write_detail), "Invalid EPC format");
            app->last_write_ok = false;
            flipper_rfid_refresh_write_result_view(app);
            flipper_rfid_switch_view(app, ViewIdWriteResult);
            return true;
        } else {
            snprintf(app->last_write_epc, sizeof(app->last_write_epc), "%s", normalized);
            snprintf(
                app->last_write_detail,
                sizeof(app->last_write_detail),
                "Lock: %s (%s)",
                app->use_access_password ? "ON" : "OFF",
                app->use_access_password ? app->access_password : "no pwd");
            flipper_rfid_refresh_write_confirm_view(app);
            flipper_rfid_switch_view(app, ViewIdWriteConfirm);
            return true;
        }
    }

    case EventWriteUserCommit: {
        flipper_rfid_uppercase_hex(app->write_user_buffer);
        app->last_write_is_user = true;
        if(!flipper_rfid_is_even_hex_max64(app->write_user_buffer)) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Invalid USER (HEX even, <=64)");
            snprintf(app->last_write_epc, sizeof(app->last_write_epc), "USER");
            snprintf(app->last_write_user, sizeof(app->last_write_user), "%s", app->write_user_buffer);
            snprintf(app->last_write_detail, sizeof(app->last_write_detail), "Invalid USER format");
            app->last_write_ok = false;
            flipper_rfid_refresh_write_result_view(app);
            flipper_rfid_switch_view(app, ViewIdWriteResult);
            return true;
        }

        char detail[64];
        snprintf(app->last_write_user, sizeof(app->last_write_user), "%s", app->write_user_buffer);
        if(flipper_rfid_attempt_write_user(app, app->write_user_buffer, 0, detail, sizeof(detail))) {
            snprintf(app->status_msg, sizeof(app->status_msg), "USER written");
            snprintf(app->last_write_epc, sizeof(app->last_write_epc), "USER");
            snprintf(app->last_write_detail, sizeof(app->last_write_detail), "%s", detail);
            app->last_write_ok = true;
            flipper_rfid_vibro_write_ok(app);
        } else {
            snprintf(app->status_msg, sizeof(app->status_msg), "Write USER failed: %.44s", detail);
            snprintf(app->last_write_epc, sizeof(app->last_write_epc), "USER");
            snprintf(app->last_write_detail, sizeof(app->last_write_detail), "%s", detail);
            app->last_write_ok = false;
        }
        flipper_rfid_refresh_write_result_view(app);
        flipper_rfid_switch_view(app, ViewIdWriteResult);
        return true;
    }

    case EventWriteDo: {
        char write_detail[64];
        if(flipper_rfid_attempt_write_epc(app, app->last_write_epc, write_detail, sizeof(write_detail))) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Tag written: %s", app->last_write_epc);
            snprintf(app->last_write_detail, sizeof(app->last_write_detail), "%s", write_detail);
            app->last_write_ok = true;
            flipper_rfid_vibro_write_ok(app);
            if(app->selected_tag_idx < app->tags_count) {
                strncpy(
                    app->tags[app->selected_tag_idx].primary_hex,
                    app->last_write_epc,
                    sizeof(app->tags[app->selected_tag_idx].primary_hex) - 1);
                app->tags[app->selected_tag_idx]
                    .primary_hex[sizeof(app->tags[app->selected_tag_idx].primary_hex) - 1] = '\0';
            }
        } else {
            snprintf(
                app->status_msg,
                sizeof(app->status_msg),
                "Write failed: %.46s",
                write_detail[0] ? write_detail : "No detail");
            snprintf(
                app->last_write_detail,
                sizeof(app->last_write_detail),
                "%s",
                write_detail[0] ? write_detail : "No detail");
            app->last_write_ok = false;
        }
        flipper_rfid_refresh_write_result_view(app);
        flipper_rfid_switch_view(app, ViewIdWriteResult);
        return true;
    }

    case EventWriteToggleAccess:
        app->use_access_password = !app->use_access_password;
        flipper_rfid_persist_settings(app);
        flipper_rfid_refresh_write_confirm_view(app);
        return true;

    case EventWriteConfirmBack:
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case EventAccessPwdCommit:
        flipper_rfid_uppercase_str(app->access_input_buffer);
        if(!flipper_rfid_is_hex8(app->access_input_buffer)) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Invalid password (exactly 8 HEX)");
        } else {
            strncpy(app->access_password, app->access_input_buffer, sizeof(app->access_password) - 1);
            app->access_password[sizeof(app->access_password) - 1] = '\0';
            flipper_rfid_persist_settings(app);
            snprintf(app->status_msg, sizeof(app->status_msg), "Access password saved: %s", app->access_password);
        }
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    case EventWriteRetry: {
        char write_detail[64];
        if(app->last_write_is_user) {
            if(app->last_write_user[0] == '\0') {
                snprintf(app->last_write_detail, sizeof(app->last_write_detail), "No USER to retry");
                app->last_write_ok = false;
            } else if(flipper_rfid_attempt_write_user(
                          app, app->last_write_user, 0, write_detail, sizeof(write_detail))) {
                snprintf(app->status_msg, sizeof(app->status_msg), "USER written");
                snprintf(app->last_write_detail, sizeof(app->last_write_detail), "%s", write_detail);
                app->last_write_ok = true;
                flipper_rfid_vibro_write_ok(app);
            } else {
                snprintf(
                    app->status_msg,
                    sizeof(app->status_msg),
                    "Write USER failed: %.40s",
                    write_detail[0] ? write_detail : "No detail");
                snprintf(
                    app->last_write_detail,
                    sizeof(app->last_write_detail),
                    "%s",
                    write_detail[0] ? write_detail : "No detail");
                app->last_write_ok = false;
            }
        } else if(app->last_write_epc[0] == '\0') {
            snprintf(app->last_write_detail, sizeof(app->last_write_detail), "No EPC to retry");
            app->last_write_ok = false;
        } else if(flipper_rfid_attempt_write_epc(app, app->last_write_epc, write_detail, sizeof(write_detail))) {
            snprintf(app->status_msg, sizeof(app->status_msg), "Tag written: %s", app->last_write_epc);
            snprintf(app->last_write_detail, sizeof(app->last_write_detail), "%s", write_detail);
            app->last_write_ok = true;
            flipper_rfid_vibro_write_ok(app);
        } else {
            snprintf(
                app->status_msg,
                sizeof(app->status_msg),
                "Write failed: %.46s",
                write_detail[0] ? write_detail : "No detail");
            snprintf(
                app->last_write_detail,
                sizeof(app->last_write_detail),
                "%s",
                write_detail[0] ? write_detail : "No detail");
            app->last_write_ok = false;
        }
        flipper_rfid_refresh_write_result_view(app);
        return true;
    }

    case EventSavedTagBack:
        flipper_rfid_build_saved_tags_menu(app);
        flipper_rfid_switch_view(app, ViewIdSavedTags);
        return true;

    case EventSavedTagDelete:
        if(app->saved_selected_idx < app->saved_tags_count) {
            (void)storage_saved_tags_delete(app->saved_selected_idx);
            flipper_rfid_saved_tags_load(app);
            if(app->saved_selected_idx >= app->saved_tags_count && app->saved_tags_count > 0) {
                app->saved_selected_idx = app->saved_tags_count - 1;
            }
        }
        flipper_rfid_build_saved_tags_menu(app);
        flipper_rfid_switch_view(app, ViewIdSavedTags);
        return true;

    case EventSavedTagClearAll:
        (void)storage_saved_tags_clear();
        flipper_rfid_saved_tags_load(app);
        flipper_rfid_build_saved_tags_menu(app);
        flipper_rfid_switch_view(app, ViewIdSavedTags);
        return true;

    case EventWriteBack:
        flipper_rfid_switch_view(app, ViewIdMainMenu);
        return true;

    default:
        return false;
    }
}

static FlipperRfidApp* flipper_rfid_app_alloc(void) {
    FlipperRfidApp* app = malloc(sizeof(FlipperRfidApp));
    if(!app) return NULL;
    memset(app, 0, sizeof(FlipperRfidApp));

    app->current_module = RfidModuleFm504;
    app->region = RfidRegionEu;
    app->scan_mode = RfidScanModeEpc;
    app->tx_power_db = 3;
    app->read_rate_ms = 100;
    app->selected_tag_idx = SIZE_MAX;
    snprintf(app->status_msg, sizeof(app->status_msg), "Ready");
    snprintf(app->scan_last_line, sizeof(app->scan_last_line), "No reads");
    snprintf(app->scan_last_epc, sizeof(app->scan_last_epc), "No reads");
    snprintf(app->scan_last_tid, sizeof(app->scan_last_tid), "No reads");
    snprintf(app->scan_last_user, sizeof(app->scan_last_user), "No reads");
    app->scan_view_tab = 0;
    app->last_write_epc[0] = '\0';
    app->last_write_user[0] = '\0';
    app->write_user_buffer[0] = '\0';
    app->last_write_detail[0] = '\0';
    app->last_write_ok = false;
    app->last_write_is_user = false;
    app->use_access_password = false;
    snprintf(app->access_password, sizeof(app->access_password), "00000000");
    app->access_input_buffer[0] = '\0';
    app->protection_epc[0] = '\0';
    snprintf(app->protection_status, sizeof(app->protection_status), "READY");
    snprintf(app->protection_detail, sizeof(app->protection_detail), "Place tag and press Check");

    RfidAppSettings settings = {
        .module = app->current_module,
        .region = app->region,
        .scan_mode = app->scan_mode,
        .tx_power_db = app->tx_power_db,
        .read_rate_ms = app->read_rate_ms,
        .use_access_password = app->use_access_password,
    };
    strncpy(settings.access_password, app->access_password, sizeof(settings.access_password) - 1);
    settings.access_password[sizeof(settings.access_password) - 1] = '\0';
    if(storage_settings_load(&settings)) {
        app->current_module = settings.module;
        app->region = settings.region;
        app->scan_mode = settings.scan_mode;
        app->tx_power_db = settings.tx_power_db;
        app->read_rate_ms = settings.read_rate_ms;
        app->use_access_password = settings.use_access_password;
        strncpy(app->access_password, settings.access_password, sizeof(app->access_password) - 1);
        app->access_password[sizeof(app->access_password) - 1] = '\0';
    }
    if(app->tx_power_db < -2) app->tx_power_db = -2;
    if(app->tx_power_db > 27) app->tx_power_db = 27;

    RfidDriverConfig driver_cfg = {
        .module = app->current_module,
        .baudrate = 38400,
        .scan_mode = app->scan_mode,
        .tx_power_db = app->tx_power_db,
        .read_rate_ms = app->read_rate_ms,
    };
    if(!rfid_driver_open(&app->driver, &driver_cfg)) {
        free(app);
        return NULL;
    }
    rfid_driver_set_enabled(app->driver, false);

    app->view_dispatcher = view_dispatcher_alloc();
    app->main_menu = submenu_alloc();
    app->config_menu = submenu_alloc();
    app->scan_widget = widget_alloc();
    app->write_select_menu = submenu_alloc();
    app->saved_tags_menu = submenu_alloc();
    app->write_input = text_input_alloc();
    app->access_input = text_input_alloc();
    app->write_confirm_widget = widget_alloc();
    app->write_result_widget = widget_alloc();
    app->saved_tag_widget = widget_alloc();
    app->clone_source_widget = widget_alloc();
    app->clone_target_widget = widget_alloc();
    app->clone_result_widget = widget_alloc();
    app->protection_widget = widget_alloc();
    app->module_menu = submenu_alloc();
    app->region_menu = submenu_alloc();
    app->read_mode_menu = submenu_alloc();
    app->tx_power_menu = submenu_alloc();
    app->read_rate_menu = submenu_alloc();
    app->about_widget = widget_alloc();
    app->scan_timer = furi_timer_alloc(flipper_rfid_scan_timer_callback, FuriTimerTypePeriodic, app);
    app->gui = furi_record_open(RECORD_GUI);
    app->notification = furi_record_open(RECORD_NOTIFICATION);

    if(!app->view_dispatcher || !app->main_menu || !app->config_menu || !app->scan_widget || !app->write_select_menu ||
       !app->saved_tags_menu ||
       !app->write_input || !app->access_input || !app->write_confirm_widget ||
       !app->write_result_widget || !app->saved_tag_widget || !app->clone_source_widget ||
       !app->clone_target_widget || !app->clone_result_widget || !app->protection_widget ||
       !app->module_menu ||
       !app->region_menu || !app->read_mode_menu || !app->tx_power_menu || !app->read_rate_menu ||
       !app->about_widget ||
       !app->scan_timer || !app->gui || !app->notification) {
        flipper_rfid_app_free(app);
        return NULL;
    }

    view_dispatcher_set_custom_event_callback(app->view_dispatcher, flipper_rfid_custom_event_callback);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    submenu_set_header(app->main_menu, "FlippeRFID UHF");
    submenu_add_item(app->main_menu, "Scan", MainActionScan, flipper_rfid_submenu_event, app);
    submenu_add_item(app->main_menu, "Write Tag", MainActionWriteTag, flipper_rfid_submenu_event, app);
    submenu_add_item(app->main_menu, "Write USER", MainActionWriteUser, flipper_rfid_submenu_event, app);
    submenu_add_item(app->main_menu, "Saved Tags", MainActionSavedTags, flipper_rfid_submenu_event, app);
    submenu_add_item(app->main_menu, "Clone", MainActionClone, flipper_rfid_submenu_event, app);
    submenu_add_item(
        app->main_menu, "Check Protection", MainActionCheckProtection, flipper_rfid_submenu_event, app);
    submenu_add_item(app->main_menu, "Config", MainActionConfig, flipper_rfid_submenu_event, app);
    submenu_add_item(app->main_menu, "About", MainActionAbout, flipper_rfid_submenu_event, app);

    view_set_previous_callback(submenu_get_view(app->main_menu), flipper_rfid_prev_exit_callback);
    view_set_previous_callback(submenu_get_view(app->config_menu), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->scan_widget), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(submenu_get_view(app->write_select_menu), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(submenu_get_view(app->saved_tags_menu), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(text_input_get_view(app->write_input), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(text_input_get_view(app->access_input), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->write_confirm_widget), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->write_result_widget), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->saved_tag_widget), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->clone_source_widget), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->clone_target_widget), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->clone_result_widget), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->protection_widget), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(submenu_get_view(app->module_menu), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(submenu_get_view(app->region_menu), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(submenu_get_view(app->read_mode_menu), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(submenu_get_view(app->tx_power_menu), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(submenu_get_view(app->read_rate_menu), flipper_rfid_prev_to_main_callback);
    view_set_previous_callback(widget_get_view(app->about_widget), flipper_rfid_prev_to_main_callback);

    view_dispatcher_add_view(app->view_dispatcher, ViewIdMainMenu, submenu_get_view(app->main_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdConfig, submenu_get_view(app->config_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdScan, widget_get_view(app->scan_widget));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdWriteSelect, submenu_get_view(app->write_select_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdSavedTags, submenu_get_view(app->saved_tags_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdWriteInput, text_input_get_view(app->write_input));
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdAccessPassword, text_input_get_view(app->access_input));
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdWriteConfirm, widget_get_view(app->write_confirm_widget));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdWriteResult, widget_get_view(app->write_result_widget));
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdSavedTagDetail, widget_get_view(app->saved_tag_widget));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdCloneSource, widget_get_view(app->clone_source_widget));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdCloneTarget, widget_get_view(app->clone_target_widget));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdCloneResult, widget_get_view(app->clone_result_widget));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdProtection, widget_get_view(app->protection_widget));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdModule, submenu_get_view(app->module_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdRegion, submenu_get_view(app->region_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdReadMode, submenu_get_view(app->read_mode_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdTxPower, submenu_get_view(app->tx_power_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdReadRate, submenu_get_view(app->read_rate_menu));
    view_dispatcher_add_view(app->view_dispatcher, ViewIdAbout, widget_get_view(app->about_widget));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    flipper_rfid_switch_view(app, ViewIdMainMenu);
    return app;
}

static void flipper_rfid_app_free(FlipperRfidApp* app) {
    if(!app) return;
    app->closing = true;
    app->scan_running = false;

    if(app->scan_timer) furi_timer_stop(app->scan_timer);
    if(app->scan_timer) furi_timer_free(app->scan_timer);

    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdMainMenu);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdConfig);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdScan);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdWriteSelect);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdSavedTags);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdWriteInput);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdAccessPassword);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdWriteConfirm);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdWriteResult);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdSavedTagDetail);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdCloneSource);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdCloneTarget);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdCloneResult);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdProtection);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdModule);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdRegion);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdReadMode);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdTxPower);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdReadRate);
    if(app->view_dispatcher) view_dispatcher_remove_view(app->view_dispatcher, ViewIdAbout);

    if(app->main_menu) submenu_free(app->main_menu);
    if(app->config_menu) submenu_free(app->config_menu);
    if(app->scan_widget) widget_free(app->scan_widget);
    if(app->write_select_menu) submenu_free(app->write_select_menu);
    if(app->saved_tags_menu) submenu_free(app->saved_tags_menu);
    if(app->write_input) text_input_free(app->write_input);
    if(app->access_input) text_input_free(app->access_input);
    if(app->write_confirm_widget) widget_free(app->write_confirm_widget);
    if(app->write_result_widget) widget_free(app->write_result_widget);
    if(app->saved_tag_widget) widget_free(app->saved_tag_widget);
    if(app->clone_source_widget) widget_free(app->clone_source_widget);
    if(app->clone_target_widget) widget_free(app->clone_target_widget);
    if(app->clone_result_widget) widget_free(app->clone_result_widget);
    if(app->protection_widget) widget_free(app->protection_widget);
    if(app->module_menu) submenu_free(app->module_menu);
    if(app->region_menu) submenu_free(app->region_menu);
    if(app->read_mode_menu) submenu_free(app->read_mode_menu);
    if(app->tx_power_menu) submenu_free(app->tx_power_menu);
    if(app->read_rate_menu) submenu_free(app->read_rate_menu);
    if(app->about_widget) widget_free(app->about_widget);
    if(app->view_dispatcher) view_dispatcher_free(app->view_dispatcher);

    if(app->gui) furi_record_close(RECORD_GUI);
    if(app->notification) furi_record_close(RECORD_NOTIFICATION);
    rfid_driver_set_enabled(app->driver, false);
    rfid_driver_close(app->driver);
    free(app);
}

int32_t flipper_rfid_app(void* p) {
    UNUSED(p);

    FlipperRfidApp* app = flipper_rfid_app_alloc();
    if(!app) {
        FURI_LOG_E(TAG, "App alloc failed");
        return -1;
    }

    view_dispatcher_run(app->view_dispatcher);
    flipper_rfid_app_free(app);
    return 0;
}
