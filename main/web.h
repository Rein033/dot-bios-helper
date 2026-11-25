/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file web.h
 * @brief The profile editor in a browser: an HTTP server on port 80 while Wi-Fi is up.
 *
 *   GET  /                    the editor page
 *   GET  /api/state           what the Dot is doing (JSON)
 *   GET  /api/profiles        the list (JSON)
 *   GET  /api/profile?name=   one profile's text
 *   GET  /api/keys            the key names
 *   POST /api/check           a text -> its errors (JSON), nothing saved
 *   POST /api/save?name=      save to /sdcard/bioshelper/<name>.txt
 *   POST /api/delete?name=    delete a card profile
 *   POST /api/cmd             "start", "stop", "go", "next", "yes", "no", "select <name>",
 *                             "code <digits>", "key <KEY>", "type <text>", "reload"
 *
 * No login: anyone on the same network can open it. web_start() is only called when the
 * setting is on.
 */
#ifndef WEB_H_
#define WEB_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool web_start(void);
bool web_running(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_H_ */
