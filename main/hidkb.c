/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file hidkb.c
 * @brief The USB keyboard (see hidkb.h).
 *
 * One task owns TinyUSB: it runs tud_task() and plays the stroke queue (press, hold, release,
 * a short pause), so nothing else calls into the stack.
 */
#include "hidkb.h"

#include <string.h>

#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "tusb.h"

static const char *TAG = "bh.kb";

#define RELEASE_GAP_MS 30u   /* a released key stays up this long before the next */
#define QUEUE_LEN      160

/* ---- descriptors ---------------------------------------------------------------------- */

enum { ITF_KB = 0, ITF_COUNT };
#define EP_KB_IN 0x81

static const uint8_t s_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };

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

static const uint8_t s_cfg[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_KB, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_report), EP_KB_IN, 8, 10),
};

static const char *const s_str[] = { "", "Dot BIOS Helper", "Dot BIOS Helper Keyboard", "DBH0001", "Keyboard" };

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_dev;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_cfg;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t d[33];
    uint8_t n;
    if ( 0 == index )
    {
        d[1] = 0x0409;
        n = 1;
    }
    else
    {
        if ( index >= sizeof(s_str) / sizeof(s_str[0]) )
        {
            return NULL;
        }
        const char *s = s_str[index];
        n = (uint8_t)strlen(s);
        if ( n > 32 )
        {
            n = 32;
        }
        for ( uint8_t i = 0; i < n; ++i )
        {
            d[1 + i] = (uint8_t)s[i];
        }
    }
    d[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * n + 2u));
    return d;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buf,
                               uint16_t len)
{
    (void)instance;
    (void)report_id;
    (void)type;
    (void)buf;
    (void)len;
    return 0;
}

static volatile uint8_t s_leds;

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t const *buf,
                           uint16_t len)
{
    (void)instance;
    (void)report_id;
    if ( (HID_REPORT_TYPE_OUTPUT == type) && (len >= 1u) )
    {
        s_leds = buf[0];
    }
}

/* ---- the task -------------------------------------------------------------------------- */

typedef struct {
    kstroke_t k;
    uint16_t  down_ms;
} stroke_t;

static QueueHandle_t   s_q;
static TaskHandle_t    s_task;
static usb_phy_handle_t s_phy;
static volatile bool   s_on;
static volatile bool   s_flush;
static volatile int    s_inflight;   /* a stroke taken off the queue and not yet released */
static hidkb_port_t    s_port;

static uint32_t ms_now(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void kb_task(void *arg)
{
    (void)arg;
    enum { K_IDLE, K_DOWN, K_GAP } st = K_IDLE;
    stroke_t cur = { { 0, 0 }, 0 };
    uint32_t t = 0;
    for ( ;; )
    {
        tud_task_ext(2, false);
        if ( s_flush )
        {
            xQueueReset(s_q);
            s_flush = false;
            if ( K_DOWN == st )
            {
                (void)tud_hid_keyboard_report(0, 0, NULL);
            }
            st = K_IDLE;
            s_inflight = 0;
        }
        const bool up = tud_mounted() && !tud_suspended();
        switch ( st )
        {
        case K_IDLE:
            if ( up && tud_hid_ready() && (pdTRUE == xQueueReceive(s_q, &cur, 0)) )
            {
                s_inflight = 1;
                uint8_t keys[6] = { cur.k.key, 0, 0, 0, 0, 0 };
                (void)tud_hid_keyboard_report(0, cur.k.mod, cur.k.key ? keys : NULL);
                t = ms_now();
                st = K_DOWN;
            }
            break;
        case K_DOWN:
            if ( !up )
            {
                st = K_IDLE; /* the PC went away: the stroke is lost with it */
                s_inflight = 0;
            }
            else if ( ((ms_now() - t) >= cur.down_ms) && tud_hid_ready() )
            {
                (void)tud_hid_keyboard_report(0, 0, NULL);
                t = ms_now();
                st = K_GAP;
            }
            break;
        case K_GAP:
            if ( (ms_now() - t) >= RELEASE_GAP_MS )
            {
                st = K_IDLE;
                s_inflight = 0;
            }
            break;
        default:
            st = K_IDLE;
            break;
        }
    }
}

bool hidkb_start(hidkb_port_t port)
{
    if ( s_on )
    {
        return port == s_port;
    }
    if ( !s_q )
    {
        s_q = xQueueCreate(QUEUE_LEN, sizeof(stroke_t));
        if ( !s_q )
        {
            return false;
        }
    }
    const usb_phy_config_t phy = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_FULL,
    };
    esp_err_t e = usb_new_phy(&phy, &s_phy);
    if ( ESP_OK != e )
    {
        ESP_LOGE(TAG, "USB PHY: %s", esp_err_to_name(e));
        return false;
    }
    if ( HIDKB_PORT_CABLE == port )
    {
        /* The USB-C pins are PHY 0. Give PHY 0 to the OTG and PHY 1 (the pads) to USB-Serial/JTAG. */
        usb_serial_jtag_ll_phy_select(1);
        ESP_LOGW(TAG, "keyboard on the USB-C socket: the console is off the cable until a reset");
    }
    const tusb_rhport_init_t init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
    if ( !tusb_rhport_init(0, &init) )
    {
        ESP_LOGE(TAG, "TinyUSB would not start");
        if ( HIDKB_PORT_CABLE == port )
        {
            usb_serial_jtag_ll_phy_select(0);
        }
        (void)usb_del_phy(s_phy);
        s_phy = NULL;
        return false;
    }
    if ( pdPASS != xTaskCreatePinnedToCore(kb_task, "bh_kb", 4096, NULL, 5, &s_task, 1) )
    {
        return false;
    }
    s_port = port;
    s_on = true;
    ESP_LOGI(TAG, "keyboard up on the %s", (HIDKB_PORT_CABLE == port) ? "USB-C socket" : "dock pads");
    return true;
}

bool hidkb_active(void)
{
    return s_on;
}

hidkb_port_t hidkb_port(void)
{
    return s_port;
}

bool hidkb_ready(void)
{
    return s_on && tud_mounted() && !tud_suspended();
}

int hidkb_pending(void)
{
    return s_q ? (int)uxQueueMessagesWaiting(s_q) + s_inflight : 0;
}

void hidkb_flush(void)
{
    if ( s_on )
    {
        s_flush = true;
    }
}

bool hidkb_send(kstroke_t k, uint16_t down_ms)
{
    if ( !s_on || (!k.key && !k.mod) )
    {
        return false;
    }
    const stroke_t s = { k, down_ms ? down_ms : 40u };
    return pdTRUE == xQueueSend(s_q, &s, 0);
}

uint8_t hidkb_leds(void)
{
    return s_leds;
}
