/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
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
#include "nghttp2_session.h"

#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>

#include "nghttp2_helper.h"
#include "nghttp2_net.h"
#include "nghttp2_priority_spec.h"
#include "nghttp2_option.h"
#include "nghttp2_http.h"
#include "nghttp2_pq.h"
#include "nghttp2_extpri.h"
#include "nghttp2_time.h"
#include "nghttp2_debug.h"
#include "nghttp2_submit.h"

/*
 * Returns non-zero if the number of outgoing opened streams is larger
 * than or equal to
 * remote_settings.max_concurrent_streams.
 */
static int
session_is_outgoing_concurrent_streams_max(nghttp2_session *session) {
  return session->remote_settings.max_concurrent_streams <=
         session->num_outgoing_streams;
}

/*
 * Returns non-zero if the number of incoming opened streams is larger
 * than or equal to
 * local_settings.max_concurrent_streams.
 */
static int
session_is_incoming_concurrent_streams_max(nghttp2_session *session) {
  return session->local_settings.max_concurrent_streams <=
         session->num_incoming_streams;
}

/*
 * Returns non-zero if the number of incoming opened streams is larger
 * than or equal to
 * session->pending_local_max_concurrent_stream.
 */
static int
session_is_incoming_concurrent_streams_pending_max(nghttp2_session *session) {
  return session->pending_local_max_concurrent_stream <=
         session->num_incoming_streams;
}

/*
 * Returns non-zero if |lib_error| is non-fatal error.
 */
static int is_non_fatal(int lib_error_code) {
  return lib_error_code < 0 && lib_error_code > NGHTTP2_ERR_FATAL;
}

int nghttp2_is_fatal(int lib_error_code) {
  return lib_error_code < NGHTTP2_ERR_FATAL;
}

static int session_enforce_http_messaging(nghttp2_session *session) {
  return (session->opt_flags & NGHTTP2_OPTMASK_NO_HTTP_MESSAGING) == 0;
}

/*
 * Returns nonzero if |frame| is trailer headers.
 */
static int session_trailer_headers(nghttp2_session *session,
                                   nghttp2_stream *stream,
                                   nghttp2_frame *frame) {
  if (!stream || frame->hd.type != NGHTTP2_HEADERS) {
    return 0;
  }
  if (session->server) {
    return frame->headers.cat == NGHTTP2_HCAT_HEADERS;
  }

  return frame->headers.cat == NGHTTP2_HCAT_HEADERS &&
         (stream->http_flags & NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE) == 0;
}

/* Returns nonzero if the |stream| is in reserved(remote) state */
static int state_reserved_remote(nghttp2_session *session,
                                 nghttp2_stream *stream) {
  return stream->state == NGHTTP2_STREAM_RESERVED &&
         !nghttp2_session_is_my_stream_id(session, stream->stream_id);
}

/* Returns nonzero if the |stream| is in reserved(local) state */
static int state_reserved_local(nghttp2_session *session,
                                nghttp2_stream *stream) {
  return stream->state == NGHTTP2_STREAM_RESERVED &&
         nghttp2_session_is_my_stream_id(session, stream->stream_id);
}

/*
 * Checks whether received stream_id is valid.  This function returns
 * 1 if it succeeds, or 0.
 */
static int session_is_new_peer_stream_id(nghttp2_session *session,
                                         int32_t stream_id) {
  return stream_id != 0 &&
         !nghttp2_session_is_my_stream_id(session, stream_id) &&
         session->last_recv_stream_id < stream_id;
}

static int session_detect_idle_stream(nghttp2_session *session,
                                      int32_t stream_id) {
  /* Assume that stream object with stream_id does not exist */
  if (nghttp2_session_is_my_stream_id(session, stream_id)) {
    if (session->last_sent_stream_id < stream_id) {
      return 1;
    }
    return 0;
  }
  if (session_is_new_peer_stream_id(session, stream_id)) {
    return 1;
  }
  return 0;
}

static int session_no_rfc7540_pri_no_fallback(nghttp2_session *session) {
  return session->pending_no_rfc7540_priorities == 1 &&
         !session->fallback_rfc7540_priorities;
}

static int check_ext_type_set(const uint8_t *ext_types, uint8_t type) {
  return (ext_types[type / 8] & (1 << (type & 0x7))) > 0;
}

static int session_call_error_callback(nghttp2_session *session,
                                       int lib_error_code, const char *fmt,
                                       ...) {
  size_t bufsize;
  va_list ap;
  char *buf;
  int rv;
  nghttp2_mem *mem;

  if (!session->callbacks.error_callback &&
      !session->callbacks.error_callback2) {
    return 0;
  }

  mem = &session->mem;

  va_start(ap, fmt);
  rv = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);

  if (rv < 0) {
    return NGHTTP2_ERR_NOMEM;
  }

  bufsize = (size_t)(rv + 1);

  buf = nghttp2_mem_malloc(mem, bufsize);
  if (buf == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  va_start(ap, fmt);
  rv = vsnprintf(buf, bufsize, fmt, ap);
  va_end(ap);

  if (rv < 0) {
    nghttp2_mem_free(mem, buf);
    /* vsnprintf may return error because of various things we can
       imagine, but typically we don't want to drop session just for
       debug callback. */
    DEBUGF("error_callback: vsnprintf failed. The template was %s\n", fmt);
    return 0;
  }

  if (session->callbacks.error_callback2) {
    rv = session->callbacks.error_callback2(session, lib_error_code, buf,
                                            (size_t)rv, session->user_data);
  } else {
    rv = session->callbacks.error_callback(session, buf, (size_t)rv,
                                           session->user_data);
  }

  nghttp2_mem_free(mem, buf);

  if (rv != 0) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int session_terminate_session(nghttp2_session *session,
                                     int32_t last_stream_id,
                                     uint32_t error_code, const char *reason) {
  int rv;
  const uint8_t *debug_data;
  size_t debug_datalen;

  if (session->goaway_flags & NGHTTP2_GOAWAY_TERM_ON_SEND) {
    return 0;
  }

  /* Ignore all incoming frames because we are going to tear down the
     session. */
  session->iframe.state = NGHTTP2_IB_IGN_ALL;

  if (reason == NULL) {
    debug_data = NULL;
    debug_datalen = 0;
  } else {
    debug_data = (const uint8_t *)reason;
    debug_datalen = strlen(reason);
  }

  rv = nghttp2_session_add_goaway(session, last_stream_id, error_code,
                                  debug_data, debug_datalen,
                                  NGHTTP2_GOAWAY_AUX_TERM_ON_SEND);

  if (rv != 0) {
    return rv;
  }

  session->goaway_flags |= NGHTTP2_GOAWAY_TERM_ON_SEND;

  return 0;
}

int nghttp2_session_terminate_session(nghttp2_session *session,
                                      uint32_t error_code) {
  return session_terminate_session(session, session->last_proc_stream_id,
                                   error_code, NULL);
}

int nghttp2_session_terminate_session2(nghttp2_session *session,
                                       int32_t last_stream_id,
                                       uint32_t error_code) {
  return session_terminate_session(session, last_stream_id, error_code, NULL);
}

int nghttp2_session_terminate_session_with_reason(nghttp2_session *session,
                                                  uint32_t error_code,
                                                  const char *reason) {
  return session_terminate_session(session, session->last_proc_stream_id,
                                   error_code, reason);
}

int nghttp2_session_is_my_stream_id(nghttp2_session *session,
                                    int32_t stream_id) {
  int rem;
  if (stream_id == 0) {
    return 0;
  }
  rem = stream_id & 0x1;
  if (session->server) {
    return rem == 0;
  }
  return rem == 1;
}

nghttp2_stream *nghttp2_session_get_stream(nghttp2_session *session,
                                           int32_t stream_id) {
  nghttp2_stream *stream;

  stream = (nghttp2_stream *)nghttp2_map_find(&session->streams, stream_id);

  if (stream == NULL || (stream->flags & NGHTTP2_STREAM_FLAG_CLOSED) ||
      stream->state == NGHTTP2_STREAM_IDLE) {
    return NULL;
  }

  return stream;
}

nghttp2_stream *nghttp2_session_get_stream_raw(nghttp2_session *session,
                                               int32_t stream_id) {
  return (nghttp2_stream *)nghttp2_map_find(&session->streams, stream_id);
}

static void session_inbound_frame_reset(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_mem *mem = &session->mem;
  /* A bit risky code, since if this function is called from
     nghttp2_session_new(), we rely on the fact that
     iframe->frame.hd.type is 0, so that no free is performed. */
  switch (iframe->frame.hd.type) {
  case NGHTTP2_DATA:
    break;
  case NGHTTP2_HEADERS:
    nghttp2_frame_headers_free(&iframe->frame.headers, mem);
    break;
  case NGHTTP2_PRIORITY:
    nghttp2_frame_priority_free(&iframe->frame.priority);
    break;
  case NGHTTP2_RST_STREAM:
    nghttp2_frame_rst_stream_free(&iframe->frame.rst_stream);
    break;
  case NGHTTP2_SETTINGS:
    nghttp2_frame_settings_free(&iframe->frame.settings, mem);

    nghttp2_mem_free(mem, iframe->iv);

    iframe->iv = NULL;
    iframe->niv = 0;
    iframe->max_niv = 0;

    break;
  case NGHTTP2_PUSH_PROMISE:
    nghttp2_frame_push_promise_free(&iframe->frame.push_promise, mem);
    break;
  case NGHTTP2_PING:
    nghttp2_frame_ping_free(&iframe->frame.ping);
    break;
  case NGHTTP2_GOAWAY:
    nghttp2_frame_goaway_free(&iframe->frame.goaway, mem);
    break;
  case NGHTTP2_WINDOW_UPDATE:
    nghttp2_frame_window_update_free(&iframe->frame.window_update);
    break;
  default:
    /* extension frame */
    if (check_ext_type_set(session->user_recv_ext_types,
                           iframe->frame.hd.type)) {
      nghttp2_frame_extension_free(&iframe->frame.ext);
    } else {
      switch (iframe->frame.hd.type) {
      case NGHTTP2_ALTSVC:
        if ((session->builtin_recv_ext_types & NGHTTP2_TYPEMASK_ALTSVC) == 0) {
          break;
        }
        nghttp2_frame_altsvc_free(&iframe->frame.ext, mem);
        break;
      case NGHTTP2_ORIGIN:
        if ((session->builtin_recv_ext_types & NGHTTP2_TYPEMASK_ORIGIN) == 0) {
          break;
        }
        nghttp2_frame_origin_free(&iframe->frame.ext, mem);
        break;
      case NGHTTP2_PRIORITY_UPDATE:
        if ((session->builtin_recv_ext_types &
             NGHTTP2_TYPEMASK_PRIORITY_UPDATE) == 0) {
          break;
        }
        /* Do not call nghttp2_frame_priority_update_free, because all
           fields point to sbuf. */
        break;
      }
    }

    break;
  }

  memset(&iframe->frame, 0, sizeof(nghttp2_frame));
  memset(&iframe->ext_frame_payload, 0, sizeof(nghttp2_ext_frame_payload));

  iframe->state = NGHTTP2_IB_READ_HEAD;

  nghttp2_buf_wrap_init(&iframe->sbuf, iframe->raw_sbuf,
                        sizeof(iframe->raw_sbuf));
  iframe->sbuf.mark += NGHTTP2_FRAME_HDLEN;

  nghttp2_buf_free(&iframe->lbuf, mem);
  nghttp2_buf_wrap_init(&iframe->lbuf, NULL, 0);

  iframe->raw_lbuf = NULL;

  iframe->payloadleft = 0;
  iframe->padlen = 0;
}

static void init_settings(nghttp2_settings_storage *settings) {
  settings->header_table_size = NGHTTP2_HD_DEFAULT_MAX_BUFFER_SIZE;
  settings->enable_push = 1;
  settings->max_concurrent_streams = NGHTTP2_DEFAULT_MAX_CONCURRENT_STREAMS;
  settings->initial_window_size = NGHTTP2_INITIAL_WINDOW_SIZE;
  settings->max_frame_size = NGHTTP2_MAX_FRAME_SIZE_MIN;
  settings->max_header_list_size = UINT32_MAX;
  settings->no_rfc7540_priorities = UINT32_MAX;
}

static void active_outbound_item_reset(nghttp2_active_outbound_item *aob,
                                       nghttp2_mem *mem) {
  DEBUGF("send: reset nghttp2_active_outbound_item\n");
  DEBUGF("send: aob->item = %p\n", aob->item);
  nghttp2_outbound_item_free(aob->item, mem);
  nghttp2_mem_free(mem, aob->item);
  aob->item = NULL;
  nghttp2_bufs_reset(&aob->framebufs);
  aob->state = NGHTTP2_OB_POP_ITEM;
}

#define NGHTTP2_STREAM_MAX_CYCLE_GAP ((uint64_t)NGHTTP2_MAX_FRAME_SIZE_MAX)

static int stream_less(const void *lhsx, const void *rhsx) {
  const nghttp2_stream *lhs, *rhs;

  lhs = nghttp2_struct_of(lhsx, nghttp2_stream, pq_entry);
  rhs = nghttp2_struct_of(rhsx, nghttp2_stream, pq_entry);

  if (lhs->cycle == rhs->cycle) {
    return lhs->seq < rhs->seq;
  }

  return rhs->cycle - lhs->cycle <= NGHTTP2_STREAM_MAX_CYCLE_GAP;
}

int nghttp2_enable_strict_preface = 1;

static int session_new(nghttp2_session **session_ptr,
                       const nghttp2_session_callbacks *callbacks,
                       void *user_data, int server,
                       const nghttp2_option *option, nghttp2_mem *mem) {
  int rv;
  size_t nbuffer;
  size_t max_deflate_dynamic_table_size =
      NGHTTP2_HD_DEFAULT_MAX_DEFLATE_BUFFER_SIZE;
  size_t i;

  if (mem == NULL) {
    mem = nghttp2_mem_default();
  }

  *session_ptr = nghttp2_mem_calloc(mem, 1, sizeof(nghttp2_session));
  if (*session_ptr == NULL) {
    rv = NGHTTP2_ERR_NOMEM;
    goto fail_session;
  }

  (*session_ptr)->mem = *mem;
  mem = &(*session_ptr)->mem;

  /* next_stream_id is initialized in either
     nghttp2_session_client_new2 or nghttp2_session_server_new2 */

  nghttp2_stream_init(&(*session_ptr)->root, 0, NGHTTP2_STREAM_FLAG_NONE,
                      NGHTTP2_STREAM_IDLE, NGHTTP2_DEFAULT_WEIGHT, 0, 0, NULL,
                      mem);

  (*session_ptr)->remote_window_size = NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE;
  (*session_ptr)->recv_window_size = 0;
  (*session_ptr)->consumed_size = 0;
  (*session_ptr)->recv_reduction = 0;
  (*session_ptr)->local_window_size = NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE;

  (*session_ptr)->goaway_flags = NGHTTP2_GOAWAY_NONE;
  (*session_ptr)->local_last_stream_id = (1u << 31) - 1;
  (*session_ptr)->remote_last_stream_id = (1u << 31) - 1;

  (*session_ptr)->pending_local_max_concurrent_stream =
      NGHTTP2_DEFAULT_MAX_CONCURRENT_STREAMS;
  (*session_ptr)->pending_enable_push = 1;
  (*session_ptr)->pending_no_rfc7540_priorities = UINT8_MAX;

  nghttp2_ratelim_init(&(*session_ptr)->stream_reset_ratelim,
                       NGHTTP2_DEFAULT_STREAM_RESET_BURST,
                       NGHTTP2_DEFAULT_STREAM_RESET_RATE);

  if (server) {
    (*session_ptr)->server = 1;
  }

  init_settings(&(*session_ptr)->remote_settings);
  init_settings(&(*session_ptr)->local_settings);

  (*session_ptr)->max_incoming_reserved_streams =
      NGHTTP2_MAX_INCOMING_RESERVED_STREAMS;

  /* Limit max outgoing concurrent streams to sensible value */
  (*session_ptr)->remote_settings.max_concurrent_streams = 100;

  (*session_ptr)->max_send_header_block_length = NGHTTP2_MAX_HEADERSLEN;
  (*session_ptr)->max_outbound_ack = NGHTTP2_DEFAULT_MAX_OBQ_FLOOD_ITEM;
  (*session_ptr)->max_settings = NGHTTP2_DEFAULT_MAX_SETTINGS;
  (*session_ptr)->max_continuations = NGHTTP2_DEFAULT_MAX_CONTINUATIONS;

  if (option) {
    if ((option->opt_set_mask & NGHTTP2_OPT_NO_AUTO_WINDOW_UPDATE) &&
        option->no_auto_window_update) {

      (*session_ptr)->opt_flags |= NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE;
    }

    if (option->opt_set_mask & NGHTTP2_OPT_PEER_MAX_CONCURRENT_STREAMS) {

      (*session_ptr)->remote_settings.max_concurrent_streams =
          option->peer_max_concurrent_streams;
    }

    if (option->opt_set_mask & NGHTTP2_OPT_MAX_RESERVED_REMOTE_STREAMS) {

      (*session_ptr)->max_incoming_reserved_streams =
          option->max_reserved_remote_streams;
    }

    if ((option->opt_set_mask & NGHTTP2_OPT_NO_RECV_CLIENT_MAGIC) &&
        option->no_recv_client_magic) {

      (*session_ptr)->opt_flags |= NGHTTP2_OPTMASK_NO_RECV_CLIENT_MAGIC;
    }

    if ((option->opt_set_mask & NGHTTP2_OPT_NO_HTTP_MESSAGING) &&
        option->no_http_messaging) {

      (*session_ptr)->opt_flags |= NGHTTP2_OPTMASK_NO_HTTP_MESSAGING;
    }

    if (option->opt_set_mask & NGHTTP2_OPT_USER_RECV_EXT_TYPES) {
      memcpy((*session_ptr)->user_recv_ext_types, option->user_recv_ext_types,
             sizeof((*session_ptr)->user_recv_ext_types));
    }

    if (option->opt_set_mask & NGHTTP2_OPT_BUILTIN_RECV_EXT_TYPES) {
      (*session_ptr)->builtin_recv_ext_types = option->builtin_recv_ext_types;
    }

    if ((option->opt_set_mask & NGHTTP2_OPT_NO_AUTO_PING_ACK) &&
        option->no_auto_ping_ack) {
      (*session_ptr)->opt_flags |= NGHTTP2_OPTMASK_NO_AUTO_PING_ACK;
    }

    if (option->opt_set_mask & NGHTTP2_OPT_MAX_SEND_HEADER_BLOCK_LENGTH) {
      (*session_ptr)->max_send_header_block_length =
          option->max_send_header_block_length;
    }

    if (option->opt_set_mask & NGHTTP2_OPT_MAX_DEFLATE_DYNAMIC_TABLE_SIZE) {
      max_deflate_dynamic_table_size = option->max_deflate_dynamic_table_size;
    }

    if ((option->opt_set_mask & NGHTTP2_OPT_NO_CLOSED_STREAMS) &&
        option->no_closed_streams) {
      (*session_ptr)->opt_flags |= NGHTTP2_OPTMASK_NO_CLOSED_STREAMS;
    }

    if (option->opt_set_mask & NGHTTP2_OPT_MAX_OUTBOUND_ACK) {
      (*session_ptr)->max_outbound_ack = option->max_outbound_ack;
    }

    if ((option->opt_set_mask & NGHTTP2_OPT_MAX_SETTINGS) &&
        option->max_settings) {
      (*session_ptr)->max_settings = option->max_settings;
    }

    if ((option->opt_set_mask &
         NGHTTP2_OPT_SERVER_FALLBACK_RFC7540_PRIORITIES) &&
        option->server_fallback_rfc7540_priorities) {
      (*session_ptr)->opt_flags |=
          NGHTTP2_OPTMASK_SERVER_FALLBACK_RFC7540_PRIORITIES;
    }

    if ((option->opt_set_mask &
         NGHTTP2_OPT_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION) &&
        option->no_rfc9113_leading_and_trailing_ws_validation) {
      (*session_ptr)->opt_flags |=
          NGHTTP2_OPTMASK_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION;
    }

    if (option->opt_set_mask & NGHTTP2_OPT_STREAM_RESET_RATE_LIMIT) {
      nghttp2_ratelim_init(&(*session_ptr)->stream_reset_ratelim,
                           option->stream_reset_burst,
                           option->stream_reset_rate);
    }

    if (option->opt_set_mask & NGHTTP2_OPT_MAX_CONTINUATIONS) {
      (*session_ptr)->max_continuations = option->max_continuations;
    }
  }

  rv = nghttp2_hd_deflate_init2(&(*session_ptr)->hd_deflater,
                                max_deflate_dynamic_table_size, mem);
  if (rv != 0) {
    goto fail_hd_deflater;
  }
  rv = nghttp2_hd_inflate_init(&(*session_ptr)->hd_inflater, mem);
  if (rv != 0) {
    goto fail_hd_inflater;
  }

  nbuffer = ((*session_ptr)->max_send_header_block_length +
             NGHTTP2_FRAMEBUF_CHUNKLEN - 1) /
            NGHTTP2_FRAMEBUF_CHUNKLEN;

  if (nbuffer == 0) {
    nbuffer = 1;
  }

  /* 1 for Pad Field. */
  rv = nghttp2_bufs_init3(&(*session_ptr)->aob.framebufs,
                          NGHTTP2_FRAMEBUF_CHUNKLEN, nbuffer, 1,
                          NGHTTP2_FRAME_HDLEN + 1, mem);
  if (rv != 0) {
    goto fail_aob_framebuf;
  }

  nghttp2_map_init(&(*session_ptr)->streams, mem);

  active_outbound_item_reset(&(*session_ptr)->aob, mem);

  (*session_ptr)->callbacks = *callbacks;
  (*session_ptr)->user_data = user_data;

  session_inbound_frame_reset(*session_ptr);

  if (nghttp2_enable_strict_preface) {
    nghttp2_inbound_frame *iframe = &(*session_ptr)->iframe;

    if (server && ((*session_ptr)->opt_flags &
                   NGHTTP2_OPTMASK_NO_RECV_CLIENT_MAGIC) == 0) {
      iframe->state = NGHTTP2_IB_READ_CLIENT_MAGIC;
      iframe->payloadleft = NGHTTP2_CLIENT_MAGIC_LEN;
    } else {
      iframe->state = NGHTTP2_IB_READ_FIRST_SETTINGS;
    }

    if (!server) {
      (*session_ptr)->aob.state = NGHTTP2_OB_SEND_CLIENT_MAGIC;
      nghttp2_bufs_add(&(*session_ptr)->aob.framebufs, NGHTTP2_CLIENT_MAGIC,
                       NGHTTP2_CLIENT_MAGIC_LEN);
    }
  }

  for (i = 0; i < NGHTTP2_EXTPRI_URGENCY_LEVELS; ++i) {
    nghttp2_pq_init(&(*session_ptr)->sched[i].ob_data, stream_less, mem);
  }

  return 0;

fail_aob_framebuf:
  nghttp2_hd_inflate_free(&(*session_ptr)->hd_inflater);
fail_hd_inflater:
  nghttp2_hd_deflate_free(&(*session_ptr)->hd_deflater);
fail_hd_deflater:
  nghttp2_mem_free(mem, *session_ptr);
fail_session:
  return rv;
}

int nghttp2_session_client_new(nghttp2_session **session_ptr,
                               const nghttp2_session_callbacks *callbacks,
                               void *user_data) {
  return nghttp2_session_client_new3(session_ptr, callbacks, user_data, NULL,
                                     NULL);
}

int nghttp2_session_client_new2(nghttp2_session **session_ptr,
                                const nghttp2_session_callbacks *callbacks,
                                void *user_data, const nghttp2_option *option) {
  return nghttp2_session_client_new3(session_ptr, callbacks, user_data, option,
                                     NULL);
}

int nghttp2_session_client_new3(nghttp2_session **session_ptr,
                                const nghttp2_session_callbacks *callbacks,
                                void *user_data, const nghttp2_option *option,
                                nghttp2_mem *mem) {
  int rv;
  nghttp2_session *session;

  rv = session_new(&session, callbacks, user_data, 0, option, mem);

  if (rv != 0) {
    return rv;
  }
  /* IDs for use in client */
  session->next_stream_id = 1;

  *session_ptr = session;

  return 0;
}

int nghttp2_session_server_new(nghttp2_session **session_ptr,
                               const nghttp2_session_callbacks *callbacks,
                               void *user_data) {
  return nghttp2_session_server_new3(session_ptr, callbacks, user_data, NULL,
                                     NULL);
}

int nghttp2_session_server_new2(nghttp2_session **session_ptr,
                                const nghttp2_session_callbacks *callbacks,
                                void *user_data, const nghttp2_option *option) {
  return nghttp2_session_server_new3(session_ptr, callbacks, user_data, option,
                                     NULL);
}

int nghttp2_session_server_new3(nghttp2_session **session_ptr,
                                const nghttp2_session_callbacks *callbacks,
                                void *user_data, const nghttp2_option *option,
                                nghttp2_mem *mem) {
  int rv;
  nghttp2_session *session;

  rv = session_new(&session, callbacks, user_data, 1, option, mem);

  if (rv != 0) {
    return rv;
  }
  /* IDs for use in client */
  session->next_stream_id = 2;

  *session_ptr = session;

  return 0;
}

static int free_streams(void *entry, void *ptr) {
  nghttp2_session *session;
  nghttp2_stream *stream;
  nghttp2_outbound_item *item;
  nghttp2_mem *mem;

  session = (nghttp2_session *)ptr;
  mem = &session->mem;
  stream = (nghttp2_stream *)entry;
  item = stream->item;

  if (item && !item->queued && item != session->aob.item) {
    nghttp2_outbound_item_free(item, mem);
    nghttp2_mem_free(mem, item);
  }

  nghttp2_stream_free(stream);
  nghttp2_mem_free(mem, stream);

  return 0;
}

static void ob_q_free(nghttp2_outbound_queue *q, nghttp2_mem *mem) {
  nghttp2_outbound_item *item, *next;
  for (item = q->head; item;) {
    next = item->qnext;
    nghttp2_outbound_item_free(item, mem);
    nghttp2_mem_free(mem, item);
    item = next;
  }
}

static int inflight_settings_new(nghttp2_inflight_settings **settings_ptr,
                                 const nghttp2_settings_entry *iv, size_t niv,
                                 nghttp2_mem *mem) {
  *settings_ptr = nghttp2_mem_malloc(mem, sizeof(nghttp2_inflight_settings));
  if (!*settings_ptr) {
    return NGHTTP2_ERR_NOMEM;
  }

  if (niv > 0) {
    (*settings_ptr)->iv = nghttp2_frame_iv_copy(iv, niv, mem);
    if (!(*settings_ptr)->iv) {
      nghttp2_mem_free(mem, *settings_ptr);
      return NGHTTP2_ERR_NOMEM;
    }
  } else {
    (*settings_ptr)->iv = NULL;
  }

  (*settings_ptr)->niv = niv;
  (*settings_ptr)->next = NULL;

  return 0;
}

static void inflight_settings_del(nghttp2_inflight_settings *settings,
                                  nghttp2_mem *mem) {
  if (!settings) {
    return;
  }

  nghttp2_mem_free(mem, settings->iv);
  nghttp2_mem_free(mem, settings);
}

void nghttp2_session_del(nghttp2_session *session) {
  nghttp2_mem *mem;
  nghttp2_inflight_settings *settings;
  size_t i;

  if (session == NULL) {
    return;
  }

  mem = &session->mem;

  for (settings = session->inflight_settings_head; settings;) {
    nghttp2_inflight_settings *next = settings->next;
    inflight_settings_del(settings, mem);
    settings = next;
  }

  for (i = 0; i < NGHTTP2_EXTPRI_URGENCY_LEVELS; ++i) {
    nghttp2_pq_free(&session->sched[i].ob_data);
  }
  nghttp2_stream_free(&session->root);

  /* Have to free streams first, so that we can check
     stream->item->queued */
  nghttp2_map_each_free(&session->streams, free_streams, session);
  nghttp2_map_free(&session->streams);

  ob_q_free(&session->ob_urgent, mem);
  ob_q_free(&session->ob_reg, mem);
  ob_q_free(&session->ob_syn, mem);

  active_outbound_item_reset(&session->aob, mem);
  session_inbound_frame_reset(session);
  nghttp2_hd_deflate_free(&session->hd_deflater);
  nghttp2_hd_inflate_free(&session->hd_inflater);
  nghttp2_bufs_free(&session->aob.framebufs);
  nghttp2_mem_free(mem, session);
}

int nghttp2_session_reprioritize_stream(
    nghttp2_session *session, nghttp2_stream *stream,
    const nghttp2_priority_spec *pri_spec_in) {
  int rv;
  nghttp2_stream *dep_stream = NULL;
  nghttp2_priority_spec pri_spec_default;
  const nghttp2_priority_spec *pri_spec = pri_spec_in;

  assert((!session->server && session->pending_no_rfc7540_priorities != 1) ||
         (session->server && !session_no_rfc7540_pri_no_fallback(session)));
  assert(pri_spec->stream_id != stream->stream_id);

  if (!nghttp2_stream_in_dep_tree(stream)) {
    return 0;
  }

  if (pri_spec->stream_id != 0) {
    dep_stream = nghttp2_session_get_stream_raw(session, pri_spec->stream_id);

    if (!dep_stream &&
        session_detect_idle_stream(session, pri_spec->stream_id)) {

      nghttp2_priority_spec_default_init(&pri_spec_default);

      dep_stream = nghttp2_session_open_stream(
          session, pri_spec->stream_id, NGHTTP2_FLAG_NONE, &pri_spec_default,
          NGHTTP2_STREAM_IDLE, NULL);

      if (dep_stream == NULL) {
        return NGHTTP2_ERR_NOMEM;
      }
    } else if (!dep_stream || !nghttp2_stream_in_dep_tree(dep_stream)) {
      nghttp2_priority_spec_default_init(&pri_spec_default);
      pri_spec = &pri_spec_default;
    }
  }

  if (pri_spec->stream_id == 0) {
    dep_stream = &session->root;
  } else if (nghttp2_stream_dep_find_ancestor(dep_stream, stream)) {
    DEBUGF("stream: cycle detected, dep_stream(%p)=%d stream(%p)=%d\n",
           dep_stream, dep_stream->stream_id, stream, stream->stream_id);

    nghttp2_stream_dep_remove_subtree(dep_stream);
    rv = nghttp2_stream_dep_add_subtree(stream->dep_prev, dep_stream);
    if (rv != 0) {
      return rv;
    }
  }

  assert(dep_stream);

  if (dep_stream == stream->dep_prev && !pri_spec->exclusive) {
    /* This is minor optimization when just weight is changed. */
    nghttp2_stream_change_weight(stream, pri_spec->weight);

    return 0;
  }

  nghttp2_stream_dep_remove_subtree(stream);

  /* We have to update weight after removing stream from tree */
  stream->weight = pri_spec->weight;

  if (pri_spec->exclusive) {
    rv = nghttp2_stream_dep_insert_subtree(dep_stream, stream);
  } else {
    rv = nghttp2_stream_dep_add_subtree(dep_stream, stream);
  }

  if (rv != 0) {
    return rv;
  }

  return 0;
}

static uint64_t pq_get_first_cycle(nghttp2_pq *pq) {
  nghttp2_stream *stream;

  if (nghttp2_pq_empty(pq)) {
    return 0;
  }

  stream = nghttp2_struct_of(nghttp2_pq_top(pq), nghttp2_stream, pq_entry);
  return stream->cycle;
}

static int session_ob_data_push(nghttp2_session *session,
                                nghttp2_stream *stream) {
  int rv;
  uint32_t urgency;
  int inc;
  nghttp2_pq *pq;

  assert(stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES);
  assert(stream->queued == 0);

  urgency = nghttp2_extpri_uint8_urgency(stream->extpri);
  inc = nghttp2_extpri_uint8_inc(stream->extpri);

  assert(urgency < NGHTTP2_EXTPRI_URGENCY_LEVELS);

  pq = &session->sched[urgency].ob_data;

  stream->cycle = pq_get_first_cycle(pq);
  if (inc) {
    stream->cycle += stream->last_writelen;
  }

  rv = nghttp2_pq_push(pq, &stream->pq_entry);
  if (rv != 0) {
    return rv;
  }

  stream->queued = 1;

  return 0;
}

static void session_ob_data_remove(nghttp2_session *session,
                                   nghttp2_stream *stream) {
  uint32_t urgency;

  assert(stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES);
  assert(stream->queued == 1);

  urgency = nghttp2_extpri_uint8_urgency(stream->extpri);

  assert(urgency < NGHTTP2_EXTPRI_URGENCY_LEVELS);

  nghttp2_pq_remove(&session->sched[urgency].ob_data, &stream->pq_entry);

  stream->queued = 0;
}

static int session_attach_stream_item(nghttp2_session *session,
                                      nghttp2_stream *stream,
                                      nghttp2_outbound_item *item) {
  int rv;

  rv = nghttp2_stream_attach_item(stream, item);
  if (rv != 0) {
    return rv;
  }

  if (!(stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES)) {
    return 0;
  }

  rv = session_ob_data_push(session, stream);
  if (rv != 0) {
    nghttp2_stream_detach_item(stream);

    return rv;
  }

  return 0;
}

static void session_detach_stream_item(nghttp2_session *session,
                                       nghttp2_stream *stream) {
  nghttp2_stream_detach_item(stream);

  if (!(stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES) ||
      !stream->queued) {
    return;
  }

  session_ob_data_remove(session, stream);
}

static void session_defer_stream_item(nghttp2_session *session,
                                      nghttp2_stream *stream, uint8_t flags) {
  nghttp2_stream_defer_item(stream, flags);

  if (!(stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES) ||
      !stream->queued) {
    return;
  }

  session_ob_data_remove(session, stream);
}

static int session_resume_deferred_stream_item(nghttp2_session *session,
                                               nghttp2_stream *stream,
                                               uint8_t flags) {
  int rv;

  rv = nghttp2_stream_resume_deferred_item(stream, flags);
  if (rv != 0) {
    return rv;
  }

  if (!(stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES) ||
      (stream->flags & NGHTTP2_STREAM_FLAG_DEFERRED_ALL)) {
    return 0;
  }

  return session_ob_data_push(session, stream);
}

static nghttp2_outbound_item *
session_sched_get_next_outbound_item(nghttp2_session *session) {
  size_t i;
  nghttp2_pq_entry *ent;
  nghttp2_stream *stream;

  for (i = 0; i < NGHTTP2_EXTPRI_URGENCY_LEVELS; ++i) {
    ent = nghttp2_pq_top(&session->sched[i].ob_data);
    if (!ent) {
      continue;
    }

    stream = nghttp2_struct_of(ent, nghttp2_stream, pq_entry);
    return stream->item;
  }

  return NULL;
}

static int session_sched_empty(nghttp2_session *session) {
  size_t i;

  for (i = 0; i < NGHTTP2_EXTPRI_URGENCY_LEVELS; ++i) {
    if (!nghttp2_pq_empty(&session->sched[i].ob_data)) {
      return 0;
    }
  }

  return 1;
}

static void session_sched_reschedule_stream(nghttp2_session *session,
                                            nghttp2_stream *stream) {
  nghttp2_pq *pq;
  uint32_t urgency = nghttp2_extpri_uint8_urgency(stream->extpri);
  int inc = nghttp2_extpri_uint8_inc(stream->extpri);
  uint64_t penalty = (uint64_t)stream->last_writelen;
  int rv;

  (void)rv;

  assert(urgency < NGHTTP2_EXTPRI_URGENCY_LEVELS);

  pq = &session->sched[urgency].ob_data;

  if (!inc || nghttp2_pq_size(pq) == 1) {
    return;
  }

  nghttp2_pq_remove(pq, &stream->pq_entry);

  stream->cycle += penalty;

  rv = nghttp2_pq_push(pq, &stream->pq_entry);

  assert(0 == rv);
}

static int session_update_stream_priority(nghttp2_session *session,
                                          nghttp2_stream *stream,
                                          uint8_t u8extpri) {
  if (stream->extpri == u8extpri) {
    return 0;
  }

  if (stream->queued) {
    session_ob_data_remove(session, stream);

    stream->extpri = u8extpri;

    return session_ob_data_push(session, stream);
  }

  stream->extpri = u8extpri;

  return 0;
}

int nghttp2_session_add_item(nghttp2_session *session,
                             nghttp2_outbound_item *item) {
  /* TODO Return error if stream is not found for the frame requiring
     stream presence. */
  int rv = 0;
  nghttp2_stream *stream;
  nghttp2_frame *frame;

  frame = &item->frame;
  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);

  switch (frame->hd.type) {
  case NGHTTP2_DATA:
    if (!stream) {
      return NGHTTP2_ERR_STREAM_CLOSED;
    }

    if (stream->item) {
      return NGHTTP2_ERR_DATA_EXIST;
    }

    rv = session_attach_stream_item(session, stream, item);

    if (rv != 0) {
      return rv;
    }

    return 0;
  case NGHTTP2_HEADERS:
    /* We push request HEADERS and push response HEADERS to
       dedicated queue because their transmission is affected by
       SETTINGS_MAX_CONCURRENT_STREAMS */
    /* TODO If 2 HEADERS are submitted for reserved stream, then
       both of them are queued into ob_syn, which is not
       desirable. */
    if (frame->headers.cat == NGHTTP2_HCAT_REQUEST ||
        (stream && stream->state == NGHTTP2_STREAM_RESERVED)) {
      nghttp2_outbound_queue_push(&session->ob_syn, item);
      item->queued = 1;
      return 0;
      ;
    }

    nghttp2_outbound_queue_push(&session->ob_reg, item);
    item->queued = 1;
    return 0;
  case NGHTTP2_SETTINGS:
  case NGHTTP2_PING:
    nghttp2_outbound_queue_push(&session->ob_urgent, item);
    item->queued = 1;
    return 0;
  case NGHTTP2_RST_STREAM:
    if (stream) {
      stream->state = NGHTTP2_STREAM_CLOSING;
    }
    nghttp2_outbound_queue_push(&session->ob_reg, item);
    item->queued = 1;
    return 0;
  case NGHTTP2_PUSH_PROMISE: {
    nghttp2_headers_aux_data *aux_data;
    nghttp2_priority_spec pri_spec;

    aux_data = &item->aux_data.headers;

    if (!stream) {
      return NGHTTP2_ERR_STREAM_CLOSED;
    }

    nghttp2_priority_spec_init(&pri_spec, stream->stream_id,
                               NGHTTP2_DEFAULT_WEIGHT, 0);

    if (!nghttp2_session_open_stream(
            session, frame->push_promise.promised_stream_id,
            NGHTTP2_STREAM_FLAG_NONE, &pri_spec, NGHTTP2_STREAM_RESERVED,
            aux_data->stream_user_data)) {
      return NGHTTP2_ERR_NOMEM;
    }

    /* We don't have to call nghttp2_session_adjust_closed_stream()
       here, since stream->stream_id is local stream_id, and it does
       not affect closed stream count. */

    nghttp2_outbound_queue_push(&session->ob_reg, item);
    item->queued = 1;

    return 0;
  }
  case NGHTTP2_WINDOW_UPDATE:
    if (stream) {
      stream->window_update_queued = 1;
    } else if (frame->hd.stream_id == 0) {
      session->window_update_queued = 1;
    }
    nghttp2_outbound_queue_push(&session->ob_reg, item);
    item->queued = 1;
    return 0;
  default:
    nghttp2_outbound_queue_push(&session->ob_reg, item);
    item->queued = 1;
    return 0;
  }
}

int nghttp2_session_add_rst_stream(nghttp2_session *session, int32_t stream_id,
                                   uint32_t error_code) {
  int rv;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_stream *stream;
  nghttp2_mem *mem;

  mem = &session->mem;
  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream && stream->state == NGHTTP2_STREAM_CLOSING) {
    return 0;
  }

  /* Sending RST_STREAM to an idle stream is subject to protocol
     violation.  Historically, nghttp2 allows this.  In order not to
     disrupt the existing applications, we don't error out this case
     and simply ignore it. */
  if (nghttp2_session_is_my_stream_id(session, stream_id)) {
    if ((uint32_t)stream_id >= session->next_stream_id) {
      return 0;
    }
  } else if (session->last_recv_stream_id < stream_id) {
    return 0;
  }

  /* Cancel pending request HEADERS in ob_syn if this RST_STREAM
     refers to that stream. */
  if (!session->server && nghttp2_session_is_my_stream_id(session, stream_id) &&
      nghttp2_outbound_queue_top(&session->ob_syn)) {
    nghttp2_headers_aux_data *aux_data;
    nghttp2_frame *headers_frame;

    headers_frame = &nghttp2_outbound_queue_top(&session->ob_syn)->frame;
    assert(headers_frame->hd.type == NGHTTP2_HEADERS);

    if (headers_frame->hd.stream_id <= stream_id) {

      for (item = session->ob_syn.head; item; item = item->qnext) {
        aux_data = &item->aux_data.headers;

        if (item->frame.hd.stream_id < stream_id) {
          continue;
        }

        /* stream_id in ob_syn queue must be strictly increasing.  If
           we found larger ID, then we can break here. */
        if (item->frame.hd.stream_id > stream_id || aux_data->canceled) {
          break;
        }

        aux_data->error_code = error_code;
        aux_data->canceled = 1;

        return 0;
      }
    }
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_outbound_item_init(item);

  frame = &item->frame;

  nghttp2_frame_rst_stream_init(&frame->rst_stream, stream_id, error_code);
  rv = nghttp2_session_add_item(session, item);
  if (rv != 0) {
    nghttp2_frame_rst_stream_free(&frame->rst_stream);
    nghttp2_mem_free(mem, item);
    return rv;
  }
  return 0;
}

nghttp2_stream *nghttp2_session_open_stream(nghttp2_session *session,
                                            int32_t stream_id, uint8_t flags,
                                            nghttp2_priority_spec *pri_spec_in,
                                            nghttp2_stream_state initial_state,
                                            void *stream_user_data) {
  int rv;
  nghttp2_stream *stream;
  nghttp2_stream *dep_stream = NULL;
  int stream_alloc = 0;
  nghttp2_priority_spec pri_spec_default;
  nghttp2_priority_spec *pri_spec = pri_spec_in;
  nghttp2_mem *mem;

  mem = &session->mem;
  stream = nghttp2_session_get_stream_raw(session, stream_id);

  if (session->opt_flags &
      NGHTTP2_OPTMASK_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION) {
    flags |= NGHTTP2_STREAM_FLAG_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION;
  }

  if (stream) {
    assert(stream->state == NGHTTP2_STREAM_IDLE);
    assert((stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES) ||
           nghttp2_stream_in_dep_tree(stream));

    nghttp2_session_detach_idle_stream(session, stream);

    if (nghttp2_stream_in_dep_tree(stream)) {
      assert(!(stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES));

      rv = nghttp2_stream_dep_remove(stream);
      if (rv != 0) {
        return NULL;
      }

      if (session_no_rfc7540_pri_no_fallback(session)) {
        stream->flags |= NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES;
      }
    }
  } else {
    stream = nghttp2_mem_malloc(mem, sizeof(nghttp2_stream));
    if (stream == NULL) {
      return NULL;
    }

    stream_alloc = 1;
  }

  if (session_no_rfc7540_pri_no_fallback(session) ||
      session->remote_settings.no_rfc7540_priorities == 1) {
    /* For client which has not received server
       SETTINGS_NO_RFC7540_PRIORITIES = 1, send a priority signal
       opportunistically. */
    if (session->server ||
        session->remote_settings.no_rfc7540_priorities == 1) {
      nghttp2_priority_spec_default_init(&pri_spec_default);
      pri_spec = &pri_spec_default;
    }

    if (session->pending_no_rfc7540_priorities == 1) {
      flags |= NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES;
    }
  } else if (pri_spec->stream_id != 0) {
    dep_stream = nghttp2_session_get_stream_raw(session, pri_spec->stream_id);

    if (!dep_stream &&
        session_detect_idle_stream(session, pri_spec->stream_id)) {
      /* Depends on idle stream, which does not exist in memory.
         Assign default priority for it. */
      nghttp2_priority_spec_default_init(&pri_spec_default);

      dep_stream = nghttp2_session_open_stream(
          session, pri_spec->stream_id, NGHTTP2_FLAG_NONE, &pri_spec_default,
          NGHTTP2_STREAM_IDLE, NULL);

      if (dep_stream == NULL) {
        if (stream_alloc) {
          nghttp2_mem_free(mem, stream);
        }

        return NULL;
      }
    } else if (!dep_stream || !nghttp2_stream_in_dep_tree(dep_stream)) {
      /* If dep_stream is not part of dependency tree, stream will get
         default priority.  This handles the case when
         pri_spec->stream_id == stream_id.  This happens because we
         don't check pri_spec->stream_id against new stream ID in
         nghttp2_submit_request.  This also handles the case when idle
         stream created by PRIORITY frame was opened.  Somehow we
         first remove the idle stream from dependency tree.  This is
         done to simplify code base, but ideally we should retain old
         dependency.  But I'm not sure this adds values. */
      nghttp2_priority_spec_default_init(&pri_spec_default);
      pri_spec = &pri_spec_default;
    }
  }

  if (initial_state == NGHTTP2_STREAM_RESERVED) {
    flags |= NGHTTP2_STREAM_FLAG_PUSH;
  }

  if (stream_alloc) {
    nghttp2_stream_init(stream, stream_id, flags, initial_state,
                        pri_spec->weight,
                        (int32_t)session->remote_settings.initial_window_size,
                        (int32_t)session->local_settings.initial_window_size,
                        stream_user_data, mem);

    if (session_no_rfc7540_pri_no_fallback(session)) {
      stream->seq = session->stream_seq++;
    }

    rv = nghttp2_map_insert(&session->streams, stream_id, stream);
    if (rv != 0) {
      nghttp2_stream_free(stream);
      nghttp2_mem_free(mem, stream);
      return NULL;
    }
  } else {
    stream->flags = flags;
    stream->state = initial_state;
    stream->weight = pri_spec->weight;
    stream->stream_user_data = stream_user_data;
  }

  switch (initial_state) {
  case NGHTTP2_STREAM_RESERVED:
    if (nghttp2_session_is_my_stream_id(session, stream_id)) {
      /* reserved (local) */
      nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_RD);
    } else {
      /* reserved (remote) */
      nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_WR);
      ++session->num_incoming_reserved_streams;
    }
    /* Reserved stream does not count in the concurrent streams
       limit. That is one of the DOS vector. */
    break;
  case NGHTTP2_STREAM_IDLE:
    /* Idle stream does not count toward the concurrent streams limit.
       This is used as anchor node in dependency tree. */
    nghttp2_session_keep_idle_stream(session, stream);
    break;
  default:
    if (nghttp2_session_is_my_stream_id(session, stream_id)) {
      ++session->num_outgoing_streams;
    } else {
      ++session->num_incoming_streams;
    }
  }

  if (stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES) {
    return stream;
  }

  if (pri_spec->stream_id == 0) {
    dep_stream = &session->root;
  }

  assert(dep_stream);

  if (pri_spec->exclusive) {
    rv = nghttp2_stream_dep_insert(dep_stream, stream);
    if (rv != 0) {
      return NULL;
    }
  } else {
    nghttp2_stream_dep_add(dep_stream, stream);
  }

  return stream;
}

int nghttp2_session_close_stream(nghttp2_session *session, int32_t stream_id,
                                 uint32_t error_code) {
  int rv;
  nghttp2_stream *stream;
  nghttp2_mem *mem;
  int is_my_stream_id;

  mem = &session->mem;
  stream = nghttp2_session_get_stream(session, stream_id);

  if (!stream) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  DEBUGF("stream: stream(%p)=%d close\n", stream, stream->stream_id);

  /* We call on_stream_close_callback even if stream->state is
     NGHTTP2_STREAM_INITIAL. This will happen while sending request
     HEADERS, a local endpoint receives RST_STREAM for that stream. It
     may be PROTOCOL_ERROR, but without notifying stream closure will
     hang the stream in a local endpoint.
  */

  if (session->callbacks.on_stream_close_callback) {
    if (session->callbacks.on_stream_close_callback(
            session, stream_id, error_code, session->user_data) != 0) {

      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }

  if (stream->item) {
    nghttp2_outbound_item *item;

    item = stream->item;

    session_detach_stream_item(session, stream);

    /* If item is queued, it will be deleted when it is popped
       (nghttp2_session_prep_frame() will fail).  If session->aob.item
       points to this item, let active_outbound_item_reset()
       free the item. */
    if (!item->queued && item != session->aob.item) {
      nghttp2_outbound_item_free(item, mem);
      nghttp2_mem_free(mem, item);
    }
  }

  is_my_stream_id = nghttp2_session_is_my_stream_id(session, stream_id);

  /* pushed streams which is not opened yet is not counted toward max
     concurrent limits */
  if ((stream->flags & NGHTTP2_STREAM_FLAG_PUSH)) {
    if (!is_my_stream_id) {
      --session->num_incoming_reserved_streams;
    }
  } else {
    if (is_my_stream_id) {
      --session->num_outgoing_streams;
    } else {
      --session->num_incoming_streams;
    }
  }

  /* Closes both directions just in case they are not closed yet */
  stream->flags |= NGHTTP2_STREAM_FLAG_CLOSED;

  if (session->pending_no_rfc7540_priorities == 1) {
    return nghttp2_session_destroy_stream(session, stream);
  }

  if ((session->opt_flags & NGHTTP2_OPTMASK_NO_CLOSED_STREAMS) == 0 &&
      session->server && !is_my_stream_id &&
      nghttp2_stream_in_dep_tree(stream)) {
    /* On server side, retain stream at most MAX_CONCURRENT_STREAMS
       combined with the current active incoming streams to make
       dependency tree work better. */
    nghttp2_session_keep_closed_stream(session, stream);
  } else {
    rv = nghttp2_session_destroy_stream(session, stream);
    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

int nghttp2_session_destroy_stream(nghttp2_session *session,
                                   nghttp2_stream *stream) {
  nghttp2_mem *mem;
  int rv;

  DEBUGF("stream: destroy closed stream(%p)=%d\n", stream, stream->stream_id);

  mem = &session->mem;

  if (nghttp2_stream_in_dep_tree(stream)) {
    rv = nghttp2_stream_dep_remove(stream);
    if (rv != 0) {
      return rv;
    }
  }

  if (stream->queued &&
      (stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES)) {
    session_ob_data_remove(session, stream);
  }

  nghttp2_map_remove(&session->streams, stream->stream_id);
  nghttp2_stream_free(stream);
  nghttp2_mem_free(mem, stream);

  return 0;
}

void nghttp2_session_keep_closed_stream(nghttp2_session *session,
                                        nghttp2_stream *stream) {
  DEBUGF("stream: keep closed stream(%p)=%d, state=%d\n", stream,
         stream->stream_id, stream->state);

  if (session->closed_stream_tail) {
    session->closed_stream_tail->closed_next = stream;
    stream->closed_prev = session->closed_stream_tail;
  } else {
    session->closed_stream_head = stream;
  }
  session->closed_stream_tail = stream;

  ++session->num_closed_streams;
}

void nghttp2_session_keep_idle_stream(nghttp2_session *session,
                                      nghttp2_stream *stream) {
  DEBUGF("stream: keep idle stream(%p)=%d, state=%d\n", stream,
         stream->stream_id, stream->state);

  if (session->idle_stream_tail) {
    session->idle_stream_tail->closed_next = stream;
    stream->closed_prev = session->idle_stream_tail;
  } else {
    session->idle_stream_head = stream;
  }
  session->idle_stream_tail = stream;

  ++session->num_idle_streams;
}

void nghttp2_session_detach_idle_stream(nghttp2_session *session,
                                        nghttp2_stream *stream) {
  nghttp2_stream *prev_stream, *next_stream;

  DEBUGF("stream: detach idle stream(%p)=%d, state=%d\n", stream,
         stream->stream_id, stream->state);

  prev_stream = stream->closed_prev;
  next_stream = stream->closed_next;

  if (prev_stream) {
    prev_stream->closed_next = next_stream;
  } else {
    session->idle_stream_head = next_stream;
  }

  if (next_stream) {
    next_stream->closed_prev = prev_stream;
  } else {
    session->idle_stream_tail = prev_stream;
  }

  stream->closed_prev = NULL;
  stream->closed_next = NULL;

  --session->num_idle_streams;
}

int nghttp2_session_adjust_closed_stream(nghttp2_session *session) {
  size_t num_stream_max;
  int rv;

  if (session->local_settings.max_concurrent_streams ==
      NGHTTP2_DEFAULT_MAX_CONCURRENT_STREAMS) {
    num_stream_max = session->pending_local_max_concurrent_stream;
  } else {
    num_stream_max = session->local_settings.max_concurrent_streams;
  }

  DEBUGF("stream: adjusting kept closed streams num_closed_streams=%zu, "
         "num_incoming_streams=%zu, max_concurrent_streams=%zu\n",
         session->num_closed_streams, session->num_incoming_streams,
         num_stream_max);

  while (session->num_closed_streams > 0 &&
         session->num_closed_streams + session->num_incoming_streams >
             num_stream_max) {
    nghttp2_stream *head_stream;
    nghttp2_stream *next;

    head_stream = session->closed_stream_head;

    assert(head_stream);

    next = head_stream->closed_next;

    rv = nghttp2_session_destroy_stream(session, head_stream);
    if (rv != 0) {
      return rv;
    }

    /* head_stream is now freed */

    session->closed_stream_head = next;

    if (session->closed_stream_head) {
      session->closed_stream_head->closed_prev = NULL;
    } else {
      session->closed_stream_tail = NULL;
    }

    --session->num_closed_streams;
  }

  return 0;
}

int nghttp2_session_adjust_idle_stream(nghttp2_session *session) {
  size_t max;
  int rv;

  /* Make minimum number of idle streams 16, and maximum 100, which
     are arbitrary chosen numbers. */
  max = nghttp2_min_uint32(
      100, nghttp2_max_uint32(
               16, nghttp2_min_uint32(
                       session->local_settings.max_concurrent_streams,
                       session->pending_local_max_concurrent_stream)));

  DEBUGF("stream: adjusting kept idle streams num_idle_streams=%zu, max=%zu\n",
         session->num_idle_streams, max);

  while (session->num_idle_streams > max) {
    nghttp2_stream *head;
    nghttp2_stream *next;

    head = session->idle_stream_head;
    assert(head);

    next = head->closed_next;

    rv = nghttp2_session_destroy_stream(session, head);
    if (rv != 0) {
      return rv;
    }

    /* head is now destroyed */

    session->idle_stream_head = next;

    if (session->idle_stream_head) {
      session->idle_stream_head->closed_prev = NULL;
    } else {
      session->idle_stream_tail = NULL;
    }

    --session->num_idle_streams;
  }

  return 0;
}

/*
 * Closes stream with stream ID |stream_id| if both transmission and
 * reception of the stream were disallowed. The |error_code| indicates
 * the reason of the closure.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_INVALID_ARGUMENT
 *   The stream is not found.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *   The callback function failed.
 */
int nghttp2_session_close_stream_if_shut_rdwr(nghttp2_session *session,
                                              nghttp2_stream *stream) {
  if ((stream->shut_flags & NGHTTP2_SHUT_RDWR) == NGHTTP2_SHUT_RDWR) {
    return nghttp2_session_close_stream(session, stream->stream_id,
                                        NGHTTP2_NO_ERROR);
  }
  return 0;
}

/*
 * Returns nonzero if local endpoint allows reception of new stream
 * from remote.
 */
static int session_allow_incoming_new_stream(nghttp2_session *session) {
  return (session->goaway_flags &
          (NGHTTP2_GOAWAY_TERM_ON_SEND | NGHTTP2_GOAWAY_SENT)) == 0;
}

/*
 * This function returns nonzero if session is closing.
 */
static int session_is_closing(nghttp2_session *session) {
  return (session->goaway_flags & NGHTTP2_GOAWAY_TERM_ON_SEND) != 0 ||
         (nghttp2_session_want_read(session) == 0 &&
          nghttp2_session_want_write(session) == 0);
}

/*
 * Check that we can send a frame to the |stream|. This function
 * returns 0 if we can send a frame to the |frame|, or one of the
 * following negative error codes:
 *
 * NGHTTP2_ERR_STREAM_CLOSED
 *   The stream is already closed.
 * NGHTTP2_ERR_STREAM_SHUT_WR
 *   The stream is half-closed for transmission.
 * NGHTTP2_ERR_SESSION_CLOSING
 *   This session is closing.
 */
static int session_predicate_for_stream_send(nghttp2_session *session,
                                             nghttp2_stream *stream) {
  if (stream == NULL) {
    return NGHTTP2_ERR_STREAM_CLOSED;
  }
  if (session_is_closing(session)) {
    return NGHTTP2_ERR_SESSION_CLOSING;
  }
  if (stream->shut_flags & NGHTTP2_SHUT_WR) {
    return NGHTTP2_ERR_STREAM_SHUT_WR;
  }
  return 0;
}

int nghttp2_session_check_request_allowed(nghttp2_session *session) {
  return !session->server && session->next_stream_id <= INT32_MAX &&
         (session->goaway_flags & NGHTTP2_GOAWAY_RECV) == 0 &&
         !session_is_closing(session);
}

/*
 * This function checks request HEADERS frame, which opens stream, can
 * be sent at this time.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_START_STREAM_NOT_ALLOWED
 *     New stream cannot be created because of GOAWAY: session is
 *     going down or received last_stream_id is strictly less than
 *     frame->hd.stream_id.
 * NGHTTP2_ERR_STREAM_CLOSING
 *     request HEADERS was canceled by RST_STREAM while it is in queue.
 */
static int session_predicate_request_headers_send(nghttp2_session *session,
                                                  nghttp2_outbound_item *item) {
  if (item->aux_data.headers.canceled) {
    return NGHTTP2_ERR_STREAM_CLOSING;
  }
  /* If we are terminating session (NGHTTP2_GOAWAY_TERM_ON_SEND),
     GOAWAY was received from peer, or session is about to close, new
     request is not allowed. */
  if ((session->goaway_flags & NGHTTP2_GOAWAY_RECV) ||
      session_is_closing(session)) {
    return NGHTTP2_ERR_START_STREAM_NOT_ALLOWED;
  }
  return 0;
}

/*
 * This function checks HEADERS, which is the first frame from the
 * server, with the |stream| can be sent at this time.  The |stream|
 * can be NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_STREAM_CLOSED
 *     The stream is already closed or does not exist.
 * NGHTTP2_ERR_STREAM_SHUT_WR
 *     The transmission is not allowed for this stream (e.g., a frame
 *     with END_STREAM flag set has already sent)
 * NGHTTP2_ERR_INVALID_STREAM_ID
 *     The stream ID is invalid.
 * NGHTTP2_ERR_STREAM_CLOSING
 *     RST_STREAM was queued for this stream.
 * NGHTTP2_ERR_INVALID_STREAM_STATE
 *     The state of the stream is not valid.
 * NGHTTP2_ERR_SESSION_CLOSING
 *     This session is closing.
 * NGHTTP2_ERR_PROTO
 *     Client side attempted to send response.
 */
static int session_predicate_response_headers_send(nghttp2_session *session,
                                                   nghttp2_stream *stream) {
  int rv;
  rv = session_predicate_for_stream_send(session, stream);
  if (rv != 0) {
    return rv;
  }
  assert(stream);
  if (!session->server) {
    return NGHTTP2_ERR_PROTO;
  }
  if (nghttp2_session_is_my_stream_id(session, stream->stream_id)) {
    return NGHTTP2_ERR_INVALID_STREAM_ID;
  }
  switch (stream->state) {
  case NGHTTP2_STREAM_OPENING:
    return 0;
  case NGHTTP2_STREAM_CLOSING:
    return NGHTTP2_ERR_STREAM_CLOSING;
  default:
    return NGHTTP2_ERR_INVALID_STREAM_STATE;
  }
}

/*
 * This function checks HEADERS for reserved stream can be sent. The
 * |stream| must be reserved state and the |session| is server side.
 * The |stream| can be NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * error codes:
 *
 * NGHTTP2_ERR_STREAM_CLOSED
 *   The stream is already closed.
 * NGHTTP2_ERR_STREAM_SHUT_WR
 *   The stream is half-closed for transmission.
 * NGHTTP2_ERR_PROTO
 *   The stream is not reserved state
 * NGHTTP2_ERR_STREAM_CLOSED
 *   RST_STREAM was queued for this stream.
 * NGHTTP2_ERR_SESSION_CLOSING
 *   This session is closing.
 * NGHTTP2_ERR_START_STREAM_NOT_ALLOWED
 *   New stream cannot be created because GOAWAY is already sent or
 *   received.
 * NGHTTP2_ERR_PROTO
 *   Client side attempted to send push response.
 */
static int
session_predicate_push_response_headers_send(nghttp2_session *session,
                                             nghttp2_stream *stream) {
  int rv;
  /* TODO Should disallow HEADERS if GOAWAY has already been issued? */
  rv = session_predicate_for_stream_send(session, stream);
  if (rv != 0) {
    return rv;
  }
  assert(stream);
  if (!session->server) {
    return NGHTTP2_ERR_PROTO;
  }
  if (stream->state != NGHTTP2_STREAM_RESERVED) {
    return NGHTTP2_ERR_PROTO;
  }
  if (session->goaway_flags & NGHTTP2_GOAWAY_RECV) {
    return NGHTTP2_ERR_START_STREAM_NOT_ALLOWED;
  }
  return 0;
}

/*
 * This function checks HEADERS, which is neither stream-opening nor
 * first response header, with the |stream| can be sent at this time.
 * The |stream| can be NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_STREAM_CLOSED
 *     The stream is already closed or does not exist.
 * NGHTTP2_ERR_STREAM_SHUT_WR
 *     The transmission is not allowed for this stream (e.g., a frame
 *     with END_STREAM flag set has already sent)
 * NGHTTP2_ERR_STREAM_CLOSING
 *     RST_STREAM was queued for this stream.
 * NGHTTP2_ERR_INVALID_STREAM_STATE
 *     The state of the stream is not valid.
 * NGHTTP2_ERR_SESSION_CLOSING
 *   This session is closing.
 */
static int session_predicate_headers_send(nghttp2_session *session,
                                          nghttp2_stream *stream) {
  int rv;
  rv = session_predicate_for_stream_send(session, stream);
  if (rv != 0) {
    return rv;
  }
  assert(stream);

  switch (stream->state) {
  case NGHTTP2_STREAM_OPENED:
    return 0;
  case NGHTTP2_STREAM_CLOSING:
    return NGHTTP2_ERR_STREAM_CLOSING;
  default:
    if (nghttp2_session_is_my_stream_id(session, stream->stream_id)) {
      return 0;
    }
    return NGHTTP2_ERR_INVALID_STREAM_STATE;
  }
}

/*
 * This function checks PUSH_PROMISE frame |frame| with the |stream|
 * can be sent at this time.  The |stream| can be NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_START_STREAM_NOT_ALLOWED
 *     New stream cannot be created because GOAWAY is already sent or
 *     received.
 * NGHTTP2_ERR_PROTO
 *     The client side attempts to send PUSH_PROMISE, or the server
 *     sends PUSH_PROMISE for the stream not initiated by the client.
 * NGHTTP2_ERR_STREAM_CLOSED
 *     The stream is already closed or does not exist.
 * NGHTTP2_ERR_STREAM_CLOSING
 *     RST_STREAM was queued for this stream.
 * NGHTTP2_ERR_STREAM_SHUT_WR
 *     The transmission is not allowed for this stream (e.g., a frame
 *     with END_STREAM flag set has already sent)
 * NGHTTP2_ERR_PUSH_DISABLED
 *     The remote peer disabled reception of PUSH_PROMISE.
 * NGHTTP2_ERR_SESSION_CLOSING
 *   This session is closing.
 */
static int session_predicate_push_promise_send(nghttp2_session *session,
                                               nghttp2_stream *stream) {
  int rv;

  if (!session->server) {
    return NGHTTP2_ERR_PROTO;
  }

  rv = session_predicate_for_stream_send(session, stream);
  if (rv != 0) {
    return rv;
  }

  assert(stream);

  if (session->remote_settings.enable_push == 0) {
    return NGHTTP2_ERR_PUSH_DISABLED;
  }
  if (stream->state == NGHTTP2_STREAM_CLOSING) {
    return NGHTTP2_ERR_STREAM_CLOSING;
  }
  if (session->goaway_flags & NGHTTP2_GOAWAY_RECV) {
    return NGHTTP2_ERR_START_STREAM_NOT_ALLOWED;
  }
  return 0;
}

/*
 * This function checks WINDOW_UPDATE with the stream ID |stream_id|
 * can be sent at this time. Note that END_STREAM flag of the previous
 * frame does not affect the transmission of the WINDOW_UPDATE frame.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_STREAM_CLOSED
 *     The stream is already closed or does not exist.
 * NGHTTP2_ERR_STREAM_CLOSING
 *     RST_STREAM was queued for this stream.
 * NGHTTP2_ERR_INVALID_STREAM_STATE
 *     The state of the stream is not valid.
 * NGHTTP2_ERR_SESSION_CLOSING
 *   This session is closing.
 */
static int session_predicate_window_update_send(nghttp2_session *session,
                                                int32_t stream_id) {
  nghttp2_stream *stream;

  if (session_is_closing(session)) {
    return NGHTTP2_ERR_SESSION_CLOSING;
  }

  if (stream_id == 0) {
    /* Connection-level window update */
    return 0;
  }
  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream == NULL) {
    return NGHTTP2_ERR_STREAM_CLOSED;
  }
  if (stream->state == NGHTTP2_STREAM_CLOSING) {
    return NGHTTP2_ERR_STREAM_CLOSING;
  }
  if (state_reserved_local(session, stream)) {
    return NGHTTP2_ERR_INVALID_STREAM_STATE;
  }
  return 0;
}

static int session_predicate_altsvc_send(nghttp2_session *session,
                                         int32_t stream_id) {
  nghttp2_stream *stream;

  if (session_is_closing(session)) {
    return NGHTTP2_ERR_SESSION_CLOSING;
  }

  if (stream_id == 0) {
    return 0;
  }

  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream == NULL) {
    return NGHTTP2_ERR_STREAM_CLOSED;
  }
  if (stream->state == NGHTTP2_STREAM_CLOSING) {
    return NGHTTP2_ERR_STREAM_CLOSING;
  }

  return 0;
}

static int session_predicate_origin_send(nghttp2_session *session) {
  if (session_is_closing(session)) {
    return NGHTTP2_ERR_SESSION_CLOSING;
  }
  return 0;
}

static int session_predicate_priority_update_send(nghttp2_session *session,
                                                  int32_t stream_id) {
  nghttp2_stream *stream;

  if (session_is_closing(session)) {
    return NGHTTP2_ERR_SESSION_CLOSING;
  }

  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream == NULL) {
    return 0;
  }
  if (stream->state == NGHTTP2_STREAM_CLOSING) {
    return NGHTTP2_ERR_STREAM_CLOSING;
  }
  if (stream->shut_flags & NGHTTP2_SHUT_RD) {
    return NGHTTP2_ERR_INVALID_STREAM_STATE;
  }

  return 0;
}

/* Take into account settings max frame size and both connection-level
   flow control here */
static nghttp2_ssize nghttp2_session_enforce_flow_control_limits(
    nghttp2_session *session, nghttp2_stream *stream,
    nghttp2_ssize requested_window_size) {
  DEBUGF("send: remote windowsize connection=%d, remote maxframsize=%u, "
         "stream(id %d)=%d\n",
         session->remote_window_size, session->remote_settings.max_frame_size,
         stream->stream_id, stream->remote_window_size);

  return nghttp2_min_int32(
      nghttp2_min_int32(nghttp2_min_int32((int32_t)requested_window_size,
                                          stream->remote_window_size),
                        session->remote_window_size),
      (int32_t)session->remote_settings.max_frame_size);
}

/*
 * Returns the maximum length of next data read. If the
 * connection-level and/or stream-wise flow control are enabled, the
 * return value takes into account those current window sizes. The remote
 * settings for max frame size is also taken into account.
 */
static size_t nghttp2_session_next_data_read(nghttp2_session *session,
                                             nghttp2_stream *stream) {
  nghttp2_ssize window_size;

  window_size = nghttp2_session_enforce_flow_control_limits(
      session, stream, NGHTTP2_DATA_PAYLOADLEN);

  DEBUGF("send: available window=%td\n", window_size);

  return window_size > 0 ? (size_t)window_size : 0;
}

/*
 * This function checks DATA with the |stream| can be sent at this
 * time.  The |stream| can be NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_STREAM_CLOSED
 *     The stream is already closed or does not exist.
 * NGHTTP2_ERR_STREAM_SHUT_WR
 *     The transmission is not allowed for this stream (e.g., a frame
 *     with END_STREAM flag set has already sent)
 * NGHTTP2_ERR_STREAM_CLOSING
 *     RST_STREAM was queued for this stream.
 * NGHTTP2_ERR_INVALID_STREAM_STATE
 *     The state of the stream is not valid.
 * NGHTTP2_ERR_SESSION_CLOSING
 *   This session is closing.
 */
static int nghttp2_session_predicate_data_send(nghttp2_session *session,
                                               nghttp2_stream *stream) {
  int rv;
  rv = session_predicate_for_stream_send(session, stream);
  if (rv != 0) {
    return rv;
  }
  assert(stream);
  if (nghttp2_session_is_my_stream_id(session, stream->stream_id)) {
    /* Request body data */
    /* If stream->state is NGHTTP2_STREAM_CLOSING, RST_STREAM was
       queued but not yet sent. In this case, we won't send DATA
       frames. */
    if (stream->state == NGHTTP2_STREAM_CLOSING) {
      return NGHTTP2_ERR_STREAM_CLOSING;
    }
    if (stream->state == NGHTTP2_STREAM_RESERVED) {
      return NGHTTP2_ERR_INVALID_STREAM_STATE;
    }
    return 0;
  }
  /* Response body data */
  if (stream->state == NGHTTP2_STREAM_OPENED) {
    return 0;
  }
  if (stream->state == NGHTTP2_STREAM_CLOSING) {
    return NGHTTP2_ERR_STREAM_CLOSING;
  }
  return NGHTTP2_ERR_INVALID_STREAM_STATE;
}

static nghttp2_ssize session_call_select_padding(nghttp2_session *session,
                                                 const nghttp2_frame *frame,
                                                 size_t max_payloadlen) {
  nghttp2_ssize rv;
  size_t max_paddedlen;

  if (frame->hd.length >= max_payloadlen ||
      (!session->callbacks.select_padding_callback2 &&
       !session->callbacks.select_padding_callback)) {
    return (nghttp2_ssize)frame->hd.length;
  }

  max_paddedlen =
      nghttp2_min_size(frame->hd.length + NGHTTP2_MAX_PADLEN, max_payloadlen);

  if (session->callbacks.select_padding_callback2) {
    rv = session->callbacks.select_padding_callback2(
        session, frame, max_paddedlen, session->user_data);
  } else {
    rv = (nghttp2_ssize)session->callbacks.select_padding_callback(
        session, frame, max_paddedlen, session->user_data);
  }
  if (rv < (nghttp2_ssize)frame->hd.length ||
      rv > (nghttp2_ssize)max_paddedlen) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
  return rv;
}

/* Add padding to HEADERS or PUSH_PROMISE. We use
   frame->headers.padlen in this function to use the fact that
   frame->push_promise has also padlen in the same position. */
static int session_headers_add_pad(nghttp2_session *session,
                                   nghttp2_frame *frame) {
  nghttp2_ssize padded_payloadlen;
  nghttp2_active_outbound_item *aob;
  nghttp2_bufs *framebufs;
  size_t padlen;
  size_t max_payloadlen;

  aob = &session->aob;
  framebufs = &aob->framebufs;

  max_payloadlen = nghttp2_min_size(NGHTTP2_MAX_PAYLOADLEN,
                                    frame->hd.length + NGHTTP2_MAX_PADLEN);

  padded_payloadlen =
      session_call_select_padding(session, frame, max_payloadlen);

  if (nghttp2_is_fatal((int)padded_payloadlen)) {
    return (int)padded_payloadlen;
  }

  padlen = (size_t)padded_payloadlen - frame->hd.length;

  DEBUGF("send: padding selected: payloadlen=%td, padlen=%zu\n",
         padded_payloadlen, padlen);

  nghttp2_frame_add_pad(framebufs, &frame->hd, padlen, 0);

  frame->headers.padlen = padlen;

  return 0;
}

static size_t session_estimate_headers_payload(nghttp2_session *session,
                                               const nghttp2_nv *nva,
                                               size_t nvlen,
                                               size_t additional) {
  return nghttp2_hd_deflate_bound(&session->hd_deflater, nva, nvlen) +
         additional;
}

static int session_pack_extension(nghttp2_session *session, nghttp2_bufs *bufs,
                                  nghttp2_frame *frame) {
  nghttp2_ssize rv;
  nghttp2_buf *buf;
  size_t buflen;
  size_t framelen;

  assert(session->callbacks.pack_extension_callback2 ||
         session->callbacks.pack_extension_callback);

  buf = &bufs->head->buf;
  buflen = nghttp2_min_size(nghttp2_buf_avail(buf), NGHTTP2_MAX_PAYLOADLEN);

  if (session->callbacks.pack_extension_callback2) {
    rv = session->callbacks.pack_extension_callback2(session, buf->last, buflen,
                                                     frame, session->user_data);
  } else {
    rv = (nghttp2_ssize)session->callbacks.pack_extension_callback(
        session, buf->last, buflen, frame, session->user_data);
  }
  if (rv == NGHTTP2_ERR_CANCEL) {
    return (int)rv;
  }

  if (rv < 0 || (size_t)rv > buflen) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }

  framelen = (size_t)rv;

  frame->hd.length = framelen;

  assert(buf->pos == buf->last);
  buf->last += framelen;
  buf->pos -= NGHTTP2_FRAME_HDLEN;

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  return 0;
}

/*
 * This function serializes frame for transmission.
 *
 * This function returns 0 if it succeeds, or one of negative error
 * codes, including both fatal and non-fatal ones.
 */
static int session_prep_frame(nghttp2_session *session,
                              nghttp2_outbound_item *item) {
  int rv;
  nghttp2_frame *frame;
  nghttp2_mem *mem;

  mem = &session->mem;
  frame = &item->frame;

  switch (frame->hd.type) {
  case NGHTTP2_DATA: {
    size_t next_readmax;
    nghttp2_stream *stream;

    stream = nghttp2_session_get_stream(session, frame->hd.stream_id);

    if (stream) {
      assert(stream->item == item);
    }

    rv = nghttp2_session_predicate_data_send(session, stream);
    if (rv != 0) {
      // If stream was already closed, nghttp2_session_get_stream()
      // returns NULL, but item is still attached to the stream.
      // Search stream including closed again.
      stream = nghttp2_session_get_stream_raw(session, frame->hd.stream_id);
      if (stream) {
        session_detach_stream_item(session, stream);
      }

      return rv;
    }
    /* Assuming stream is not NULL */
    assert(stream);
    next_readmax = nghttp2_session_next_data_read(session, stream);

    if (next_readmax == 0) {

      /* This must be true since we only pop DATA frame item from
         queue when session->remote_window_size > 0 */
      assert(session->remote_window_size > 0);

      session_defer_stream_item(session, stream,
                                NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL);

      session->aob.item = NULL;
      active_outbound_item_reset(&session->aob, mem);
      return NGHTTP2_ERR_DEFERRED;
    }

    rv = nghttp2_session_pack_data(session, &session->aob.framebufs,
                                   next_readmax, frame, &item->aux_data.data,
                                   stream);
    if (rv == NGHTTP2_ERR_PAUSE) {
      return rv;
    }
    if (rv == NGHTTP2_ERR_DEFERRED) {
      session_defer_stream_item(session, stream,
                                NGHTTP2_STREAM_FLAG_DEFERRED_USER);

      session->aob.item = NULL;
      active_outbound_item_reset(&session->aob, mem);
      return NGHTTP2_ERR_DEFERRED;
    }
    if (rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
      session_detach_stream_item(session, stream);

      rv = nghttp2_session_add_rst_stream(session, frame->hd.stream_id,
                                          NGHTTP2_INTERNAL_ERROR);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }
      return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
    }
    if (rv != 0) {
      session_detach_stream_item(session, stream);

      return rv;
    }
    return 0;
  }
  case NGHTTP2_HEADERS: {
    nghttp2_headers_aux_data *aux_data;
    size_t estimated_payloadlen;

    aux_data = &item->aux_data.headers;

    if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
      /* initial HEADERS, which opens stream */
      nghttp2_stream *stream;

      stream = nghttp2_session_open_stream(
          session, frame->hd.stream_id, NGHTTP2_STREAM_FLAG_NONE,
          &frame->headers.pri_spec, NGHTTP2_STREAM_INITIAL,
          aux_data->stream_user_data);

      if (stream == NULL) {
        return NGHTTP2_ERR_NOMEM;
      }

      /* We don't call nghttp2_session_adjust_closed_stream() here,
         since we don't keep closed stream in client side */

      rv = session_predicate_request_headers_send(session, item);
      if (rv != 0) {
        return rv;
      }

      if (session_enforce_http_messaging(session)) {
        nghttp2_http_record_request_method(stream, frame);
      }
    } else {
      nghttp2_stream *stream;

      stream = nghttp2_session_get_stream(session, frame->hd.stream_id);

      if (stream && stream->state == NGHTTP2_STREAM_RESERVED) {
        rv = session_predicate_push_response_headers_send(session, stream);
        if (rv == 0) {
          frame->headers.cat = NGHTTP2_HCAT_PUSH_RESPONSE;

          if (aux_data->stream_user_data) {
            stream->stream_user_data = aux_data->stream_user_data;
          }
        }
      } else if (session_predicate_response_headers_send(session, stream) ==
                 0) {
        frame->headers.cat = NGHTTP2_HCAT_RESPONSE;
        rv = 0;
      } else {
        frame->headers.cat = NGHTTP2_HCAT_HEADERS;

        rv = session_predicate_headers_send(session, stream);
      }

      if (rv != 0) {
        return rv;
      }
    }

    estimated_payloadlen = session_estimate_headers_payload(
        session, frame->headers.nva, frame->headers.nvlen,
        NGHTTP2_PRIORITY_SPECLEN);

    if (estimated_payloadlen > session->max_send_header_block_length) {
      return NGHTTP2_ERR_FRAME_SIZE_ERROR;
    }

    rv = nghttp2_frame_pack_headers(&session->aob.framebufs, &frame->headers,
                                    &session->hd_deflater);

    if (rv != 0) {
      return rv;
    }

    DEBUGF("send: before padding, HEADERS serialized in %zu bytes\n",
           nghttp2_bufs_len(&session->aob.framebufs));

    rv = session_headers_add_pad(session, frame);

    if (rv != 0) {
      return rv;
    }

    DEBUGF("send: HEADERS finally serialized in %zu bytes\n",
           nghttp2_bufs_len(&session->aob.framebufs));

    if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
      assert(session->last_sent_stream_id < frame->hd.stream_id);
      session->last_sent_stream_id = frame->hd.stream_id;
    }

    return 0;
  }
  case NGHTTP2_PRIORITY: {
    if (session_is_closing(session)) {
      return NGHTTP2_ERR_SESSION_CLOSING;
    }
    /* PRIORITY frame can be sent at any time and to any stream
       ID. */
    nghttp2_frame_pack_priority(&session->aob.framebufs, &frame->priority);

    /* Peer can send PRIORITY frame against idle stream to create
       "anchor" in dependency tree.  Only client can do this in
       nghttp2.  In nghttp2, only server retains non-active (closed
       or idle) streams in memory, so we don't open stream here. */
    return 0;
  }
  case NGHTTP2_RST_STREAM:
    if (session_is_closing(session)) {
      return NGHTTP2_ERR_SESSION_CLOSING;
    }
    nghttp2_frame_pack_rst_stream(&session->aob.framebufs, &frame->rst_stream);
    return 0;
  case NGHTTP2_SETTINGS: {
    if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
      assert(session->obq_flood_counter_ > 0);
      --session->obq_flood_counter_;
      /* When session is about to close, don't send SETTINGS ACK.
         We are required to send SETTINGS without ACK though; for
         example, we have to send SETTINGS as a part of connection
         preface. */
      if (session_is_closing(session)) {
        return NGHTTP2_ERR_SESSION_CLOSING;
      }
    }

    rv = nghttp2_frame_pack_settings(&session->aob.framebufs, &frame->settings);
    if (rv != 0) {
      return rv;
    }
    return 0;
  }
  case NGHTTP2_PUSH_PROMISE: {
    nghttp2_stream *stream;
    size_t estimated_payloadlen;

    /* stream could be NULL if associated stream was already
       closed. */
    stream = nghttp2_session_get_stream(session, frame->hd.stream_id);

    /* predicate should fail if stream is NULL. */
    rv = session_predicate_push_promise_send(session, stream);
    if (rv != 0) {
      return rv;
    }

    assert(stream);

    estimated_payloadlen = session_estimate_headers_payload(
        session, frame->push_promise.nva, frame->push_promise.nvlen, 0);

    if (estimated_payloadlen > session->max_send_header_block_length) {
      return NGHTTP2_ERR_FRAME_SIZE_ERROR;
    }

    rv = nghttp2_frame_pack_push_promise(
        &session->aob.framebufs, &frame->push_promise, &session->hd_deflater);
    if (rv != 0) {
      return rv;
    }
    rv = session_headers_add_pad(session, frame);
    if (rv != 0) {
      return rv;
    }

    assert(session->last_sent_stream_id + 2 <=
           frame->push_promise.promised_stream_id);
    session->last_sent_stream_id = frame->push_promise.promised_stream_id;

    return 0;
  }
  case NGHTTP2_PING:
    if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
      assert(session->obq_flood_counter_ > 0);
      --session->obq_flood_counter_;
    }
    /* PING frame is allowed to be sent unless termination GOAWAY is
       sent */
    if (session->goaway_flags & NGHTTP2_GOAWAY_TERM_ON_SEND) {
      return NGHTTP2_ERR_SESSION_CLOSING;
    }
    nghttp2_frame_pack_ping(&session->aob.framebufs, &frame->ping);
    return 0;
  case NGHTTP2_GOAWAY:
    rv = nghttp2_frame_pack_goaway(&session->aob.framebufs, &frame->goaway);
    if (rv != 0) {
      return rv;
    }
    session->local_last_stream_id = frame->goaway.last_stream_id;

    return 0;
  case NGHTTP2_WINDOW_UPDATE:
    rv = session_predicate_window_update_send(session, frame->hd.stream_id);
    if (rv != 0) {
      return rv;
    }
    nghttp2_frame_pack_window_update(&session->aob.framebufs,
                                     &frame->window_update);
    return 0;
  case NGHTTP2_CONTINUATION:
    /* We never handle CONTINUATION here. */
    assert(0);
    return 0;
  default: {
    nghttp2_ext_aux_data *aux_data;

    /* extension frame */

    aux_data = &item->aux_data.ext;

    if (aux_data->builtin == 0) {
      if (session_is_closing(session)) {
        return NGHTTP2_ERR_SESSION_CLOSING;
      }

      return session_pack_extension(session, &session->aob.framebufs, frame);
    }

    switch (frame->hd.type) {
    case NGHTTP2_ALTSVC:
      rv = session_predicate_altsvc_send(session, frame->hd.stream_id);
      if (rv != 0) {
        return rv;
      }

      nghttp2_frame_pack_altsvc(&session->aob.framebufs, &frame->ext);

      return 0;
    case NGHTTP2_ORIGIN:
      rv = session_predicate_origin_send(session);
      if (rv != 0) {
        return rv;
      }

      rv = nghttp2_frame_pack_origin(&session->aob.framebufs, &frame->ext);
      if (rv != 0) {
        return rv;
      }

      return 0;
    case NGHTTP2_PRIORITY_UPDATE: {
      nghttp2_ext_priority_update *priority_update = frame->ext.payload;
      rv = session_predicate_priority_update_send(session,
                                                  priority_update->stream_id);
      if (rv != 0) {
        return rv;
      }

      nghttp2_frame_pack_priority_update(&session->aob.framebufs, &frame->ext);

      return 0;
    }
    default:
      /* Unreachable here */
      assert(0);
      return 0;
    }
  }
  }
}

nghttp2_outbound_item *
nghttp2_session_get_next_ob_item(nghttp2_session *session) {
  nghttp2_outbound_item *item;

  if (nghttp2_outbound_queue_top(&session->ob_urgent)) {
    return nghttp2_outbound_queue_top(&session->ob_urgent);
  }

  if (nghttp2_outbound_queue_top(&session->ob_reg)) {
    return nghttp2_outbound_queue_top(&session->ob_reg);
  }

  if (!session_is_outgoing_concurrent_streams_max(session)) {
    if (nghttp2_outbound_queue_top(&session->ob_syn)) {
      return nghttp2_outbound_queue_top(&session->ob_syn);
    }
  }

  if (session->remote_window_size > 0) {
    item = nghttp2_stream_next_outbound_item(&session->root);
    if (item) {
      return item;
    }

    return session_sched_get_next_outbound_item(session);
  }

  return NULL;
}

nghttp2_outbound_item *
nghttp2_session_pop_next_ob_item(nghttp2_session *session) {
  nghttp2_outbound_item *item;

  item = nghttp2_outbound_queue_top(&session->ob_urgent);
  if (item) {
    nghttp2_outbound_queue_pop(&session->ob_urgent);
    item->queued = 0;
    return item;
  }

  item = nghttp2_outbound_queue_top(&session->ob_reg);
  if (item) {
    nghttp2_outbound_queue_pop(&session->ob_reg);
    item->queued = 0;
    return item;
  }

  if (!session_is_outgoing_concurrent_streams_max(session)) {
    item = nghttp2_outbound_queue_top(&session->ob_syn);
    if (item) {
      nghttp2_outbound_queue_pop(&session->ob_syn);
      item->queued = 0;
      return item;
    }
  }

  if (session->remote_window_size > 0) {
    item = nghttp2_stream_next_outbound_item(&session->root);
    if (item) {
      return item;
    }

    return session_sched_get_next_outbound_item(session);
  }

  return NULL;
}

static int session_call_before_frame_send(nghttp2_session *session,
                                          nghttp2_frame *frame) {
  int rv;
  if (session->callbacks.before_frame_send_callback) {
    rv = session->callbacks.before_frame_send_callback(session, frame,
                                                       session->user_data);
    if (rv == NGHTTP2_ERR_CANCEL) {
      return rv;
    }

    if (rv != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

static int session_call_on_frame_send(nghttp2_session *session,
                                      nghttp2_frame *frame) {
  int rv;
  if (session->callbacks.on_frame_send_callback) {
    rv = session->callbacks.on_frame_send_callback(session, frame,
                                                   session->user_data);
    if (rv != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

static int find_stream_on_goaway_func(void *entry, void *ptr) {
  nghttp2_close_stream_on_goaway_arg *arg;
  nghttp2_stream *stream;

  arg = (nghttp2_close_stream_on_goaway_arg *)ptr;
  stream = (nghttp2_stream *)entry;

  if (nghttp2_session_is_my_stream_id(arg->session, stream->stream_id)) {
    if (arg->incoming) {
      return 0;
    }
  } else if (!arg->incoming) {
    return 0;
  }

  if (stream->state != NGHTTP2_STREAM_IDLE &&
      (stream->flags & NGHTTP2_STREAM_FLAG_CLOSED) == 0 &&
      stream->stream_id > arg->last_stream_id) {
    /* We are collecting streams to close because we cannot call
       nghttp2_session_close_stream() inside nghttp2_map_each().
       Reuse closed_next member.. bad choice? */
    assert(stream->closed_next == NULL);
    assert(stream->closed_prev == NULL);

    if (arg->head) {
      stream->closed_next = arg->head;
      arg->head = stream;
    } else {
      arg->head = stream;
    }
  }

  return 0;
}

/* Closes non-idle and non-closed streams whose stream ID >
   last_stream_id.  If incoming is nonzero, we are going to close
   incoming streams.  Otherwise, close outgoing streams. */
static int session_close_stream_on_goaway(nghttp2_session *session,
                                          int32_t last_stream_id,
                                          int incoming) {
  int rv;
  nghttp2_stream *stream, *next_stream;
  nghttp2_close_stream_on_goaway_arg arg = {session, NULL, last_stream_id,
                                            incoming};

  rv = nghttp2_map_each(&session->streams, find_stream_on_goaway_func, &arg);
  assert(rv == 0);

  stream = arg.head;
  while (stream) {
    next_stream = stream->closed_next;
    stream->closed_next = NULL;
    rv = nghttp2_session_close_stream(session, stream->stream_id,
                                      NGHTTP2_REFUSED_STREAM);

    /* stream may be deleted here */

    stream = next_stream;

    if (nghttp2_is_fatal(rv)) {
      /* Clean up closed_next member just in case */
      while (stream) {
        next_stream = stream->closed_next;
        stream->closed_next = NULL;
        stream = next_stream;
      }
      return rv;
    }
  }

  return 0;
}

static void session_reschedule_stream(nghttp2_session *session,
                                      nghttp2_stream *stream) {
  stream->last_writelen = stream->item->frame.hd.length;

  if (!(stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES)) {
    nghttp2_stream_reschedule(stream);
    return;
  }

  if (!session->server) {
    return;
  }

  session_sched_reschedule_stream(session, stream);
}

static int session_update_stream_consumed_size(nghttp2_session *session,
                                               nghttp2_stream *stream,
                                               size_t delta_size);

static int session_update_connection_consumed_size(nghttp2_session *session,
                                                   size_t delta_size);

/*
 * Called after a frame is sent.  This function runs
 * on_frame_send_callback and handles stream closure upon END_STREAM
 * or RST_STREAM.  This function does not reset session->aob.  It is a
 * responsibility of session_after_frame_sent2.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 */
static int session_after_frame_sent1(nghttp2_session *session) {
  int rv;
  nghttp2_active_outbound_item *aob = &session->aob;
  nghttp2_outbound_item *item = aob->item;
  nghttp2_bufs *framebufs = &aob->framebufs;
  nghttp2_frame *frame;
  nghttp2_stream *stream;

  frame = &item->frame;

  if (frame->hd.type == NGHTTP2_DATA) {
    nghttp2_data_aux_data *aux_data;

    aux_data = &item->aux_data.data;

    stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
    /* We update flow control window after a frame was completely
       sent. This is possible because we choose payload length not to
       exceed the window */
    session->remote_window_size -= (int32_t)frame->hd.length;
    if (stream) {
      stream->remote_window_size -= (int32_t)frame->hd.length;
    }

    if (stream && aux_data->eof) {
      session_detach_stream_item(session, stream);

      /* Call on_frame_send_callback after
         nghttp2_stream_detach_item(), so that application can issue
         nghttp2_submit_data2() in the callback. */
      if (session->callbacks.on_frame_send_callback) {
        rv = session_call_on_frame_send(session, frame);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }
      }

      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        int stream_closed;

        stream_closed =
            (stream->shut_flags & NGHTTP2_SHUT_RDWR) == NGHTTP2_SHUT_RDWR;

        nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_WR);

        rv = nghttp2_session_close_stream_if_shut_rdwr(session, stream);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }
        /* stream may be NULL if it was closed */
        if (stream_closed) {
          stream = NULL;
        }
      }
      return 0;
    }

    if (session->callbacks.on_frame_send_callback) {
      rv = session_call_on_frame_send(session, frame);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }
    }

    return 0;
  }

  /* non-DATA frame */

  if (frame->hd.type == NGHTTP2_HEADERS ||
      frame->hd.type == NGHTTP2_PUSH_PROMISE) {
    if (nghttp2_bufs_next_present(framebufs)) {
      DEBUGF("send: CONTINUATION exists, just return\n");
      return 0;
    }
  }
  rv = session_call_on_frame_send(session, frame);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }
  switch (frame->hd.type) {
  case NGHTTP2_HEADERS: {
    nghttp2_headers_aux_data *aux_data;

    stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
    if (!stream) {
      return 0;
    }

    switch (frame->headers.cat) {
    case NGHTTP2_HCAT_REQUEST: {
      stream->state = NGHTTP2_STREAM_OPENING;
      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_WR);
      }
      rv = nghttp2_session_close_stream_if_shut_rdwr(session, stream);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }
      /* We assume aux_data is a pointer to nghttp2_headers_aux_data */
      aux_data = &item->aux_data.headers;
      if (aux_data->dpw.data_prd.read_callback) {
        /* nghttp2_submit_data_shared() makes a copy of
           aux_data->dpw */
        rv = nghttp2_submit_data_shared(session, NGHTTP2_FLAG_END_STREAM,
                                        frame->hd.stream_id, &aux_data->dpw);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }
        /* TODO nghttp2_submit_data_shared() may fail if stream has
           already DATA frame item.  We might have to handle it
           here. */
      }
      return 0;
    }
    case NGHTTP2_HCAT_PUSH_RESPONSE:
      stream->flags = (uint8_t)(stream->flags & ~NGHTTP2_STREAM_FLAG_PUSH);
      ++session->num_outgoing_streams;
    /* Fall through */
    case NGHTTP2_HCAT_RESPONSE:
      stream->state = NGHTTP2_STREAM_OPENED;
    /* Fall through */
    case NGHTTP2_HCAT_HEADERS:
      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_WR);
      }
      rv = nghttp2_session_close_stream_if_shut_rdwr(session, stream);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }
      /* We assume aux_data is a pointer to nghttp2_headers_aux_data */
      aux_data = &item->aux_data.headers;
      if (aux_data->dpw.data_prd.read_callback) {
        rv = nghttp2_submit_data_shared(session, NGHTTP2_FLAG_END_STREAM,
                                        frame->hd.stream_id, &aux_data->dpw);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }
        /* TODO nghttp2_submit_data_shared() may fail if stream has
           already DATA frame item.  We might have to handle it
           here. */
      }
      return 0;
    default:
      /* Unreachable */
      assert(0);
      return 0;
    }
  }
  case NGHTTP2_PRIORITY:
    if (session->server || session->pending_no_rfc7540_priorities == 1) {
      return 0;
    }

    stream = nghttp2_session_get_stream_raw(session, frame->hd.stream_id);

    if (!stream) {
      if (!session_detect_idle_stream(session, frame->hd.stream_id)) {
        return 0;
      }

      stream = nghttp2_session_open_stream(
          session, frame->hd.stream_id, NGHTTP2_FLAG_NONE,
          &frame->priority.pri_spec, NGHTTP2_STREAM_IDLE, NULL);
      if (!stream) {
        return NGHTTP2_ERR_NOMEM;
      }
    } else {
      rv = nghttp2_session_reprioritize_stream(session, stream,
                                               &frame->priority.pri_spec);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }
    }

    rv = nghttp2_session_adjust_idle_stream(session);

    if (nghttp2_is_fatal(rv)) {
      return rv;
    }

    return 0;
  case NGHTTP2_RST_STREAM:
    rv = nghttp2_session_close_stream(session, frame->hd.stream_id,
                                      frame->rst_stream.error_code);
    if (nghttp2_is_fatal(rv)) {
      return rv;
    }
    return 0;
  case NGHTTP2_GOAWAY: {
    nghttp2_goaway_aux_data *aux_data;

    aux_data = &item->aux_data.goaway;

    if ((aux_data->flags & NGHTTP2_GOAWAY_AUX_SHUTDOWN_NOTICE) == 0) {

      if (aux_data->flags & NGHTTP2_GOAWAY_AUX_TERM_ON_SEND) {
        session->goaway_flags |= NGHTTP2_GOAWAY_TERM_SENT;
      }

      session->goaway_flags |= NGHTTP2_GOAWAY_SENT;

      rv = session_close_stream_on_goaway(session, frame->goaway.last_stream_id,
                                          1);

      if (nghttp2_is_fatal(rv)) {
        return rv;
      }
    }

    return 0;
  }
  case NGHTTP2_WINDOW_UPDATE:
    if (frame->hd.stream_id == 0) {
      session->window_update_queued = 0;
      if (session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE) {
        rv = session_update_connection_consumed_size(session, 0);
      } else {
        rv = nghttp2_session_update_recv_connection_window_size(session, 0);
      }

      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      return 0;
    }

    stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
    if (!stream) {
      return 0;
    }

    stream->window_update_queued = 0;

    /* We don't have to send WINDOW_UPDATE if END_STREAM from peer
       is seen. */
    if (stream->shut_flags & NGHTTP2_SHUT_RD) {
      return 0;
    }

    if (session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE) {
      rv = session_update_stream_consumed_size(session, stream, 0);
    } else {
      rv =
          nghttp2_session_update_recv_stream_window_size(session, stream, 0, 1);
    }

    if (nghttp2_is_fatal(rv)) {
      return rv;
    }

    return 0;
  default:
    return 0;
  }
}

/*
 * Called after a frame is sent and session_after_frame_sent1.  This
 * function is responsible to reset session->aob.
 */
static void session_after_frame_sent2(nghttp2_session *session) {
  nghttp2_active_outbound_item *aob = &session->aob;
  nghttp2_outbound_item *item = aob->item;
  nghttp2_bufs *framebufs = &aob->framebufs;
  nghttp2_frame *frame;
  nghttp2_mem *mem;
  nghttp2_stream *stream;
  nghttp2_data_aux_data *aux_data;

  mem = &session->mem;
  frame = &item->frame;

  if (frame->hd.type != NGHTTP2_DATA) {

    if (frame->hd.type == NGHTTP2_HEADERS ||
        frame->hd.type == NGHTTP2_PUSH_PROMISE) {

      if (nghttp2_bufs_next_present(framebufs)) {
        framebufs->cur = framebufs->cur->next;

        DEBUGF("send: next CONTINUATION frame, %zu bytes\n",
               nghttp2_buf_len(&framebufs->cur->buf));

        return;
      }
    }

    active_outbound_item_reset(&session->aob, mem);

    return;
  }

  /* DATA frame */

  aux_data = &item->aux_data.data;

  /* On EOF, we have already detached data.  Please note that
     application may issue nghttp2_submit_data2() in
     on_frame_send_callback (call from session_after_frame_sent1),
     which attach data to stream.  We don't want to detach it. */
  if (aux_data->eof) {
    active_outbound_item_reset(aob, mem);

    return;
  }

  /* Reset no_copy here because next write may not use this. */
  aux_data->no_copy = 0;

  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);

  /* If session is closed or RST_STREAM was queued, we won't send
     further data. */
  if (nghttp2_session_predicate_data_send(session, stream) != 0) {
    if (stream) {
      session_detach_stream_item(session, stream);
    }

    active_outbound_item_reset(aob, mem);

    return;
  }

  aob->item = NULL;
  active_outbound_item_reset(&session->aob, mem);

  return;
}

static int session_call_send_data(nghttp2_session *session,
                                  nghttp2_outbound_item *item,
                                  nghttp2_bufs *framebufs) {
  int rv;
  nghttp2_buf *buf;
  size_t length;
  nghttp2_frame *frame;
  nghttp2_data_aux_data *aux_data;

  buf = &framebufs->cur->buf;
  frame = &item->frame;
  length = frame->hd.length - frame->data.padlen;
  aux_data = &item->aux_data.data;

  rv = session->callbacks.send_data_callback(session, frame, buf->pos, length,
                                             &aux_data->dpw.data_prd.source,
                                             session->user_data);

  switch (rv) {
  case 0:
  case NGHTTP2_ERR_WOULDBLOCK:
  case NGHTTP2_ERR_PAUSE:
  case NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE:
    return rv;
  default:
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
}

static nghttp2_ssize nghttp2_session_mem_send_internal(nghttp2_session *session,
                                                       const uint8_t **data_ptr,
                                                       int fast_cb) {
  int rv;
  nghttp2_active_outbound_item *aob;
  nghttp2_bufs *framebufs;
  nghttp2_mem *mem;

  mem = &session->mem;
  aob = &session->aob;
  framebufs = &aob->framebufs;

  /* We may have idle streams more than we expect (e.g.,
     nghttp2_session_change_stream_priority() or
     nghttp2_session_create_idle_stream()).  Adjust them here. */
  rv = nghttp2_session_adjust_idle_stream(session);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  for (;;) {
    switch (aob->state) {
    case NGHTTP2_OB_POP_ITEM: {
      nghttp2_outbound_item *item;

      item = nghttp2_session_pop_next_ob_item(session);
      if (item == NULL) {
        return 0;
      }

      rv = session_prep_frame(session, item);
      if (rv == NGHTTP2_ERR_PAUSE) {
        return 0;
      }
      if (rv == NGHTTP2_ERR_DEFERRED) {
        DEBUGF("send: frame transmission deferred\n");
        break;
      }
      if (rv < 0) {
        int32_t opened_stream_id = 0;
        uint32_t error_code = NGHTTP2_INTERNAL_ERROR;
        int rv2 = 0;

        DEBUGF("send: frame preparation failed with %s\n",
               nghttp2_strerror(rv));
        /* TODO If the error comes from compressor, the connection
           must be closed. */
        if (item->frame.hd.type != NGHTTP2_DATA &&
            session->callbacks.on_frame_not_send_callback && is_non_fatal(rv)) {
          nghttp2_frame *frame = &item->frame;
          /* The library is responsible for the transmission of
             WINDOW_UPDATE frame, so we don't call error callback for
             it. */
          if (frame->hd.type != NGHTTP2_WINDOW_UPDATE &&
              session->callbacks.on_frame_not_send_callback(
                  session, frame, rv, session->user_data) != 0) {

            nghttp2_outbound_item_free(item, mem);
            nghttp2_mem_free(mem, item);

            return NGHTTP2_ERR_CALLBACK_FAILURE;
          }
        }
        /* We have to close stream opened by failed request HEADERS
           or PUSH_PROMISE. */
        switch (item->frame.hd.type) {
        case NGHTTP2_HEADERS:
          if (item->frame.headers.cat == NGHTTP2_HCAT_REQUEST) {
            opened_stream_id = item->frame.hd.stream_id;
            if (item->aux_data.headers.canceled) {
              error_code = item->aux_data.headers.error_code;
            } else {
              /* Set error_code to REFUSED_STREAM so that application
                 can send request again. */
              error_code = NGHTTP2_REFUSED_STREAM;
            }
          }
          break;
        case NGHTTP2_PUSH_PROMISE:
          opened_stream_id = item->frame.push_promise.promised_stream_id;
          break;
        }
        if (opened_stream_id) {
          /* careful not to override rv */
          rv2 = nghttp2_session_close_stream(session, opened_stream_id,
                                             error_code);
        }

        nghttp2_outbound_item_free(item, mem);
        nghttp2_mem_free(mem, item);
        active_outbound_item_reset(aob, mem);

        if (nghttp2_is_fatal(rv2)) {
          return rv2;
        }

        if (rv == NGHTTP2_ERR_HEADER_COMP) {
          /* If header compression error occurred, should terminate
             connection. */
          rv = nghttp2_session_terminate_session(session,
                                                 NGHTTP2_INTERNAL_ERROR);
        }
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }
        break;
      }

      aob->item = item;

      nghttp2_bufs_rewind(framebufs);

      if (item->frame.hd.type != NGHTTP2_DATA) {
        nghttp2_frame *frame;

        frame = &item->frame;

        DEBUGF("send: next frame: payloadlen=%zu, type=%u, flags=0x%02x, "
               "stream_id=%d\n",
               frame->hd.length, frame->hd.type, frame->hd.flags,
               frame->hd.stream_id);

        rv = session_call_before_frame_send(session, frame);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        if (rv == NGHTTP2_ERR_CANCEL) {
          int32_t opened_stream_id = 0;
          uint32_t error_code = NGHTTP2_INTERNAL_ERROR;

          if (session->callbacks.on_frame_not_send_callback) {
            if (session->callbacks.on_frame_not_send_callback(
                    session, frame, rv, session->user_data) != 0) {
              return NGHTTP2_ERR_CALLBACK_FAILURE;
            }
          }

          /* We have to close stream opened by canceled request
             HEADERS or PUSH_PROMISE. */
          switch (item->frame.hd.type) {
          case NGHTTP2_HEADERS:
            if (item->frame.headers.cat == NGHTTP2_HCAT_REQUEST) {
              opened_stream_id = item->frame.hd.stream_id;
              /* We don't have to check
                 item->aux_data.headers.canceled since it has already
                 been checked. */
              /* Set error_code to REFUSED_STREAM so that application
                 can send request again. */
              error_code = NGHTTP2_REFUSED_STREAM;
            }
            break;
          case NGHTTP2_PUSH_PROMISE:
            opened_stream_id = item->frame.push_promise.promised_stream_id;
            break;
          }
          if (opened_stream_id) {
            /* careful not to override rv */
            int rv2;
            rv2 = nghttp2_session_close_stream(session, opened_stream_id,
                                               error_code);

            if (nghttp2_is_fatal(rv2)) {
              return rv2;
            }
          }

          active_outbound_item_reset(aob, mem);

          break;
        }
      } else {
        DEBUGF("send: next frame: DATA\n");

        if (item->aux_data.data.no_copy) {
          aob->state = NGHTTP2_OB_SEND_NO_COPY;
          break;
        }
      }

      DEBUGF("send: start transmitting frame type=%u, length=%td\n",
             framebufs->cur->buf.pos[3],
             framebufs->cur->buf.last - framebufs->cur->buf.pos);

      aob->state = NGHTTP2_OB_SEND_DATA;

      break;
    }
    case NGHTTP2_OB_SEND_DATA: {
      size_t datalen;
      nghttp2_buf *buf;

      buf = &framebufs->cur->buf;

      if (buf->pos == buf->last) {
        DEBUGF("send: end transmission of a frame\n");

        /* Frame has completely sent */
        if (fast_cb) {
          session_after_frame_sent2(session);
        } else {
          rv = session_after_frame_sent1(session);
          if (rv < 0) {
            /* FATAL */
            assert(nghttp2_is_fatal(rv));
            return rv;
          }
          session_after_frame_sent2(session);
        }
        /* We have already adjusted the next state */
        break;
      }

      *data_ptr = buf->pos;
      datalen = nghttp2_buf_len(buf);

      /* We increment the offset here. If send_callback does not send
         everything, we will adjust it. */
      buf->pos += datalen;

      return (nghttp2_ssize)datalen;
    }
    case NGHTTP2_OB_SEND_NO_COPY: {
      nghttp2_stream *stream;
      nghttp2_frame *frame;
      int pause;

      DEBUGF("send: no copy DATA\n");

      frame = &aob->item->frame;

      stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
      if (stream == NULL) {
        DEBUGF("send: no copy DATA cancelled because stream was closed\n");

        active_outbound_item_reset(aob, mem);

        break;
      }

      rv = session_call_send_data(session, aob->item, framebufs);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      if (rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
        session_detach_stream_item(session, stream);

        rv = nghttp2_session_add_rst_stream(session, frame->hd.stream_id,
                                            NGHTTP2_INTERNAL_ERROR);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        active_outbound_item_reset(aob, mem);

        break;
      }

      if (rv == NGHTTP2_ERR_WOULDBLOCK) {
        return 0;
      }

      pause = (rv == NGHTTP2_ERR_PAUSE);

      rv = session_after_frame_sent1(session);
      if (rv < 0) {
        assert(nghttp2_is_fatal(rv));
        return rv;
      }
      session_after_frame_sent2(session);

      /* We have already adjusted the next state */

      if (pause) {
        return 0;
      }

      break;
    }
    case NGHTTP2_OB_SEND_CLIENT_MAGIC: {
      size_t datalen;
      nghttp2_buf *buf;

      buf = &framebufs->cur->buf;

      if (buf->pos == buf->last) {
        DEBUGF("send: end transmission of client magic\n");
        active_outbound_item_reset(aob, mem);
        break;
      }

      *data_ptr = buf->pos;
      datalen = nghttp2_buf_len(buf);

      buf->pos += datalen;

      return (nghttp2_ssize)datalen;
    }
    }
  }
}

ssize_t nghttp2_session_mem_send(nghttp2_session *session,
                                 const uint8_t **data_ptr) {
  return (ssize_t)nghttp2_session_mem_send2(session, data_ptr);
}

nghttp2_ssize nghttp2_session_mem_send2(nghttp2_session *session,
                                        const uint8_t **data_ptr) {
  int rv;
  nghttp2_ssize len;

  *data_ptr = NULL;

  len = nghttp2_session_mem_send_internal(session, data_ptr, 1);
  if (len <= 0) {
    return len;
  }

  if (session->aob.item) {
    /* We have to call session_after_frame_sent1 here to handle stream
       closure upon transmission of frames.  Otherwise, END_STREAM may
       be reached to client before we call nghttp2_session_mem_send
       again and we may get exceeding number of incoming streams. */
    rv = session_after_frame_sent1(session);
    if (rv < 0) {
      assert(nghttp2_is_fatal(rv));
      return (nghttp2_ssize)rv;
    }
  }

  return len;
}

int nghttp2_session_send(nghttp2_session *session) {
  const uint8_t *data = NULL;
  nghttp2_ssize datalen;
  nghttp2_ssize sentlen;
  nghttp2_bufs *framebufs;

  framebufs = &session->aob.framebufs;

  for (;;) {
    datalen = nghttp2_session_mem_send_internal(session, &data, 0);
    if (datalen <= 0) {
      return (int)datalen;
    }
    if (session->callbacks.send_callback2) {
      sentlen = session->callbacks.send_callback2(
          session, data, (size_t)datalen, 0, session->user_data);
    } else {
      sentlen = (nghttp2_ssize)session->callbacks.send_callback(
          session, data, (size_t)datalen, 0, session->user_data);
    }
    if (sentlen < 0) {
      if (sentlen == NGHTTP2_ERR_WOULDBLOCK) {
        /* Transmission canceled. Rewind the offset */
        framebufs->cur->buf.pos -= datalen;

        return 0;
      }
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    /* Rewind the offset to the amount of unsent bytes */
    framebufs->cur->buf.pos -= datalen - sentlen;
  }
}

static nghttp2_ssize session_recv(nghttp2_session *session, uint8_t *buf,
                                  size_t len) {
  nghttp2_ssize rv;

  if (session->callbacks.recv_callback2) {
    rv = session->callbacks.recv_callback2(session, buf, len, 0,
                                           session->user_data);
  } else {
    rv = (nghttp2_ssize)session->callbacks.recv_callback(session, buf, len, 0,
                                                         session->user_data);
  }
  if (rv > 0) {
    if ((size_t)rv > len) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  } else if (rv < 0 && rv != NGHTTP2_ERR_WOULDBLOCK && rv != NGHTTP2_ERR_EOF) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
  return rv;
}

static int session_call_on_begin_frame(nghttp2_session *session,
                                       const nghttp2_frame_hd *hd) {
  int rv;

  if (session->callbacks.on_begin_frame_callback) {

    rv = session->callbacks.on_begin_frame_callback(session, hd,
                                                    session->user_data);

    if (rv != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }

  return 0;
}

static int session_call_on_frame_received(nghttp2_session *session,
                                          nghttp2_frame *frame) {
  int rv;
  if (session->callbacks.on_frame_recv_callback) {
    rv = session->callbacks.on_frame_recv_callback(session, frame,
                                                   session->user_data);
    if (rv != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

static int session_call_on_begin_headers(nghttp2_session *session,
                                         nghttp2_frame *frame) {
  int rv;
  DEBUGF("recv: call on_begin_headers callback stream_id=%d\n",
         frame->hd.stream_id);
  if (session->callbacks.on_begin_headers_callback) {
    rv = session->callbacks.on_begin_headers_callback(session, frame,
                                                      session->user_data);
    if (rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
      return rv;
    }
    if (rv != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

static int session_call_on_header(nghttp2_session *session,
                                  const nghttp2_frame *frame,
                                  const nghttp2_hd_nv *nv) {
  int rv = 0;
  if (session->callbacks.on_header_callback2) {
    rv = session->callbacks.on_header_callback2(
        session, frame, nv->name, nv->value, nv->flags, session->user_data);
  } else if (session->callbacks.on_header_callback) {
    rv = session->callbacks.on_header_callback(
        session, frame, nv->name->base, nv->name->len, nv->value->base,
        nv->value->len, nv->flags, session->user_data);
  }

  if (rv == NGHTTP2_ERR_PAUSE || rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
    return rv;
  }
  if (rv != 0) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int session_call_on_invalid_header(nghttp2_session *session,
                                          const nghttp2_frame *frame,
                                          const nghttp2_hd_nv *nv) {
  int rv;
  if (session->callbacks.on_invalid_header_callback2) {
    rv = session->callbacks.on_invalid_header_callback2(
        session, frame, nv->name, nv->value, nv->flags, session->user_data);
  } else if (session->callbacks.on_invalid_header_callback) {
    rv = session->callbacks.on_invalid_header_callback(
        session, frame, nv->name->base, nv->name->len, nv->value->base,
        nv->value->len, nv->flags, session->user_data);
  } else {
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }

  if (rv == NGHTTP2_ERR_PAUSE || rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
    return rv;
  }
  if (rv != 0) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

static int
session_call_on_extension_chunk_recv_callback(nghttp2_session *session,
                                              const uint8_t *data, size_t len) {
  int rv;
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  if (session->callbacks.on_extension_chunk_recv_callback) {
    rv = session->callbacks.on_extension_chunk_recv_callback(
        session, &frame->hd, data, len, session->user_data);
    if (rv == NGHTTP2_ERR_CANCEL) {
      return rv;
    }
    if (rv != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }

  return 0;
}

static int session_call_unpack_extension_callback(nghttp2_session *session) {
  int rv;
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;
  void *payload = NULL;

  rv = session->callbacks.unpack_extension_callback(
      session, &payload, &frame->hd, session->user_data);
  if (rv == NGHTTP2_ERR_CANCEL) {
    return rv;
  }
  if (rv != 0) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }

  frame->ext.payload = payload;

  return 0;
}

/*
 * Handles frame size error.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *   Out of memory.
 */
static int session_handle_frame_size_error(nghttp2_session *session) {
  /* TODO Currently no callback is called for this error, because we
     call this callback before reading any payload */
  return nghttp2_session_terminate_session(session, NGHTTP2_FRAME_SIZE_ERROR);
}

static uint32_t get_error_code_from_lib_error_code(int lib_error_code) {
  switch (lib_error_code) {
  case NGHTTP2_ERR_STREAM_CLOSED:
    return NGHTTP2_STREAM_CLOSED;
  case NGHTTP2_ERR_HEADER_COMP:
    return NGHTTP2_COMPRESSION_ERROR;
  case NGHTTP2_ERR_FRAME_SIZE_ERROR:
    return NGHTTP2_FRAME_SIZE_ERROR;
  case NGHTTP2_ERR_FLOW_CONTROL:
    return NGHTTP2_FLOW_CONTROL_ERROR;
  case NGHTTP2_ERR_REFUSED_STREAM:
    return NGHTTP2_REFUSED_STREAM;
  case NGHTTP2_ERR_PROTO:
  case NGHTTP2_ERR_HTTP_HEADER:
  case NGHTTP2_ERR_HTTP_MESSAGING:
    return NGHTTP2_PROTOCOL_ERROR;
  default:
    return NGHTTP2_INTERNAL_ERROR;
  }
}

/*
 * Calls on_invalid_frame_recv_callback if it is set to |session|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *   User defined callback function fails.
 */
static int session_call_on_invalid_frame_recv_callback(nghttp2_session *session,
                                                       nghttp2_frame *frame,
                                                       int lib_error_code) {
  if (session->callbacks.on_invalid_frame_recv_callback) {
    if (session->callbacks.on_invalid_frame_recv_callback(
            session, frame, lib_error_code, session->user_data) != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

static int session_handle_invalid_stream2(nghttp2_session *session,
                                          int32_t stream_id,
                                          nghttp2_frame *frame,
                                          int lib_error_code) {
  int rv;
  rv = nghttp2_session_add_rst_stream(
      session, stream_id, get_error_code_from_lib_error_code(lib_error_code));
  if (rv != 0) {
    return rv;
  }
  if (session->callbacks.on_invalid_frame_recv_callback) {
    if (session->callbacks.on_invalid_frame_recv_callback(
            session, frame, lib_error_code, session->user_data) != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

static int session_handle_invalid_stream(nghttp2_session *session,
                                         nghttp2_frame *frame,
                                         int lib_error_code) {
  return session_handle_invalid_stream2(session, frame->hd.stream_id, frame,
                                        lib_error_code);
}

static int session_inflate_handle_invalid_stream(nghttp2_session *session,
                                                 nghttp2_frame *frame,
                                                 int lib_error_code) {
  int rv;
  rv = session_handle_invalid_stream(session, frame, lib_error_code);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }
  return NGHTTP2_ERR_IGN_HEADER_BLOCK;
}

/*
 * Handles invalid frame which causes connection error.
 */
static int session_handle_invalid_connection(nghttp2_session *session,
                                             nghttp2_frame *frame,
                                             int lib_error_code,
                                             const char *reason) {
  if (session->callbacks.on_invalid_frame_recv_callback) {
    if (session->callbacks.on_invalid_frame_recv_callback(
            session, frame, lib_error_code, session->user_data) != 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return nghttp2_session_terminate_session_with_reason(
      session, get_error_code_from_lib_error_code(lib_error_code), reason);
}

static int session_inflate_handle_invalid_connection(nghttp2_session *session,
                                                     nghttp2_frame *frame,
                                                     int lib_error_code,
                                                     const char *reason) {
  int rv;
  rv =
      session_handle_invalid_connection(session, frame, lib_error_code, reason);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }
  return NGHTTP2_ERR_IGN_HEADER_BLOCK;
}

/*
 * Inflates header block in the memory pointed by |in| with |inlen|
 * bytes. If this function returns NGHTTP2_ERR_PAUSE, the caller must
 * call this function again, until it returns 0 or one of negative
 * error code.  If |call_header_cb| is zero, the on_header_callback
 * are not invoked and the function never return NGHTTP2_ERR_PAUSE. If
 * the given |in| is the last chunk of header block, the |final| must
 * be nonzero. If header block is successfully processed (which is
 * indicated by the return value 0, NGHTTP2_ERR_PAUSE or
 * NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE), the number of processed
 * input bytes is assigned to the |*readlen_ptr|.
 *
 * This function return 0 if it succeeds, or one of the negative error
 * codes:
 *
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 * NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE
 *     The callback returns this error code, indicating that this
 *     stream should be RST_STREAMed.
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_PAUSE
 *     The callback function returned NGHTTP2_ERR_PAUSE
 * NGHTTP2_ERR_HEADER_COMP
 *     Header decompression failed
 */
static int inflate_header_block(nghttp2_session *session, nghttp2_frame *frame,
                                size_t *readlen_ptr, uint8_t *in, size_t inlen,
                                int final, int call_header_cb) {
  nghttp2_ssize proclen;
  int rv;
  int inflate_flags;
  nghttp2_hd_nv nv;
  nghttp2_stream *stream;
  nghttp2_stream *subject_stream;
  int trailer = 0;

  *readlen_ptr = 0;
  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);

  if (frame->hd.type == NGHTTP2_PUSH_PROMISE) {
    subject_stream = nghttp2_session_get_stream(
        session, frame->push_promise.promised_stream_id);
  } else {
    subject_stream = stream;
    trailer = session_trailer_headers(session, stream, frame);
  }

  DEBUGF("recv: decoding header block %zu bytes\n", inlen);
  for (;;) {
    inflate_flags = 0;
    proclen = nghttp2_hd_inflate_hd_nv(&session->hd_inflater, &nv,
                                       &inflate_flags, in, inlen, final);
    if (nghttp2_is_fatal((int)proclen)) {
      return (int)proclen;
    }
    if (proclen < 0) {
      if (session->iframe.state == NGHTTP2_IB_READ_HEADER_BLOCK) {
        if (subject_stream && subject_stream->state != NGHTTP2_STREAM_CLOSING) {
          /* Adding RST_STREAM here is very important. It prevents
             from invoking subsequent callbacks for the same stream
             ID. */
          rv = nghttp2_session_add_rst_stream(
              session, subject_stream->stream_id, NGHTTP2_COMPRESSION_ERROR);

          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
        }
      }
      rv =
          nghttp2_session_terminate_session(session, NGHTTP2_COMPRESSION_ERROR);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      return NGHTTP2_ERR_HEADER_COMP;
    }
    in += proclen;
    inlen -= (size_t)proclen;
    *readlen_ptr += (size_t)proclen;

    DEBUGF("recv: proclen=%td\n", proclen);

    if (call_header_cb && (inflate_flags & NGHTTP2_HD_INFLATE_EMIT)) {
      rv = 0;
      if (subject_stream) {
        if (session_enforce_http_messaging(session)) {
          rv = nghttp2_http_on_header(session, subject_stream, frame, &nv,
                                      trailer);

          if (rv == NGHTTP2_ERR_IGN_HTTP_HEADER) {
            /* Don't overwrite rv here */
            int rv2;

            rv2 = session_call_on_invalid_header(session, frame, &nv);
            if (rv2 == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
              rv = NGHTTP2_ERR_HTTP_HEADER;
            } else {
              if (rv2 != 0) {
                return rv2;
              }

              /* header is ignored */
              DEBUGF("recv: HTTP ignored: type=%u, id=%d, header %.*s: %.*s\n",
                     frame->hd.type, frame->hd.stream_id, (int)nv.name->len,
                     nv.name->base, (int)nv.value->len, nv.value->base);

              rv2 = session_call_error_callback(
                  session, NGHTTP2_ERR_HTTP_HEADER,
                  "Ignoring received invalid HTTP header field: frame type: "
                  "%u, stream: %d, name: [%.*s], value: [%.*s]",
                  frame->hd.type, frame->hd.stream_id, (int)nv.name->len,
                  nv.name->base, (int)nv.value->len, nv.value->base);

              if (nghttp2_is_fatal(rv2)) {
                return rv2;
              }
            }
          }

          if (rv == NGHTTP2_ERR_HTTP_HEADER) {
            DEBUGF("recv: HTTP error: type=%u, id=%d, header %.*s: %.*s\n",
                   frame->hd.type, frame->hd.stream_id, (int)nv.name->len,
                   nv.name->base, (int)nv.value->len, nv.value->base);

            rv = session_call_error_callback(
                session, NGHTTP2_ERR_HTTP_HEADER,
                "Invalid HTTP header field was received: frame type: "
                "%u, stream: %d, name: [%.*s], value: [%.*s]",
                frame->hd.type, frame->hd.stream_id, (int)nv.name->len,
                nv.name->base, (int)nv.value->len, nv.value->base);

            if (nghttp2_is_fatal(rv)) {
              return rv;
            }

            rv = session_handle_invalid_stream2(session,
                                                subject_stream->stream_id,
                                                frame, NGHTTP2_ERR_HTTP_HEADER);
            if (nghttp2_is_fatal(rv)) {
              return rv;
            }
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
          }
        }
        if (rv == 0) {
          rv = session_call_on_header(session, frame, &nv);
          /* This handles NGHTTP2_ERR_PAUSE and
             NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE as well */
          if (rv != 0) {
            return rv;
          }
        }
      }
    }
    if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) {
      nghttp2_hd_inflate_end_headers(&session->hd_inflater);
      break;
    }
    if ((inflate_flags & NGHTTP2_HD_INFLATE_EMIT) == 0 && inlen == 0) {
      break;
    }
  }
  return 0;
}

/*
 * Call this function when HEADERS frame was completely received.
 *
 * This function returns 0 if it succeeds, or one of negative error
 * codes:
 *
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
static int session_end_stream_headers_received(nghttp2_session *session,
                                               nghttp2_frame *frame,
                                               nghttp2_stream *stream) {
  int rv;

  assert(frame->hd.type == NGHTTP2_HEADERS);

  if (session->server && session_enforce_http_messaging(session) &&
      frame->headers.cat == NGHTTP2_HCAT_REQUEST &&
      (stream->flags & NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES) &&
      !(stream->flags & NGHTTP2_STREAM_FLAG_IGNORE_CLIENT_PRIORITIES) &&
      (stream->http_flags & NGHTTP2_HTTP_FLAG_PRIORITY)) {
    rv = session_update_stream_priority(session, stream, stream->http_extpri);
    if (rv != 0) {
      assert(nghttp2_is_fatal(rv));
      return rv;
    }
  }

  if ((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) == 0) {
    return 0;
  }

  nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_RD);
  rv = nghttp2_session_close_stream_if_shut_rdwr(session, stream);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  return 0;
}

static int session_after_header_block_received(nghttp2_session *session) {
  int rv = 0;
  nghttp2_frame *frame = &session->iframe.frame;
  nghttp2_stream *stream;

  /* We don't call on_frame_recv_callback if stream has been closed
     already or being closed. */
  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
  if (!stream || stream->state == NGHTTP2_STREAM_CLOSING) {
    return 0;
  }

  if (session_enforce_http_messaging(session)) {
    if (frame->hd.type == NGHTTP2_PUSH_PROMISE) {
      nghttp2_stream *subject_stream;

      subject_stream = nghttp2_session_get_stream(
          session, frame->push_promise.promised_stream_id);
      if (subject_stream) {
        rv = nghttp2_http_on_request_headers(subject_stream, frame);
      }
    } else {
      assert(frame->hd.type == NGHTTP2_HEADERS);
      switch (frame->headers.cat) {
      case NGHTTP2_HCAT_REQUEST:
        rv = nghttp2_http_on_request_headers(stream, frame);
        break;
      case NGHTTP2_HCAT_RESPONSE:
      case NGHTTP2_HCAT_PUSH_RESPONSE:
        rv = nghttp2_http_on_response_headers(stream);
        break;
      case NGHTTP2_HCAT_HEADERS:
        if (stream->http_flags & NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE) {
          assert(!session->server);
          rv = nghttp2_http_on_response_headers(stream);
        } else {
          rv = nghttp2_http_on_trailer_headers(stream, frame);
        }
        break;
      default:
        assert(0);
      }
      if (rv == 0 && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
        rv = nghttp2_http_on_remote_end_stream(stream);
      }
    }
    if (rv != 0) {
      int32_t stream_id;

      if (frame->hd.type == NGHTTP2_PUSH_PROMISE) {
        stream_id = frame->push_promise.promised_stream_id;
      } else {
        stream_id = frame->hd.stream_id;
      }

      rv = session_handle_invalid_stream2(session, stream_id, frame,
                                          NGHTTP2_ERR_HTTP_MESSAGING);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      if (frame->hd.type == NGHTTP2_HEADERS &&
          (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
        nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_RD);
        /* Don't call nghttp2_session_close_stream_if_shut_rdwr
           because RST_STREAM has been submitted. */
      }
      return 0;
    }
  }

  rv = session_call_on_frame_received(session, frame);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  if (frame->hd.type != NGHTTP2_HEADERS) {
    return 0;
  }

  return session_end_stream_headers_received(session, frame, stream);
}

int nghttp2_session_on_request_headers_received(nghttp2_session *session,
                                                nghttp2_frame *frame) {
  int rv = 0;
  nghttp2_stream *stream;
  if (frame->hd.stream_id == 0) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "request HEADERS: stream_id == 0");
  }

  /* If client receives idle stream from server, it is invalid
     regardless stream ID is even or odd.  This is because client is
     not expected to receive request from server. */
  if (!session->server) {
    if (session_detect_idle_stream(session, frame->hd.stream_id)) {
      return session_inflate_handle_invalid_connection(
          session, frame, NGHTTP2_ERR_PROTO,
          "request HEADERS: client received request");
    }

    return NGHTTP2_ERR_IGN_HEADER_BLOCK;
  }

  assert(session->server);

  if (!session_is_new_peer_stream_id(session, frame->hd.stream_id)) {
    if (frame->hd.stream_id == 0 ||
        nghttp2_session_is_my_stream_id(session, frame->hd.stream_id)) {
      return session_inflate_handle_invalid_connection(
          session, frame, NGHTTP2_ERR_PROTO,
          "request HEADERS: invalid stream_id");
    }

    /* RFC 7540 says if an endpoint receives a HEADERS with invalid
     * stream ID (e.g, numerically smaller than previous), it MUST
     * issue connection error with error code PROTOCOL_ERROR.  It is a
     * bit hard to detect this, since we cannot remember all streams
     * we observed so far.
     *
     * You might imagine this is really easy.  But no.  HTTP/2 is
     * asynchronous protocol, and usually client and server do not
     * share the complete picture of open/closed stream status.  For
     * example, after server sends RST_STREAM for a stream, client may
     * send trailer HEADERS for that stream.  If naive server detects
     * that, and issued connection error, then it is a bug of server
     * implementation since client is not wrong if it did not get
     * RST_STREAM when it issued trailer HEADERS.
     *
     * At the moment, we are very conservative here.  We only use
     * connection error if stream ID refers idle stream, or we are
     * sure that stream is half-closed(remote) or closed.  Otherwise
     * we just ignore HEADERS for now.
     */
    stream = nghttp2_session_get_stream_raw(session, frame->hd.stream_id);
    if (stream && (stream->shut_flags & NGHTTP2_SHUT_RD)) {
      return session_inflate_handle_invalid_connection(
          session, frame, NGHTTP2_ERR_STREAM_CLOSED, "HEADERS: stream closed");
    }

    return NGHTTP2_ERR_IGN_HEADER_BLOCK;
  }
  session->last_recv_stream_id = frame->hd.stream_id;

  if (session_is_incoming_concurrent_streams_max(session)) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO,
        "request HEADERS: max concurrent streams exceeded");
  }

  if (!session_allow_incoming_new_stream(session)) {
    /* We just ignore stream after GOAWAY was sent */
    return NGHTTP2_ERR_IGN_HEADER_BLOCK;
  }

  if (frame->headers.pri_spec.stream_id == frame->hd.stream_id) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "request HEADERS: depend on itself");
  }

  if (session_is_incoming_concurrent_streams_pending_max(session)) {
    return session_inflate_handle_invalid_stream(session, frame,
                                                 NGHTTP2_ERR_REFUSED_STREAM);
  }

  stream = nghttp2_session_open_stream(
      session, frame->hd.stream_id, NGHTTP2_STREAM_FLAG_NONE,
      &frame->headers.pri_spec, NGHTTP2_STREAM_OPENING, NULL);
  if (!stream) {
    return NGHTTP2_ERR_NOMEM;
  }

  rv = nghttp2_session_adjust_closed_stream(session);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  session->last_proc_stream_id = session->last_recv_stream_id;

  rv = session_call_on_begin_headers(session, frame);
  if (rv != 0) {
    return rv;
  }
  return 0;
}

int nghttp2_session_on_response_headers_received(nghttp2_session *session,
                                                 nghttp2_frame *frame,
                                                 nghttp2_stream *stream) {
  int rv;
  /* This function is only called if stream->state ==
     NGHTTP2_STREAM_OPENING and stream_id is local side initiated. */
  assert(stream->state == NGHTTP2_STREAM_OPENING &&
         nghttp2_session_is_my_stream_id(session, frame->hd.stream_id));
  if (frame->hd.stream_id == 0) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "response HEADERS: stream_id == 0");
  }
  if (stream->shut_flags & NGHTTP2_SHUT_RD) {
    /* half closed (remote): from the spec:

       If an endpoint receives additional frames for a stream that is
       in this state it MUST respond with a stream error (Section
       5.4.2) of type STREAM_CLOSED.

       We go further, and make it connection error.
    */
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_STREAM_CLOSED, "HEADERS: stream closed");
  }
  stream->state = NGHTTP2_STREAM_OPENED;
  rv = session_call_on_begin_headers(session, frame);
  if (rv != 0) {
    return rv;
  }
  return 0;
}

int nghttp2_session_on_push_response_headers_received(nghttp2_session *session,
                                                      nghttp2_frame *frame,
                                                      nghttp2_stream *stream) {
  int rv = 0;
  assert(stream->state == NGHTTP2_STREAM_RESERVED);
  if (frame->hd.stream_id == 0) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO,
        "push response HEADERS: stream_id == 0");
  }

  if (session->server) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO,
        "HEADERS: no HEADERS allowed from client in reserved state");
  }

  if (session_is_incoming_concurrent_streams_max(session)) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO,
        "push response HEADERS: max concurrent streams exceeded");
  }

  if (!session_allow_incoming_new_stream(session)) {
    /* We don't accept new stream after GOAWAY was sent. */
    return NGHTTP2_ERR_IGN_HEADER_BLOCK;
  }

  if (session_is_incoming_concurrent_streams_pending_max(session)) {
    return session_inflate_handle_invalid_stream(session, frame,
                                                 NGHTTP2_ERR_REFUSED_STREAM);
  }

  nghttp2_stream_promise_fulfilled(stream);
  if (!nghttp2_session_is_my_stream_id(session, stream->stream_id)) {
    --session->num_incoming_reserved_streams;
  }
  ++session->num_incoming_streams;
  rv = session_call_on_begin_headers(session, frame);
  if (rv != 0) {
    return rv;
  }
  return 0;
}

int nghttp2_session_on_headers_received(nghttp2_session *session,
                                        nghttp2_frame *frame,
                                        nghttp2_stream *stream) {
  int rv = 0;
  if (frame->hd.stream_id == 0) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "HEADERS: stream_id == 0");
  }
  if ((stream->shut_flags & NGHTTP2_SHUT_RD)) {
    /* half closed (remote): from the spec:

       If an endpoint receives additional frames for a stream that is
       in this state it MUST respond with a stream error (Section
       5.4.2) of type STREAM_CLOSED.

       we go further, and make it connection error.
    */
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_STREAM_CLOSED, "HEADERS: stream closed");
  }
  if (nghttp2_session_is_my_stream_id(session, frame->hd.stream_id)) {
    if (stream->state == NGHTTP2_STREAM_OPENED) {
      rv = session_call_on_begin_headers(session, frame);
      if (rv != 0) {
        return rv;
      }
      return 0;
    }

    return NGHTTP2_ERR_IGN_HEADER_BLOCK;
  }
  /* If this is remote peer initiated stream, it is OK unless it
     has sent END_STREAM frame already. But if stream is in
     NGHTTP2_STREAM_CLOSING, we discard the frame. This is a race
     condition. */
  if (stream->state != NGHTTP2_STREAM_CLOSING) {
    rv = session_call_on_begin_headers(session, frame);
    if (rv != 0) {
      return rv;
    }
    return 0;
  }
  return NGHTTP2_ERR_IGN_HEADER_BLOCK;
}

static int session_process_headers_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;
  nghttp2_stream *stream;

  nghttp2_frame_unpack_headers_payload(&frame->headers, iframe->sbuf.pos);

  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
  if (!stream) {
    frame->headers.cat = NGHTTP2_HCAT_REQUEST;
    return nghttp2_session_on_request_headers_received(session, frame);
  }

  if (stream->state == NGHTTP2_STREAM_RESERVED) {
    frame->headers.cat = NGHTTP2_HCAT_PUSH_RESPONSE;
    return nghttp2_session_on_push_response_headers_received(session, frame,
                                                             stream);
  }

  if (stream->state == NGHTTP2_STREAM_OPENING &&
      nghttp2_session_is_my_stream_id(session, frame->hd.stream_id)) {
    frame->headers.cat = NGHTTP2_HCAT_RESPONSE;
    return nghttp2_session_on_response_headers_received(session, frame, stream);
  }

  frame->headers.cat = NGHTTP2_HCAT_HEADERS;
  return nghttp2_session_on_headers_received(session, frame, stream);
}

int nghttp2_session_on_priority_received(nghttp2_session *session,
                                         nghttp2_frame *frame) {
  int rv;
  nghttp2_stream *stream;

  assert(!session_no_rfc7540_pri_no_fallback(session));

  if (frame->hd.stream_id == 0) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "PRIORITY: stream_id == 0");
  }

  if (frame->priority.pri_spec.stream_id == frame->hd.stream_id) {
    return nghttp2_session_terminate_session_with_reason(
        session, NGHTTP2_PROTOCOL_ERROR, "depend on itself");
  }

  if (!session->server) {
    /* Re-prioritization works only in server */
    return session_call_on_frame_received(session, frame);
  }

  stream = nghttp2_session_get_stream_raw(session, frame->hd.stream_id);

  if (!stream) {
    /* PRIORITY against idle stream can create anchor node in
       dependency tree. */
    if (!session_detect_idle_stream(session, frame->hd.stream_id)) {
      return 0;
    }

    stream = nghttp2_session_open_stream(
        session, frame->hd.stream_id, NGHTTP2_STREAM_FLAG_NONE,
        &frame->priority.pri_spec, NGHTTP2_STREAM_IDLE, NULL);

    if (stream == NULL) {
      return NGHTTP2_ERR_NOMEM;
    }

    rv = nghttp2_session_adjust_idle_stream(session);
    if (nghttp2_is_fatal(rv)) {
      return rv;
    }
  } else {
    rv = nghttp2_session_reprioritize_stream(session, stream,
                                             &frame->priority.pri_spec);

    if (nghttp2_is_fatal(rv)) {
      return rv;
    }

    rv = nghttp2_session_adjust_idle_stream(session);
    if (nghttp2_is_fatal(rv)) {
      return rv;
    }
  }

  return session_call_on_frame_received(session, frame);
}

static int session_process_priority_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  assert(!session_no_rfc7540_pri_no_fallback(session));

  nghttp2_frame_unpack_priority_payload(&frame->priority, iframe->sbuf.pos);

  return nghttp2_session_on_priority_received(session, frame);
}

static int session_update_stream_reset_ratelim(nghttp2_session *session) {
  if (!session->server || (session->goaway_flags & NGHTTP2_GOAWAY_SUBMITTED)) {
    return 0;
  }

  nghttp2_ratelim_update(&session->stream_reset_ratelim,
                         nghttp2_time_now_sec());

  if (nghttp2_ratelim_drain(&session->stream_reset_ratelim, 1) == 0) {
    return 0;
  }

  return nghttp2_session_add_goaway(session, session->last_recv_stream_id,
                                    NGHTTP2_INTERNAL_ERROR, NULL, 0,
                                    NGHTTP2_GOAWAY_AUX_NONE);
}

int nghttp2_session_on_rst_stream_received(nghttp2_session *session,
                                           nghttp2_frame *frame) {
  int rv;
  nghttp2_stream *stream;
  if (frame->hd.stream_id == 0) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "RST_STREAM: stream_id == 0");
  }

  if (session_detect_idle_stream(session, frame->hd.stream_id)) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "RST_STREAM: stream in idle");
  }

  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
  if (stream) {
    /* We may use stream->shut_flags for strict error checking. */
    nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_RD);
  }

  rv = session_call_on_frame_received(session, frame);
  if (rv != 0) {
    return rv;
  }
  rv = nghttp2_session_close_stream(session, frame->hd.stream_id,
                                    frame->rst_stream.error_code);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  return session_update_stream_reset_ratelim(session);
}

static int session_process_rst_stream_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  nghttp2_frame_unpack_rst_stream_payload(&frame->rst_stream, iframe->sbuf.pos);

  return nghttp2_session_on_rst_stream_received(session, frame);
}

static int update_remote_initial_window_size_func(void *entry, void *ptr) {
  int rv;
  nghttp2_update_window_size_arg *arg;
  nghttp2_stream *stream;

  arg = (nghttp2_update_window_size_arg *)ptr;
  stream = (nghttp2_stream *)entry;

  rv = nghttp2_stream_update_remote_initial_window_size(
      stream, arg->new_window_size, arg->old_window_size);
  if (rv != 0) {
    return nghttp2_session_add_rst_stream(arg->session, stream->stream_id,
                                          NGHTTP2_FLOW_CONTROL_ERROR);
  }

  /* If window size gets positive, push deferred DATA frame to
     outbound queue. */
  if (stream->remote_window_size > 0 &&
      nghttp2_stream_check_deferred_by_flow_control(stream)) {

    rv = session_resume_deferred_stream_item(
        arg->session, stream, NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL);

    if (nghttp2_is_fatal(rv)) {
      return rv;
    }
  }
  return 0;
}

/*
 * Updates the remote initial window size of all active streams.  If
 * error occurs, all streams may not be updated.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
static int
session_update_remote_initial_window_size(nghttp2_session *session,
                                          int32_t new_initial_window_size) {
  nghttp2_update_window_size_arg arg;

  arg.session = session;
  arg.new_window_size = new_initial_window_size;
  arg.old_window_size = (int32_t)session->remote_settings.initial_window_size;

  return nghttp2_map_each(&session->streams,
                          update_remote_initial_window_size_func, &arg);
}

static int update_local_initial_window_size_func(void *entry, void *ptr) {
  int rv;
  nghttp2_update_window_size_arg *arg;
  nghttp2_stream *stream;
  arg = (nghttp2_update_window_size_arg *)ptr;
  stream = (nghttp2_stream *)entry;
  rv = nghttp2_stream_update_local_initial_window_size(
      stream, arg->new_window_size, arg->old_window_size);
  if (rv != 0) {
    return nghttp2_session_add_rst_stream(arg->session, stream->stream_id,
                                          NGHTTP2_FLOW_CONTROL_ERROR);
  }

  if (stream->window_update_queued) {
    return 0;
  }

  if (arg->session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE) {
    return session_update_stream_consumed_size(arg->session, stream, 0);
  }

  if (nghttp2_should_send_window_update(stream->local_window_size,
                                        stream->recv_window_size)) {

    rv = nghttp2_session_add_window_update(arg->session, NGHTTP2_FLAG_NONE,
                                           stream->stream_id,
                                           stream->recv_window_size);
    if (rv != 0) {
      return rv;
    }

    stream->recv_window_size = 0;
  }
  return 0;
}

/*
 * Updates the local initial window size of all active streams.  If
 * error occurs, all streams may not be updated.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
static int
session_update_local_initial_window_size(nghttp2_session *session,
                                         int32_t new_initial_window_size,
                                         int32_t old_initial_window_size) {
  nghttp2_update_window_size_arg arg;
  arg.session = session;
  arg.new_window_size = new_initial_window_size;
  arg.old_window_size = old_initial_window_size;
  return nghttp2_map_each(&session->streams,
                          update_local_initial_window_size_func, &arg);
}

/*
 * Apply SETTINGS values |iv| having |niv| elements to the local
 * settings.  We assumes that all values in |iv| is correct, since we
 * validated them in nghttp2_session_add_settings() already.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_HEADER_COMP
 *     The header table size is out of range
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_session_update_local_settings(nghttp2_session *session,
                                          nghttp2_settings_entry *iv,
                                          size_t niv) {
  int rv;
  size_t i;
  int32_t new_initial_window_size = -1;
  uint32_t header_table_size = 0;
  uint32_t min_header_table_size = UINT32_MAX;
  uint8_t header_table_size_seen = 0;
  /* For NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, use the value last
     seen.  For NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, use both minimum
     value and last seen value. */
  for (i = 0; i < niv; ++i) {
    switch (iv[i].settings_id) {
    case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
      header_table_size_seen = 1;
      header_table_size = iv[i].value;
      min_header_table_size =
          nghttp2_min_uint32(min_header_table_size, iv[i].value);
      break;
    case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
      new_initial_window_size = (int32_t)iv[i].value;
      break;
    }
  }
  if (header_table_size_seen) {
    if (min_header_table_size < header_table_size) {
      rv = nghttp2_hd_inflate_change_table_size(&session->hd_inflater,
                                                min_header_table_size);
      if (rv != 0) {
        return rv;
      }
    }

    rv = nghttp2_hd_inflate_change_table_size(&session->hd_inflater,
                                              header_table_size);
    if (rv != 0) {
      return rv;
    }
  }
  if (new_initial_window_size != -1) {
    rv = session_update_local_initial_window_size(
        session, new_initial_window_size,
        (int32_t)session->local_settings.initial_window_size);
    if (rv != 0) {
      return rv;
    }
  }

  for (i = 0; i < niv; ++i) {
    switch (iv[i].settings_id) {
    case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
      session->local_settings.header_table_size = iv[i].value;
      break;
    case NGHTTP2_SETTINGS_ENABLE_PUSH:
      session->local_settings.enable_push = iv[i].value;
      break;
    case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
      session->local_settings.max_concurrent_streams = iv[i].value;
      break;
    case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
      session->local_settings.initial_window_size = iv[i].value;
      break;
    case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:
      session->local_settings.max_frame_size = iv[i].value;
      break;
    case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
      session->local_settings.max_header_list_size = iv[i].value;
      break;
    case NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL:
      session->local_settings.enable_connect_protocol = iv[i].value;
      break;
    case NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES:
      session->local_settings.no_rfc7540_priorities = iv[i].value;
      break;
    }
  }

  return 0;
}

int nghttp2_session_on_settings_received(nghttp2_session *session,
                                         nghttp2_frame *frame, int noack) {
  int rv;
  size_t i;
  nghttp2_mem *mem;
  nghttp2_inflight_settings *settings;

  mem = &session->mem;

  if (frame->hd.stream_id != 0) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "SETTINGS: stream_id != 0");
  }
  if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
    if (frame->settings.niv != 0) {
      return session_handle_invalid_connection(
          session, frame, NGHTTP2_ERR_FRAME_SIZE_ERROR,
          "SETTINGS: ACK and payload != 0");
    }

    settings = session->inflight_settings_head;

    if (!settings) {
      return session_handle_invalid_connection(
          session, frame, NGHTTP2_ERR_PROTO, "SETTINGS: unexpected ACK");
    }

    rv = nghttp2_session_update_local_settings(session, settings->iv,
                                               settings->niv);

    session->inflight_settings_head = settings->next;

    inflight_settings_del(settings, mem);

    if (rv != 0) {
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }
      return session_handle_invalid_connection(session, frame, rv, NULL);
    }
    return session_call_on_frame_received(session, frame);
  }

  if (!session->remote_settings_received) {
    session->remote_settings.max_concurrent_streams =
        NGHTTP2_DEFAULT_MAX_CONCURRENT_STREAMS;
    session->remote_settings_received = 1;
  }

  for (i = 0; i < frame->settings.niv; ++i) {
    nghttp2_settings_entry *entry = &frame->settings.iv[i];

    switch (entry->settings_id) {
    case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:

      rv = nghttp2_hd_deflate_change_table_size(&session->hd_deflater,
                                                entry->value);
      if (rv != 0) {
        if (nghttp2_is_fatal(rv)) {
          return rv;
        } else {
          return session_handle_invalid_connection(
              session, frame, NGHTTP2_ERR_HEADER_COMP, NULL);
        }
      }

      session->remote_settings.header_table_size = entry->value;

      break;
    case NGHTTP2_SETTINGS_ENABLE_PUSH:

      if (entry->value != 0 && entry->value != 1) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_PROTO,
            "SETTINGS: invalid SETTINGS_ENBLE_PUSH");
      }

      if (!session->server && entry->value != 0) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_PROTO,
            "SETTINGS: server attempted to enable push");
      }

      session->remote_settings.enable_push = entry->value;

      break;
    case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:

      session->remote_settings.max_concurrent_streams = entry->value;

      break;
    case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:

      /* Update the initial window size of the all active streams */
      /* Check that initial_window_size < (1u << 31) */
      if (entry->value > NGHTTP2_MAX_WINDOW_SIZE) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_FLOW_CONTROL,
            "SETTINGS: too large SETTINGS_INITIAL_WINDOW_SIZE");
      }

      rv = session_update_remote_initial_window_size(session,
                                                     (int32_t)entry->value);

      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      if (rv != 0) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_FLOW_CONTROL, NULL);
      }

      session->remote_settings.initial_window_size = entry->value;

      break;
    case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:

      if (entry->value < NGHTTP2_MAX_FRAME_SIZE_MIN ||
          entry->value > NGHTTP2_MAX_FRAME_SIZE_MAX) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_PROTO,
            "SETTINGS: invalid SETTINGS_MAX_FRAME_SIZE");
      }

      session->remote_settings.max_frame_size = entry->value;

      break;
    case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:

      session->remote_settings.max_header_list_size = entry->value;

      break;
    case NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL:

      if (entry->value != 0 && entry->value != 1) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_PROTO,
            "SETTINGS: invalid SETTINGS_ENABLE_CONNECT_PROTOCOL");
      }

      if (!session->server &&
          session->remote_settings.enable_connect_protocol &&
          entry->value == 0) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_PROTO,
            "SETTINGS: server attempted to disable "
            "SETTINGS_ENABLE_CONNECT_PROTOCOL");
      }

      session->remote_settings.enable_connect_protocol = entry->value;

      break;
    case NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES:

      if (entry->value != 0 && entry->value != 1) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_PROTO,
            "SETTINGS: invalid SETTINGS_NO_RFC7540_PRIORITIES");
      }

      if (session->remote_settings.no_rfc7540_priorities != UINT32_MAX &&
          session->remote_settings.no_rfc7540_priorities != entry->value) {
        return session_handle_invalid_connection(
            session, frame, NGHTTP2_ERR_PROTO,
            "SETTINGS: SETTINGS_NO_RFC7540_PRIORITIES cannot be changed");
      }

      session->remote_settings.no_rfc7540_priorities = entry->value;

      break;
    }
  }

  if (session->remote_settings.no_rfc7540_priorities == UINT32_MAX) {
    session->remote_settings.no_rfc7540_priorities = 0;

    if (session->server && session->pending_no_rfc7540_priorities &&
        (session->opt_flags &
         NGHTTP2_OPTMASK_SERVER_FALLBACK_RFC7540_PRIORITIES)) {
      session->fallback_rfc7540_priorities = 1;
    }
  }

  if (!noack && !session_is_closing(session)) {
    rv = nghttp2_session_add_settings(session, NGHTTP2_FLAG_ACK, NULL, 0);

    if (rv != 0) {
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      return session_handle_invalid_connection(session, frame,
                                               NGHTTP2_ERR_INTERNAL, NULL);
    }
  }

  return session_call_on_frame_received(session, frame);
}

static int session_process_settings_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;
  size_t i;
  nghttp2_settings_entry min_header_size_entry;

  if (iframe->max_niv) {
    min_header_size_entry = iframe->iv[iframe->max_niv - 1];

    if (min_header_size_entry.value < UINT32_MAX) {
      /* If we have less value, then we must have
         SETTINGS_HEADER_TABLE_SIZE in i < iframe->niv */
      for (i = 0; i < iframe->niv; ++i) {
        if (iframe->iv[i].settings_id == NGHTTP2_SETTINGS_HEADER_TABLE_SIZE) {
          break;
        }
      }

      assert(i < iframe->niv);

      if (min_header_size_entry.value != iframe->iv[i].value) {
        iframe->iv[iframe->niv++] = iframe->iv[i];
        iframe->iv[i] = min_header_size_entry;
      }
    }
  }

  nghttp2_frame_unpack_settings_payload(&frame->settings, iframe->iv,
                                        iframe->niv);

  iframe->iv = NULL;
  iframe->niv = 0;
  iframe->max_niv = 0;

  return nghttp2_session_on_settings_received(session, frame, 0 /* ACK */);
}

int nghttp2_session_on_push_promise_received(nghttp2_session *session,
                                             nghttp2_frame *frame) {
  int rv;
  nghttp2_stream *stream;
  nghttp2_stream *promised_stream;
  nghttp2_priority_spec pri_spec;

  if (frame->hd.stream_id == 0) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "PUSH_PROMISE: stream_id == 0");
  }
  if (session->server || session->local_settings.enable_push == 0) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "PUSH_PROMISE: push disabled");
  }

  if (!nghttp2_session_is_my_stream_id(session, frame->hd.stream_id)) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "PUSH_PROMISE: invalid stream_id");
  }

  if (!session_allow_incoming_new_stream(session)) {
    /* We just discard PUSH_PROMISE after GOAWAY was sent */
    return NGHTTP2_ERR_IGN_HEADER_BLOCK;
  }

  if (!session_is_new_peer_stream_id(session,
                                     frame->push_promise.promised_stream_id)) {
    /* The spec says if an endpoint receives a PUSH_PROMISE with
       illegal stream ID is subject to a connection error of type
       PROTOCOL_ERROR. */
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO,
        "PUSH_PROMISE: invalid promised_stream_id");
  }

  if (session_detect_idle_stream(session, frame->hd.stream_id)) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "PUSH_PROMISE: stream in idle");
  }

  session->last_recv_stream_id = frame->push_promise.promised_stream_id;
  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
  if (!stream || stream->state == NGHTTP2_STREAM_CLOSING ||
      !session->pending_enable_push ||
      session->num_incoming_reserved_streams >=
          session->max_incoming_reserved_streams) {
    /* Currently, client does not retain closed stream, so we don't
       check NGHTTP2_SHUT_RD condition here. */

    rv = nghttp2_session_add_rst_stream(
        session, frame->push_promise.promised_stream_id, NGHTTP2_CANCEL);
    if (rv != 0) {
      return rv;
    }
    return NGHTTP2_ERR_IGN_HEADER_BLOCK;
  }

  if (stream->shut_flags & NGHTTP2_SHUT_RD) {
    return session_inflate_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_STREAM_CLOSED,
        "PUSH_PROMISE: stream closed");
  }

  nghttp2_priority_spec_init(&pri_spec, stream->stream_id,
                             NGHTTP2_DEFAULT_WEIGHT, 0);

  promised_stream = nghttp2_session_open_stream(
      session, frame->push_promise.promised_stream_id, NGHTTP2_STREAM_FLAG_NONE,
      &pri_spec, NGHTTP2_STREAM_RESERVED, NULL);

  if (!promised_stream) {
    return NGHTTP2_ERR_NOMEM;
  }

  /* We don't call nghttp2_session_adjust_closed_stream(), since we
     don't keep closed stream in client side */

  session->last_proc_stream_id = session->last_recv_stream_id;
  rv = session_call_on_begin_headers(session, frame);
  if (rv != 0) {
    return rv;
  }
  return 0;
}

static int session_process_push_promise_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  nghttp2_frame_unpack_push_promise_payload(&frame->push_promise,
                                            iframe->sbuf.pos);

  return nghttp2_session_on_push_promise_received(session, frame);
}

int nghttp2_session_on_ping_received(nghttp2_session *session,
                                     nghttp2_frame *frame) {
  int rv = 0;
  if (frame->hd.stream_id != 0) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "PING: stream_id != 0");
  }
  if ((session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_PING_ACK) == 0 &&
      (frame->hd.flags & NGHTTP2_FLAG_ACK) == 0 &&
      !session_is_closing(session)) {
    /* Peer sent ping, so ping it back */
    rv = nghttp2_session_add_ping(session, NGHTTP2_FLAG_ACK,
                                  frame->ping.opaque_data);
    if (rv != 0) {
      return rv;
    }
  }
  return session_call_on_frame_received(session, frame);
}

static int session_process_ping_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  nghttp2_frame_unpack_ping_payload(&frame->ping, iframe->sbuf.pos);

  return nghttp2_session_on_ping_received(session, frame);
}

int nghttp2_session_on_goaway_received(nghttp2_session *session,
                                       nghttp2_frame *frame) {
  int rv;

  if (frame->hd.stream_id != 0) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "GOAWAY: stream_id != 0");
  }
  /* Spec says Endpoints MUST NOT increase the value they send in the
     last stream identifier. */
  if ((frame->goaway.last_stream_id > 0 &&
       !nghttp2_session_is_my_stream_id(session,
                                        frame->goaway.last_stream_id)) ||
      session->remote_last_stream_id < frame->goaway.last_stream_id) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "GOAWAY: invalid last_stream_id");
  }

  session->goaway_flags |= NGHTTP2_GOAWAY_RECV;

  session->remote_last_stream_id = frame->goaway.last_stream_id;

  rv = session_call_on_frame_received(session, frame);

  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  return session_close_stream_on_goaway(session, frame->goaway.last_stream_id,
                                        0);
}

static int session_process_goaway_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  nghttp2_frame_unpack_goaway_payload(&frame->goaway, iframe->sbuf.pos,
                                      iframe->lbuf.pos,
                                      nghttp2_buf_len(&iframe->lbuf));

  nghttp2_buf_wrap_init(&iframe->lbuf, NULL, 0);

  return nghttp2_session_on_goaway_received(session, frame);
}

static int
session_on_connection_window_update_received(nghttp2_session *session,
                                             nghttp2_frame *frame) {
  /* Handle connection-level flow control */
  if (frame->window_update.window_size_increment == 0) {
    return session_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO,
        "WINDOW_UPDATE: window_size_increment == 0");
  }

  if (NGHTTP2_MAX_WINDOW_SIZE - frame->window_update.window_size_increment <
      session->remote_window_size) {
    return session_handle_invalid_connection(session, frame,
                                             NGHTTP2_ERR_FLOW_CONTROL, NULL);
  }
  session->remote_window_size += frame->window_update.window_size_increment;

  return session_call_on_frame_received(session, frame);
}

static int session_on_stream_window_update_received(nghttp2_session *session,
                                                    nghttp2_frame *frame) {
  int rv;
  nghttp2_stream *stream;

  if (session_detect_idle_stream(session, frame->hd.stream_id)) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "WINDOW_UPDATE to idle stream");
  }

  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
  if (!stream) {
    return 0;
  }
  if (state_reserved_remote(session, stream)) {
    return session_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO, "WINDOW_UPADATE to reserved stream");
  }
  if (frame->window_update.window_size_increment == 0) {
    return session_handle_invalid_connection(
        session, frame, NGHTTP2_ERR_PROTO,
        "WINDOW_UPDATE: window_size_increment == 0");
  }
  if (NGHTTP2_MAX_WINDOW_SIZE - frame->window_update.window_size_increment <
      stream->remote_window_size) {
    return session_handle_invalid_stream(session, frame,
                                         NGHTTP2_ERR_FLOW_CONTROL);
  }
  stream->remote_window_size += frame->window_update.window_size_increment;

  if (stream->remote_window_size > 0 &&
      nghttp2_stream_check_deferred_by_flow_control(stream)) {

    rv = session_resume_deferred_stream_item(
        session, stream, NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL);

    if (nghttp2_is_fatal(rv)) {
      return rv;
    }
  }
  return session_call_on_frame_received(session, frame);
}

int nghttp2_session_on_window_update_received(nghttp2_session *session,
                                              nghttp2_frame *frame) {
  if (frame->hd.stream_id == 0) {
    return session_on_connection_window_update_received(session, frame);
  } else {
    return session_on_stream_window_update_received(session, frame);
  }
}

static int session_process_window_update_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  nghttp2_frame_unpack_window_update_payload(&frame->window_update,
                                             iframe->sbuf.pos);

  return nghttp2_session_on_window_update_received(session, frame);
}

int nghttp2_session_on_altsvc_received(nghttp2_session *session,
                                       nghttp2_frame *frame) {
  nghttp2_ext_altsvc *altsvc;
  nghttp2_stream *stream;

  altsvc = frame->ext.payload;

  /* session->server case has been excluded */

  if (frame->hd.stream_id == 0) {
    if (altsvc->origin_len == 0) {
      return session_call_on_invalid_frame_recv_callback(session, frame,
                                                         NGHTTP2_ERR_PROTO);
    }
  } else {
    if (altsvc->origin_len > 0) {
      return session_call_on_invalid_frame_recv_callback(session, frame,
                                                         NGHTTP2_ERR_PROTO);
    }

    stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
    if (!stream) {
      return 0;
    }

    if (stream->state == NGHTTP2_STREAM_CLOSING) {
      return 0;
    }
  }

  if (altsvc->field_value_len == 0) {
    return session_call_on_invalid_frame_recv_callback(session, frame,
                                                       NGHTTP2_ERR_PROTO);
  }

  return session_call_on_frame_received(session, frame);
}

int nghttp2_session_on_origin_received(nghttp2_session *session,
                                       nghttp2_frame *frame) {
  return session_call_on_frame_received(session, frame);
}

int nghttp2_session_on_priority_update_received(nghttp2_session *session,
                                                nghttp2_frame *frame) {
  nghttp2_ext_priority_update *priority_update;
  nghttp2_stream *stream;
  nghttp2_priority_spec pri_spec;
  nghttp2_extpri extpri;
  int rv;

  assert(session->server);

  priority_update = frame->ext.payload;

  if (frame->hd.stream_id != 0) {
    return session_handle_invalid_connection(session, frame, NGHTTP2_ERR_PROTO,
                                             "PRIORITY_UPDATE: stream_id == 0");
  }

  if (nghttp2_session_is_my_stream_id(session, priority_update->stream_id)) {
    if (session_detect_idle_stream(session, priority_update->stream_id)) {
      return session_handle_invalid_connection(
          session, frame, NGHTTP2_ERR_PROTO,
          "PRIORITY_UPDATE: prioritizing idle push is not allowed");
    }

    /* TODO Ignore priority signal to a push stream for now */
    return session_call_on_frame_received(session, frame);
  }

  stream = nghttp2_session_get_stream_raw(session, priority_update->stream_id);
  if (stream) {
    /* Stream already exists. */
    if (stream->flags & NGHTTP2_STREAM_FLAG_IGNORE_CLIENT_PRIORITIES) {
      return session_call_on_frame_received(session, frame);
    }
  } else if (session_detect_idle_stream(session, priority_update->stream_id)) {
    if (session->num_idle_streams + session->num_incoming_streams >=
        session->local_settings.max_concurrent_streams) {
      return session_handle_invalid_connection(
          session, frame, NGHTTP2_ERR_PROTO,
          "PRIORITY_UPDATE: max concurrent streams exceeded");
    }

    nghttp2_priority_spec_default_init(&pri_spec);
    stream = nghttp2_session_open_stream(session, priority_update->stream_id,
                                         NGHTTP2_FLAG_NONE, &pri_spec,
                                         NGHTTP2_STREAM_IDLE, NULL);
    if (!stream) {
      return NGHTTP2_ERR_NOMEM;
    }
  } else {
    return session_call_on_frame_received(session, frame);
  }

  extpri.urgency = NGHTTP2_EXTPRI_DEFAULT_URGENCY;
  extpri.inc = 0;

  rv = nghttp2_http_parse_priority(&extpri, priority_update->field_value,
                                   priority_update->field_value_len);
  if (rv != 0) {
    /* Just ignore field_value if it cannot be parsed. */
    return session_call_on_frame_received(session, frame);
  }

  rv = session_update_stream_priority(session, stream,
                                      nghttp2_extpri_to_uint8(&extpri));
  if (rv != 0) {
    if (nghttp2_is_fatal(rv)) {
      return rv;
    }
  }

  return session_call_on_frame_received(session, frame);
}

static int session_process_altsvc_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  nghttp2_frame_unpack_altsvc_payload(
      &frame->ext, nghttp2_get_uint16(iframe->sbuf.pos), iframe->lbuf.pos,
      nghttp2_buf_len(&iframe->lbuf));

  /* nghttp2_frame_unpack_altsvc_payload steals buffer from
     iframe->lbuf */
  nghttp2_buf_wrap_init(&iframe->lbuf, NULL, 0);

  return nghttp2_session_on_altsvc_received(session, frame);
}

static int session_process_origin_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;
  nghttp2_mem *mem = &session->mem;
  int rv;

  rv = nghttp2_frame_unpack_origin_payload(&frame->ext, iframe->lbuf.pos,
                                           nghttp2_buf_len(&iframe->lbuf), mem);
  if (rv != 0) {
    if (nghttp2_is_fatal(rv)) {
      return rv;
    }
    /* Ignore ORIGIN frame which cannot be parsed. */
    return 0;
  }

  return nghttp2_session_on_origin_received(session, frame);
}

static int session_process_priority_update_frame(nghttp2_session *session) {
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  nghttp2_frame_unpack_priority_update_payload(&frame->ext, iframe->sbuf.pos,
                                               nghttp2_buf_len(&iframe->sbuf));

  return nghttp2_session_on_priority_update_received(session, frame);
}

static int session_process_extension_frame(nghttp2_session *session) {
  int rv;
  nghttp2_inbound_frame *iframe = &session->iframe;
  nghttp2_frame *frame = &iframe->frame;

  rv = session_call_unpack_extension_callback(session);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  /* This handles the case where rv == NGHTTP2_ERR_CANCEL as well */
  if (rv != 0) {
    return 0;
  }

  return session_call_on_frame_received(session, frame);
}

int nghttp2_session_on_data_received(nghttp2_session *session,
                                     nghttp2_frame *frame) {
  int rv = 0;
  nghttp2_stream *stream;

  /* We don't call on_frame_recv_callback if stream has been closed
     already or being closed. */
  stream = nghttp2_session_get_stream(session, frame->hd.stream_id);
  if (!stream || stream->state == NGHTTP2_STREAM_CLOSING) {
    /* This should be treated as stream error, but it results in lots
       of RST_STREAM. So just ignore frame against nonexistent stream
       for now. */
    return 0;
  }

  if (session_enforce_http_messaging(session) &&
      (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
    if (nghttp2_http_on_remote_end_stream(stream) != 0) {
      rv = nghttp2_session_add_rst_stream(session, stream->stream_id,
                                          NGHTTP2_PROTOCOL_ERROR);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_RD);
      /* Don't call nghttp2_session_close_stream_if_shut_rdwr because
         RST_STREAM has been submitted. */
      return 0;
    }
  }

  rv = session_call_on_frame_received(session, frame);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
    nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_RD);
    rv = nghttp2_session_close_stream_if_shut_rdwr(session, stream);
    if (nghttp2_is_fatal(rv)) {
      return rv;
    }
  }
  return 0;
}

/* For errors, this function only returns FATAL error. */
static int session_process_data_frame(nghttp2_session *session) {
  int rv;
  nghttp2_frame *public_data_frame = &session->iframe.frame;
  rv = nghttp2_session_on_data_received(session, public_data_frame);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }
  return 0;
}

/*
 * Now we have SETTINGS synchronization, flow control error can be
 * detected strictly. If DATA frame is received with length > 0 and
 * current received window size + delta length is strictly larger than
 * local window size, it is subject to FLOW_CONTROL_ERROR, so return
 * -1. Note that local_window_size is calculated after SETTINGS ACK is
 * received from peer, so peer must honor this limit. If the resulting
 * recv_window_size is strictly larger than NGHTTP2_MAX_WINDOW_SIZE,
 * return -1 too.
 */
static int adjust_recv_window_size(int32_t *recv_window_size_ptr, size_t delta,
                                   int32_t local_window_size) {
  if (*recv_window_size_ptr > local_window_size - (int32_t)delta ||
      *recv_window_size_ptr > NGHTTP2_MAX_WINDOW_SIZE - (int32_t)delta) {
    return -1;
  }
  *recv_window_size_ptr += (int32_t)delta;
  return 0;
}

int nghttp2_session_update_recv_stream_window_size(nghttp2_session *session,
                                                   nghttp2_stream *stream,
                                                   size_t delta_size,
                                                   int send_window_update) {
  int rv;
  rv = adjust_recv_window_size(&stream->recv_window_size, delta_size,
                               stream->local_window_size);
  if (rv != 0) {
    return nghttp2_session_add_rst_stream(session, stream->stream_id,
                                          NGHTTP2_FLOW_CONTROL_ERROR);
  }
  /* We don't have to send WINDOW_UPDATE if the data received is the
     last chunk in the incoming stream. */
  /* We have to use local_settings here because it is the constraint
     the remote endpoint should honor. */
  if (send_window_update &&
      !(session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE) &&
      stream->window_update_queued == 0 &&
      nghttp2_should_send_window_update(stream->local_window_size,
                                        stream->recv_window_size)) {
    rv = nghttp2_session_add_window_update(session, NGHTTP2_FLAG_NONE,
                                           stream->stream_id,
                                           stream->recv_window_size);
    if (rv != 0) {
      return rv;
    }

    stream->recv_window_size = 0;
  }
  return 0;
}

int nghttp2_session_update_recv_connection_window_size(nghttp2_session *session,
                                                       size_t delta_size) {
  int rv;
  rv = adjust_recv_window_size(&session->recv_window_size, delta_size,
                               session->local_window_size);
  if (rv != 0) {
    return nghttp2_session_terminate_session(session,
                                             NGHTTP2_FLOW_CONTROL_ERROR);
  }
  if (!(session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE) &&
      session->window_update_queued == 0 &&
      nghttp2_should_send_window_update(session->local_window_size,
                                        session->recv_window_size)) {
    /* Use stream ID 0 to update connection-level flow control
       window */
    rv = nghttp2_session_add_window_update(session, NGHTTP2_FLAG_NONE, 0,
                                           session->recv_window_size);
    if (rv != 0) {
      return rv;
    }

    session->recv_window_size = 0;
  }
  return 0;
}

static int session_update_consumed_size(nghttp2_session *session,
                                        int32_t *consumed_size_ptr,
                                        int32_t *recv_window_size_ptr,
                                        uint8_t window_update_queued,
                                        int32_t stream_id, size_t delta_size,
                                        int32_t local_window_size) {
  int32_t recv_size;
  int rv;

  if ((size_t)*consumed_size_ptr > NGHTTP2_MAX_WINDOW_SIZE - delta_size) {
    return nghttp2_session_terminate_session(session,
                                             NGHTTP2_FLOW_CONTROL_ERROR);
  }

  *consumed_size_ptr += (int32_t)delta_size;

  if (window_update_queued == 0) {
    /* recv_window_size may be smaller than consumed_size, because it
       may be decreased by negative value with
       nghttp2_submit_window_update(). */
    recv_size = nghttp2_min_int32(*consumed_size_ptr, *recv_window_size_ptr);

    if (nghttp2_should_send_window_update(local_window_size, recv_size)) {
      rv = nghttp2_session_add_window_update(session, NGHTTP2_FLAG_NONE,
                                             stream_id, recv_size);

      if (rv != 0) {
        return rv;
      }

      *recv_window_size_ptr -= recv_size;
      *consumed_size_ptr -= recv_size;
    }
  }

  return 0;
}

static int session_update_stream_consumed_size(nghttp2_session *session,
                                               nghttp2_stream *stream,
                                               size_t delta_size) {
  return session_update_consumed_size(
      session, &stream->consumed_size, &stream->recv_window_size,
      stream->window_update_queued, stream->stream_id, delta_size,
      stream->local_window_size);
}

static int session_update_connection_consumed_size(nghttp2_session *session,
                                                   size_t delta_size) {
  return session_update_consumed_size(
      session, &session->consumed_size, &session->recv_window_size,
      session->window_update_queued, 0, delta_size, session->local_window_size);
}

/*
 * Checks that we can receive the DATA frame for stream, which is
 * indicated by |session->iframe.frame.hd.stream_id|. If it is a
 * connection error situation, GOAWAY frame will be issued by this
 * function.
 *
 * If the DATA frame is allowed, returns 0.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_IGN_PAYLOAD
 *   The reception of DATA frame is connection error; or should be
 *   ignored.
 * NGHTTP2_ERR_NOMEM
 *   Out of memory.
 */
static int session_on_data_received_fail_fast(nghttp2_session *session) {
  int rv;
  nghttp2_stream *stream;
  nghttp2_inbound_frame *iframe;
  int32_t stream_id;
  const char *failure_reason;
  uint32_t error_code = NGHTTP2_PROTOCOL_ERROR;

  iframe = &session->iframe;
  stream_id = iframe->frame.hd.stream_id;

  if (stream_id == 0) {
    /* The spec says that if a DATA frame is received whose stream ID
       is 0, the recipient MUST respond with a connection error of
       type PROTOCOL_ERROR. */
    failure_reason = "DATA: stream_id == 0";
    goto fail;
  }

  if (session_detect_idle_stream(session, stream_id)) {
    failure_reason = "DATA: stream in idle";
    error_code = NGHTTP2_PROTOCOL_ERROR;
    goto fail;
  }

  stream = nghttp2_session_get_stream(session, stream_id);
  if (!stream) {
    stream = nghttp2_session_get_stream_raw(session, stream_id);
    if (stream && (stream->shut_flags & NGHTTP2_SHUT_RD)) {
      failure_reason = "DATA: stream closed";
      error_code = NGHTTP2_STREAM_CLOSED;
      goto fail;
    }

    return NGHTTP2_ERR_IGN_PAYLOAD;
  }
  if (stream->shut_flags & NGHTTP2_SHUT_RD) {
    failure_reason = "DATA: stream in half-closed(remote)";
    error_code = NGHTTP2_STREAM_CLOSED;
    goto fail;
  }

  if (nghttp2_session_is_my_stream_id(session, stream_id)) {
    if (stream->state == NGHTTP2_STREAM_CLOSING) {
      return NGHTTP2_ERR_IGN_PAYLOAD;
    }
    if (stream->state != NGHTTP2_STREAM_OPENED) {
      failure_reason = "DATA: stream not opened";
      goto fail;
    }
    return 0;
  }
  if (stream->state == NGHTTP2_STREAM_RESERVED) {
    failure_reason = "DATA: stream in reserved";
    goto fail;
  }
  if (stream->state == NGHTTP2_STREAM_CLOSING) {
    return NGHTTP2_ERR_IGN_PAYLOAD;
  }
  return 0;
fail:
  rv = nghttp2_session_terminate_session_with_reason(session, error_code,
                                                     failure_reason);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }
  return NGHTTP2_ERR_IGN_PAYLOAD;
}

static size_t inbound_frame_payload_readlen(nghttp2_inbound_frame *iframe,
                                            const uint8_t *in,
                                            const uint8_t *last) {
  return nghttp2_min_size((size_t)(last - in), iframe->payloadleft);
}

/*
 * Resets iframe->sbuf and advance its mark pointer by |left| bytes.
 */
static void inbound_frame_set_mark(nghttp2_inbound_frame *iframe, size_t left) {
  nghttp2_buf_reset(&iframe->sbuf);
  iframe->sbuf.mark += left;
}

static size_t inbound_frame_buf_read(nghttp2_inbound_frame *iframe,
                                     const uint8_t *in, const uint8_t *last) {
  size_t readlen;

  readlen = nghttp2_min_size((size_t)(last - in),
                             nghttp2_buf_mark_avail(&iframe->sbuf));

  iframe->sbuf.last = nghttp2_cpymem(iframe->sbuf.last, in, readlen);

  return readlen;
}

/*
 * Unpacks SETTINGS entry in iframe->sbuf.
 */
static void inbound_frame_set_settings_entry(nghttp2_inbound_frame *iframe) {
  nghttp2_settings_entry iv;
  nghttp2_settings_entry *min_header_table_size_entry;
  size_t i;

  nghttp2_frame_unpack_settings_entry(&iv, iframe->sbuf.pos);

  switch (iv.settings_id) {
  case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
  case NGHTTP2_SETTINGS_ENABLE_PUSH:
  case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
  case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
  case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:
  case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
  case NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL:
  case NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES:
    break;
  default:
    DEBUGF("recv: unknown settings id=0x%02x\n", iv.settings_id);

    iframe->iv[iframe->niv++] = iv;

    return;
  }

  for (i = 0; i < iframe->niv; ++i) {
    if (iframe->iv[i].settings_id == iv.settings_id) {
      iframe->iv[i] = iv;
      break;
    }
  }

  if (i == iframe->niv) {
    iframe->iv[iframe->niv++] = iv;
  }

  if (iv.settings_id == NGHTTP2_SETTINGS_HEADER_TABLE_SIZE) {
    /* Keep track of minimum value of SETTINGS_HEADER_TABLE_SIZE */
    min_header_table_size_entry = &iframe->iv[iframe->max_niv - 1];

    if (iv.value < min_header_table_size_entry->value) {
      min_header_table_size_entry->value = iv.value;
    }
  }
}

/*
 * Checks PADDED flags and set iframe->sbuf to read them accordingly.
 * If padding is set, this function returns 1.  If no padding is set,
 * this function returns 0.  On error, returns -1.
 */
static int inbound_frame_handle_pad(nghttp2_inbound_frame *iframe,
                                    nghttp2_frame_hd *hd) {
  if (hd->flags & NGHTTP2_FLAG_PADDED) {
    if (hd->length < 1) {
      return -1;
    }
    inbound_frame_set_mark(iframe, 1);
    return 1;
  }
  DEBUGF("recv: no padding in payload\n");
  return 0;
}

/*
 * Computes number of padding based on flags. This function returns
 * the calculated length if it succeeds, or -1.
 */
static nghttp2_ssize inbound_frame_compute_pad(nghttp2_inbound_frame *iframe) {
  size_t padlen;

  /* 1 for Pad Length field */
  padlen = (size_t)(iframe->sbuf.pos[0] + 1);

  DEBUGF("recv: padlen=%zu\n", padlen);

  /* We cannot use iframe->frame.hd.length because of CONTINUATION */
  if (padlen - 1 > iframe->payloadleft) {
    return -1;
  }

  iframe->padlen = padlen;

  return (nghttp2_ssize)padlen;
}

/*
 * This function returns the effective payload length in the data of
 * length |readlen| when the remaining payload is |payloadleft|. The
 * |payloadleft| does not include |readlen|. If padding was started
 * strictly before this data chunk, this function returns -1.
 */
static nghttp2_ssize
inbound_frame_effective_readlen(nghttp2_inbound_frame *iframe,
                                size_t payloadleft, size_t readlen) {
  size_t trail_padlen =
      nghttp2_frame_trail_padlen(&iframe->frame, iframe->padlen);

  if (trail_padlen > payloadleft) {
    size_t padlen;
    padlen = trail_padlen - payloadleft;
    if (readlen < padlen) {
      return -1;
    }
    return (nghttp2_ssize)(readlen - padlen);
  }
  return (nghttp2_ssize)(readlen);
}

static const uint8_t static_in[] = {0};

ssize_t nghttp2_session_mem_recv(nghttp2_session *session, const uint8_t *in,
                                 size_t inlen) {
  return (ssize_t)nghttp2_session_mem_recv2(session, in, inlen);
}

nghttp2_ssize nghttp2_session_mem_recv2(nghttp2_session *session,
                                        const uint8_t *in, size_t inlen) {
  const uint8_t *first, *last;
  nghttp2_inbound_frame *iframe = &session->iframe;
  size_t readlen;
  nghttp2_ssize padlen;
  int rv;
  int busy = 0;
  nghttp2_frame_hd cont_hd;
  nghttp2_stream *stream;
  size_t pri_fieldlen;
  nghttp2_mem *mem;

  if (in == NULL) {
    assert(inlen == 0);
    in = static_in;
  }

  first = in;
  last = in + inlen;

  DEBUGF("recv: connection recv_window_size=%d, local_window=%d\n",
         session->recv_window_size, session->local_window_size);

  mem = &session->mem;

  /* We may have idle streams more than we expect (e.g.,
     nghttp2_session_change_stream_priority() or
     nghttp2_session_create_idle_stream()).  Adjust them here. */
  rv = nghttp2_session_adjust_idle_stream(session);
  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  if (!nghttp2_session_want_read(session)) {
    return (nghttp2_ssize)inlen;
  }

  for (;;) {
    switch (iframe->state) {
    case NGHTTP2_IB_READ_CLIENT_MAGIC:
      readlen = nghttp2_min_size(inlen, iframe->payloadleft);

      if (memcmp(&NGHTTP2_CLIENT_MAGIC[NGHTTP2_CLIENT_MAGIC_LEN -
                                       iframe->payloadleft],
                 in, readlen) != 0) {
        return NGHTTP2_ERR_BAD_CLIENT_MAGIC;
      }

      iframe->payloadleft -= readlen;
      in += readlen;

      if (iframe->payloadleft == 0) {
        session_inbound_frame_reset(session);
        iframe->state = NGHTTP2_IB_READ_FIRST_SETTINGS;
      }

      break;
    case NGHTTP2_IB_READ_FIRST_SETTINGS:
      DEBUGF("recv: [IB_READ_FIRST_SETTINGS]\n");

      readlen = inbound_frame_buf_read(iframe, in, last);
      in += readlen;

      if (nghttp2_buf_mark_avail(&iframe->sbuf)) {
        return (nghttp2_ssize)(in - first);
      }

      if (iframe->sbuf.pos[3] != NGHTTP2_SETTINGS ||
          (iframe->sbuf.pos[4] & NGHTTP2_FLAG_ACK)) {
        rv = session_call_error_callback(
            session, NGHTTP2_ERR_SETTINGS_EXPECTED,
            "Remote peer returned unexpected data while we expected "
            "SETTINGS frame.  Perhaps, peer does not support HTTP/2 "
            "properly.");

        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        rv = nghttp2_session_terminate_session_with_reason(
            session, NGHTTP2_PROTOCOL_ERROR, "SETTINGS expected");

        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        return (nghttp2_ssize)inlen;
      }

      iframe->state = NGHTTP2_IB_READ_HEAD;

    /* Fall through */
    case NGHTTP2_IB_READ_HEAD: {
      int on_begin_frame_called = 0;

      DEBUGF("recv: [IB_READ_HEAD]\n");

      readlen = inbound_frame_buf_read(iframe, in, last);
      in += readlen;

      if (nghttp2_buf_mark_avail(&iframe->sbuf)) {
        return (nghttp2_ssize)(in - first);
      }

      nghttp2_frame_unpack_frame_hd(&iframe->frame.hd, iframe->sbuf.pos);
      iframe->payloadleft = iframe->frame.hd.length;

      DEBUGF("recv: payloadlen=%zu, type=%u, flags=0x%02x, stream_id=%d\n",
             iframe->frame.hd.length, iframe->frame.hd.type,
             iframe->frame.hd.flags, iframe->frame.hd.stream_id);

      if (iframe->frame.hd.length > session->local_settings.max_frame_size) {
        DEBUGF("recv: length is too large %zu > %u\n", iframe->frame.hd.length,
               session->local_settings.max_frame_size);

        rv = nghttp2_session_terminate_session_with_reason(
            session, NGHTTP2_FRAME_SIZE_ERROR, "too large frame size");

        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        return (nghttp2_ssize)inlen;
      }

      switch (iframe->frame.hd.type) {
      case NGHTTP2_DATA: {
        DEBUGF("recv: DATA\n");

        iframe->frame.hd.flags &=
            (NGHTTP2_FLAG_END_STREAM | NGHTTP2_FLAG_PADDED);
        /* Check stream is open. If it is not open or closing,
           ignore payload. */
        busy = 1;

        rv = session_on_data_received_fail_fast(session);
        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }
        if (rv == NGHTTP2_ERR_IGN_PAYLOAD) {
          DEBUGF("recv: DATA not allowed stream_id=%d\n",
                 iframe->frame.hd.stream_id);
          iframe->state = NGHTTP2_IB_IGN_DATA;
          break;
        }

        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        rv = inbound_frame_handle_pad(iframe, &iframe->frame.hd);
        if (rv < 0) {
          rv = nghttp2_session_terminate_session_with_reason(
              session, NGHTTP2_PROTOCOL_ERROR,
              "DATA: insufficient padding space");

          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
          return (nghttp2_ssize)inlen;
        }

        if (rv == 1) {
          iframe->state = NGHTTP2_IB_READ_PAD_DATA;
          break;
        }

        iframe->state = NGHTTP2_IB_READ_DATA;
        break;
      }
      case NGHTTP2_HEADERS:

        DEBUGF("recv: HEADERS\n");

        iframe->frame.hd.flags &=
            (NGHTTP2_FLAG_END_STREAM | NGHTTP2_FLAG_END_HEADERS |
             NGHTTP2_FLAG_PADDED | NGHTTP2_FLAG_PRIORITY);

        rv = inbound_frame_handle_pad(iframe, &iframe->frame.hd);
        if (rv < 0) {
          rv = nghttp2_session_terminate_session_with_reason(
              session, NGHTTP2_PROTOCOL_ERROR,
              "HEADERS: insufficient padding space");
          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
          return (nghttp2_ssize)inlen;
        }

        if (rv == 1) {
          iframe->state = NGHTTP2_IB_READ_NBYTE;
          break;
        }

        pri_fieldlen = nghttp2_frame_priority_len(iframe->frame.hd.flags);

        if (pri_fieldlen > 0) {
          if (iframe->payloadleft < pri_fieldlen) {
            busy = 1;
            iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
            break;
          }

          iframe->state = NGHTTP2_IB_READ_NBYTE;

          inbound_frame_set_mark(iframe, pri_fieldlen);

          break;
        }

        /* Call on_begin_frame_callback here because
           session_process_headers_frame() may call
           on_begin_headers_callback */
        rv = session_call_on_begin_frame(session, &iframe->frame.hd);

        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        on_begin_frame_called = 1;

        rv = session_process_headers_frame(session);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        busy = 1;

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        if (rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
          rv = nghttp2_session_add_rst_stream(
              session, iframe->frame.hd.stream_id, NGHTTP2_INTERNAL_ERROR);
          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
          iframe->state = NGHTTP2_IB_IGN_HEADER_BLOCK;
          break;
        }

        if (rv == NGHTTP2_ERR_IGN_HEADER_BLOCK) {
          iframe->state = NGHTTP2_IB_IGN_HEADER_BLOCK;
          break;
        }

        iframe->state = NGHTTP2_IB_READ_HEADER_BLOCK;

        break;
      case NGHTTP2_PRIORITY:
        DEBUGF("recv: PRIORITY\n");

        iframe->frame.hd.flags = NGHTTP2_FLAG_NONE;

        if (iframe->payloadleft != NGHTTP2_PRIORITY_SPECLEN) {
          busy = 1;

          iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;

          break;
        }

        iframe->state = NGHTTP2_IB_READ_NBYTE;

        inbound_frame_set_mark(iframe, NGHTTP2_PRIORITY_SPECLEN);

        break;
      case NGHTTP2_RST_STREAM:
      case NGHTTP2_WINDOW_UPDATE:
#ifdef DEBUGBUILD
        switch (iframe->frame.hd.type) {
        case NGHTTP2_RST_STREAM:
          DEBUGF("recv: RST_STREAM\n");
          break;
        case NGHTTP2_WINDOW_UPDATE:
          DEBUGF("recv: WINDOW_UPDATE\n");
          break;
        }
#endif /* DEBUGBUILD */

        iframe->frame.hd.flags = NGHTTP2_FLAG_NONE;

        if (iframe->payloadleft != 4) {
          busy = 1;
          iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
          break;
        }

        iframe->state = NGHTTP2_IB_READ_NBYTE;

        inbound_frame_set_mark(iframe, 4);

        break;
      case NGHTTP2_SETTINGS:
        DEBUGF("recv: SETTINGS\n");

        iframe->frame.hd.flags &= NGHTTP2_FLAG_ACK;

        if ((iframe->frame.hd.length % NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH) ||
            ((iframe->frame.hd.flags & NGHTTP2_FLAG_ACK) &&
             iframe->payloadleft > 0)) {
          busy = 1;
          iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
          break;
        }

        /* Check the settings flood counter early to be safe */
        if (session->obq_flood_counter_ >= session->max_outbound_ack &&
            !(iframe->frame.hd.flags & NGHTTP2_FLAG_ACK)) {
          return NGHTTP2_ERR_FLOODED;
        }

        iframe->state = NGHTTP2_IB_READ_SETTINGS;

        if (iframe->payloadleft) {
          nghttp2_settings_entry *min_header_table_size_entry;

          /* We allocate iv with additional one entry, to store the
             minimum header table size. */
          iframe->max_niv =
              iframe->frame.hd.length / NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH + 1;

          if (iframe->max_niv - 1 > session->max_settings) {
            rv = nghttp2_session_terminate_session_with_reason(
                session, NGHTTP2_ENHANCE_YOUR_CALM,
                "SETTINGS: too many setting entries");
            if (nghttp2_is_fatal(rv)) {
              return rv;
            }
            return (nghttp2_ssize)inlen;
          }

          iframe->iv = nghttp2_mem_malloc(mem, sizeof(nghttp2_settings_entry) *
                                                   iframe->max_niv);

          if (!iframe->iv) {
            return NGHTTP2_ERR_NOMEM;
          }

          min_header_table_size_entry = &iframe->iv[iframe->max_niv - 1];
          min_header_table_size_entry->settings_id =
              NGHTTP2_SETTINGS_HEADER_TABLE_SIZE;
          min_header_table_size_entry->value = UINT32_MAX;

          inbound_frame_set_mark(iframe, NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH);
          break;
        }

        busy = 1;

        inbound_frame_set_mark(iframe, 0);

        break;
      case NGHTTP2_PUSH_PROMISE:
        DEBUGF("recv: PUSH_PROMISE\n");

        iframe->frame.hd.flags &=
            (NGHTTP2_FLAG_END_HEADERS | NGHTTP2_FLAG_PADDED);

        rv = inbound_frame_handle_pad(iframe, &iframe->frame.hd);
        if (rv < 0) {
          rv = nghttp2_session_terminate_session_with_reason(
              session, NGHTTP2_PROTOCOL_ERROR,
              "PUSH_PROMISE: insufficient padding space");
          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
          return (nghttp2_ssize)inlen;
        }

        if (rv == 1) {
          iframe->state = NGHTTP2_IB_READ_NBYTE;
          break;
        }

        if (iframe->payloadleft < 4) {
          busy = 1;
          iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
          break;
        }

        iframe->state = NGHTTP2_IB_READ_NBYTE;

        inbound_frame_set_mark(iframe, 4);

        break;
      case NGHTTP2_PING:
        DEBUGF("recv: PING\n");

        iframe->frame.hd.flags &= NGHTTP2_FLAG_ACK;

        if (iframe->payloadleft != 8) {
          busy = 1;
          iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
          break;
        }

        iframe->state = NGHTTP2_IB_READ_NBYTE;
        inbound_frame_set_mark(iframe, 8);

        break;
      case NGHTTP2_GOAWAY:
        DEBUGF("recv: GOAWAY\n");

        iframe->frame.hd.flags = NGHTTP2_FLAG_NONE;

        if (iframe->payloadleft < 8) {
          busy = 1;
          iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
          break;
        }

        iframe->state = NGHTTP2_IB_READ_NBYTE;
        inbound_frame_set_mark(iframe, 8);

        break;
      case NGHTTP2_CONTINUATION:
        DEBUGF("recv: unexpected CONTINUATION\n");

        /* Receiving CONTINUATION in this state are subject to
           connection error of type PROTOCOL_ERROR */
        rv = nghttp2_session_terminate_session_with_reason(
            session, NGHTTP2_PROTOCOL_ERROR, "CONTINUATION: unexpected");
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        return (nghttp2_ssize)inlen;
      default:
        DEBUGF("recv: extension frame\n");

        if (check_ext_type_set(session->user_recv_ext_types,
                               iframe->frame.hd.type)) {
          if (!session->callbacks.unpack_extension_callback) {
            /* Silently ignore unknown frame type. */

            busy = 1;

            iframe->state = NGHTTP2_IB_IGN_PAYLOAD;

            break;
          }

          busy = 1;

          iframe->state = NGHTTP2_IB_READ_EXTENSION_PAYLOAD;

          break;
        } else {
          switch (iframe->frame.hd.type) {
          case NGHTTP2_ALTSVC:
            if ((session->builtin_recv_ext_types & NGHTTP2_TYPEMASK_ALTSVC) ==
                0) {
              busy = 1;
              iframe->state = NGHTTP2_IB_IGN_PAYLOAD;
              break;
            }

            DEBUGF("recv: ALTSVC\n");

            iframe->frame.hd.flags = NGHTTP2_FLAG_NONE;
            iframe->frame.ext.payload = &iframe->ext_frame_payload.altsvc;

            if (session->server) {
              busy = 1;
              iframe->state = NGHTTP2_IB_IGN_PAYLOAD;
              break;
            }

            if (iframe->payloadleft < 2) {
              busy = 1;
              iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
              break;
            }

            busy = 1;

            iframe->state = NGHTTP2_IB_READ_NBYTE;
            inbound_frame_set_mark(iframe, 2);

            break;
          case NGHTTP2_ORIGIN:
            if (!(session->builtin_recv_ext_types & NGHTTP2_TYPEMASK_ORIGIN)) {
              busy = 1;
              iframe->state = NGHTTP2_IB_IGN_PAYLOAD;
              break;
            }

            DEBUGF("recv: ORIGIN\n");

            iframe->frame.ext.payload = &iframe->ext_frame_payload.origin;

            if (session->server || iframe->frame.hd.stream_id ||
                (iframe->frame.hd.flags & 0xf0)) {
              busy = 1;
              iframe->state = NGHTTP2_IB_IGN_PAYLOAD;
              break;
            }

            iframe->frame.hd.flags = NGHTTP2_FLAG_NONE;

            if (iframe->payloadleft) {
              iframe->raw_lbuf = nghttp2_mem_malloc(mem, iframe->payloadleft);

              if (iframe->raw_lbuf == NULL) {
                return NGHTTP2_ERR_NOMEM;
              }

              nghttp2_buf_wrap_init(&iframe->lbuf, iframe->raw_lbuf,
                                    iframe->payloadleft);
            } else {
              busy = 1;
            }

            iframe->state = NGHTTP2_IB_READ_ORIGIN_PAYLOAD;

            break;
          case NGHTTP2_PRIORITY_UPDATE:
            if ((session->builtin_recv_ext_types &
                 NGHTTP2_TYPEMASK_PRIORITY_UPDATE) == 0) {
              busy = 1;
              iframe->state = NGHTTP2_IB_IGN_PAYLOAD;
              break;
            }

            DEBUGF("recv: PRIORITY_UPDATE\n");

            iframe->frame.hd.flags = NGHTTP2_FLAG_NONE;
            iframe->frame.ext.payload =
                &iframe->ext_frame_payload.priority_update;

            if (!session->server) {
              rv = nghttp2_session_terminate_session_with_reason(
                  session, NGHTTP2_PROTOCOL_ERROR,
                  "PRIORITY_UPDATE is received from server");
              if (nghttp2_is_fatal(rv)) {
                return rv;
              }
              return (nghttp2_ssize)inlen;
            }

            if (iframe->payloadleft < 4) {
              busy = 1;
              iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
              break;
            }

            if (!session_no_rfc7540_pri_no_fallback(session) ||
                iframe->payloadleft > sizeof(iframe->raw_sbuf)) {
              busy = 1;
              iframe->state = NGHTTP2_IB_IGN_PAYLOAD;
              break;
            }

            busy = 1;

            iframe->state = NGHTTP2_IB_READ_NBYTE;
            inbound_frame_set_mark(iframe, iframe->payloadleft);

            break;
          default:
            busy = 1;

            iframe->state = NGHTTP2_IB_IGN_PAYLOAD;

            break;
          }
        }
      }

      if (!on_begin_frame_called) {
        switch (iframe->state) {
        case NGHTTP2_IB_IGN_HEADER_BLOCK:
        case NGHTTP2_IB_IGN_PAYLOAD:
        case NGHTTP2_IB_FRAME_SIZE_ERROR:
        case NGHTTP2_IB_IGN_DATA:
        case NGHTTP2_IB_IGN_ALL:
          break;
        default:
          rv = session_call_on_begin_frame(session, &iframe->frame.hd);

          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
        }
      }

      break;
    }
    case NGHTTP2_IB_READ_NBYTE:
      DEBUGF("recv: [IB_READ_NBYTE]\n");

      readlen = inbound_frame_buf_read(iframe, in, last);
      in += readlen;
      iframe->payloadleft -= readlen;

      DEBUGF("recv: readlen=%zu, payloadleft=%zu, left=%zu\n", readlen,
             iframe->payloadleft, nghttp2_buf_mark_avail(&iframe->sbuf));

      if (nghttp2_buf_mark_avail(&iframe->sbuf)) {
        return (nghttp2_ssize)(in - first);
      }

      switch (iframe->frame.hd.type) {
      case NGHTTP2_HEADERS:
        if (iframe->padlen == 0 &&
            (iframe->frame.hd.flags & NGHTTP2_FLAG_PADDED)) {
          pri_fieldlen = nghttp2_frame_priority_len(iframe->frame.hd.flags);
          padlen = inbound_frame_compute_pad(iframe);
          if (padlen < 0 ||
              (size_t)padlen + pri_fieldlen > 1 + iframe->payloadleft) {
            rv = nghttp2_session_terminate_session_with_reason(
                session, NGHTTP2_PROTOCOL_ERROR, "HEADERS: invalid padding");
            if (nghttp2_is_fatal(rv)) {
              return rv;
            }
            return (nghttp2_ssize)inlen;
          }
          iframe->frame.headers.padlen = (size_t)padlen;

          if (pri_fieldlen > 0) {
            if (iframe->payloadleft < pri_fieldlen) {
              busy = 1;
              iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
              break;
            }
            iframe->state = NGHTTP2_IB_READ_NBYTE;
            inbound_frame_set_mark(iframe, pri_fieldlen);
            break;
          } else {
            /* Truncate buffers used for padding spec */
            inbound_frame_set_mark(iframe, 0);
          }
        }

        rv = session_process_headers_frame(session);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        busy = 1;

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        if (rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
          rv = nghttp2_session_add_rst_stream(
              session, iframe->frame.hd.stream_id, NGHTTP2_INTERNAL_ERROR);
          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
          iframe->state = NGHTTP2_IB_IGN_HEADER_BLOCK;
          break;
        }

        if (rv == NGHTTP2_ERR_IGN_HEADER_BLOCK) {
          iframe->state = NGHTTP2_IB_IGN_HEADER_BLOCK;
          break;
        }

        iframe->state = NGHTTP2_IB_READ_HEADER_BLOCK;

        break;
      case NGHTTP2_PRIORITY:
        if (!session_no_rfc7540_pri_no_fallback(session) &&
            session->remote_settings.no_rfc7540_priorities != 1) {
          rv = session_process_priority_frame(session);
          if (nghttp2_is_fatal(rv)) {
            return rv;
          }

          if (iframe->state == NGHTTP2_IB_IGN_ALL) {
            return (nghttp2_ssize)inlen;
          }
        }

        session_inbound_frame_reset(session);

        break;
      case NGHTTP2_RST_STREAM:
        rv = session_process_rst_stream_frame(session);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        session_inbound_frame_reset(session);

        break;
      case NGHTTP2_PUSH_PROMISE:
        if (iframe->padlen == 0 &&
            (iframe->frame.hd.flags & NGHTTP2_FLAG_PADDED)) {
          padlen = inbound_frame_compute_pad(iframe);
          if (padlen < 0 || (size_t)padlen + 4 /* promised stream id */
                                > 1 + iframe->payloadleft) {
            rv = nghttp2_session_terminate_session_with_reason(
                session, NGHTTP2_PROTOCOL_ERROR,
                "PUSH_PROMISE: invalid padding");
            if (nghttp2_is_fatal(rv)) {
              return rv;
            }
            return (nghttp2_ssize)inlen;
          }

          iframe->frame.push_promise.padlen = (size_t)padlen;

          if (iframe->payloadleft < 4) {
            busy = 1;
            iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
            break;
          }

          iframe->state = NGHTTP2_IB_READ_NBYTE;

          inbound_frame_set_mark(iframe, 4);

          break;
        }

        rv = session_process_push_promise_frame(session);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        busy = 1;

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        if (rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
          rv = nghttp2_session_add_rst_stream(
              session, iframe->frame.push_promise.promised_stream_id,
              NGHTTP2_INTERNAL_ERROR);
          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
          iframe->state = NGHTTP2_IB_IGN_HEADER_BLOCK;
          break;
        }

        if (rv == NGHTTP2_ERR_IGN_HEADER_BLOCK) {
          iframe->state = NGHTTP2_IB_IGN_HEADER_BLOCK;
          break;
        }

        iframe->state = NGHTTP2_IB_READ_HEADER_BLOCK;

        break;
      case NGHTTP2_PING:
        rv = session_process_ping_frame(session);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        session_inbound_frame_reset(session);

        break;
      case NGHTTP2_GOAWAY: {
        size_t debuglen;

        /* 8 is Last-stream-ID + Error Code */
        debuglen = iframe->frame.hd.length - 8;

        if (debuglen > 0) {
          iframe->raw_lbuf = nghttp2_mem_malloc(mem, debuglen);

          if (iframe->raw_lbuf == NULL) {
            return NGHTTP2_ERR_NOMEM;
          }

          nghttp2_buf_wrap_init(&iframe->lbuf, iframe->raw_lbuf, debuglen);
        }

        busy = 1;

        iframe->state = NGHTTP2_IB_READ_GOAWAY_DEBUG;

        break;
      }
      case NGHTTP2_WINDOW_UPDATE:
        rv = session_process_window_update_frame(session);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        session_inbound_frame_reset(session);

        break;
      case NGHTTP2_ALTSVC: {
        size_t origin_len;

        origin_len = nghttp2_get_uint16(iframe->sbuf.pos);

        DEBUGF("recv: origin_len=%zu\n", origin_len);

        if (origin_len > iframe->payloadleft) {
          busy = 1;
          iframe->state = NGHTTP2_IB_FRAME_SIZE_ERROR;
          break;
        }

        if (iframe->frame.hd.length > 2) {
          iframe->raw_lbuf =
              nghttp2_mem_malloc(mem, iframe->frame.hd.length - 2);

          if (iframe->raw_lbuf == NULL) {
            return NGHTTP2_ERR_NOMEM;
          }

          nghttp2_buf_wrap_init(&iframe->lbuf, iframe->raw_lbuf,
                                iframe->frame.hd.length);
        }

        busy = 1;

        iframe->state = NGHTTP2_IB_READ_ALTSVC_PAYLOAD;

        break;
      case NGHTTP2_PRIORITY_UPDATE:
        DEBUGF("recv: prioritized_stream_id=%d\n",
               nghttp2_get_uint32(iframe->sbuf.pos) & NGHTTP2_STREAM_ID_MASK);

        rv = session_process_priority_update_frame(session);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        session_inbound_frame_reset(session);

        break;
      }
      default:
        /* This is unknown frame */
        session_inbound_frame_reset(session);

        break;
      }
      break;
    case NGHTTP2_IB_READ_HEADER_BLOCK:
    case NGHTTP2_IB_IGN_HEADER_BLOCK: {
      nghttp2_ssize data_readlen;
      size_t trail_padlen;
      int final;
#ifdef DEBUGBUILD
      if (iframe->state == NGHTTP2_IB_READ_HEADER_BLOCK) {
        DEBUGF("recv: [IB_READ_HEADER_BLOCK]\n");
      } else {
        DEBUGF("recv: [IB_IGN_HEADER_BLOCK]\n");
      }
#endif /* DEBUGBUILD */

      readlen = inbound_frame_payload_readlen(iframe, in, last);

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft - readlen);

      data_readlen = inbound_frame_effective_readlen(
          iframe, iframe->payloadleft - readlen, readlen);

      if (data_readlen == -1) {
        /* everything is padding */
        data_readlen = 0;
      }

      trail_padlen = nghttp2_frame_trail_padlen(&iframe->frame, iframe->padlen);

      final = (iframe->frame.hd.flags & NGHTTP2_FLAG_END_HEADERS) &&
              iframe->payloadleft - (size_t)data_readlen == trail_padlen;

      if (data_readlen > 0 || (data_readlen == 0 && final)) {
        size_t hd_proclen = 0;

        DEBUGF("recv: block final=%d\n", final);

        rv =
            inflate_header_block(session, &iframe->frame, &hd_proclen,
                                 (uint8_t *)in, (size_t)data_readlen, final,
                                 iframe->state == NGHTTP2_IB_READ_HEADER_BLOCK);

        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        if (rv == NGHTTP2_ERR_PAUSE) {
          in += hd_proclen;
          iframe->payloadleft -= hd_proclen;

          return (nghttp2_ssize)(in - first);
        }

        if (rv == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE) {
          /* The application says no more headers. We decompress the
             rest of the header block but not invoke on_header_callback
             and on_frame_recv_callback. */
          in += hd_proclen;
          iframe->payloadleft -= hd_proclen;

          /* Use promised stream ID for PUSH_PROMISE */
          rv = nghttp2_session_add_rst_stream(
              session,
              iframe->frame.hd.type == NGHTTP2_PUSH_PROMISE
                  ? iframe->frame.push_promise.promised_stream_id
                  : iframe->frame.hd.stream_id,
              NGHTTP2_INTERNAL_ERROR);
          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
          busy = 1;
          iframe->state = NGHTTP2_IB_IGN_HEADER_BLOCK;
          break;
        }

        in += readlen;
        iframe->payloadleft -= readlen;

        if (rv == NGHTTP2_ERR_HEADER_COMP) {
          /* GOAWAY is already issued */
          if (iframe->payloadleft == 0) {
            session_inbound_frame_reset(session);
          } else {
            busy = 1;
            iframe->state = NGHTTP2_IB_IGN_PAYLOAD;
          }
          break;
        }
      } else {
        in += readlen;
        iframe->payloadleft -= readlen;
      }

      if (iframe->payloadleft) {
        break;
      }

      if ((iframe->frame.hd.flags & NGHTTP2_FLAG_END_HEADERS) == 0) {

        inbound_frame_set_mark(iframe, NGHTTP2_FRAME_HDLEN);

        iframe->padlen = 0;

        if (iframe->state == NGHTTP2_IB_READ_HEADER_BLOCK) {
          iframe->state = NGHTTP2_IB_EXPECT_CONTINUATION;
        } else {
          iframe->state = NGHTTP2_IB_IGN_CONTINUATION;
        }
      } else {
        if (iframe->state == NGHTTP2_IB_READ_HEADER_BLOCK) {
          rv = session_after_header_block_received(session);
          if (nghttp2_is_fatal(rv)) {
            return rv;
          }
        }
        session_inbound_frame_reset(session);

        session->num_continuations = 0;
      }
      break;
    }
    case NGHTTP2_IB_IGN_PAYLOAD:
      DEBUGF("recv: [IB_IGN_PAYLOAD]\n");

      readlen = inbound_frame_payload_readlen(iframe, in, last);
      iframe->payloadleft -= readlen;
      in += readlen;

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft);

      if (iframe->payloadleft) {
        break;
      }

      switch (iframe->frame.hd.type) {
      case NGHTTP2_HEADERS:
      case NGHTTP2_PUSH_PROMISE:
      case NGHTTP2_CONTINUATION:
        /* Mark inflater bad so that we won't perform further decoding */
        session->hd_inflater.ctx.bad = 1;
        break;
      default:
        break;
      }

      session_inbound_frame_reset(session);

      break;
    case NGHTTP2_IB_FRAME_SIZE_ERROR:
      DEBUGF("recv: [IB_FRAME_SIZE_ERROR]\n");

      rv = session_handle_frame_size_error(session);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      assert(iframe->state == NGHTTP2_IB_IGN_ALL);

      return (nghttp2_ssize)inlen;
    case NGHTTP2_IB_READ_SETTINGS:
      DEBUGF("recv: [IB_READ_SETTINGS]\n");

      readlen = inbound_frame_buf_read(iframe, in, last);
      iframe->payloadleft -= readlen;
      in += readlen;

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft);

      if (nghttp2_buf_mark_avail(&iframe->sbuf)) {
        break;
      }

      if (readlen > 0) {
        inbound_frame_set_settings_entry(iframe);
      }
      if (iframe->payloadleft) {
        inbound_frame_set_mark(iframe, NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH);
        break;
      }

      rv = session_process_settings_frame(session);

      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      if (iframe->state == NGHTTP2_IB_IGN_ALL) {
        return (nghttp2_ssize)inlen;
      }

      session_inbound_frame_reset(session);

      break;
    case NGHTTP2_IB_READ_GOAWAY_DEBUG:
      DEBUGF("recv: [IB_READ_GOAWAY_DEBUG]\n");

      readlen = inbound_frame_payload_readlen(iframe, in, last);

      if (readlen > 0) {
        iframe->lbuf.last = nghttp2_cpymem(iframe->lbuf.last, in, readlen);

        iframe->payloadleft -= readlen;
        in += readlen;
      }

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft);

      if (iframe->payloadleft) {
        assert(nghttp2_buf_avail(&iframe->lbuf) > 0);

        break;
      }

      rv = session_process_goaway_frame(session);

      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      if (iframe->state == NGHTTP2_IB_IGN_ALL) {
        return (nghttp2_ssize)inlen;
      }

      session_inbound_frame_reset(session);

      break;
    case NGHTTP2_IB_EXPECT_CONTINUATION:
    case NGHTTP2_IB_IGN_CONTINUATION:
#ifdef DEBUGBUILD
      if (iframe->state == NGHTTP2_IB_EXPECT_CONTINUATION) {
        fprintf(stderr, "recv: [IB_EXPECT_CONTINUATION]\n");
      } else {
        fprintf(stderr, "recv: [IB_IGN_CONTINUATION]\n");
      }
#endif /* DEBUGBUILD */

      if (++session->num_continuations > session->max_continuations) {
        return NGHTTP2_ERR_TOO_MANY_CONTINUATIONS;
      }

      readlen = inbound_frame_buf_read(iframe, in, last);
      in += readlen;

      if (nghttp2_buf_mark_avail(&iframe->sbuf)) {
        return (nghttp2_ssize)(in - first);
      }

      nghttp2_frame_unpack_frame_hd(&cont_hd, iframe->sbuf.pos);
      iframe->payloadleft = cont_hd.length;

      DEBUGF("recv: payloadlen=%zu, type=%u, flags=0x%02x, stream_id=%d\n",
             cont_hd.length, cont_hd.type, cont_hd.flags, cont_hd.stream_id);

      if (cont_hd.type != NGHTTP2_CONTINUATION ||
          cont_hd.stream_id != iframe->frame.hd.stream_id) {
        DEBUGF("recv: expected stream_id=%d, type=%d, but got stream_id=%d, "
               "type=%u\n",
               iframe->frame.hd.stream_id, NGHTTP2_CONTINUATION,
               cont_hd.stream_id, cont_hd.type);
        rv = nghttp2_session_terminate_session_with_reason(
            session, NGHTTP2_PROTOCOL_ERROR,
            "unexpected non-CONTINUATION frame or stream_id is invalid");
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        return (nghttp2_ssize)inlen;
      }

      /* CONTINUATION won't bear NGHTTP2_PADDED flag */

      iframe->frame.hd.flags =
          (uint8_t)(iframe->frame.hd.flags |
                    (cont_hd.flags & NGHTTP2_FLAG_END_HEADERS));
      iframe->frame.hd.length += cont_hd.length;

      busy = 1;

      if (iframe->state == NGHTTP2_IB_EXPECT_CONTINUATION) {
        iframe->state = NGHTTP2_IB_READ_HEADER_BLOCK;

        rv = session_call_on_begin_frame(session, &cont_hd);

        if (nghttp2_is_fatal(rv)) {
          return rv;
        }
      } else {
        iframe->state = NGHTTP2_IB_IGN_HEADER_BLOCK;
      }

      break;
    case NGHTTP2_IB_READ_PAD_DATA:
      DEBUGF("recv: [IB_READ_PAD_DATA]\n");

      readlen = inbound_frame_buf_read(iframe, in, last);
      in += readlen;
      iframe->payloadleft -= readlen;

      DEBUGF("recv: readlen=%zu, payloadleft=%zu, left=%zu\n", readlen,
             iframe->payloadleft, nghttp2_buf_mark_avail(&iframe->sbuf));

      if (nghttp2_buf_mark_avail(&iframe->sbuf)) {
        return (nghttp2_ssize)(in - first);
      }

      /* Pad Length field is subject to flow control */
      rv = nghttp2_session_update_recv_connection_window_size(session, readlen);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      if (iframe->state == NGHTTP2_IB_IGN_ALL) {
        return (nghttp2_ssize)inlen;
      }

      /* Pad Length field is consumed immediately */
      rv =
          nghttp2_session_consume(session, iframe->frame.hd.stream_id, readlen);

      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      if (iframe->state == NGHTTP2_IB_IGN_ALL) {
        return (nghttp2_ssize)inlen;
      }

      stream = nghttp2_session_get_stream(session, iframe->frame.hd.stream_id);
      if (stream) {
        rv = nghttp2_session_update_recv_stream_window_size(
            session, stream, readlen,
            iframe->payloadleft ||
                (iframe->frame.hd.flags & NGHTTP2_FLAG_END_STREAM) == 0);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }
      }

      busy = 1;

      padlen = inbound_frame_compute_pad(iframe);
      if (padlen < 0) {
        rv = nghttp2_session_terminate_session_with_reason(
            session, NGHTTP2_PROTOCOL_ERROR, "DATA: invalid padding");
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }
        return (nghttp2_ssize)inlen;
      }

      iframe->frame.data.padlen = (size_t)padlen;

      iframe->state = NGHTTP2_IB_READ_DATA;

      break;
    case NGHTTP2_IB_READ_DATA:
      stream = nghttp2_session_get_stream(session, iframe->frame.hd.stream_id);

      if (!stream) {
        busy = 1;
        iframe->state = NGHTTP2_IB_IGN_DATA;
        break;
      }

      DEBUGF("recv: [IB_READ_DATA]\n");

      readlen = inbound_frame_payload_readlen(iframe, in, last);
      iframe->payloadleft -= readlen;
      in += readlen;

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft);

      if (readlen > 0) {
        nghttp2_ssize data_readlen;

        rv = nghttp2_session_update_recv_connection_window_size(session,
                                                                readlen);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        rv = nghttp2_session_update_recv_stream_window_size(
            session, stream, readlen,
            iframe->payloadleft ||
                (iframe->frame.hd.flags & NGHTTP2_FLAG_END_STREAM) == 0);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        data_readlen = inbound_frame_effective_readlen(
            iframe, iframe->payloadleft, readlen);

        if (data_readlen == -1) {
          /* everything is padding */
          data_readlen = 0;
        }

        padlen = (nghttp2_ssize)readlen - data_readlen;

        if (padlen > 0) {
          /* Padding is considered as "consumed" immediately */
          rv = nghttp2_session_consume(session, iframe->frame.hd.stream_id,
                                       (size_t)padlen);

          if (nghttp2_is_fatal(rv)) {
            return rv;
          }

          if (iframe->state == NGHTTP2_IB_IGN_ALL) {
            return (nghttp2_ssize)inlen;
          }
        }

        DEBUGF("recv: data_readlen=%td\n", data_readlen);

        if (data_readlen > 0) {
          if (session_enforce_http_messaging(session)) {
            if (nghttp2_http_on_data_chunk(stream, (size_t)data_readlen) != 0) {
              if (session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE) {
                /* Consume all data for connection immediately here */
                rv = session_update_connection_consumed_size(
                    session, (size_t)data_readlen);

                if (nghttp2_is_fatal(rv)) {
                  return rv;
                }

                if (iframe->state == NGHTTP2_IB_IGN_DATA) {
                  return (nghttp2_ssize)inlen;
                }
              }

              rv = nghttp2_session_add_rst_stream(
                  session, iframe->frame.hd.stream_id, NGHTTP2_PROTOCOL_ERROR);
              if (nghttp2_is_fatal(rv)) {
                return rv;
              }
              busy = 1;
              iframe->state = NGHTTP2_IB_IGN_DATA;
              break;
            }
          }
          if (session->callbacks.on_data_chunk_recv_callback) {
            rv = session->callbacks.on_data_chunk_recv_callback(
                session, iframe->frame.hd.flags, iframe->frame.hd.stream_id,
                in - readlen, (size_t)data_readlen, session->user_data);
            if (rv == NGHTTP2_ERR_PAUSE) {
              return (nghttp2_ssize)(in - first);
            }

            if (nghttp2_is_fatal(rv)) {
              return NGHTTP2_ERR_CALLBACK_FAILURE;
            }
          }
        }
      }

      if (iframe->payloadleft) {
        break;
      }

      rv = session_process_data_frame(session);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      session_inbound_frame_reset(session);

      break;
    case NGHTTP2_IB_IGN_DATA:
      DEBUGF("recv: [IB_IGN_DATA]\n");

      readlen = inbound_frame_payload_readlen(iframe, in, last);
      iframe->payloadleft -= readlen;
      in += readlen;

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft);

      if (readlen > 0) {
        /* Update connection-level flow control window for ignored
           DATA frame too */
        rv = nghttp2_session_update_recv_connection_window_size(session,
                                                                readlen);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        if (iframe->state == NGHTTP2_IB_IGN_ALL) {
          return (nghttp2_ssize)inlen;
        }

        if (session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE) {

          /* Ignored DATA is considered as "consumed" immediately. */
          rv = session_update_connection_consumed_size(session, readlen);

          if (nghttp2_is_fatal(rv)) {
            return rv;
          }

          if (iframe->state == NGHTTP2_IB_IGN_ALL) {
            return (nghttp2_ssize)inlen;
          }
        }
      }

      if (iframe->payloadleft) {
        break;
      }

      session_inbound_frame_reset(session);

      break;
    case NGHTTP2_IB_IGN_ALL:
      return (nghttp2_ssize)inlen;
    case NGHTTP2_IB_READ_EXTENSION_PAYLOAD:
      DEBUGF("recv: [IB_READ_EXTENSION_PAYLOAD]\n");

      readlen = inbound_frame_payload_readlen(iframe, in, last);
      iframe->payloadleft -= readlen;
      in += readlen;

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft);

      if (readlen > 0) {
        rv = session_call_on_extension_chunk_recv_callback(
            session, in - readlen, readlen);
        if (nghttp2_is_fatal(rv)) {
          return rv;
        }

        if (rv != 0) {
          busy = 1;

          iframe->state = NGHTTP2_IB_IGN_PAYLOAD;

          break;
        }
      }

      if (iframe->payloadleft > 0) {
        break;
      }

      rv = session_process_extension_frame(session);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      session_inbound_frame_reset(session);

      break;
    case NGHTTP2_IB_READ_ALTSVC_PAYLOAD:
      DEBUGF("recv: [IB_READ_ALTSVC_PAYLOAD]\n");

      readlen = inbound_frame_payload_readlen(iframe, in, last);
      if (readlen > 0) {
        iframe->lbuf.last = nghttp2_cpymem(iframe->lbuf.last, in, readlen);

        iframe->payloadleft -= readlen;
        in += readlen;
      }

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft);

      if (iframe->payloadleft) {
        assert(nghttp2_buf_avail(&iframe->lbuf) > 0);

        break;
      }

      rv = session_process_altsvc_frame(session);
      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      session_inbound_frame_reset(session);

      break;
    case NGHTTP2_IB_READ_ORIGIN_PAYLOAD:
      DEBUGF("recv: [IB_READ_ORIGIN_PAYLOAD]\n");

      readlen = inbound_frame_payload_readlen(iframe, in, last);

      if (readlen > 0) {
        iframe->lbuf.last = nghttp2_cpymem(iframe->lbuf.last, in, readlen);

        iframe->payloadleft -= readlen;
        in += readlen;
      }

      DEBUGF("recv: readlen=%zu, payloadleft=%zu\n", readlen,
             iframe->payloadleft);

      if (iframe->payloadleft) {
        assert(nghttp2_buf_avail(&iframe->lbuf) > 0);

        break;
      }

      rv = session_process_origin_frame(session);

      if (nghttp2_is_fatal(rv)) {
        return rv;
      }

      if (iframe->state == NGHTTP2_IB_IGN_ALL) {
        return (nghttp2_ssize)inlen;
      }

      session_inbound_frame_reset(session);

      break;
    }

    if (!busy && in == last) {
      break;
    }

    busy = 0;
  }

  assert(in == last);

  return (nghttp2_ssize)(in - first);
}

int nghttp2_session_recv(nghttp2_session *session) {
  uint8_t buf[NGHTTP2_INBOUND_BUFFER_LENGTH];
  while (1) {
    nghttp2_ssize readlen;
    readlen = session_recv(session, buf, sizeof(buf));
    if (readlen > 0) {
      nghttp2_ssize proclen =
          nghttp2_session_mem_recv2(session, buf, (size_t)readlen);
      if (proclen < 0) {
        return (int)proclen;
      }
      assert(proclen == readlen);
    } else if (readlen == 0 || readlen == NGHTTP2_ERR_WOULDBLOCK) {
      return 0;
    } else if (readlen == NGHTTP2_ERR_EOF) {
      return NGHTTP2_ERR_EOF;
    } else if (readlen < 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
}

/*
 * Returns the number of active streams, which includes streams in
 * reserved state.
 */
static size_t session_get_num_active_streams(nghttp2_session *session) {
  return nghttp2_map_size(&session->streams) - session->num_closed_streams -
         session->num_idle_streams;
}

int nghttp2_session_want_read(nghttp2_session *session) {
  size_t num_active_streams;

  /* If this flag is set, we don't want to read. The application
     should drop the connection. */
  if (session->goaway_flags & NGHTTP2_GOAWAY_TERM_SENT) {
    return 0;
  }

  num_active_streams = session_get_num_active_streams(session);

  /* Unless termination GOAWAY is sent or received, we always want to
     read incoming frames. */

  if (num_active_streams > 0) {
    return 1;
  }

  /* If there is no active streams and GOAWAY has been sent or
     received, we are done with this session. */
  return (session->goaway_flags &
          (NGHTTP2_GOAWAY_SENT | NGHTTP2_GOAWAY_RECV)) == 0;
}

int nghttp2_session_want_write(nghttp2_session *session) {
  /* If these flag is set, we don't want to write any data. The
     application should drop the connection. */
  if (session->goaway_flags & NGHTTP2_GOAWAY_TERM_SENT) {
    return 0;
  }

  /*
   * Unless termination GOAWAY is sent or received, we want to write
   * frames if there is pending ones. If pending frame is request/push
   * response HEADERS and concurrent stream limit is reached, we don't
   * want to write them.
   */
  return session->aob.item || nghttp2_outbound_queue_top(&session->ob_urgent) ||
         nghttp2_outbound_queue_top(&session->ob_reg) ||
         ((!nghttp2_pq_empty(&session->root.obq) ||
           !session_sched_empty(session)) &&
          session->remote_window_size > 0) ||
         (nghttp2_outbound_queue_top(&session->ob_syn) &&
          !session_is_outgoing_concurrent_streams_max(session));
}

int nghttp2_session_add_ping(nghttp2_session *session, uint8_t flags,
                             const uint8_t *opaque_data) {
  int rv;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_mem *mem;

  mem = &session->mem;

  if ((flags & NGHTTP2_FLAG_ACK) &&
      session->obq_flood_counter_ >= session->max_outbound_ack) {
    return NGHTTP2_ERR_FLOODED;
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_outbound_item_init(item);

  frame = &item->frame;

  nghttp2_frame_ping_init(&frame->ping, flags, opaque_data);

  rv = nghttp2_session_add_item(session, item);

  if (rv != 0) {
    nghttp2_frame_ping_free(&frame->ping);
    nghttp2_mem_free(mem, item);
    return rv;
  }

  if (flags & NGHTTP2_FLAG_ACK) {
    ++session->obq_flood_counter_;
  }

  return 0;
}

int nghttp2_session_add_goaway(nghttp2_session *session, int32_t last_stream_id,
                               uint32_t error_code, const uint8_t *opaque_data,
                               size_t opaque_data_len, uint8_t aux_flags) {
  int rv;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  uint8_t *opaque_data_copy = NULL;
  nghttp2_goaway_aux_data *aux_data;
  nghttp2_mem *mem;

  mem = &session->mem;

  if (nghttp2_session_is_my_stream_id(session, last_stream_id)) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (opaque_data_len) {
    if (opaque_data_len + 8 > NGHTTP2_MAX_PAYLOADLEN) {
      return NGHTTP2_ERR_INVALID_ARGUMENT;
    }
    opaque_data_copy = nghttp2_mem_malloc(mem, opaque_data_len);
    if (opaque_data_copy == NULL) {
      return NGHTTP2_ERR_NOMEM;
    }
    memcpy(opaque_data_copy, opaque_data, opaque_data_len);
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    nghttp2_mem_free(mem, opaque_data_copy);
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_outbound_item_init(item);

  frame = &item->frame;

  /* last_stream_id must not be increased from the value previously
     sent */
  last_stream_id =
      nghttp2_min_int32(last_stream_id, session->local_last_stream_id);

  nghttp2_frame_goaway_init(&frame->goaway, last_stream_id, error_code,
                            opaque_data_copy, opaque_data_len);

  aux_data = &item->aux_data.goaway;
  aux_data->flags = aux_flags;

  rv = nghttp2_session_add_item(session, item);
  if (rv != 0) {
    nghttp2_frame_goaway_free(&frame->goaway, mem);
    nghttp2_mem_free(mem, item);
    return rv;
  }

  session->goaway_flags |= NGHTTP2_GOAWAY_SUBMITTED;

  return 0;
}

int nghttp2_session_add_window_update(nghttp2_session *session, uint8_t flags,
                                      int32_t stream_id,
                                      int32_t window_size_increment) {
  int rv;
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_mem *mem;

  mem = &session->mem;
  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_outbound_item_init(item);

  frame = &item->frame;

  nghttp2_frame_window_update_init(&frame->window_update, flags, stream_id,
                                   window_size_increment);

  rv = nghttp2_session_add_item(session, item);

  if (rv != 0) {
    nghttp2_frame_window_update_free(&frame->window_update);
    nghttp2_mem_free(mem, item);
    return rv;
  }
  return 0;
}

static void
session_append_inflight_settings(nghttp2_session *session,
                                 nghttp2_inflight_settings *settings) {
  nghttp2_inflight_settings **i;

  for (i = &session->inflight_settings_head; *i; i = &(*i)->next)
    ;

  *i = settings;
}

int nghttp2_session_add_settings(nghttp2_session *session, uint8_t flags,
                                 const nghttp2_settings_entry *iv, size_t niv) {
  nghttp2_outbound_item *item;
  nghttp2_frame *frame;
  nghttp2_settings_entry *iv_copy;
  size_t i;
  int rv;
  nghttp2_mem *mem;
  nghttp2_inflight_settings *inflight_settings = NULL;
  uint8_t no_rfc7540_pri = session->pending_no_rfc7540_priorities;

  mem = &session->mem;

  if (flags & NGHTTP2_FLAG_ACK) {
    if (niv != 0) {
      return NGHTTP2_ERR_INVALID_ARGUMENT;
    }

    if (session->obq_flood_counter_ >= session->max_outbound_ack) {
      return NGHTTP2_ERR_FLOODED;
    }
  }

  if (!nghttp2_iv_check(iv, niv)) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  for (i = 0; i < niv; ++i) {
    if (iv[i].settings_id != NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES) {
      continue;
    }

    if (no_rfc7540_pri == UINT8_MAX) {
      no_rfc7540_pri = (uint8_t)iv[i].value;
      continue;
    }

    if (iv[i].value != (uint32_t)no_rfc7540_pri) {
      return NGHTTP2_ERR_INVALID_ARGUMENT;
    }
  }

  item = nghttp2_mem_malloc(mem, sizeof(nghttp2_outbound_item));
  if (item == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  if (niv > 0) {
    iv_copy = nghttp2_frame_iv_copy(iv, niv, mem);
    if (iv_copy == NULL) {
      nghttp2_mem_free(mem, item);
      return NGHTTP2_ERR_NOMEM;
    }
  } else {
    iv_copy = NULL;
  }

  if ((flags & NGHTTP2_FLAG_ACK) == 0) {
    rv = inflight_settings_new(&inflight_settings, iv, niv, mem);
    if (rv != 0) {
      assert(nghttp2_is_fatal(rv));
      nghttp2_mem_free(mem, iv_copy);
      nghttp2_mem_free(mem, item);
      return rv;
    }
  }

  nghttp2_outbound_item_init(item);

  frame = &item->frame;

  nghttp2_frame_settings_init(&frame->settings, flags, iv_copy, niv);
  rv = nghttp2_session_add_item(session, item);
  if (rv != 0) {
    /* The only expected error is fatal one */
    assert(nghttp2_is_fatal(rv));

    inflight_settings_del(inflight_settings, mem);

    nghttp2_frame_settings_free(&frame->settings, mem);
    nghttp2_mem_free(mem, item);

    return rv;
  }

  if (flags & NGHTTP2_FLAG_ACK) {
    ++session->obq_flood_counter_;
  } else {
    session_append_inflight_settings(session, inflight_settings);
  }

  /* Extract NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS and ENABLE_PUSH
     here.  We use it to refuse the incoming stream and PUSH_PROMISE
     with RST_STREAM. */

  for (i = niv; i > 0; --i) {
    if (iv[i - 1].settings_id == NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS) {
      session->pending_local_max_concurrent_stream = iv[i - 1].value;
      break;
    }
  }

  for (i = niv; i > 0; --i) {
    if (iv[i - 1].settings_id == NGHTTP2_SETTINGS_ENABLE_PUSH) {
      session->pending_enable_push = (uint8_t)iv[i - 1].value;
      break;
    }
  }

  for (i = niv; i > 0; --i) {
    if (iv[i - 1].settings_id == NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL) {
      session->pending_enable_connect_protocol = (uint8_t)iv[i - 1].value;
      break;
    }
  }

  if (no_rfc7540_pri == UINT8_MAX) {
    session->pending_no_rfc7540_priorities = 0;
  } else {
    session->pending_no_rfc7540_priorities = no_rfc7540_pri;
  }

  return 0;
}

int nghttp2_session_pack_data(nghttp2_session *session, nghttp2_bufs *bufs,
                              size_t datamax, nghttp2_frame *frame,
                              nghttp2_data_aux_data *aux_data,
                              nghttp2_stream *stream) {
  int rv;
  uint32_t data_flags;
  nghttp2_ssize payloadlen;
  nghttp2_ssize padded_payloadlen;
  nghttp2_buf *buf;
  size_t max_payloadlen;

  assert(bufs->head == bufs->cur);

  buf = &bufs->cur->buf;

  if (session->callbacks.read_length_callback2 ||
      session->callbacks.read_length_callback) {
    if (session->callbacks.read_length_callback2) {
      payloadlen = session->callbacks.read_length_callback2(
          session, frame->hd.type, stream->stream_id,
          session->remote_window_size, stream->remote_window_size,
          session->remote_settings.max_frame_size, session->user_data);
    } else {
      payloadlen = (nghttp2_ssize)session->callbacks.read_length_callback(
          session, frame->hd.type, stream->stream_id,
          session->remote_window_size, stream->remote_window_size,
          session->remote_settings.max_frame_size, session->user_data);
    }

    DEBUGF("send: read_length_callback=%td\n", payloadlen);

    payloadlen = nghttp2_session_enforce_flow_control_limits(session, stream,
                                                             payloadlen);

    DEBUGF("send: read_length_callback after flow control=%td\n", payloadlen);

    if (payloadlen <= 0) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    if ((size_t)payloadlen > nghttp2_buf_avail(buf)) {
      /* Resize the current buffer(s).  The reason why we do +1 for
         buffer size is for possible padding field. */
      rv = nghttp2_bufs_realloc(&session->aob.framebufs,
                                (size_t)(NGHTTP2_FRAME_HDLEN + 1 + payloadlen));

      if (rv != 0) {
        DEBUGF("send: realloc buffer failed rv=%d", rv);
        /* If reallocation failed, old buffers are still in tact.  So
           use safe limit. */
        payloadlen = (nghttp2_ssize)datamax;

        DEBUGF("send: use safe limit payloadlen=%td", payloadlen);
      } else {
        assert(&session->aob.framebufs == bufs);

        buf = &bufs->cur->buf;
      }
    }
    datamax = (size_t)payloadlen;
  }

  /* Current max DATA length is less then buffer chunk size */
  assert(nghttp2_buf_avail(buf) >= datamax);

  data_flags = NGHTTP2_DATA_FLAG_NONE;
  switch (aux_data->dpw.version) {
  case NGHTTP2_DATA_PROVIDER_V1:
    payloadlen = (nghttp2_ssize)aux_data->dpw.data_prd.v1.read_callback(
        session, frame->hd.stream_id, buf->pos, datamax, &data_flags,
        &aux_data->dpw.data_prd.source, session->user_data);

    break;
  case NGHTTP2_DATA_PROVIDER_V2:
    payloadlen = aux_data->dpw.data_prd.v2.read_callback(
        session, frame->hd.stream_id, buf->pos, datamax, &data_flags,
        &aux_data->dpw.data_prd.source, session->user_data);

    break;
  default:
    assert(0);
    abort();
  }

  if (payloadlen == NGHTTP2_ERR_DEFERRED ||
      payloadlen == NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE ||
      payloadlen == NGHTTP2_ERR_PAUSE) {
    DEBUGF("send: DATA postponed due to %s\n",
           nghttp2_strerror((int)payloadlen));

    return (int)payloadlen;
  }

  if (payloadlen < 0 || datamax < (size_t)payloadlen) {
    /* This is the error code when callback is failed. */
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }

  buf->last = buf->pos + payloadlen;
  buf->pos -= NGHTTP2_FRAME_HDLEN;

  /* Clear flags, because this may contain previous flags of previous
     DATA */
  frame->hd.flags = NGHTTP2_FLAG_NONE;

  if (data_flags & NGHTTP2_DATA_FLAG_EOF) {
    aux_data->eof = 1;
    /* If NGHTTP2_DATA_FLAG_NO_END_STREAM is set, don't set
       NGHTTP2_FLAG_END_STREAM */
    if ((aux_data->flags & NGHTTP2_FLAG_END_STREAM) &&
        (data_flags & NGHTTP2_DATA_FLAG_NO_END_STREAM) == 0) {
      frame->hd.flags |= NGHTTP2_FLAG_END_STREAM;
    }
  }

  if (data_flags & NGHTTP2_DATA_FLAG_NO_COPY) {
    if (session->callbacks.send_data_callback == NULL) {
      DEBUGF("NGHTTP2_DATA_FLAG_NO_COPY requires send_data_callback set\n");

      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    aux_data->no_copy = 1;
  }

  frame->hd.length = (size_t)payloadlen;
  frame->data.padlen = 0;

  max_payloadlen =
      nghttp2_min_size(datamax, frame->hd.length + NGHTTP2_MAX_PADLEN);

  padded_payloadlen =
      session_call_select_padding(session, frame, max_payloadlen);

  if (nghttp2_is_fatal((int)padded_payloadlen)) {
    return (int)padded_payloadlen;
  }

  frame->data.padlen = (size_t)(padded_payloadlen - payloadlen);

  nghttp2_frame_pack_frame_hd(buf->pos, &frame->hd);

  nghttp2_frame_add_pad(bufs, &frame->hd, frame->data.padlen,
                        aux_data->no_copy);

  session_reschedule_stream(session, stream);

  if (frame->hd.length == 0 && (data_flags & NGHTTP2_DATA_FLAG_EOF) &&
      (data_flags & NGHTTP2_DATA_FLAG_NO_END_STREAM)) {
    /* DATA payload length is 0, and DATA frame does not bear
       END_STREAM.  In this case, there is no point to send 0 length
       DATA frame. */
    return NGHTTP2_ERR_CANCEL;
  }

  return 0;
}

void *nghttp2_session_get_stream_user_data(nghttp2_session *session,
                                           int32_t stream_id) {
  nghttp2_stream *stream;
  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream) {
    return stream->stream_user_data;
  } else {
    return NULL;
  }
}

int nghttp2_session_set_stream_user_data(nghttp2_session *session,
                                         int32_t stream_id,
                                         void *stream_user_data) {
  nghttp2_stream *stream;
  nghttp2_frame *frame;
  nghttp2_outbound_item *item;

  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream) {
    stream->stream_user_data = stream_user_data;
    return 0;
  }

  if (session->server || !nghttp2_session_is_my_stream_id(session, stream_id) ||
      !nghttp2_outbound_queue_top(&session->ob_syn)) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  frame = &nghttp2_outbound_queue_top(&session->ob_syn)->frame;
  assert(frame->hd.type == NGHTTP2_HEADERS);

  if (frame->hd.stream_id > stream_id ||
      (uint32_t)stream_id >= session->next_stream_id) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  for (item = session->ob_syn.head; item; item = item->qnext) {
    if (item->frame.hd.stream_id < stream_id) {
      continue;
    }

    if (item->frame.hd.stream_id > stream_id) {
      break;
    }

    item->aux_data.headers.stream_user_data = stream_user_data;
    return 0;
  }

  return NGHTTP2_ERR_INVALID_ARGUMENT;
}

int nghttp2_session_resume_data(nghttp2_session *session, int32_t stream_id) {
  int rv;
  nghttp2_stream *stream;
  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream == NULL || !nghttp2_stream_check_deferred_item(stream)) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  rv = session_resume_deferred_stream_item(session, stream,
                                           NGHTTP2_STREAM_FLAG_DEFERRED_USER);

  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  return 0;
}

size_t nghttp2_session_get_outbound_queue_size(nghttp2_session *session) {
  return nghttp2_outbound_queue_size(&session->ob_urgent) +
         nghttp2_outbound_queue_size(&session->ob_reg) +
         nghttp2_outbound_queue_size(&session->ob_syn);
  /* TODO account for item attached to stream */
}

int32_t
nghttp2_session_get_stream_effective_recv_data_length(nghttp2_session *session,
                                                      int32_t stream_id) {
  nghttp2_stream *stream;
  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream == NULL) {
    return -1;
  }
  return stream->recv_window_size < 0 ? 0 : stream->recv_window_size;
}

int32_t
nghttp2_session_get_stream_effective_local_window_size(nghttp2_session *session,
                                                       int32_t stream_id) {
  nghttp2_stream *stream;
  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream == NULL) {
    return -1;
  }
  return stream->local_window_size;
}

int32_t nghttp2_session_get_stream_local_window_size(nghttp2_session *session,
                                                     int32_t stream_id) {
  nghttp2_stream *stream;
  int32_t size;
  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream == NULL) {
    return -1;
  }

  size = stream->local_window_size - stream->recv_window_size;

  /* size could be negative if local endpoint reduced
     SETTINGS_INITIAL_WINDOW_SIZE */
  if (size < 0) {
    return 0;
  }

  return size;
}

int32_t
nghttp2_session_get_effective_recv_data_length(nghttp2_session *session) {
  return session->recv_window_size < 0 ? 0 : session->recv_window_size;
}

int32_t
nghttp2_session_get_effective_local_window_size(nghttp2_session *session) {
  return session->local_window_size;
}

int32_t nghttp2_session_get_local_window_size(nghttp2_session *session) {
  return session->local_window_size - session->recv_window_size;
}

int32_t nghttp2_session_get_stream_remote_window_size(nghttp2_session *session,
                                                      int32_t stream_id) {
  nghttp2_stream *stream;

  stream = nghttp2_session_get_stream(session, stream_id);
  if (stream == NULL) {
    return -1;
  }

  /* stream->remote_window_size can be negative when
     SETTINGS_INITIAL_WINDOW_SIZE is changed. */
  return nghttp2_max_int32(0, stream->remote_window_size);
}

int32_t nghttp2_session_get_remote_window_size(nghttp2_session *session) {
  return session->remote_window_size;
}

uint32_t nghttp2_session_get_remote_settings(nghttp2_session *session,
                                             nghttp2_settings_id id) {
  switch (id) {
  case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
    return session->remote_settings.header_table_size;
  case NGHTTP2_SETTINGS_ENABLE_PUSH:
    return session->remote_settings.enable_push;
  case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
    return session->remote_settings.max_concurrent_streams;
  case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
    return session->remote_settings.initial_window_size;
  case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:
    return session->remote_settings.max_frame_size;
  case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
    return session->remote_settings.max_header_list_size;
  case NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL:
    return session->remote_settings.enable_connect_protocol;
  case NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES:
    return session->remote_settings.no_rfc7540_priorities;
  }

  assert(0);
  abort(); /* if NDEBUG is set */
}

uint32_t nghttp2_session_get_local_settings(nghttp2_session *session,
                                            nghttp2_settings_id id) {
  switch (id) {
  case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
    return session->local_settings.header_table_size;
  case NGHTTP2_SETTINGS_ENABLE_PUSH:
    return session->local_settings.enable_push;
  case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
    return session->local_settings.max_concurrent_streams;
  case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
    return session->local_settings.initial_window_size;
  case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:
    return session->local_settings.max_frame_size;
  case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
    return session->local_settings.max_header_list_size;
  case NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL:
    return session->local_settings.enable_connect_protocol;
  case NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES:
    return session->local_settings.no_rfc7540_priorities;
  }

  assert(0);
  abort(); /* if NDEBUG is set */
}

static int nghttp2_session_upgrade_internal(nghttp2_session *session,
                                            const uint8_t *settings_payload,
                                            size_t settings_payloadlen,
                                            void *stream_user_data) {
  nghttp2_stream *stream;
  nghttp2_frame frame;
  nghttp2_settings_entry *iv;
  size_t niv;
  int rv;
  nghttp2_priority_spec pri_spec;
  nghttp2_mem *mem;

  mem = &session->mem;

  if ((!session->server && session->next_stream_id != 1) ||
      (session->server && session->last_recv_stream_id >= 1)) {
    return NGHTTP2_ERR_PROTO;
  }
  if (settings_payloadlen % NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }
  /* SETTINGS frame contains too many settings */
  if (settings_payloadlen / NGHTTP2_FRAME_SETTINGS_ENTRY_LENGTH >
      session->max_settings) {
    return NGHTTP2_ERR_TOO_MANY_SETTINGS;
  }
  rv = nghttp2_frame_unpack_settings_payload2(&iv, &niv, settings_payload,
                                              settings_payloadlen, mem);
  if (rv != 0) {
    return rv;
  }

  if (session->server) {
    nghttp2_frame_hd_init(&frame.hd, settings_payloadlen, NGHTTP2_SETTINGS,
                          NGHTTP2_FLAG_NONE, 0);
    frame.settings.iv = iv;
    frame.settings.niv = niv;
    rv = nghttp2_session_on_settings_received(session, &frame, 1 /* No ACK */);
  } else {
    rv = nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, iv, niv);
  }
  nghttp2_mem_free(mem, iv);
  if (rv != 0) {
    return rv;
  }

  nghttp2_priority_spec_default_init(&pri_spec);

  stream = nghttp2_session_open_stream(
      session, 1, NGHTTP2_STREAM_FLAG_NONE, &pri_spec, NGHTTP2_STREAM_OPENING,
      session->server ? NULL : stream_user_data);
  if (stream == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  /* We don't call nghttp2_session_adjust_closed_stream(), since this
     should be the first stream open. */

  if (session->server) {
    nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_RD);
    session->last_recv_stream_id = 1;
    session->last_proc_stream_id = 1;
  } else {
    nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_WR);
    session->last_sent_stream_id = 1;
    session->next_stream_id += 2;
  }
  return 0;
}

int nghttp2_session_upgrade(nghttp2_session *session,
                            const uint8_t *settings_payload,
                            size_t settings_payloadlen,
                            void *stream_user_data) {
  int rv;
  nghttp2_stream *stream;

  rv = nghttp2_session_upgrade_internal(session, settings_payload,
                                        settings_payloadlen, stream_user_data);
  if (rv != 0) {
    return rv;
  }

  stream = nghttp2_session_get_stream(session, 1);
  assert(stream);

  /* We have no information about request header fields when Upgrade
     was happened.  So we don't know the request method here.  If
     request method is HEAD, we have a trouble because we may have
     nonzero content-length header field in response headers, and we
     will going to check it against the actual DATA frames, but we may
     get mismatch because HEAD response body must be empty.  Because
     of this reason, nghttp2_session_upgrade() was deprecated in favor
     of nghttp2_session_upgrade2(), which has |head_request| parameter
     to indicate that request method is HEAD or not. */
  stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_UPGRADE_WORKAROUND;
  return 0;
}

int nghttp2_session_upgrade2(nghttp2_session *session,
                             const uint8_t *settings_payload,
                             size_t settings_payloadlen, int head_request,
                             void *stream_user_data) {
  int rv;
  nghttp2_stream *stream;

  rv = nghttp2_session_upgrade_internal(session, settings_payload,
                                        settings_payloadlen, stream_user_data);
  if (rv != 0) {
    return rv;
  }

  stream = nghttp2_session_get_stream(session, 1);
  assert(stream);

  if (head_request) {
    stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_HEAD;
  }

  return 0;
}

int nghttp2_session_get_stream_local_close(nghttp2_session *session,
                                           int32_t stream_id) {
  nghttp2_stream *stream;

  stream = nghttp2_session_get_stream(session, stream_id);

  if (!stream) {
    return -1;
  }

  return (stream->shut_flags & NGHTTP2_SHUT_WR) != 0;
}

int nghttp2_session_get_stream_remote_close(nghttp2_session *session,
                                            int32_t stream_id) {
  nghttp2_stream *stream;

  stream = nghttp2_session_get_stream(session, stream_id);

  if (!stream) {
    return -1;
  }

  return (stream->shut_flags & NGHTTP2_SHUT_RD) != 0;
}

int nghttp2_session_consume(nghttp2_session *session, int32_t stream_id,
                            size_t size) {
  int rv;
  nghttp2_stream *stream;

  if (stream_id == 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (!(session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE)) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  rv = session_update_connection_consumed_size(session, size);

  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  stream = nghttp2_session_get_stream(session, stream_id);

  if (!stream) {
    return 0;
  }

  rv = session_update_stream_consumed_size(session, stream, size);

  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  return 0;
}

int nghttp2_session_consume_connection(nghttp2_session *session, size_t size) {
  int rv;

  if (!(session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE)) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  rv = session_update_connection_consumed_size(session, size);

  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  return 0;
}

int nghttp2_session_consume_stream(nghttp2_session *session, int32_t stream_id,
                                   size_t size) {
  int rv;
  nghttp2_stream *stream;

  if (stream_id == 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (!(session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE)) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  stream = nghttp2_session_get_stream(session, stream_id);

  if (!stream) {
    return 0;
  }

  rv = session_update_stream_consumed_size(session, stream, size);

  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  return 0;
}

int nghttp2_session_set_next_stream_id(nghttp2_session *session,
                                       int32_t next_stream_id) {
  if (next_stream_id <= 0 ||
      session->next_stream_id > (uint32_t)next_stream_id) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (session->server) {
    if (next_stream_id % 2) {
      return NGHTTP2_ERR_INVALID_ARGUMENT;
    }
  } else if (next_stream_id % 2 == 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  session->next_stream_id = (uint32_t)next_stream_id;
  return 0;
}

uint32_t nghttp2_session_get_next_stream_id(nghttp2_session *session) {
  return session->next_stream_id;
}

int32_t nghttp2_session_get_last_proc_stream_id(nghttp2_session *session) {
  return session->last_proc_stream_id;
}

nghttp2_stream *nghttp2_session_find_stream(nghttp2_session *session,
                                            int32_t stream_id) {
  if (stream_id == 0) {
    return &session->root;
  }

  return nghttp2_session_get_stream_raw(session, stream_id);
}

nghttp2_stream *nghttp2_session_get_root_stream(nghttp2_session *session) {
  return &session->root;
}

int nghttp2_session_check_server_session(nghttp2_session *session) {
  return session->server;
}

int nghttp2_session_change_stream_priority(
    nghttp2_session *session, int32_t stream_id,
    const nghttp2_priority_spec *pri_spec) {
  int rv;
  nghttp2_stream *stream;
  nghttp2_priority_spec pri_spec_copy;

  if (session->pending_no_rfc7540_priorities == 1) {
    return 0;
  }

  if (stream_id == 0 || stream_id == pri_spec->stream_id) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  stream = nghttp2_session_get_stream_raw(session, stream_id);
  if (!stream) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  pri_spec_copy = *pri_spec;
  nghttp2_priority_spec_normalize_weight(&pri_spec_copy);

  rv = nghttp2_session_reprioritize_stream(session, stream, &pri_spec_copy);

  if (nghttp2_is_fatal(rv)) {
    return rv;
  }

  /* We don't intentionally call nghttp2_session_adjust_idle_stream()
     so that idle stream created by this function, and existing ones
     are kept for application.  We will adjust number of idle stream
     in nghttp2_session_mem_send2 or nghttp2_session_mem_recv2 is
     called. */
  return 0;
}

int nghttp2_session_create_idle_stream(nghttp2_session *session,
                                       int32_t stream_id,
                                       const nghttp2_priority_spec *pri_spec) {
  nghttp2_stream *stream;
  nghttp2_priority_spec pri_spec_copy;

  if (session->pending_no_rfc7540_priorities == 1) {
    return 0;
  }

  if (stream_id == 0 || stream_id == pri_spec->stream_id ||
      !session_detect_idle_stream(session, stream_id)) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  stream = nghttp2_session_get_stream_raw(session, stream_id);
  if (stream) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  pri_spec_copy = *pri_spec;
  nghttp2_priority_spec_normalize_weight(&pri_spec_copy);

  stream =
      nghttp2_session_open_stream(session, stream_id, NGHTTP2_STREAM_FLAG_NONE,
                                  &pri_spec_copy, NGHTTP2_STREAM_IDLE, NULL);
  if (!stream) {
    return NGHTTP2_ERR_NOMEM;
  }

  /* We don't intentionally call nghttp2_session_adjust_idle_stream()
     so that idle stream created by this function, and existing ones
     are kept for application.  We will adjust number of idle stream
     in nghttp2_session_mem_send2 or nghttp2_session_mem_recv2 is
     called. */
  return 0;
}

size_t
nghttp2_session_get_hd_inflate_dynamic_table_size(nghttp2_session *session) {
  return nghttp2_hd_inflate_get_dynamic_table_size(&session->hd_inflater);
}

size_t
nghttp2_session_get_hd_deflate_dynamic_table_size(nghttp2_session *session) {
  return nghttp2_hd_deflate_get_dynamic_table_size(&session->hd_deflater);
}

void nghttp2_session_set_user_data(nghttp2_session *session, void *user_data) {
  session->user_data = user_data;
}

int nghttp2_session_change_extpri_stream_priority(
    nghttp2_session *session, int32_t stream_id,
    const nghttp2_extpri *extpri_in, int ignore_client_signal) {
  nghttp2_stream *stream;
  nghttp2_extpri extpri = *extpri_in;

  if (!session->server) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  if (session->pending_no_rfc7540_priorities != 1) {
    return 0;
  }

  if (stream_id == 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  stream = nghttp2_session_get_stream_raw(session, stream_id);
  if (!stream) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  if (extpri.urgency > NGHTTP2_EXTPRI_URGENCY_LOW) {
    extpri.urgency = NGHTTP2_EXTPRI_URGENCY_LOW;
  }

  if (ignore_client_signal) {
    stream->flags |= NGHTTP2_STREAM_FLAG_IGNORE_CLIENT_PRIORITIES;
  }

  return session_update_stream_priority(session, stream,
                                        nghttp2_extpri_to_uint8(&extpri));
}

int nghttp2_session_get_extpri_stream_priority(nghttp2_session *session,
                                               nghttp2_extpri *extpri,
                                               int32_t stream_id) {
  nghttp2_stream *stream;

  if (!session->server) {
    return NGHTTP2_ERR_INVALID_STATE;
  }

  if (session->pending_no_rfc7540_priorities != 1) {
    return 0;
  }

  if (stream_id == 0) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  stream = nghttp2_session_get_stream_raw(session, stream_id);
  if (!stream) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  nghttp2_extpri_from_uint8(extpri, stream->extpri);

  return 0;
}
