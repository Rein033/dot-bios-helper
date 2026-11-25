/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file tusb_config.h
 * @brief TinyUSB for this app: a device with one HID interface on the full-speed port.
 *
 * main/CMakeLists.txt puts this folder on the TinyUSB component's include path.
 */
#pragma once

#include "sdkconfig.h"

#define CFG_TUSB_OS              OPT_OS_FREERTOS
#define CFG_TUD_ENABLED          1
#define CFG_TUD_MAX_SPEED        OPT_MODE_FULL_SPEED
#define CFG_TUD_DWC2_SLAVE_ENABLE 1
#define CFG_TUSB_DEBUG           0

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN       __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE   64

#define CFG_TUD_HID              1
#define CFG_TUD_HID_EP_BUFSIZE   8
#define CFG_TUD_CDC              0
#define CFG_TUD_MSC              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0
