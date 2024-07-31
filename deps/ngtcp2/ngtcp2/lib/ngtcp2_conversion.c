/*
 * ngtcp2
 *
 * Copyright (c) 2023 ngtcp2 contributors
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
#include "ngtcp2_conversion.h"

#include <string.h>
#include <assert.h>

static void transport_params_copy(int transport_params_version,
                                  ngtcp2_transport_params *dest,
                                  const ngtcp2_transport_params *src) {
  assert(transport_params_version != NGTCP2_TRANSPORT_PARAMS_VERSION);

  switch (transport_params_version) {
  case NGTCP2_TRANSPORT_PARAMS_V1:
    memcpy(dest, src,
           offsetof(ngtcp2_transport_params, version_info_present) +
               sizeof(src->version_info_present));

    break;
  }
}

const ngtcp2_transport_params *
ngtcp2_transport_params_convert_to_latest(ngtcp2_transport_params *dest,
                                          int transport_params_version,
                                          const ngtcp2_transport_params *src) {
  if (transport_params_version == NGTCP2_TRANSPORT_PARAMS_VERSION) {
    return src;
  }

  ngtcp2_transport_params_default(dest);

  transport_params_copy(transport_params_version, dest, src);

  return dest;
}

void ngtcp2_transport_params_convert_to_old(
    int transport_params_version, ngtcp2_transport_params *dest,
    const ngtcp2_transport_params *src) {
  assert(transport_params_version != NGTCP2_TRANSPORT_PARAMS_VERSION);

  transport_params_copy(transport_params_version, dest, src);
}
