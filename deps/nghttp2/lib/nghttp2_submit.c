/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012, 2013 Tatsuhiro Tsujikawa
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
#include "nghttp2_submit.h"

#include <string.h>
#include <assert.h>

#include "nghttp2_session.h"
#include "nghttp2_frame.h"
#include "nghttp2_helper.h"
#include "nghttp2_priority_spec.h"

/*
 * Detects the dependency error, that is stream attempted to depend on
 * itself.  If |stream_id| is -1, we use session->next_stream_id as
 * stream ID.
 *
 * This function returns 0 if it succeeds, or one of the following
 * error codes:
 *
 * NGHTTP2_ERR_INVALID_ARGUMENT
 *   Stream attempted to depend on itself.
 */
static int detect_self_dependency(nghttp2_session *session, int32_t stream_id,
                                  const nghttp2_priority_spec *pri_spec) {
  assert(pri_spec);

  if (stream_id == -1) {
    if ((int32_t)session->next_stream_id == pri_spec->stream_id) {
      return NGHTTP2_ERR_INVALID_ARGUMENT;
    }
    return 0;
  }

  if (stream_id == pri_spec->stream_id) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  return 0;
}

/* This function takes ownership of |nva_copy|. Regardless of the
   return value, the caller must not free |nva_copy| after this
   function returns. */
static int32_t submit_headers_shared(nghttp2_session *session, uint8_t flags,
                                     int32_t stream_id,
                                     const nghttp2_priority_spec *pri_spec,
                                     nghttp2_nv *nva_copy, size_t nvlen,
                                     const nghttp2_data_provider_wrap *dpw,
                                     void *stream_user_data) {
  int rv;
  uint8_t flags_copy;
  nghttp2_outbound_item *item = NULL;
  nghttp2_frame *frame = NULL;
  nghttp2_headers_category hcat;
  nghttp2_mem *mem;

  mem = &session->mem;

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    rv = NGHTTP2_ERR_NOMEM;
    goto fail;
  }

  nghttp2_outbound_item_init(item);

  if (dpw != NULL && dpw->data_prd.read_callback != NULL) {
    item->aux_data.headers.dpw = *dpw;
  }

  item->aux_data.headers.stream_user_data = stream_user_data;

  flags_copy =
      (uint8_t)((flags & (NGHTTP2_FLAG_END_STREAM | NGHTTP2_FLAG_PRIORITY)) |
                NGHTTP2_FLAG_END_HEADERS);

  if (stream_id == -1) {
    if (session->next_stream_id > INT32_MAX) {
      rv = NGHTTP2_ERR_STREAM_ID_NOT_AVAILABLE;
      goto fail;
    }

    stream_id = (int32_t)session->next_stream_id;
    session->next_stream_id += 2;

    hcat = NGHTTP2_HCAT_REQUEST;
  } else {
    /* More specific categorization will be done later. */
    hcat = NGHTTP2_HCAT_HEADERS;
  }

  frame = &item->frame;

  nghttp2_frame_headers_init(&frame->headers, flags_copy, stream_id, hcat,
                             pri_spec, nva_copy, nvlen);

  rv = nghttp2_session_add_item(session, item);

  if (rv != 0) {
    nghttp2_frame_headers_free(&frame->headers, mem);
    goto fail2;
  }

  if (hcat == NGHTTP2_HCAT_REQUEST) {
    return stream_id;
  }

  return 0;

fail:
  /* nghttp2_frame_headers_init() takes ownership of nva_copy. */
  nghttp2_nv_array_del(nva_copy, mem);
fail2:
  nghttp2_mem_free(mem, item);

  return rv;
}

static int32_t submit_headers_shared_nva(nghttp2_session *session,
                                         uint8_t flags, int32_t stream_id,
                                         const nghttp2_priority_spec *pri_spec,
                                         const nghttp2_nv *nva, size_t nvlen,
                                         const nghttp2_data_provider_wrap *dpw,
                                         void *stream_user_data) {
  int rv;
  nghttp2_nv *nva_copy;
  nghttp2_priority_spec copy_pri_spec;
  nghttp2_mem *mem;

  mem = &session->mem;

  if (pri_spec) {
    copy_pri_spec = *pri_spec;
    nghttp2_priority_spec_normalize_weight(&copy_pri_spec);
  } else {
    nghttp2_priority_spec_default_init(&copy_pri_spec);
  }

  rv = nghttp2_nv_array_copy(&nva_copy, nva, nvlen, mem);
  if (rv < 0) {
    return rv;
  }

  return submit_headers_shared(session, flags, stream_id, &copy_pri_spec,
                               nva_copy, nvlen, dpw, stream_user_data);
}

int nghttp2_submit_trailer(nghttp2_session *session, int32_t stream_id,
                           const nghttp2_nv *nva, size_t nvlen) {
  if (stream_id <= 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  return (int)submit_headers_shared_nva(session, NGHTTP2_FLAG_END_STREAM,
                                        stream_id, NULL, nva, nvlen, NULL,
                                        NULL);
}

int32_t nghttp2_submit_headers(nghttp2_session *session, uint8_t flags,
                               int32_t stream_id,
                               const nghttp2_priority_spec *pri_spec,
                               const nghttp2_nv *nva, size_t nvlen,
                               void *stream_user_data) {
  int rv;

  if (stream_id == -1) {
    if (session->server) {
      return NGHTTP2_ERR_PROTO;
    }
  } else if (stream_id <= 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  flags &= NGHTTP2_FLAG_END_STREAM;

  if (pri_spec && !nghttp2_priority_spec_check_default(pri_spec) &&
      session->remote_settings.no_rfc7540_priorities != 1) {
    rv = detect_self_dependency(session, stream_id, pri_spec);
    if (rv != 0) {
      return rv;
    }

    flags |= NGHTTP2_FLAG_PRIORITY;
  } else {
    pri_spec = NULL;
  }

  return submit_headers_shared_nva(session, flags, stream_id, pri_spec, nva,
                                   nvlen, NULL, stream_user_data);
}

int nghttp2_submit_ping(nghttp2_session *session, uint8_t flags,
                        const uint8_t *opaque_data) {
  flags &= NGHTTP2_FLAG_ACK;
  return nghttp2_session_add_ping(session, flags, opaque_data);
}

int nghttp2_submit_priority(nghttp2_session *session, uint8_t flags,
                            int32_t stream_id,
                            const nghttp2_priority_spec *pri_spec) {
  int rv;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_priority_spec copy_pri_spec;
  nghttp2_mem *mem;
  (void)flags;

  mem = &session->mem;

  if (session->remote_settings.no_rfc7540_priorities == 1) {
    return 0;
  }

  if (stream_id == 0 || pri_spec == NULL) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (stream_id == pri_spec->stream_id) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  copy_pri_spec = *pri_spec;

  nghttp2_priority_spec_normalize_weight(&copy_pri_spec);

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));

  if (item == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_outbound_item_init(item);

  frame = &item->frame;

  nghttp2_frame_priority_init(&frame->priority, stream_id, &copy_pri_spec);

  rv = nghttp2_session_add_item(session, item);

  if (rv != 0) {
    nghttp2_frame_priority_free(&frame->priority);
    nghttp2_mem_free(mem, item);

    return rv;
  }

  return 0;
}

int nghttp2_submit_rst_stream(nghttp2_session *session, uint8_t flags,
                              int32_t stream_id, uint32_t error_code) {
  (void)flags;

  if (stream_id == 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  return nghttp2_session_add_rst_stream(session, stream_id, error_code);
}

int nghttp2_submit_goaway(nghttp2_session *session, uint8_t flags,
                          int32_t last_stream_id, uint32_t error_code,
                          const uint8_t *opaque_data, size_t opaque_data_len) {
  (void)flags;

  if (session->goaway_flags & NGHTTP2_GOAWAY_TERM_ON_SEND) {
    return 0;
  }
  return nghttp2_session_add_goaway(session, last_stream_id, error_code,
                                    opaque_data, opaque_data_len,
                                    NGHTTP2_GOAWAY_AUX_NONE);
}

int nghttp2_submit_shutdown_notice(nghttp2_session *session) {
  if (!session->server) {
    return NGHTTP2_ERR_INVALID_STATE;
  }
  if (session->goaway_flags) {
    return 0;
  }
  return nghttp2_session_add_goaway(session, (1u << 31) - 1, NGHTTP2_NO_ERROR,
                                    NULL, 0,
                                    NGHTTP2_GOAWAY_AUX_SHUTDOWN_NOTICE);
}

int nghttp2_submit_settings(nghttp2_session *session, uint8_t flags,
                            const nghttp2_settings_entry *iv, size_t niv) {
  (void)flags;
  return nghttp2_session_add_settings(session, NGHTTP2_FLAG_NONE, iv, niv);
}

int32_t nghttp2_submit_push_promise(nghttp2_session *session, uint8_t flags,
                                    int32_t stream_id, const nghttp2_nv *nva,
                                    size_t nvlen,
                                    void *promised_stream_user_data) {
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_nv *nva_copy;
  uint8_t flags_copy;
  int32_t promised_stream_id;
  int rv;
  nghttp2_mem *mem;
  (void)flags;

  mem = &session->mem;

  if (stream_id <= 0 || nghttp2_session_is_my_stream_id(session, stream_id)) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (!session->server) {
    return NGHTTP2_ERR_PROTO;
  }

  /* All 32bit signed stream IDs are spent. */
  if (session->next_stream_id > INT32_MAX) {
    return NGHTTP2_ERR_STREAM_ID_NOT_AVAILABLE;
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_outbound_item_init(item);

  item->aux_data.headers.stream_user_data = promised_stream_user_data;

  frame = &item->frame;

  rv = nghttp2_nv_array_copy(&nva_copy, nva, nvlen, mem);
  if (rv < 0) {
    nghttp2_mem_free(mem, item);
    return rv;
  }

  flags_copy = NGHTTP2_FLAG_END_HEADERS;

  promised_stream_id = (int32_t)session->next_stream_id;
  session->next_stream_id += 2;

  nghttp2_frame_push_promise_init(&frame->push_promise, flags_copy, stream_id,
                                  promised_stream_id, nva_copy, nvlen);

  rv = nghttp2_session_add_item(session, item);

  if (rv != 0) {
    nghttp2_frame_push_promise_free(&frame->push_promise, mem);
    nghttp2_mem_free(mem, item);

    return rv;
  }

  return promised_stream_id;
}

int nghttp2_submit_window_update(nghttp2_session *session, uint8_t flags,
                                 int32_t stream_id,
                                 int32_t window_size_increment) {
  int rv;
  nghttp2_stream *stream = 0;
  (void)flags;

  if (window_size_increment == 0) {
    return 0;
  }
  if (stream_id == 0) {
    rv = nghttp2_adjust_local_window_size(
        &session->local_window_size, &session->recv_window_size,
        &session->recv_reduction, &window_size_increment);
    if (rv != 0) {
      return rv;
    }
  } else {
    stream = nghttp2_session_get_stream(session, stream_id);
    if (!stream) {
      return 0;
    }

    rv = nghttp2_adjust_local_window_size(
        &stream->local_window_size, &stream->recv_window_size,
        &stream->recv_reduction, &window_size_increment);
    if (rv != 0) {
      return rv;
    }
  }

  if (window_size_increment > 0) {
    if (stream_id == 0) {
      session->consumed_size =
          nghttp2_max_int32(0, session->consumed_size - window_size_increment);
    } else {
      stream->consumed_size =
          nghttp2_max_int32(0, stream->consumed_size - window_size_increment);
    }

    return nghttp2_session_add_window_update(session, 0, stream_id,
                                             window_size_increment);
  }
  return 0;
}

int nghttp2_session_set_local_window_size(nghttp2_session *session,
                                          uint8_t flags, int32_t stream_id,
                                          int32_t window_size) {
  int32_t window_size_increment;
  nghttp2_stream *stream;
  int rv;
  (void)flags;

  if (window_size < 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (stream_id == 0) {
    window_size_increment = window_size - session->local_window_size;

    if (window_size_increment == 0) {
      return 0;
    }

    if (window_size_increment < 0) {
      return nghttp2_adjust_local_window_size(
          &session->local_window_size, &session->recv_window_size,
          &session->recv_reduction, &window_size_increment);
    }

    rv = nghttp2_increase_local_window_size(
        &session->local_window_size, &session->recv_window_size,
        &session->recv_reduction, &window_size_increment);

    if (rv != 0) {
      return rv;
    }

    if (window_size_increment > 0) {
      return nghttp2_session_add_window_update(session, 0, stream_id,
                                               window_size_increment);
    }

    return nghttp2_session_update_recv_connection_window_size(session, 0);
  } else {
    stream = nghttp2_session_get_stream(session, stream_id);

    if (stream == NULL) {
      return 0;
    }

    window_size_increment = window_size - stream->local_window_size;

    if (window_size_increment == 0) {
      return 0;
    }

    if (window_size_increment < 0) {
      return nghttp2_adjust_local_window_size(
          &stream->local_window_size, &stream->recv_window_size,
          &stream->recv_reduction, &window_size_increment);
    }

    rv = nghttp2_increase_local_window_size(
        &stream->local_window_size, &stream->recv_window_size,
        &stream->recv_reduction, &window_size_increment);

    if (rv != 0) {
      return rv;
    }

    if (window_size_increment > 0) {
      return nghttp2_session_add_window_update(session, 0, stream_id,
                                               window_size_increment);
    }

    return nghttp2_session_update_recv_stream_window_size(session, stream, 0,
                                                          1);
  }
}

int nghttp2_submit_altsvc(nghttp2_session *session, uint8_t flags,
                          int32_t stream_id, const uint8_t *origin,
                          size_t origin_len, const uint8_t *field_value,
                          size_t field_value_len) {
  nghttp2_mem *mem;
  uint8_t *buf, *p;
  uint8_t *origin_copy;
  uint8_t *field_value_copy;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_ext_altsvc *altsvc;
  int rv;
  (void)flags;

  mem = &session->mem;

  if (!session->server) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  if (2 + origin_len + field_value_len > NGHTTP2_MAX_PAYLOADLEN) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (stream_id == 0) {
    if (origin_len == 0) {
      return NGHTTP2_ERR_INVALID_ARGUMENT;
    }
  } else if (origin_len != 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  buf = nghttp2_mem_malloc(mem, origin_len + field_value_len + 2);
  if (buf == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  p = buf;

  origin_copy = p;
  if (origin_len) {
    p = nghttp2_cpymem(p, origin, origin_len);
  }
  *p++ = '\0';

  field_value_copy = p;
  if (field_value_len) {
    p = nghttp2_cpymem(p, field_value, field_value_len);
  }
  *p++ = '\0';

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    rv = NGHTTP2_ERR_NOMEM;
    goto fail_item_malloc;
  }

  nghttp2_outbound_item_init(item);

  item->aux_data.ext.builtin = 1;

  altsvc = &item->ext_frame_payload.altsvc;

  frame = &item->frame;
  frame->ext.payload = altsvc;

  nghttp2_frame_altsvc_init(&frame->ext, stream_id, origin_copy, origin_len,
                            field_value_copy, field_value_len);

  rv = nghttp2_session_add_item(session, item);
  if (rv != 0) {
    nghttp2_frame_altsvc_free(&frame->ext, mem);
    nghttp2_mem_free(mem, item);

    return rv;
  }

  return 0;

fail_item_malloc:
  free(buf);

  return rv;
}

int nghttp2_submit_origin(nghttp2_session *session, uint8_t flags,
                          const nghttp2_origin_entry *ov, size_t nov) {
  nghttp2_mem *mem;
  uint8_t *p;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_ext_origin *origin;
  nghttp2_origin_entry *ov_copy;
  size_t len = 0;
  size_t i;
  int rv;
  (void)flags;

  mem = &session->mem;

  if (!session->server) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  if (nov) {
    for (i = 0; i < nov; ++i) {
      len += ov[i].origin_len;
    }

    if (2 * nov + len > NGHTTP2_MAX_PAYLOADLEN) {
      return NGHTTP2_ERR_INVALID_ARGUMENT;
    }

    /* The last nov is added for terminal NULL character. */
    ov_copy =
        nghttp2_mem_malloc(mem, nov * sizeof(nghttp2_origin_entry) + len + nov);
    if (ov_copy == NULL) {
      return NGHTTP2_ERR_NOMEM;
    }

    p = (uint8_t *)ov_copy + nov * sizeof(nghttp2_origin_entry);

    for (i = 0; i < nov; ++i) {
      ov_copy[i].origin = p;
      ov_copy[i].origin_len = ov[i].origin_len;
      p = nghttp2_cpymem(p, ov[i].origin, ov[i].origin_len);
      *p++ = '\0';
    }

    assert((size_t)(p - (uint8_t *)ov_copy) ==
           nov * sizeof(nghttp2_origin_entry) + len + nov);
  } else {
    ov_copy = NULL;
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    rv = NGHTTP2_ERR_NOMEM;
    goto fail_item_malloc;
  }

  nghttp2_outbound_item_init(item);

  item->aux_data.ext.builtin = 1;

  origin = &item->ext_frame_payload.origin;

  frame = &item->frame;
  frame->ext.payload = origin;

  nghttp2_frame_origin_init(&frame->ext, ov_copy, nov);

  rv = nghttp2_session_add_item(session, item);
  if (rv != 0) {
    nghttp2_frame_origin_free(&frame->ext, mem);
    nghttp2_mem_free(mem, item);

    return rv;
  }

  return 0;

fail_item_malloc:
  free(ov_copy);

  return rv;
}

int nghttp2_submit_priority_update(nghttp2_session *session, uint8_t flags,
                                   int32_t stream_id,
                                   const uint8_t *field_value,
                                   size_t field_value_len) {
  nghttp2_mem *mem;
  uint8_t *buf, *p;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_ext_priority_update *priority_update;
  int rv;
  (void)flags;

  mem = &session->mem;

  if (session->server) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  if (session->remote_settings.no_rfc7540_priorities == 0) {
    return 0;
  }

  if (stream_id == 0 || 4 + field_value_len > NGHTTP2_MAX_PAYLOADLEN) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (field_value_len) {
    buf = nghttp2_mem_malloc(mem, field_value_len + 1);
    if (buf == NULL) {
      return NGHTTP2_ERR_NOMEM;
    }

    p = nghttp2_cpymem(buf, field_value, field_value_len);
    *p = '\0';
  } else {
    buf = NULL;
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    rv = NGHTTP2_ERR_NOMEM;
    goto fail_item_malloc;
  }

  nghttp2_outbound_item_init(item);

  item->aux_data.ext.builtin = 1;

  priority_update = &item->ext_frame_payload.priority_update;

  frame = &item->frame;
  frame->ext.payload = priority_update;

  nghttp2_frame_priority_update_init(&frame->ext, stream_id, buf,
                                     field_value_len);

  rv = nghttp2_session_add_item(session, item);
  if (rv != 0) {
    nghttp2_frame_priority_update_free(&frame->ext, mem);
    nghttp2_mem_free(mem, item);

    return rv;
  }

  return 0;

fail_item_malloc:
  free(buf);

  return rv;
}

static uint8_t set_request_flags(const nghttp2_priority_spec *pri_spec,
                                 const nghttp2_data_provider_wrap *dpw) {
  uint8_t flags = NGHTTP2_FLAG_NONE;
  if (dpw == NULL || dpw->data_prd.read_callback == NULL) {
    flags |= NGHTTP2_FLAG_END_STREAM;
  }

  if (pri_spec) {
    flags |= NGHTTP2_FLAG_PRIORITY;
  }

  return flags;
}

static int32_t submit_request_shared(nghttp2_session *session,
                                     const nghttp2_priority_spec *pri_spec,
                                     const nghttp2_nv *nva, size_t nvlen,
                                     const nghttp2_data_provider_wrap *dpw,
                                     void *stream_user_data) {
  uint8_t flags;
  int rv;

  if (session->server) {
    return NGHTTP2_ERR_PROTO;
  }

  if (pri_spec && !nghttp2_priority_spec_check_default(pri_spec) &&
      session->remote_settings.no_rfc7540_priorities != 1) {
    rv = detect_self_dependency(session, -1, pri_spec);
    if (rv != 0) {
      return rv;
    }
  } else {
    pri_spec = NULL;
  }

  flags = set_request_flags(pri_spec, dpw);

  return submit_headers_shared_nva(session, flags, -1, pri_spec, nva, nvlen,
                                   dpw, stream_user_data);
}

int32_t nghttp2_submit_request(nghttp2_session *session,
                               const nghttp2_priority_spec *pri_spec,
                               const nghttp2_nv *nva, size_t nvlen,
                               const nghttp2_data_provider *data_prd,
                               void *stream_user_data) {
  nghttp2_data_provider_wrap dpw;

  return submit_request_shared(session, pri_spec, nva, nvlen,
                               nghttp2_data_provider_wrap_v1(&dpw, data_prd),
                               stream_user_data);
}

int32_t nghttp2_submit_request2(nghttp2_session *session,
                                const nghttp2_priority_spec *pri_spec,
                                const nghttp2_nv *nva, size_t nvlen,
                                const nghttp2_data_provider2 *data_prd,
                                void *stream_user_data) {
  nghttp2_data_provider_wrap dpw;

  return submit_request_shared(session, pri_spec, nva, nvlen,
                               nghttp2_data_provider_wrap_v2(&dpw, data_prd),
                               stream_user_data);
}

static uint8_t set_response_flags(const nghttp2_data_provider_wrap *dpw) {
  uint8_t flags = NGHTTP2_FLAG_NONE;
  if (dpw == NULL || dpw->data_prd.read_callback == NULL) {
    flags |= NGHTTP2_FLAG_END_STREAM;
  }
  return flags;
}

static int submit_response_shared(nghttp2_session *session, int32_t stream_id,
                                  const nghttp2_nv *nva, size_t nvlen,
                                  const nghttp2_data_provider_wrap *dpw) {
  uint8_t flags;

  if (stream_id <= 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (!session->server) {
    return NGHTTP2_ERR_PROTO;
  }

  flags = set_response_flags(dpw);
  return submit_headers_shared_nva(session, flags, stream_id, NULL, nva, nvlen,
                                   dpw, NULL);
}

int nghttp2_submit_response(nghttp2_session *session, int32_t stream_id,
                            const nghttp2_nv *nva, size_t nvlen,
                            const nghttp2_data_provider *data_prd) {
  nghttp2_data_provider_wrap dpw;

  return submit_response_shared(session, stream_id, nva, nvlen,
                                nghttp2_data_provider_wrap_v1(&dpw, data_prd));
}

int nghttp2_submit_response2(nghttp2_session *session, int32_t stream_id,
                             const nghttp2_nv *nva, size_t nvlen,
                             const nghttp2_data_provider2 *data_prd) {
  nghttp2_data_provider_wrap dpw;

  return submit_response_shared(session, stream_id, nva, nvlen,
                                nghttp2_data_provider_wrap_v2(&dpw, data_prd));
}

int nghttp2_submit_data_shared(nghttp2_session *session, uint8_t flags,
                               int32_t stream_id,
                               const nghttp2_data_provider_wrap *dpw) {
  int rv;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_data_aux_data *aux_data;
  uint8_t nflags = flags & NGHTTP2_FLAG_END_STREAM;
  nghttp2_mem *mem;

  mem = &session->mem;

  if (stream_id == 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_outbound_item_init(item);

  frame = &item->frame;
  aux_data = &item->aux_data.data;
  aux_data->dpw = *dpw;
  aux_data->eof = 0;
  aux_data->flags = nflags;

  /* flags are sent on transmission */
  nghttp2_frame_data_init(&frame->data, NGHTTP2_FLAG_NONE, stream_id);

  rv = nghttp2_session_add_item(session, item);
  if (rv != 0) {
    nghttp2_frame_data_free(&frame->data);
    nghttp2_mem_free(mem, item);
    return rv;
  }
  return 0;
}

int nghttp2_submit_data(nghttp2_session *session, uint8_t flags,
                        int32_t stream_id,
                        const nghttp2_data_provider *data_prd) {
  nghttp2_data_provider_wrap dpw;

  assert(data_prd);

  return nghttp2_submit_data_shared(
      session, flags, stream_id, nghttp2_data_provider_wrap_v1(&dpw, data_prd));
}

int nghttp2_submit_data2(nghttp2_session *session, uint8_t flags,
                         int32_t stream_id,
                         const nghttp2_data_provider2 *data_prd) {
  nghttp2_data_provider_wrap dpw;

  assert(data_prd);

  return nghttp2_submit_data_shared(
      session, flags, stream_id, nghttp2_data_provider_wrap_v2(&dpw, data_prd));
}

ssize_t nghttp2_pack_settings_payload(uint8_t *buf, size_t buflen,
                                      const nghttp2_settings_entry *iv,
                                      size_t niv) {
  return (ssize_t)nghttp2_pack_settings_payload2(buf, buflen, iv, niv);
}

nghttp2_ssize nghttp2_pack_settings_payload2(uint8_t *buf, size_t buflen,
                                             const nghttp2_settings_entry *iv,
                                             size_t niv) {
  if (!nghttp2_iv_check(iv, niv)) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (buflen < (niv * NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH)) {
    return NGHTTP2_ERR_INSUFF_BUFSIZE;
  }

  return (nghttp2_ssize)nghttp2_frame_pack_settings_payload(buf, iv, niv);
}

int nghttp2_submit_extension(nghttp2_session *session, uint8_t type,
                             uint8_t flags, int32_t stream_id, void *payload) {
  int rv;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_mem *mem;

  mem = &session->mem;

  if (type <= NGHTTP2_CONTINUATION) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (!session->callbacks.pack_extension_callback2 &&
      !session->callbacks.pack_extension_callback) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_outbound_item_init(item);

  frame = &item->frame;
  nghttp2_frame_extension_init(&frame->ext, type, flags, stream_id, payload);

  rv = nghttp2_session_add_item(session, item);
  if (rv != 0) {
    nghttp2_frame_extension_free(&frame->ext);
    nghttp2_mem_free(mem, item);
    return rv;
  }

  return 0;
}
