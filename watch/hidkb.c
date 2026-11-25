/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file hidkb.c
 * @brief USB keyboard for the BIOS helper: a queue of key taps, sent by a
 *        small task so the OS loop never waits on the bus.
 */
#include "hidkb.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "hal/usb_serial_jtag_ll.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

static const char *TAG = "hidkb";

#define KB_DOWN_MS 40
#define KB_GAP_MS 60

enum { ITF_KB, ITF_N };
#define EP_KB 0x81

static const uint8_t s_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };

static const uint8_t s_cfg[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_N, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_KB, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_report), EP_KB, 8, 10),
};

static const tusb_desc_device_t s_dev = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4004,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const char *s_str[] = {
    (const char[]){ 0x09, 0x04 },
    "Kode",
    "Kode Watch Keyboard",
    "KW0001",
    "Keyboard",
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;   /* LEDs: not needed */
}

typedef struct { uint8_t mod, key; uint16_t down; } tap_t;

static QueueHandle_t s_q;
static TaskHandle_t s_task;
static volatile bool s_on, s_quit, s_parked;

static bool wait_ready(int ms)
{
    while ( ms > 0 )
    {
        if ( tud_mounted() && !tud_suspended() && tud_hid_ready() ) { return true; }
        vTaskDelay(pdMS_TO_TICKS(5));
        ms -= 5;
    }
    return false;
}

static void send_task(void *arg)
{
    (void)arg;
    tap_t t;
    while ( !s_quit )
    {
        if ( pdTRUE != xQueueReceive(s_q, &t, pdMS_TO_TICKS(100)) ) { continue; }
        if ( !wait_ready(500) ) { continue; }   /* no host (yet): the tap is lost, the BIOS was not listening */
        uint8_t keys[6] = { t.key, 0, 0, 0, 0, 0 };
        tud_hid_keyboard_report(0, t.mod, t.key ? keys : NULL);
        vTaskDelay(pdMS_TO_TICKS(t.down ? t.down : KB_DOWN_MS));
        (void)wait_ready(100);
        tud_hid_keyboard_report(0, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(KB_GAP_MS));
    }
    s_parked = true;
    for ( ;; ) { vTaskDelay(portMAX_DELAY); }   /* hidkb_stop deletes us (PSRAM stack) */
}

bool hidkb_start(void)
{
    if ( s_on ) { return true; }
    if ( !s_q ) { s_q = xQueueCreate(64, sizeof(tap_t)); }
    if ( !s_q ) { return false; }
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.device = &s_dev;
    cfg.descriptor.string = s_str;
    cfg.descriptor.string_count = sizeof(s_str) / sizeof(s_str[0]);
    cfg.descriptor.full_speed_config = s_cfg;
    esp_err_t const e = tinyusb_driver_install(&cfg);
    if ( ESP_OK != e )
    {
        ESP_LOGE(TAG, "install: %s", esp_err_to_name(e));
        usb_serial_jtag_ll_phy_enable_external(false);
        return false;
    }
    s_quit = false;
    s_parked = false;
    xQueueReset(s_q);
    if ( pdPASS != xTaskCreatePinnedToCoreWithCaps(send_task, "hidkb", 3072, NULL, 5, &s_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) )   /* stack in PSRAM: internal RAM is for Wi-Fi/TLS */
    {
        (void)tinyusb_driver_uninstall();
        usb_serial_jtag_ll_phy_enable_external(false);
        return false;
    }
    s_on = true;
    ESP_LOGI(TAG, "keyboard on");
    return true;
}

void hidkb_stop(void)
{
    if ( !s_on ) { return; }
    s_on = false;
    s_quit = true;
    for ( int i = 0; (i < 120) && !s_parked; ++i ) { vTaskDelay(pdMS_TO_TICKS(10)); }
    if ( s_task ) { vTaskDeleteWithCaps(s_task); s_task = NULL; }
    (void)tinyusb_driver_uninstall();
    usb_serial_jtag_ll_phy_enable_external(false);   /* the PHY back to USB Serial/JTAG: console and flashing again */
    ESP_LOGI(TAG, "keyboard off, console back");
}

bool hidkb_active(void) { return s_on; }
bool hidkb_ready(void) { return s_on && tud_mounted() && !tud_suspended(); }
int hidkb_pending(void) { return s_q ? (int)uxQueueMessagesWaiting(s_q) : 0; }
void hidkb_flush(void) { if ( s_q ) { xQueueReset(s_q); } }

bool hidkb_tap(uint8_t mod, uint8_t key)
{
    return hidkb_tap_hold(mod, key, 0);
}

bool hidkb_tap_hold(uint8_t mod, uint8_t key, uint16_t down_ms)
{
    if ( !s_on || (!key && !mod) ) { return false; }
    tap_t const t = { mod, key, down_ms };
    return pdTRUE == xQueueSend(s_q, &t, 0);
}

typedef struct { const char *n; uint8_t k; } named_t;
static const named_t NAMED[] = {
    { "ENTER", HID_KEY_ENTER }, { "ESC", HID_KEY_ESCAPE }, { "TAB", HID_KEY_TAB }, { "SPACE", HID_KEY_SPACE },
    { "BKSP", HID_KEY_BACKSPACE }, { "DEL", HID_KEY_DELETE },
    { "UP", HID_KEY_ARROW_UP }, { "DOWN", HID_KEY_ARROW_DOWN }, { "LEFT", HID_KEY_ARROW_LEFT }, { "RIGHT", HID_KEY_ARROW_RIGHT },
    { "HOME", HID_KEY_HOME }, { "END", HID_KEY_END }, { "PGUP", HID_KEY_PAGE_UP }, { "PGDN", HID_KEY_PAGE_DOWN },
    { "F1", HID_KEY_F1 }, { "F2", HID_KEY_F2 }, { "F3", HID_KEY_F3 }, { "F4", HID_KEY_F4 }, { "F5", HID_KEY_F5 }, { "F6", HID_KEY_F6 },
    { "F7", HID_KEY_F7 }, { "F8", HID_KEY_F8 }, { "F9", HID_KEY_F9 }, { "F10", HID_KEY_F10 }, { "F11", HID_KEY_F11 }, { "F12", HID_KEY_F12 },
    { "PLUS", HID_KEY_KEYPAD_ADD }, { "MINUS", HID_KEY_KEYPAD_SUBTRACT },
};

uint8_t hidkb_code(const char *name, uint8_t *mod)
{
    *mod = 0;
    if ( !name || !name[0] ) { return 0; }
    /* SHIFT+X, CTRL+X, ALT+X */
    const char *p = strchr(name, '+');
    if ( p && (p != name) && p[1] )
    {
        uint8_t m = 0;
        if ( 0 == strncasecmp(name, "SHIFT", (size_t)(p - name)) ) { m = KEYBOARD_MODIFIER_LEFTSHIFT; }
        else if ( 0 == strncasecmp(name, "CTRL", (size_t)(p - name)) ) { m = KEYBOARD_MODIFIER_LEFTCTRL; }
        else if ( 0 == strncasecmp(name, "ALT", (size_t)(p - name)) ) { m = KEYBOARD_MODIFIER_LEFTALT; }
        if ( m )
        {
            uint8_t m2;
            uint8_t const k = hidkb_code(p + 1, &m2);
            *mod = m | m2;
            return k;
        }
    }
    for ( size_t i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); ++i )
    {
        if ( 0 == strcasecmp(name, NAMED[i].n) ) { return NAMED[i].k; }
    }
    if ( name[1] ) { return 0; }
    char const c = name[0];
    if ( (c >= 'a') && (c <= 'z') ) { return (uint8_t)(HID_KEY_A + (c - 'a')); }
    if ( (c >= 'A') && (c <= 'Z') ) { *mod = KEYBOARD_MODIFIER_LEFTSHIFT; return (uint8_t)(HID_KEY_A + (c - 'A')); }
    if ( (c >= '1') && (c <= '9') ) { return (uint8_t)(HID_KEY_1 + (c - '1')); }
    if ( '0' == c ) { return HID_KEY_0; }
    if ( '-' == c ) { return HID_KEY_MINUS; }
    if ( '.' == c ) { return HID_KEY_PERIOD; }
    if ( ' ' == c ) { return HID_KEY_SPACE; }
    return 0;
}
