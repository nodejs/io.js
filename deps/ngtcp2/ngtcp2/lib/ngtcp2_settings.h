/*
 * ngtcp2
 *
 * Copyright (c) 2024 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef NGTCP2_SETTINGS_H
#define NGTCP2_SETTINGS_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

/*
 * ngtcp2_settings_convert_to_latest converts |src| of version
 * |settings_version| to the latest version NGTCP2_SETTINGS_VERSION.
 *
 * |dest| must point to the latest version.  |src| may be the older
 * version, and if so, it may have fewer fields.  Accessing those
 * fields causes undefined behavior.
 *
 * If |settings_version| == NGTCP2_SETTINGS_VERSION, no conversion is
 * made, and |src| is returned.  Otherwise, first |dest| is
 * initialized via ngtcp2_settings_default, and then all valid fields
 * in |src| are copied into |dest|.  Finally, |dest| is returned.
 */
const ngtcp2_settings *
ngtcp2_settings_convert_to_latest(ngtcp2_settings *dest, int settings_version,
                                  const ngtcp2_settings *src);

/*
 * ngtcp2_settings_convert_to_old converts |src| of the latest version
 * to |dest| of version |settings_version|.
 *
 * |settings_version| must not be the latest version
 *  NGTCP2_SETTINGS_VERSION.
 *
 * |dest| points to the older version, and it may have fewer fields.
 * Accessing those fields causes undefined behavior.
 *
 * This function copies all valid fields in version |settings_version|
 * from |src| to |dest|.
 */
void ngtcp2_settings_convert_to_old(int settings_version, ngtcp2_settings *dest,
                                    const ngtcp2_settings *src);

/*
 * ngtcp2_settingslen_version returns the effective length of
 * ngtcp2_settings at the version |settings_version|.
 */
size_t ngtcp2_settingslen_version(int settings_version);

#endif /* !defined(NGTCP2_SETTINGS_H) */
