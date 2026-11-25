/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file builtin.h
 * @brief Profiles built into the app. They cannot be changed in place; the editor
 *        saves a copy to the microSD card, which then comes first.
 */
#ifndef BUILTIN_H_
#define BUILTIN_H_

#ifdef __cplusplus
extern "C" {
#endif

int builtin_count(void);
/** @brief A file-name-like id: "hp-t550-secure-boot". */
const char *builtin_name(int i);
const char *builtin_text(int i);

#ifdef __cplusplus
}
#endif

#endif /* BUILTIN_H_ */
