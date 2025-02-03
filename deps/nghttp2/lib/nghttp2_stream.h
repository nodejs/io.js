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
#ifndef NGHTTP2_STREAM_H
#define NGHTTP2_STREAM_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp2/nghttp2.h>
#include "nghttp2_outbound_item.h"
#include "nghttp2_map.h"
#include "nghttp2_pq.h"
#include "nghttp2_int.h"

/*
 * If local peer is stream initiator:
 * NGHTTP2_STREAM_OPENING : upon sending request HEADERS
 * NGHTTP2_STREAM_OPENED : upon receiving response HEADERS
 * NGHTTP2_STREAM_CLOSING : upon queuing RST_STREAM
 *
 * If remote peer is stream initiator:
 * NGHTTP2_STREAM_OPENING : upon receiving request HEADERS
 * NGHTTP2_STREAM_OPENED : upon sending response HEADERS
 * NGHTTP2_STREAM_CLOSING : upon queuing RST_STREAM
 */
typedef enum {
  /* Initial state */
  NGHTTP2_STREAM_INITIAL,
  /* For stream initiator: request HEADERS has been sent, but response
     HEADERS has not been received yet.  For receiver: request HEADERS
     has been received, but it does not send response HEADERS yet. */
  NGHTTP2_STREAM_OPENING,
  /* For stream initiator: response HEADERS is received. For receiver:
     response HEADERS is sent. */
  NGHTTP2_STREAM_OPENED,
  /* RST_STREAM is received, but somehow we need to keep stream in
     memory. */
  NGHTTP2_STREAM_CLOSING,
  /* PUSH_PROMISE is received or sent */
  NGHTTP2_STREAM_RESERVED,
  /* Stream is created in this state if it is used as anchor in
     dependency tree. */
  NGHTTP2_STREAM_IDLE
} nghttp2_stream_state;

typedef enum {
  NGHTTP2_SHUT_NONE = 0,
  /* Indicates further receptions will be disallowed. */
  NGHTTP2_SHUT_RD = 0x01,
  /* Indicates further transmissions will be disallowed. */
  NGHTTP2_SHUT_WR = 0x02,
  /* Indicates both further receptions and transmissions will be
     disallowed. */
  NGHTTP2_SHUT_RDWR = NGHTTP2_SHUT_RD | NGHTTP2_SHUT_WR
} nghttp2_shut_flag;

typedef enum {
  NGHTTP2_STREAM_FLAG_NONE = 0,
  /* Indicates that this stream is pushed stream and not opened
     yet. */
  NGHTTP2_STREAM_FLAG_PUSH = 0x01,
  /* Indicates that this stream was closed */
  NGHTTP2_STREAM_FLAG_CLOSED = 0x02,
  /* Indicates the item is deferred due to flow control. */
  NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL = 0x04,
  /* Indicates the item is deferred by user callback */
  NGHTTP2_STREAM_FLAG_DEFERRED_USER = 0x08,
  /* bitwise OR of NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL and
     NGHTTP2_STREAM_FLAG_DEFERRED_USER. */
  NGHTTP2_STREAM_FLAG_DEFERRED_ALL = 0x0c,
  /* Indicates that this stream is not subject to RFC7540
     priorities scheme. */
  NGHTTP2_STREAM_FLAG_NO_RFC7540_PRIORITIES = 0x10,
  /* Ignore client RFC 9218 priority signal. */
  NGHTTP2_STREAM_FLAG_IGNORE_CLIENT_PRIORITIES = 0x20,
  /* Indicates that RFC 9113 leading and trailing white spaces
     validation against a field value is not performed. */
  NGHTTP2_STREAM_FLAG_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION = 0x40,
} nghttp2_stream_flag;

/* HTTP related flags to enforce HTTP semantics */
typedef enum {
  NGHTTP2_HTTP_FLAG_NONE = 0,
  /* header field seen so far */
  NGHTTP2_HTTP_FLAG__AUTHORITY = 1,
  NGHTTP2_HTTP_FLAG__PATH = 1 << 1,
  NGHTTP2_HTTP_FLAG__METHOD = 1 << 2,
  NGHTTP2_HTTP_FLAG__SCHEME = 1 << 3,
  /* host is not pseudo header, but we require either host or
     :authority */
  NGHTTP2_HTTP_FLAG_HOST = 1 << 4,
  NGHTTP2_HTTP_FLAG__STATUS = 1 << 5,
  /* required header fields for HTTP request except for CONNECT
     method. */
  NGHTTP2_HTTP_FLAG_REQ_HEADERS = NGHTTP2_HTTP_FLAG__METHOD |
                                  NGHTTP2_HTTP_FLAG__PATH |
                                  NGHTTP2_HTTP_FLAG__SCHEME,
  NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED = 1 << 6,
  /* HTTP method flags */
  NGHTTP2_HTTP_FLAG_METH_CONNECT = 1 << 7,
  NGHTTP2_HTTP_FLAG_METH_HEAD = 1 << 8,
  NGHTTP2_HTTP_FLAG_METH_OPTIONS = 1 << 9,
  NGHTTP2_HTTP_FLAG_METH_UPGRADE_WORKAROUND = 1 << 10,
  NGHTTP2_HTTP_FLAG_METH_ALL =
    NGHTTP2_HTTP_FLAG_METH_CONNECT | NGHTTP2_HTTP_FLAG_METH_HEAD |
    NGHTTP2_HTTP_FLAG_METH_OPTIONS | NGHTTP2_HTTP_FLAG_METH_UPGRADE_WORKAROUND,
  /* :path category */
  /* path starts with "/" */
  NGHTTP2_HTTP_FLAG_PATH_REGULAR = 1 << 11,
  /* path "*" */
  NGHTTP2_HTTP_FLAG_PATH_ASTERISK = 1 << 12,
  /* scheme */
  /* "http" or "https" scheme */
  NGHTTP2_HTTP_FLAG_SCHEME_HTTP = 1 << 13,
  /* set if final response is expected */
  NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE = 1 << 14,
  NGHTTP2_HTTP_FLAG__PROTOCOL = 1 << 15,
  /* set if priority header field is received */
  NGHTTP2_HTTP_FLAG_PRIORITY = 1 << 16,
  /* set if an error is encountered while parsing priority header
     field */
  NGHTTP2_HTTP_FLAG_BAD_PRIORITY = 1 << 17,
} nghttp2_http_flag;

struct nghttp2_stream {
  /* Entry for dep_prev->obq */
  nghttp2_pq_entry pq_entry;
  /* Priority Queue storing direct descendant (nghttp2_stream).  Only
     streams which itself has some data to send, or has a descendant
     which has some data to sent. */
  nghttp2_pq obq;
  /* Content-Length of request/response body.  -1 if unknown. */
  int64_t content_length;
  /* Received body so far */
  int64_t recv_content_length;
  /* Base last_cycle for direct descendent streams. */
  uint64_t descendant_last_cycle;
  /* Next scheduled time to sent item */
  uint64_t cycle;
  /* Next seq used for direct descendant streams */
  uint64_t descendant_next_seq;
  /* Secondary key for prioritization to break a tie for cycle.  This
     value is monotonically increased for single parent stream. */
  uint64_t seq;
  /* pointers to form dependency tree.  If multiple streams depend on
     a stream, only one stream (left most) has non-NULL dep_prev which
     points to the stream it depends on. The remaining streams are
     linked using sib_prev and sib_next.  The stream which has
     non-NULL dep_prev always NULL sib_prev.  The right most stream
     has NULL sib_next.  If this stream is a root of dependency tree,
     dep_prev and sib_prev are NULL. */
  nghttp2_stream *dep_prev, *dep_next;
  nghttp2_stream *sib_prev, *sib_next;
  /* When stream is kept after closure, it may be kept in doubly
     linked list pointed by nghttp2_session closed_stream_head.
     closed_next points to the next stream object if it is the element
     of the list. */
  nghttp2_stream *closed_prev, *closed_next;
  /* The arbitrary data provided by user for this stream. */
  void *stream_user_data;
  /* Item to send */
  nghttp2_outbound_item *item;
  /* Last written length of frame payload */
  size_t last_writelen;
  /* stream ID */
  int32_t stream_id;
  /* Current remote window size. This value is computed against the
     current initial window size of remote endpoint. */
  int32_t remote_window_size;
  /* Keep track of the number of bytes received without
     WINDOW_UPDATE. This could be negative after submitting negative
     value to WINDOW_UPDATE */
  int32_t recv_window_size;
  /* The number of bytes consumed by the application and now is
     subject to WINDOW_UPDATE.  This is only used when auto
     WINDOW_UPDATE is turned off. */
  int32_t consumed_size;
  /* The amount of recv_window_size cut using submitting negative
     value to WINDOW_UPDATE */
  int32_t recv_reduction;
  /* window size for local flow control. It is initially set to
     NGHTTP2_INITIAL_WINDOW_SIZE and could be increased/decreased by
     submitting WINDOW_UPDATE. See nghttp2_submit_window_update(). */
  int32_t local_window_size;
  /* weight of this stream */
  int32_t weight;
  /* This is unpaid penalty (offset) when calculating cycle. */
  uint32_t pending_penalty;
  /* sum of weight of direct descendants */
  int32_t sum_dep_weight;
  nghttp2_stream_state state;
  /* status code from remote server */
  int16_t status_code;
  /* Bitwise OR of zero or more nghttp2_http_flag values */
  uint32_t http_flags;
  /* This is bitwise-OR of 0 or more of nghttp2_stream_flag. */
  uint8_t flags;
  /* Bitwise OR of zero or more nghttp2_shut_flag values */
  uint8_t shut_flags;
  /* Nonzero if this stream has been queued to stream pointed by
     dep_prev.  We maintain the invariant that if a stream is queued,
     then its ancestors, except for root, are also queued.  This
     invariant may break in fatal error condition. */
  uint8_t queued;
  /* This flag is used to reduce excessive queuing of WINDOW_UPDATE to
     this stream.  The nonzero does not necessarily mean WINDOW_UPDATE
     is not queued. */
  uint8_t window_update_queued;
  /* extpri is a stream priority produced by nghttp2_extpri_to_uint8
     used by RFC 9218 extensible priorities. */
  uint8_t extpri;
  /* http_extpri is a stream priority received in HTTP request header
     fields and produced by nghttp2_extpri_to_uint8. */
  uint8_t http_extpri;
};

void nghttp2_stream_init(nghttp2_stream *stream, int32_t stream_id,
                         uint8_t flags, nghttp2_stream_state initial_state,
                         int32_t weight, int32_t remote_initial_window_size,
                         int32_t local_initial_window_size,
                         void *stream_user_data, nghttp2_mem *mem);

void nghttp2_stream_free(nghttp2_stream *stream);

/*
 * Disallow either further receptions or transmissions, or both.
 * |flag| is bitwise OR of one or more of nghttp2_shut_flag.
 */
void nghttp2_stream_shutdown(nghttp2_stream *stream, nghttp2_shut_flag flag);

/*
 * Defer |stream->item|.  We won't call this function in the situation
 * where |stream->item| == NULL.  The |flags| is bitwise OR of zero or
 * more of NGHTTP2_STREAM_FLAG_DEFERRED_USER and
 * NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL.  The |flags| indicates
 * the reason of this action.
 */
void nghttp2_stream_defer_item(nghttp2_stream *stream, uint8_t flags);

/*
 * Put back deferred data in this stream to active state.  The |flags|
 * are one or more of bitwise OR of the following values:
 * NGHTTP2_STREAM_FLAG_DEFERRED_USER and
 * NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL and given masks are
 * cleared if they are set.  So even if this function is called, if
 * one of flag is still set, data does not become active.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_stream_resume_deferred_item(nghttp2_stream *stream, uint8_t flags);

/*
 * Returns nonzero if item is deferred by whatever reason.
 */
int nghttp2_stream_check_deferred_item(nghttp2_stream *stream);

/*
 * Returns nonzero if item is deferred by flow control.
 */
int nghttp2_stream_check_deferred_by_flow_control(nghttp2_stream *stream);

/*
 * Updates the remote window size with the new value
 * |new_initial_window_size|. The |old_initial_window_size| is used to
 * calculate the current window size.
 *
 * This function returns 0 if it succeeds or -1. The failure is due to
 * overflow.
 */
int nghttp2_stream_update_remote_initial_window_size(
  nghttp2_stream *stream, int32_t new_initial_window_size,
  int32_t old_initial_window_size);

/*
 * Updates the local window size with the new value
 * |new_initial_window_size|. The |old_initial_window_size| is used to
 * calculate the current window size.
 *
 * This function returns 0 if it succeeds or -1. The failure is due to
 * overflow.
 */
int nghttp2_stream_update_local_initial_window_size(
  nghttp2_stream *stream, int32_t new_initial_window_size,
  int32_t old_initial_window_size);

/*
 * Call this function if promised stream |stream| is replied with
 * HEADERS.  This function makes the state of the |stream| to
 * NGHTTP2_STREAM_OPENED.
 */
void nghttp2_stream_promise_fulfilled(nghttp2_stream *stream);

/*
 * Returns nonzero if |target| is an ancestor of |stream|.
 */
int nghttp2_stream_dep_find_ancestor(nghttp2_stream *stream,
                                     nghttp2_stream *target);

/*
 * Computes distributed weight of a stream of the |weight| under the
 * |stream| if |stream| is removed from a dependency tree.
 */
int32_t nghttp2_stream_dep_distributed_weight(nghttp2_stream *stream,
                                              int32_t weight);

/*
 * Makes the |stream| depend on the |dep_stream|.  This dependency is
 * exclusive.  All existing direct descendants of |dep_stream| become
 * the descendants of the |stream|.  This function assumes
 * |stream->item| is NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_stream_dep_insert(nghttp2_stream *dep_stream,
                              nghttp2_stream *stream);

/*
 * Makes the |stream| depend on the |dep_stream|.  This dependency is
 * not exclusive.  This function assumes |stream->item| is NULL.
 */
void nghttp2_stream_dep_add(nghttp2_stream *dep_stream, nghttp2_stream *stream);

/*
 * Removes the |stream| from the current dependency tree.  This
 * function assumes |stream->item| is NULL.
 */
int nghttp2_stream_dep_remove(nghttp2_stream *stream);

/*
 * Attaches |item| to |stream|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_stream_attach_item(nghttp2_stream *stream,
                               nghttp2_outbound_item *item);

/*
 * Detaches |stream->item|.  This function does not free
 * |stream->item|.  The caller must free it.
 */
void nghttp2_stream_detach_item(nghttp2_stream *stream);

/*
 * Makes the |stream| depend on the |dep_stream|.  This dependency is
 * exclusive.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_stream_dep_insert_subtree(nghttp2_stream *dep_stream,
                                      nghttp2_stream *stream);

/*
 * Makes the |stream| depend on the |dep_stream|.  This dependency is
 * not exclusive.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_stream_dep_add_subtree(nghttp2_stream *dep_stream,
                                   nghttp2_stream *stream);

/*
 * Removes subtree whose root stream is |stream|.  The
 * effective_weight of streams in removed subtree is not updated.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
void nghttp2_stream_dep_remove_subtree(nghttp2_stream *stream);

/*
 * Returns nonzero if |stream| is in any dependency tree.
 */
int nghttp2_stream_in_dep_tree(nghttp2_stream *stream);

/*
 * Schedules transmission of |stream|'s item, assuming stream->item is
 * attached, and stream->last_writelen was updated.
 */
void nghttp2_stream_reschedule(nghttp2_stream *stream);

/*
 * Changes |stream|'s weight to |weight|.  If |stream| is queued, it
 * will be rescheduled based on new weight.
 */
void nghttp2_stream_change_weight(nghttp2_stream *stream, int32_t weight);

/*
 * Returns a stream which has highest priority, updating
 * descendant_last_cycle of selected stream's ancestors.
 */
nghttp2_outbound_item *
nghttp2_stream_next_outbound_item(nghttp2_stream *stream);

#endif /* NGHTTP2_STREAM */
