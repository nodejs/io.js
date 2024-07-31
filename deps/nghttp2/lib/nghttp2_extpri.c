/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2022 nghttp3 contributors
 * Copyright (c) 2022 nghttp2 contributors
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
#include "nghttp2_extpri.h"
#include "nghttp2_http.h"

uint8_t nghttp2_extpri_to_uint8(const nghttp2_extpri *extpri) {
  return (uint8_t)((uint32_t)extpri->inc << 7 | extpri->urgency);
}

void nghttp2_extpri_from_uint8(nghttp2_extpri *extpri, uint8_t u8extpri) {
  extpri->urgency = nghttp2_extpri_uint8_urgency(u8extpri);
  extpri->inc = nghttp2_extpri_uint8_inc(u8extpri);
}

int nghttp2_extpri_parse_priority(nghttp2_extpri *extpri, const uint8_t *value,
                                  size_t len) {
  return nghttp2_http_parse_priority(extpri, value, len);
}
