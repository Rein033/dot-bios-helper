/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file hidkb.h
 * @brief The Dot as a USB keyboard: TinyUSB, one HID interface, boot protocol, so a BIOS takes it.
 *
 * The P4's full-speed OTG controller is the keyboard. It sits on one of two places:
 *
 *  - HIDKB_PORT_DOCK:  the magnetic pads (GPIO26/27), the OTG's own PHY. This is where the Dot's
 *                      USB device apps live, and what the simulator models.
 *  - HIDKB_PORT_CABLE: the USB-C socket. The two full-speed PHYs are swapped (LP_SYS
 *                      usb_ctrl.sw_usb_phy_sel), so the OTG drives the USB-C pins and the
 *                      USB-Serial/JTAG console moves to the pads. Not tried on a board yet. While it
 *                      is on, the console and the flasher are not on the cable; a reset gives them back.
 *
 * Key strokes are queued and sent by a task of their own, so the app's Active Object never waits
 * on the bus.
 */
#ifndef HIDKB_H_
#define HIDKB_H_

#include <stdbool.h>
#include <stdint.h>

#include "keys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HIDKB_PORT_DOCK = 0,
    HIDKB_PORT_CABLE = 1,
} hidkb_port_t;

/** @brief Bring the keyboard up on @p port. false when TinyUSB or the PHY would not start. */
bool hidkb_start(hidkb_port_t port);
bool hidkb_active(void);
hidkb_port_t hidkb_port(void);
/** @brief A PC has configured the keyboard and is not suspended. */
bool hidkb_ready(void);
/** @brief Strokes queued and not yet sent. */
int hidkb_pending(void);
void hidkb_flush(void);
/** @brief Queue a press of @p k held @p down_ms, then the release. */
bool hidkb_send(kstroke_t k, uint16_t down_ms);
/** @brief Caps / Num / Scroll lock LEDs as the PC last set them (bit 0 num, 1 caps, 2 scroll). */
uint8_t hidkb_leds(void);

#ifdef __cplusplus
}
#endif

#endif /* HIDKB_H_ */
