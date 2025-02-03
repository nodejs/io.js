/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2013 Tatsuhiro Tsujikawa
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
#include "nghttp2_frame.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>

#include "nghttp2_helper.h"
#include "nghttp2_net.h"
#include "nghttp2_priority_spec.h"
#include "nghttp2_debug.h"

void nghttp2_frame_pack_frame_hd(uint8_t *buf, const nghttp2_frame_hd *hd) {
  nghttp2_put_uint32be(&buf[0], (uint32_t)(hd->length << 8));
  buf[3] = hd->type;
  buf[4] = hd->flags;
  nghttp2_put_uint32be(&buf[5], (uint32_t)hd->stream_id);
  /* ignore hd->reserved for now */
}

void nghttp2_frame_unpack_frame_hd(nghttp2_frame_hd *hd, const uint8_t *buf) {
  hd->length = nghttp2_get_uint32(&buf[0]) >> 8;
  hd->type = buf[3];
  hd->flags = buf[4];
  hd->stream_id = nghttp2_get_uint32(&buf[5]) & NGHTTP2_STREAM_ID_MASK;
  hd->reserved = 0;
}

void nghttp2_frame_hd_init(nghttp2_frame_hd *hd, size_t length, uint8_t type,
                           uint8_t flags, int32_t stream_id) {
  hd->length = length;
  hd->type = type;
  hd->flags = flags;
  hd->stream_id = stream_id;
  hd->reserved = 0;
}

void nghttp2_frame_headers_init(nghttp2_headers *frame, uint8_t flags,
                                int32_t stream_id, nghttp2_headers_category cat,
                                const nghttp2_priority_spec *pri_spec,
                                nghttp2_nv *nva, size_t nvlen) {
  nghttp2_frame_hd_init(&frame->hd, 0, NGHTTP2_HEADERS, flags, stream_id);
  frame->padlen = 0;
  frame->nva = nva;
  frame->nvlen = nvlen;
  frame->cat = cat;

  if (pri_spec) {
    frame->pri_spec = *pri_spec;
  } else {
    nghttp2_priority_spec_default_init(&frame->pri_spec);
  }
}

void nghttp2_frame_headers_free(nghttp2_headers *frame, nghttp2_mem *mem) {
  nghttp2_nv_array_del(frame->nva, mem);
}

void nghttp2_frame_priority_init(nghttp2_priority *frame, int32_t stream_id,
                                 const nghttp2_priority_spec *pri_spec) {
  nghttp2_frame_hd_init(&frame->hd, NGHTTP2_PRIORITY_SPECLEN, NGHTTP2_PRIORITY,
                        NGHTTP2_FLAG_NONE, stream_id);
  frame->pri_spec = *pri_spec;
}

void nghttp2_frame_priority_free(nghttp2_priority *frame) { (void)frame; }

void nghttp2_frame_rst_stream_init(nghttp2_rst_stream *frame, int32_t stream_id,
                                   uint32_t error_code) {
  nghttp2_frame_hd_init(&frame->hd, 4, NGHTTP2_RST_STREAM, NGHTTP2_FLAG_NONE,
                        stream_id);
  frame->error_code = error_code;
}

void nghttp2_frame_rst_stream_free(nghttp2_rst_stream *frame) { (void)frame; }

void nghttp2_frame_settings_init(nghttp2_settings *frame, uint8_t flags,
                                 nghttp2_settings_entry *iv, size_t niv) {
  nghttp2_frame_hd_init(&frame->hd, niv * NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH,
                        NGHTTP2_SETTINGS, flags, 0);
  frame->niv = niv;
  frame->iv = iv;
}

void nghttp2_frame_settings_free(nghttp2_settings *frame, nghttp2_mem *mem) {
  nghttp2_mem_free(mem, frame->iv);
}

void nghttp2_frame_push_promise_init(nghttp2_push_promise *frame, uint8_t flags,
                                     int32_t stream_id,
                                     int32_t promised_stream_id,
                                     nghttp2_nv *nva, size_t nvlen) {
  nghttp2_frame_hd_init(&frame->hd, 0, NGHTTP2_PUSH_PROMISE, flags, stream_id);
  frame->padlen = 0;
  frame->nva = nva;
  frame->nvlen = nvlen;
  frame->promised_stream_id = promised_stream_id;
  frame->reserved = 0;
}

void nghttp2_frame_push_promise_free(nghttp2_push_promise *frame,
                                     nghttp2_mem *mem) {
  nghttp2_nv_array_del(frame->nva, mem);
}

void nghttp2_frame_ping_init(nghttp2_ping *frame, uint8_t flags,
                             const uint8_t *opaque_data) {
  nghttp2_frame_hd_init(&frame->hd, 8, NGHTTP2_PING, flags, 0);
  if (opaque_data) {
    memcpy(frame->opaque_data, opaque_data, sizeof(frame->opaque_data));
  } else {
    memset(frame->opaque_data, 0, sizeof(frame->opaque_data));
  }
}

void nghttp2_frame_ping_free(nghttp2_ping *frame) { (void)frame; }

void nghttp2_frame_goaway_init(nghttp2_goaway *frame, int32_t last_stream_id,
                               uint32_t error_code, uint8_t *opaque_data,
                               size_t opaque_data_len) {
  nghttp2_frame_hd_init(&frame->hd, 8 + opaque_data_len, NGHTTP2_GOAWAY,
                        NGHTTP2_FLAG_NONE, 0);
  frame->last_stream_id = last_stream_id;
  frame->error_code = error_code;
  frame->opaque_data = opaque_data;
  frame->opaque_data_len = opaque_data_len;
  frame->reserved = 0;
}

void nghttp2_frame_goaway_free(nghttp2_goaway *frame, nghttp2_mem *mem) {
  nghttp2_mem_free(mem, frame->opaque_data);
}

void nghttp2_frame_window_update_init(nghttp2_window_update *frame,
                                      uint8_t flags, int32_t stream_id,
                                      int32_t window_size_increment) {
  nghttp2_frame_hd_init(&frame->hd, 4, NGHTTP2_WINDOW_UPDATE, flags, stream_id);
  frame->window_size_increment = window_size_increment;
  frame->reserved = 0;
}

void nghttp2_frame_window_update_free(nghttp2_window_update *frame) {
  (void)frame;
}

size_t nghttp2_frame_trail_padlen(nghttp2_frame *frame, size_t padlen) {
  /* We have iframe->padlen == 0, but iframe->frame.hd.flags may have
     NGHTTP2_FLAG_PADDED set.  This happens when receiving
     CONTINUATION frame, since we don't reset flags after HEADERS was
     received. */
  if (padlen == 0) {
    return 0;
  }
  return padlen - ((frame->hd.flags & NGHTTP2_FLAG_PADDED) > 0);
}

void nghttp2_frame_data_init(nghttp2_data *frame, uint8_t flags,
                             int32_t stream_id) {
  /* At this moment, the length of DATA frame is unknown */
  nghttp2_frame_hd_init(&frame->hd, 0, NGHTTP2_DATA, flags, stream_id);
  frame->padlen = 0;
}

void nghttp2_frame_data_free(nghttp2_data *frame) { (void)frame; }

void nghttp2_frame_extension_init(nghttp2_extension *frame, uint8_t type,
                                  uint8_t flags, int32_t stream_id,
                                  void *payload) {
  nghttp2_frame_hd_init(&frame->hd, 0, type, flags, stream_id);
  frame->payload = payload;
}

void nghttp2_frame_extension_free(nghttp2_extension *frame) { (void)frame; }

void nghttp2_frame_altsvc_init(nghttp2_extension *frame, int32_t stream_id,
                               uint8_t *origin, size_t origin_len,
                               uint8_t *field_value, size_t field_value_len) {
  nghttp2_ext_altsvc *altsvc;

  nghttp2_frame_hd_init(&frame->hd, 2 + origin_len + field_value_len,
                        NGHTTP2_ALTSVC, NGHTTP2_FLAG_NONE, stream_id);

  altsvc = frame->payload;
  altsvc->origin = origin;
  altsvc->origin_len = origin_len;
  altsvc->field_value = field_value;
  altsvc->field_value_len = field_value_len;
}

void nghttp2_frame_altsvc_free(nghttp2_extension *frame, nghttp2_mem *mem) {
  nghttp2_ext_altsvc *altsvc;

  altsvc = frame->payload;
  if (altsvc == NULL) {
    return;
  }
  /* We use the same buffer for altsvc->origin and
     altsvc->field_value. */
  nghttp2_mem_free(mem, altsvc->origin);
}

void nghttp2_frame_origin_init(nghttp2_extension *frame,
                               nghttp2_origin_entry *ov, size_t nov) {
  nghttp2_ext_origin *origin;
  size_t payloadlen = 0;
  size_t i;

  for (i = 0; i < nov; ++i) {
    payloadlen += 2 + ov[i].origin_len;
  }

  nghttp2_frame_hd_init(&frame->hd, payloadlen, NGHTTP2_ORIGIN,
                        NGHTTP2_FLAG_NONE, 0);

  origin = frame->payload;
  origin->ov = ov;
  origin->nov = nov;
}

void nghttp2_frame_origin_free(nghttp2_extension *frame, nghttp2_mem *mem) {
  nghttp2_ext_origin *origin;

  origin = frame->payload;
  if (origin == NULL) {
    return;
  }
  /* We use the same buffer for all resources pointed by the field of
     origin directly or indirectly. */
  nghttp2_mem_free(mem, origin->ov);
}

void nghttp2_frame_priority_update_init(nghttp2_extension *frame,
                                        int32_t stream_id, uint8_t *field_value,
                                        size_t field_value_len) {
  nghttp2_ext_priority_update *priority_update;

  nghttp2_frame_hd_init(&frame->hd, 4 + field_value_len,
                        NGHTTP2_PRIORITY_UPDATE, NGHTTP2_FLAG_NONE, 0);

  priority_update = frame->payload;
  priority_update->stream_id = stream_id;
  priority_update->field_value = field_value;
  priority_update->field_value_len = field_value_len;
}

void nghttp2_frame_priority_update_free(nghttp2_extension *frame,
                                        nghttp2_mem *mem) {
  nghttp2_ext_priority_update *priority_update;

  priority_update = frame->payload;
  if (priority_update == NULL) {
    return;
  }
  nghttp2_mem_free(mem, priority_update->field_value);
}

size_t nghttp2_frame_priority_len(uint8_t flags) {
  if (flags & NGHTTP2_FLAG_PRIORITY) {
    return NGHTTP2_PRIORITY_SPECLEN;
  }

  return 0;
}

size_t nghttp2_frame_headers_payload_nv_offset(nghttp2_headers *frame) {
  return nghttp2_frame_priority_len(frame->hd.flags);
}

/*
 * Call this function after payload was serialized, but not before
 * changing buf->pos and serializing frame header.
 *
 * This function assumes bufs->cur points to the last buf chain of the
 * frame(s).
 *
 * This function serializes frame header for HEADERS/PUSH_PROMISE and
 * handles their successive CONTINUATION frames.
 *
 * We don't process any padding here.
 */
static int frame_pack_headers_shared(nghttp2_bufs *bufs,
                                     nghttp2_frame_hd *frame_hd) {
  nghttp2_buf *buf;
  nghttp2_buf_chain *ci, *ce;
  nghttp2_frame_hd hd;

  buf = &bufs->head->buf;

  hd = *frame_hd;
  hd.length = nghttp2_buf_len(buf);

  DEBUGF("send: HEADERS/PUSH_PROMISE, payloadlen=%zu\n", hd.length);

  /* We have multiple frame buffers, which means one or more
     CONTINUATION frame is involved. Remove END_HEADERS flag from the
     first frame. */
  if (bufs->head != bufs->cur) {
    hd.flags = (uint8_t)(hd.flags & ~NGHTTP2_FLAG_END_HEADERS);
  }

  buf->pos -= NGHTTP2_FRAME_HDLEN;
  nghttp2_frame_pack_frame_hd(buf->pos, &hd);

  if (bufs->head != bufs->cur) {
    /* 2nd and later frames are CONTINUATION frames. */
    hd.type = NGHTTP2_CONTINUATION;
    /* We don't have no flags except for last CONTINUATION */
    hd.flags = NGHTTP2_FLAG_NONE;

    ce = bufs->cur;

    for (ci = bufs->head->next; ci != ce; ci = ci->next) {
      buf = &ci->buf;

      hd.length = nghttp2_buf_len(buf);

      DEBUGF("send: int CONTINUATION, payloadlen=%zu\n", hd.length);

      buf->pos -= NGHTTP2_FRAME_HDLEN;
      nghttp2_frame_pack_frame_hd(buf->pos, &hd);
    }

    buf = &ci->buf;
    hd.length = nghttp2_buf_len(buf);
    /* Set END_HEADERS flag for last CONTINUATION */
    hd.flags = NGHTTP2_FLAG_END_HEADERS;

    DEBUGF("send: last CONTINUATION, payloadlen=%zu\n", hd.length);

    buf->pos -= NGHTTP2_FRAME_HDLEN;
    nghttp2_frame_pack_frame_hd(buf->pos, &hd);
  }

  return 0;
}

int nghttp2_frame_pack_headers(nghttp2_bufs *bufs, nghttp2_headers *frame,
                               nghttp2_hd_deflater *deflater) {
  size_t nv_offset;
  int rv;
  nghttp2_buf *buf;

  assert(bufs->head == bufs->cur);

  nv_offset = nghttp2_frame_headers_payload_nv_offset(frame);

  buf = &bufs->cur->buf;

  buf->pos += nv_offset;
  buf->last = buf->pos;

  /* This call will adjust buf->last to the correct position */
  rv = nghttp2_hd_deflate_hd_bufs(deflater, bufs, frame->nva, frame->nvlen);

  if (rv == NGHTTP2_ERR_BUFFER_ERROR) {
    rv = NGHTTP2_ERR_HEADER_COMP;
  }

  buf->pos -= nv_offset;

  if (rv != 0) {
    return rv;
  }

  if (frame->hd.flags & NGHTTP2_FLAG_PRIORITY) {
    nghttp2_frame_pack_priority_spec(buf->pos, &frame->pri_spec);
  }

  frame->padlen = 0;
  frame->hd.length = nghttp2_bufs_len(bufs);

  return frame_pack_headers_shared(bufs, &frame->hd);
}

void nghttp2_frame_pack_priority_spec(uint8_t *buf,
                                      const nghttp2_priority_spec *pri_spec) {
  nghttp2_put_uint32be(buf, (uint32_t)pri_spec->stream_id);
  if (pri_spec->exclusive) {
    buf[0] |= 0x80;
  }
  buf[4] = (uint8_t)(pri_spec->weight - 1);
}

void nghttp2_frame_unpack_priority_spec(nghttp2_priority_spec *pri_spec,
                                        const uint8_t *payload) {
  int32_t dep_stream_id;
  uint8_t exclusive;
  int32_t weight;

  dep_stream_id = nghttp2_get_uint32(payload) & NGHTTP2_STREAM_ID_MASK;
  exclusive = (payload[0] & 0x80) > 0;
  weight = payload[4] + 1;

  nghttp2_priority_spec_init(pri_spec, dep_stream_id, weight, exclusive);
}

void nghttp2_frame_unpack_headers_payload(nghttp2_headers *frame,
                                          const uint8_t *payload) {
  if (frame->hd.flags & NGHTTP2_FLAG_PRIORITY) {
    nghttp2_frame_unpack_priority_spec(&frame->pri_spec, payload);
  } else {
    nghttp2_priority_spec_default_init(&frame->pri_spec);
  }

  frame->nva = NULL;
  frame->nvlen = 0;
}

void nghttp2_frame_pack_priority(nghttp2_bufs *bufs, nghttp2_priority *frame) {
  nghttp2_buf *buf;

  assert(bufs->head == bufs->cur);

  buf = &bufs->head->buf;

  assert(nghttp2_buf_avail(buf) >= NGHTTP2_PRIORITY_SPECLEN);

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  nghttp2_frame_pack_priority_spec(buf->last, &frame->pri_spec);

  buf->last += NGHTTP2_PRIORITY_SPECLEN;
}

void nghttp2_frame_unpack_priority_payload(nghttp2_priority *frame,
                                           const uint8_t *payload) {
  nghttp2_frame_unpack_priority_spec(&frame->pri_spec, payload);
}

void nghttp2_frame_pack_rst_stream(nghttp2_bufs *bufs,
                                   nghttp2_rst_stream *frame) {
  nghttp2_buf *buf;

  assert(bufs->head == bufs->cur);

  buf = &bufs->head->buf;

  assert(nghttp2_buf_avail(buf) >= 4);

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  nghttp2_put_uint32be(buf->last, frame->error_code);
  buf->last += 4;
}

void nghttp2_frame_unpack_rst_stream_payload(nghttp2_rst_stream *frame,
                                             const uint8_t *payload) {
  frame->error_code = nghttp2_get_uint32(payload);
}

int nghttp2_frame_pack_settings(nghttp2_bufs *bufs, nghttp2_settings *frame) {
  nghttp2_buf *buf;

  assert(bufs->head == bufs->cur);

  buf = &bufs->head->buf;

  if (nghttp2_buf_avail(buf) < frame->hd.length) {
    return NGHTTP2_ERR_FRAME_SIZE_ERROR;
  }

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  buf->last +=
    nghttp2_frame_pack_settings_payload(buf->last, frame->iv, frame->niv);

  return 0;
}

size_t nghttp2_frame_pack_settings_payload(uint8_t *buf,
                                           const nghttp2_settings_entry *iv,
                                           size_t niv) {
  size_t i;
  for (i = 0; i < niv; ++i, buf += NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH) {
    nghttp2_put_uint16be(buf, (uint16_t)iv[i].settings_id);
    nghttp2_put_uint32be(buf + 2, iv[i].value);
  }
  return NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH * niv;
}

void nghttp2_frame_unpack_settings_payload(nghttp2_settings *frame,
                                           nghttp2_settings_entry *iv,
                                           size_t niv) {
  frame->iv = iv;
  frame->niv = niv;
}

void nghttp2_frame_unpack_settings_entry(nghttp2_settings_entry *iv,
                                         const uint8_t *payload) {
  iv->settings_id = nghttp2_get_uint16(&payload[0]);
  iv->value = nghttp2_get_uint32(&payload[2]);
}

int nghttp2_frame_unpack_settings_payload2(nghttp2_settings_entry **iv_ptr,
                                           size_t *niv_ptr,
                                           const uint8_t *payload,
                                           size_t payloadlen,
                                           nghttp2_mem *mem) {
  size_t i;

  *niv_ptr = payloadlen / NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH;

  if (*niv_ptr == 0) {
    *iv_ptr = NULL;

    return 0;
  }

  *iv_ptr =
    nghttp2_mem_malloc(mem, (*niv_ptr) * sizeof(nghttp2_settings_entry));

  if (*iv_ptr == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  for (i = 0; i < *niv_ptr; ++i) {
    size_t off = i * NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH;
    nghttp2_frame_unpack_settings_entry(&(*iv_ptr)[i], &payload[off]);
  }

  return 0;
}

int nghttp2_frame_pack_push_promise(nghttp2_bufs *bufs,
                                    nghttp2_push_promise *frame,
                                    nghttp2_hd_deflater *deflater) {
  size_t nv_offset = 4;
  int rv;
  nghttp2_buf *buf;

  assert(bufs->head == bufs->cur);

  buf = &bufs->cur->buf;

  buf->pos += nv_offset;
  buf->last = buf->pos;

  /* This call will adjust buf->last to the correct position */
  rv = nghttp2_hd_deflate_hd_bufs(deflater, bufs, frame->nva, frame->nvlen);

  if (rv == NGHTTP2_ERR_BUFFER_ERROR) {
    rv = NGHTTP2_ERR_HEADER_COMP;
  }

  buf->pos -= nv_offset;

  if (rv != 0) {
    return rv;
  }

  nghttp2_put_uint32be(buf->pos, (uint32_t)frame->promised_stream_id);

  frame->padlen = 0;
  frame->hd.length = nghttp2_bufs_len(bufs);

  return frame_pack_headers_shared(bufs, &frame->hd);
}

void nghttp2_frame_unpack_push_promise_payload(nghttp2_push_promise *frame,
                                               const uint8_t *payload) {
  frame->promised_stream_id =
    nghttp2_get_uint32(payload) & NGHTTP2_STREAM_ID_MASK;
  frame->nva = NULL;
  frame->nvlen = 0;
}

void nghttp2_frame_pack_ping(nghttp2_bufs *bufs, nghttp2_ping *frame) {
  nghttp2_buf *buf;

  assert(bufs->head == bufs->cur);

  buf = &bufs->head->buf;

  assert(nghttp2_buf_avail(buf) >= 8);

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  buf->last =
    nghttp2_cpymem(buf->last, frame->opaque_data, sizeof(frame->opaque_data));
}

void nghttp2_frame_unpack_ping_payload(nghttp2_ping *frame,
                                       const uint8_t *payload) {
  memcpy(frame->opaque_data, payload, sizeof(frame->opaque_data));
}

int nghttp2_frame_pack_goaway(nghttp2_bufs *bufs, nghttp2_goaway *frame) {
  int rv;
  nghttp2_buf *buf;

  assert(bufs->head == bufs->cur);

  buf = &bufs->head->buf;

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  nghttp2_put_uint32be(buf->last, (uint32_t)frame->last_stream_id);
  buf->last += 4;

  nghttp2_put_uint32be(buf->last, frame->error_code);
  buf->last += 4;

  rv = nghttp2_bufs_add(bufs, frame->opaque_data, frame->opaque_data_len);

  if (rv == NGHTTP2_ERR_BUFFER_ERROR) {
    return NGHTTP2_ERR_FRAME_SIZE_ERROR;
  }

  if (rv != 0) {
    return rv;
  }

  return 0;
}

void nghttp2_frame_unpack_goaway_payload(nghttp2_goaway *frame,
                                         const uint8_t *payload,
                                         uint8_t *var_gift_payload,
                                         size_t var_gift_payloadlen) {
  frame->last_stream_id = nghttp2_get_uint32(payload) & NGHTTP2_STREAM_ID_MASK;
  frame->error_code = nghttp2_get_uint32(payload + 4);

  frame->opaque_data = var_gift_payload;
  frame->opaque_data_len = var_gift_payloadlen;
}

int nghttp2_frame_unpack_goaway_payload2(nghttp2_goaway *frame,
                                         const uint8_t *payload,
                                         size_t payloadlen, nghttp2_mem *mem) {
  uint8_t *var_gift_payload;
  size_t var_gift_payloadlen;

  if (payloadlen > 8) {
    var_gift_payloadlen = payloadlen - 8;
  } else {
    var_gift_payloadlen = 0;
  }

  if (!var_gift_payloadlen) {
    var_gift_payload = NULL;
  } else {
    var_gift_payload = nghttp2_mem_malloc(mem, var_gift_payloadlen);

    if (var_gift_payload == NULL) {
      return NGHTTP2_ERR_NOMEM;
    }

    memcpy(var_gift_payload, payload + 8, var_gift_payloadlen);
  }

  nghttp2_frame_unpack_goaway_payload(frame, payload, var_gift_payload,
                                      var_gift_payloadlen);

  return 0;
}

void nghttp2_frame_pack_window_update(nghttp2_bufs *bufs,
                                      nghttp2_window_update *frame) {
  nghttp2_buf *buf;

  assert(bufs->head == bufs->cur);

  buf = &bufs->head->buf;

  assert(nghttp2_buf_avail(buf) >= 4);

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  nghttp2_put_uint32be(buf->last, (uint32_t)frame->window_size_increment);
  buf->last += 4;
}

void nghttp2_frame_unpack_window_update_payload(nghttp2_window_update *frame,
                                                const uint8_t *payload) {
  frame->window_size_increment =
    nghttp2_get_uint32(payload) & NGHTTP2_WINDOW_SIZE_INCREMENT_MASK;
}

void nghttp2_frame_pack_altsvc(nghttp2_bufs *bufs, nghttp2_extension *frame) {
  int rv;
  nghttp2_buf *buf;
  nghttp2_ext_altsvc *altsvc;

  /* This is required with --disable-assert. */
  (void)rv;

  altsvc = frame->payload;

  buf = &bufs->head->buf;

  assert(nghttp2_buf_avail(buf) >=
         2 + altsvc->origin_len + altsvc->field_value_len);

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  nghttp2_put_uint16be(buf->last, (uint16_t)altsvc->origin_len);
  buf->last += 2;

  rv = nghttp2_bufs_add(bufs, altsvc->origin, altsvc->origin_len);

  assert(rv == 0);

  rv = nghttp2_bufs_add(bufs, altsvc->field_value, altsvc->field_value_len);

  assert(rv == 0);
}

void nghttp2_frame_unpack_altsvc_payload(nghttp2_extension *frame,
                                         size_t origin_len, uint8_t *payload,
                                         size_t payloadlen) {
  nghttp2_ext_altsvc *altsvc;
  uint8_t *p;

  altsvc = frame->payload;
  p = payload;

  altsvc->origin = p;

  p += origin_len;

  altsvc->origin_len = origin_len;

  altsvc->field_value = p;
  altsvc->field_value_len = (size_t)(payload + payloadlen - p);
}

int nghttp2_frame_unpack_altsvc_payload2(nghttp2_extension *frame,
                                         const uint8_t *payload,
                                         size_t payloadlen, nghttp2_mem *mem) {
  uint8_t *buf;
  size_t origin_len;

  if (payloadlen < 2) {
    return NGHTTP2_FRAME_SIZE_ERROR;
  }

  origin_len = nghttp2_get_uint16(payload);

  buf = nghttp2_mem_malloc(mem, payloadlen - 2);
  if (!buf) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_cpymem(buf, payload + 2, payloadlen - 2);

  nghttp2_frame_unpack_altsvc_payload(frame, origin_len, buf, payloadlen - 2);

  return 0;
}

int nghttp2_frame_pack_origin(nghttp2_bufs *bufs, nghttp2_extension *frame) {
  nghttp2_buf *buf;
  nghttp2_ext_origin *origin;
  nghttp2_origin_entry *orig;
  size_t i;

  origin = frame->payload;

  buf = &bufs->head->buf;

  if (nghttp2_buf_avail(buf) < frame->hd.length) {
    return NGHTTP2_ERR_FRAME_SIZE_ERROR;
  }

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  for (i = 0; i < origin->nov; ++i) {
    orig = &origin->ov[i];
    nghttp2_put_uint16be(buf->last, (uint16_t)orig->origin_len);
    buf->last += 2;
    buf->last = nghttp2_cpymem(buf->last, orig->origin, orig->origin_len);
  }

  assert(nghttp2_buf_len(buf) == NGHTTP2_FRAME_HDLEN + frame->hd.length);

  return 0;
}

int nghttp2_frame_unpack_origin_payload(nghttp2_extension *frame,
                                        const uint8_t *payload,
                                        size_t payloadlen, nghttp2_mem *mem) {
  nghttp2_ext_origin *origin;
  const uint8_t *p, *end;
  uint8_t *dst;
  size_t originlen;
  nghttp2_origin_entry *ov;
  size_t nov = 0;
  size_t len = 0;

  origin = frame->payload;
  p = end = payload;
  if (payloadlen) {
    end += payloadlen;
  }

  for (; p != end;) {
    if (end - p < 2) {
      return NGHTTP2_ERR_FRAME_SIZE_ERROR;
    }
    originlen = nghttp2_get_uint16(p);
    p += 2;
    if (originlen == 0) {
      continue;
    }
    if (originlen > (size_t)(end - p)) {
      return NGHTTP2_ERR_FRAME_SIZE_ERROR;
    }
    p += originlen;
    /* 1 for terminal NULL */
    len += originlen + 1;
    ++nov;
  }

  if (nov == 0) {
    origin->ov = NULL;
    origin->nov = 0;

    return 0;
  }

  len += nov * sizeof(nghttp2_origin_entry);

  ov = nghttp2_mem_malloc(mem, len);
  if (ov == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  origin->ov = ov;
  origin->nov = nov;

  dst = (uint8_t *)ov + nov * sizeof(nghttp2_origin_entry);
  p = payload;

  for (; p != end;) {
    originlen = nghttp2_get_uint16(p);
    p += 2;
    if (originlen == 0) {
      continue;
    }
    ov->origin = dst;
    ov->origin_len = originlen;
    dst = nghttp2_cpymem(dst, p, originlen);
    *dst++ = '\0';
    p += originlen;
    ++ov;
  }

  return 0;
}

void nghttp2_frame_pack_priority_update(nghttp2_bufs *bufs,
                                        nghttp2_extension *frame) {
  int rv;
  nghttp2_buf *buf;
  nghttp2_ext_priority_update *priority_update;

  /* This is required with --disable-assert. */
  (void)rv;

  priority_update = frame->payload;

  buf = &bufs->head->buf;

  assert(nghttp2_buf_avail(buf) >= 4 + priority_update->field_value_len);

  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  nghttp2_put_uint32be(buf->last, (uint32_t)priority_update->stream_id);
  buf->last += 4;

  rv = nghttp2_bufs_add(bufs, priority_update->field_value,
                        priority_update->field_value_len);

  assert(rv == 0);
}

void nghttp2_frame_unpack_priority_update_payload(nghttp2_extension *frame,
                                                  uint8_t *payload,
                                                  size_t payloadlen) {
  nghttp2_ext_priority_update *priority_update;

  assert(payloadlen >= 4);

  priority_update = frame->payload;

  priority_update->stream_id =
    nghttp2_get_uint32(payload) & NGHTTP2_STREAM_ID_MASK;

  if (payloadlen > 4) {
    priority_update->field_value = payload + 4;
    priority_update->field_value_len = payloadlen - 4;
  } else {
    priority_update->field_value = NULL;
    priority_update->field_value_len = 0;
  }
}

nghttp2_settings_entry *nghttp2_frame_iv_copy(const nghttp2_settings_entry *iv,
                                              size_t niv, nghttp2_mem *mem) {
  nghttp2_settings_entry *iv_copy;
  size_t len = niv * sizeof(nghttp2_settings_entry);

  if (len == 0) {
    return NULL;
  }

  iv_copy = nghttp2_mem_malloc(mem, len);

  if (iv_copy == NULL) {
    return NULL;
  }

  memcpy(iv_copy, iv, len);

  return iv_copy;
}

int nghttp2_nv_equal(const nghttp2_nv *a, const nghttp2_nv *b) {
  if (a->namelen != b->namelen || a->valuelen != b->valuelen) {
    return 0;
  }

  if (a->name == NULL || b->name == NULL) {
    assert(a->namelen == 0);
    assert(b->namelen == 0);
  } else if (memcmp(a->name, b->name, a->namelen) != 0) {
    return 0;
  }

  if (a->value == NULL || b->value == NULL) {
    assert(a->valuelen == 0);
    assert(b->valuelen == 0);
  } else if (memcmp(a->value, b->value, a->valuelen) != 0) {
    return 0;
  }

  return 1;
}

void nghttp2_nv_array_del(nghttp2_nv *nva, nghttp2_mem *mem) {
  nghttp2_mem_free(mem, nva);
}

static int bytes_compar(const uint8_t *a, size_t alen, const uint8_t *b,
                        size_t blen) {
  int rv;

  if (alen == blen) {
    return memcmp(a, b, alen);
  }

  if (alen < blen) {
    rv = memcmp(a, b, alen);

    if (rv == 0) {
      return -1;
    }

    return rv;
  }

  rv = memcmp(a, b, blen);

  if (rv == 0) {
    return 1;
  }

  return rv;
}

int nghttp2_nv_compare_name(const nghttp2_nv *lhs, const nghttp2_nv *rhs) {
  return bytes_compar(lhs->name, lhs->namelen, rhs->name, rhs->namelen);
}

static int nv_compar(const void *lhs, const void *rhs) {
  const nghttp2_nv *a = (const nghttp2_nv *)lhs;
  const nghttp2_nv *b = (const nghttp2_nv *)rhs;
  int rv;

  rv = bytes_compar(a->name, a->namelen, b->name, b->namelen);

  if (rv == 0) {
    return bytes_compar(a->value, a->valuelen, b->value, b->valuelen);
  }

  return rv;
}

void nghttp2_nv_array_sort(nghttp2_nv *nva, size_t nvlen) {
  qsort(nva, nvlen, sizeof(nghttp2_nv), nv_compar);
}

int nghttp2_nv_array_copy(nghttp2_nv **nva_ptr, const nghttp2_nv *nva,
                          size_t nvlen, nghttp2_mem *mem) {
  size_t i;
  uint8_t *data = NULL;
  size_t buflen = 0;
  nghttp2_nv *p;

  if (nvlen == 0) {
    *nva_ptr = NULL;

    return 0;
  }

  for (i = 0; i < nvlen; ++i) {
    /* + 1 for null-termination */
    if ((nva[i].flags & NGHTTP2_NV_FLAG_NO_COPY_NAME) == 0) {
      buflen += nva[i].namelen + 1;
    }
    if ((nva[i].flags & NGHTTP2_NV_FLAG_NO_COPY_VALUE) == 0) {
      buflen += nva[i].valuelen + 1;
    }
  }

  buflen += sizeof(nghttp2_nv) * nvlen;

  *nva_ptr = nghttp2_mem_malloc(mem, buflen);

  if (*nva_ptr == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  p = *nva_ptr;
  data = (uint8_t *)(*nva_ptr) + sizeof(nghttp2_nv) * nvlen;

  for (i = 0; i < nvlen; ++i) {
    p->flags = nva[i].flags;

    if (nva[i].flags & NGHTTP2_NV_FLAG_NO_COPY_NAME) {
      p->name = nva[i].name;
      p->namelen = nva[i].namelen;
    } else {
      if (nva[i].namelen) {
        memcpy(data, nva[i].name, nva[i].namelen);
      }
      p->name = data;
      p->namelen = nva[i].namelen;
      data[p->namelen] = '\0';
      nghttp2_downcase(p->name, p->namelen);
      data += nva[i].namelen + 1;
    }

    if (nva[i].flags & NGHTTP2_NV_FLAG_NO_COPY_VALUE) {
      p->value = nva[i].value;
      p->valuelen = nva[i].valuelen;
    } else {
      if (nva[i].valuelen) {
        memcpy(data, nva[i].value, nva[i].valuelen);
      }
      p->value = data;
      p->valuelen = nva[i].valuelen;
      data[p->valuelen] = '\0';
      data += nva[i].valuelen + 1;
    }

    ++p;
  }
  return 0;
}

int nghttp2_iv_check(const nghttp2_settings_entry *iv, size_t niv) {
  size_t i;
  for (i = 0; i < niv; ++i) {
    switch (iv[i].settings_id) {
    case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
      break;
    case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
      break;
    case NGHTTP2_SETTINGS_ENABLE_PUSH:
      if (iv[i].value != 0 && iv[i].value != 1) {
        return 0;
      }
      break;
    case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
      if (iv[i].value > (uint32_t)NGHTTP2_MAX_WINDOW_SIZE) {
        return 0;
      }
      break;
    case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:
      if (iv[i].value < NGHTTP2_MAX_FRAME_SIZE_MIN ||
          iv[i].value > NGHTTP2_MAX_FRAME_SIZE_MAX) {
        return 0;
      }
      break;
    case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
      break;
    case NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL:
      if (iv[i].value != 0 && iv[i].value != 1) {
        return 0;
      }
      break;
    case NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES:
      if (iv[i].value != 0 && iv[i].value != 1) {
        return 0;
      }
      break;
    }
  }
  return 1;
}

static void frame_set_pad(nghttp2_buf *buf, size_t padlen, int framehd_only) {
  size_t trail_padlen;
  size_t newlen;

  DEBUGF("send: padlen=%zu, shift left 1 bytes\n", padlen);

  memmove(buf->pos - 1, buf->pos, NGHTTP2_FRAME_HDLEN);

  --buf->pos;

  buf->pos[4] |= NGHTTP2_FLAG_PADDED;

  newlen = (nghttp2_get_uint32(buf->pos) >> 8) + padlen;
  nghttp2_put_uint32be(buf->pos, (uint32_t)((newlen << 8) + buf->pos[3]));

  if (framehd_only) {
    return;
  }

  trail_padlen = padlen - 1;
  buf->pos[NGHTTP2_FRAME_HDLEN] = (uint8_t)trail_padlen;

  /* zero out padding */
  memset(buf->last, 0, trail_padlen);
  /* extend buffers trail_padlen bytes, since we ate previous padlen -
     trail_padlen byte(s) */
  buf->last += trail_padlen;
}

void nghttp2_frame_add_pad(nghttp2_bufs *bufs, nghttp2_frame_hd *hd,
                           size_t padlen, int framehd_only) {
  nghttp2_buf *buf;

  if (padlen == 0) {
    DEBUGF("send: padlen = 0, nothing to do\n");

    return;
  }

  /*
   * We have arranged bufs like this:
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * | |Frame header     | Frame payload...                          :
   * +-+-----------------+-------------------------------------------+
   * | |Frame header     | Frame payload...                          :
   * +-+-----------------+-------------------------------------------+
   * | |Frame header     | Frame payload...                          :
   * +-+-----------------+-------------------------------------------+
   *
   * We arranged padding so that it is included in the first frame
   * completely.  For padded frame, we are going to adjust buf->pos of
   * frame which includes padding and serialize (memmove) frame header
   * in the correct position.  Also extends buf->last to include
   * padding.
   */

  buf = &bufs->head->buf;

  assert(nghttp2_buf_avail(buf) >= padlen - 1);

  frame_set_pad(buf, padlen, framehd_only);

  hd->length += padlen;
  hd->flags |= NGHTTP2_FLAG_PADDED;

  DEBUGF("send: final payloadlen=%zu, padlen=%zu\n", hd->length, padlen);
}
