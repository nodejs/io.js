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
#ifndef NGHTTP2_SESSION_H
#define NGHTTP2_SESSION_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp2/nghttp2.h>
#include "nghttp2_map.h"
#include "nghttp2_frame.h"
#include "nghttp2_hd.h"
#include "nghttp2_stream.h"
#include "nghttp2_outbound_item.h"
#include "nghttp2_int.h"
#include "nghttp2_buf.h"
#include "nghttp2_callbacks.h"
#include "nghttp2_mem.h"
#include "nghttp2_ratelim.h"

/* The global variable for tests where we want to disable strict
   preface handling. */
extern int nghttp2_enable_strict_preface;

extern nghttp2_stream root;

/*
 * Option flags.
 */
typedef enum {
  NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE = 1 << 0,
  NGHTTP2_OPTMASK_NO_RECV_CLIENT_MAGIC = 1 << 1,
  NGHTTP2_OPTMASK_NO_HTTP_MESSAGING = 1 << 2,
  NGHTTP2_OPTMASK_NO_AUTO_PING_ACK = 1 << 3,
  NGHTTP2_OPTMASK_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION = 1 << 6,
} nghttp2_optmask;

/*
 * bitmask for built-in type to enable the default handling for that
 * type of the frame.
 */
typedef enum {
  NGHTTP2_TYPEMASK_NONE = 0,
  NGHTTP2_TYPEMASK_ALTSVC = 1 << 0,
  NGHTTP2_TYPEMASK_ORIGIN = 1 << 1,
  NGHTTP2_TYPEMASK_PRIORITY_UPDATE = 1 << 2
} nghttp2_typemask;

typedef enum {
  NGHTTP2_OB_POP_ITEM,
  NGHTTP2_OB_SEND_DATA,
  NGHTTP2_OB_SEND_NO_COPY,
  NGHTTP2_OB_SEND_CLIENT_MAGIC
} nghttp2_outbound_state;

typedef struct {
  nghttp2_outbound_item *item;
  nghttp2_bufs framebufs;
  nghttp2_outbound_state state;
} nghttp2_active_outbound_item;

/* Buffer length for inbound raw byte stream used in
   nghttp2_session_recv(). */
#define NGHTTP2_INBOUND_BUFFER_LENGTH 16384

/* The default maximum number of incoming reserved streams */
#define NGHTTP2_MAX_INCOMING_RESERVED_STREAMS 200

/* The maximum number of items in outbound queue, which is considered
   as flooding caused by peer.  All frames are not considered here.
   We only consider PING + ACK and SETTINGS + ACK.  This is because
   they both are response to the frame initiated by peer and peer can
   send as many of them as they want.  If peer does not read network,
   response frames are stacked up, which leads to memory exhaustion.
   The value selected here is arbitrary, but safe value and if we have
   these frames in this number, it is considered suspicious. */
#define NGHTTP2_DEFAULT_MAX_OBQ_FLOOD_ITEM 1000

/* The default value of maximum number of concurrent streams. */
#define NGHTTP2_DEFAULT_MAX_CONCURRENT_STREAMS 0xffffffffu

/* The default values for stream reset rate limiter. */
#define NGHTTP2_DEFAULT_STREAM_RESET_BURST 1000
#define NGHTTP2_DEFAULT_STREAM_RESET_RATE 33

/* The default max number of CONTINUATION frames following an incoming
   HEADER frame. */
#define NGHTTP2_DEFAULT_MAX_CONTINUATIONS 8

/* Internal state when receiving incoming frame */
typedef enum {
  /* Receiving frame header */
  NGHTTP2_IB_READ_CLIENT_MAGIC,
  NGHTTP2_IB_READ_FIRST_SETTINGS,
  NGHTTP2_IB_READ_HEAD,
  NGHTTP2_IB_READ_NBYTE,
  NGHTTP2_IB_READ_HEADER_BLOCK,
  NGHTTP2_IB_IGN_HEADER_BLOCK,
  NGHTTP2_IB_IGN_PAYLOAD,
  NGHTTP2_IB_FRAME_SIZE_ERROR,
  NGHTTP2_IB_READ_SETTINGS,
  NGHTTP2_IB_READ_GOAWAY_DEBUG,
  NGHTTP2_IB_EXPECT_CONTINUATION,
  NGHTTP2_IB_IGN_CONTINUATION,
  NGHTTP2_IB_READ_PAD_DATA,
  NGHTTP2_IB_READ_DATA,
  NGHTTP2_IB_IGN_DATA,
  NGHTTP2_IB_IGN_ALL,
  NGHTTP2_IB_READ_ALTSVC_PAYLOAD,
  NGHTTP2_IB_READ_ORIGIN_PAYLOAD,
  NGHTTP2_IB_READ_EXTENSION_PAYLOAD
} nghttp2_inbound_state;

typedef struct {
  nghttp2_frame frame;
  /* Storage for extension frame payload.  frame->ext.payload points
     to this structure to avoid frequent memory allocation. */
  nghttp2_ext_frame_payload ext_frame_payload;
  /* The received SETTINGS entry.  For the standard settings entries,
     we only keep the last seen value.  For
     SETTINGS_HEADER_TABLE_SIZE, we also keep minimum value in the
     last index. */
  nghttp2_settings_entry *iv;
  /* buffer pointers to small buffer, raw_sbuf */
  nghttp2_buf sbuf;
  /* buffer pointers to large buffer, raw_lbuf */
  nghttp2_buf lbuf;
  /* Large buffer, malloced on demand */
  uint8_t *raw_lbuf;
  /* The number of entry filled in |iv| */
  size_t niv;
  /* The number of entries |iv| can store. */
  size_t max_niv;
  /* How many bytes we still need to receive for current frame */
  size_t payloadleft;
  /* padding length for the current frame */
  size_t padlen;
  nghttp2_inbound_state state;
  /* Small fixed sized buffer. */
  uint8_t raw_sbuf[32];
} nghttp2_inbound_frame;

typedef struct {
  uint32_t header_table_size;
  uint32_t enable_push;
  uint32_t max_concurrent_streams;
  uint32_t initial_window_size;
  uint32_t max_frame_size;
  uint32_t max_header_list_size;
  uint32_t enable_connect_protocol;
  uint32_t no_rfc7540_priorities;
} nghttp2_settings_storage;

typedef enum {
  NGHTTP2_GOAWAY_NONE = 0,
  /* Flag means that connection should be terminated after sending GOAWAY. */
  NGHTTP2_GOAWAY_TERM_ON_SEND = 0x1,
  /* Flag means GOAWAY to terminate session has been sent */
  NGHTTP2_GOAWAY_TERM_SENT = 0x2,
  /* Flag means GOAWAY was sent */
  NGHTTP2_GOAWAY_SENT = 0x4,
  /* Flag means GOAWAY was received */
  NGHTTP2_GOAWAY_RECV = 0x8,
  /* Flag means GOAWAY has been submitted at least once */
  NGHTTP2_GOAWAY_SUBMITTED = 0x10
} nghttp2_goaway_flag;

/* nghttp2_inflight_settings stores the SETTINGS entries which local
   endpoint has sent to the remote endpoint, and has not received ACK
   yet. */
struct nghttp2_inflight_settings {
  struct nghttp2_inflight_settings *next;
  nghttp2_settings_entry *iv;
  size_t niv;
};

typedef struct nghttp2_inflight_settings nghttp2_inflight_settings;

struct nghttp2_session {
  nghttp2_map /* <nghttp2_stream*> */ streams;
  /* Queue for outbound urgent frames (PING and SETTINGS) */
  nghttp2_outbound_queue ob_urgent;
  /* Queue for non-DATA frames */
  nghttp2_outbound_queue ob_reg;
  /* Queue for outbound stream-creating HEADERS (request or push
     response) frame, which are subject to
     SETTINGS_MAX_CONCURRENT_STREAMS limit. */
  nghttp2_outbound_queue ob_syn;
  /* Queues for DATA frames which is used when
     SETTINGS_NO_RFC7540_PRIORITIES is enabled.  This implements RFC
     9218 extensible prioritization scheme. */
  struct {
    nghttp2_pq ob_data;
  } sched[NGHTTP2_EXTPRI_URGENCY_LEVELS];
  nghttp2_active_outbound_item aob;
  nghttp2_inbound_frame iframe;
  nghttp2_hd_deflater hd_deflater;
  nghttp2_hd_inflater hd_inflater;
  nghttp2_session_callbacks callbacks;
  /* Memory allocator */
  nghttp2_mem mem;
  void *user_data;
  /* Queue of In-flight SETTINGS values.  SETTINGS bearing ACK is not
     considered as in-flight. */
  nghttp2_inflight_settings *inflight_settings_head;
  /* Stream reset rate limiter.  If receiving excessive amount of
     stream resets, GOAWAY will be sent. */
  nghttp2_ratelim stream_reset_ratelim;
  /* Sequential number across all streams to process streams in
     FIFO. */
  uint64_t stream_seq;
  /* The number of outgoing streams. This will be capped by
     remote_settings.max_concurrent_streams. */
  size_t num_outgoing_streams;
  /* The number of incoming streams. This will be capped by
     local_settings.max_concurrent_streams. */
  size_t num_incoming_streams;
  /* The number of incoming reserved streams.  This is the number of
     streams in reserved (remote) state.  RFC 7540 does not limit this
     number.  nghttp2 offers
     nghttp2_option_set_max_reserved_remote_streams() to achieve this.
     If it is used, num_incoming_streams is capped by
     max_incoming_reserved_streams.  Client application should
     consider to set this because without that server can send
     arbitrary number of PUSH_PROMISE, and exhaust client's memory. */
  size_t num_incoming_reserved_streams;
  /* The maximum number of incoming reserved streams (reserved
     (remote) state).  RST_STREAM will be sent for the pushed stream
     which exceeds this limit. */
  size_t max_incoming_reserved_streams;
  /* The number of closed streams still kept in |streams| hash.  The
     closed streams can be accessed through single linked list
     |closed_stream_head|.  The current implementation only keeps
     incoming streams and session is initialized as server. */
  size_t num_closed_streams;
  /* The number of idle streams kept in |streams| hash.  The current
     implementation only keeps idle streams if session is initialized
     as server. */
  size_t num_idle_streams;
  /* The number of bytes allocated for nvbuf */
  size_t nvbuflen;
  /* Counter for detecting flooding in outbound queue.  If it exceeds
     max_outbound_ack, session will be closed. */
  size_t obq_flood_counter_;
  /* The maximum number of outgoing SETTINGS ACK and PING ACK in
     outbound queue. */
  size_t max_outbound_ack;
  /* The maximum length of header block to send.  Calculated by the
     same way as nghttp2_hd_deflate_bound() does. */
  size_t max_send_header_block_length;
  /* The maximum number of settings accepted per SETTINGS frame. */
  size_t max_settings;
  /* The maximum number of CONTINUATION frames following an incoming
     HEADER frame. */
  size_t max_continuations;
  /* The number of CONTINUATION frames following an incoming HEADER
     frame.  This variable is reset when END_HEADERS flag is seen. */
  size_t num_continuations;
  /* Next Stream ID. Made unsigned int to detect >= (1 << 31). */
  uint32_t next_stream_id;
  /* The last stream ID this session initiated.  For client session,
     this is the last stream ID it has sent.  For server session, it
     is the last promised stream ID sent in PUSH_PROMISE. */
  int32_t last_sent_stream_id;
  /* The largest stream ID received so far */
  int32_t last_recv_stream_id;
  /* The largest stream ID which has been processed in some way. This
     value will be used as last-stream-id when sending GOAWAY
     frame. */
  int32_t last_proc_stream_id;
  /* Counter of unique ID of PING. Wraps when it exceeds
     NGHTTP2_MAX_UNIQUE_ID */
  uint32_t next_unique_id;
  /* This is the last-stream-ID we have sent in GOAWAY */
  int32_t local_last_stream_id;
  /* This is the value in GOAWAY frame received from remote endpoint. */
  int32_t remote_last_stream_id;
  /* Current sender window size. This value is computed against the
     current initial window size of remote endpoint. */
  int32_t remote_window_size;
  /* Keep track of the number of bytes received without
     WINDOW_UPDATE. This could be negative after submitting negative
     value to WINDOW_UPDATE. */
  int32_t recv_window_size;
  /* The number of bytes consumed by the application and now is
     subject to WINDOW_UPDATE.  This is only used when auto
     WINDOW_UPDATE is turned off. */
  int32_t consumed_size;
  /* The amount of recv_window_size cut using submitting negative
     value to WINDOW_UPDATE */
  int32_t recv_reduction;
  /* window size for local flow control. It is initially set to
     NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE and could be
     increased/decreased by submitting WINDOW_UPDATE. See
     nghttp2_submit_window_update(). */
  int32_t local_window_size;
  /* This flag is used to indicate that the local endpoint received initial
     SETTINGS frame from the remote endpoint. */
  uint8_t remote_settings_received;
  /* Settings value received from the remote endpoint. */
  nghttp2_settings_storage remote_settings;
  /* Settings value of the local endpoint. */
  nghttp2_settings_storage local_settings;
  /* Option flags. This is bitwise-OR of 0 or more of nghttp2_optmask. */
  uint32_t opt_flags;
  /* Unacked local SETTINGS_MAX_CONCURRENT_STREAMS value. We use this
     to refuse the incoming stream if it exceeds this value. */
  uint32_t pending_local_max_concurrent_stream;
  /* The bitwise OR of zero or more of nghttp2_typemask to indicate
     that the default handling of extension frame is enabled. */
  uint32_t builtin_recv_ext_types;
  /* Unacked local ENABLE_PUSH value.  We use this to refuse
     PUSH_PROMISE before SETTINGS ACK is received. */
  uint8_t pending_enable_push;
  /* Unacked local ENABLE_CONNECT_PROTOCOL value.  We use this to
     accept :protocol header field before SETTINGS_ACK is received. */
  uint8_t pending_enable_connect_protocol;
  /* Unacked local SETTINGS_NO_RFC7540_PRIORITIES value, which is
     effective before it is acknowledged. */
  uint8_t pending_no_rfc7540_priorities;
  /* Nonzero if the session is server side. */
  uint8_t server;
  /* Flags indicating GOAWAY is sent and/or received. The flags are
     composed by bitwise OR-ing nghttp2_goaway_flag. */
  uint8_t goaway_flags;
  /* This flag is used to reduce excessive queuing of WINDOW_UPDATE to
     this session.  The nonzero does not necessarily mean
     WINDOW_UPDATE is not queued. */
  uint8_t window_update_queued;
  /* Bitfield of extension frame types that application is willing to
     receive.  To designate the bit of given frame type i, use
     user_recv_ext_types[i / 8] & (1 << (i & 0x7)).  First 10 frame
     types are standard frame types and not used in this bitfield.  If
     bit is set, it indicates that incoming frame with that type is
     passed to user defined callbacks, otherwise they are ignored. */
  uint8_t user_recv_ext_types[32];
};

/* Struct used when updating initial window size of each active
   stream. */
typedef struct {
  nghttp2_session *session;
  int32_t new_window_size, old_window_size;
} nghttp2_update_window_size_arg;

typedef struct {
  nghttp2_session *session;
  /* linked list of streams to close */
  nghttp2_stream *head;
  int32_t last_stream_id;
  /* nonzero if GOAWAY is sent to peer, which means we are going to
     close incoming streams.  zero if GOAWAY is received from peer and
     we are going to close outgoing streams. */
  int incoming;
} nghttp2_close_stream_on_goaway_arg;

/* TODO stream timeout etc */

/*
 * Returns nonzero value if |stream_id| is initiated by local
 * endpoint.
 */
int nghttp2_session_is_my_stream_id(nghttp2_session *session,
                                    int32_t stream_id);

/*
 * Adds |item| to the outbound queue in |session|.  When this function
 * succeeds, it takes ownership of |item|. So caller must not free it
 * on success.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_STREAM_CLOSED
 *     Stream already closed (DATA and PUSH_PROMISE frame only)
 */
int nghttp2_session_add_item(nghttp2_session *session,
                             nghttp2_outbound_item *item);

/*
 * Adds RST_STREAM frame for the stream |stream_id| with the error
 * code |error_code|. This is a convenient function built on top of
 * nghttp2_session_add_frame() to add RST_STREAM easily.
 *
 * This function simply returns 0 without adding RST_STREAM frame if
 * given stream is in NGHTTP2_STREAM_CLOSING state, because multiple
 * RST_STREAM for a stream is redundant.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
int nghttp2_session_add_rst_stream(nghttp2_session *session, int32_t stream_id,
                                   uint32_t error_code);

/*
 * Adds PING frame. This is a convenient function built on top of
 * nghttp2_session_add_frame() to add PING easily.
 *
 * If the |opaque_data| is not NULL, it must point to 8 bytes memory
 * region of data. The data pointed by |opaque_data| is copied. It can
 * be NULL. In this case, 8 bytes NULL is used.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_FLOODED
 *     There are too many items in outbound queue; this only happens
 *     if NGHTTP2_FLAG_ACK is set in |flags|
 */
int nghttp2_session_add_ping(nghttp2_session *session, uint8_t flags,
                             const uint8_t *opaque_data);

/*
 * Adds GOAWAY frame with the last-stream-ID |last_stream_id| and the
 * error code |error_code|. This is a convenient function built on top
 * of nghttp2_session_add_frame() to add GOAWAY easily.  The
 * |aux_flags| are bitwise-OR of one or more of
 * nghttp2_goaway_aux_flag.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_INVALID_ARGUMENT
 *     The |opaque_data_len| is too large.
 */
int nghttp2_session_add_goaway(nghttp2_session *session, int32_t last_stream_id,
                               uint32_t error_code, const uint8_t *opaque_data,
                               size_t opaque_data_len, uint8_t aux_flags);

/*
 * Adds WINDOW_UPDATE frame with stream ID |stream_id| and
 * window-size-increment |window_size_increment|. This is a convenient
 * function built on top of nghttp2_session_add_frame() to add
 * WINDOW_UPDATE easily.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
int nghttp2_session_add_window_update(nghttp2_session *session, uint8_t flags,
                                      int32_t stream_id,
                                      int32_t window_size_increment);

/*
 * Adds SETTINGS frame.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_FLOODED
 *     There are too many items in outbound queue; this only happens
 *     if NGHTTP2_FLAG_ACK is set in |flags|
 */
int nghttp2_session_add_settings(nghttp2_session *session, uint8_t flags,
                                 const nghttp2_settings_entry *iv, size_t niv);

/*
 * Creates new stream in |session| with stream ID |stream_id|,
 * priority |pri_spec| and flags |flags|.  The |flags| is bitwise OR
 * of nghttp2_stream_flag.  Since this function is called when initial
 * HEADERS is sent or received, these flags are taken from it.  The
 * state of stream is set to |initial_state|. The |stream_user_data|
 * is a pointer to the arbitrary user supplied data to be associated
 * to this stream.
 *
 * If |initial_state| is NGHTTP2_STREAM_RESERVED, this function sets
 * NGHTTP2_STREAM_FLAG_PUSH flag set.
 *
 * This function returns a pointer to created new stream object, or
 * NULL.
 */
nghttp2_stream *nghttp2_session_open_stream(nghttp2_session *session,
                                            int32_t stream_id, uint8_t flags,
                                            nghttp2_stream_state initial_state,
                                            void *stream_user_data);

/*
 * Closes stream whose stream ID is |stream_id|. The reason of closure
 * is indicated by the |error_code|. When closing the stream,
 * on_stream_close_callback will be called.
 *
 * This function returns 0 if it succeeds, or one the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 * NGHTTP2_ERR_INVALID_ARGUMENT
 *     The specified stream does not exist.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 */
int nghttp2_session_close_stream(nghttp2_session *session, int32_t stream_id,
                                 uint32_t error_code);

/*
 * Deletes |stream| from memory.  After this function returns, stream
 * cannot be accessed.
 */
void nghttp2_session_destroy_stream(nghttp2_session *session,
                                    nghttp2_stream *stream);

/*
 * If further receptions and transmissions over the stream |stream_id|
 * are disallowed, close the stream with error code NGHTTP2_NO_ERROR.
 *
 * This function returns 0 if it
 * succeeds, or one of the following negative error codes:
 *
 * NGHTTP2_ERR_INVALID_ARGUMENT
 *     The specified stream does not exist.
 */
int nghttp2_session_close_stream_if_shut_rdwr(nghttp2_session *session,
                                              nghttp2_stream *stream);

int nghttp2_session_on_request_headers_received(nghttp2_session *session,
                                                nghttp2_frame *frame);

int nghttp2_session_on_response_headers_received(nghttp2_session *session,
                                                 nghttp2_frame *frame,
                                                 nghttp2_stream *stream);

int nghttp2_session_on_push_response_headers_received(nghttp2_session *session,
                                                      nghttp2_frame *frame,
                                                      nghttp2_stream *stream);

/*
 * Called when HEADERS is received, assuming |frame| is properly
 * initialized.  This function does first validate received frame and
 * then open stream and call callback functions.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_IGN_HEADER_BLOCK
 *     Frame was rejected and header block must be decoded but
 *     result must be ignored.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The read_callback failed
 */
int nghttp2_session_on_headers_received(nghttp2_session *session,
                                        nghttp2_frame *frame,
                                        nghttp2_stream *stream);

/*
 * Called when PRIORITY is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The read_callback failed
 */
int nghttp2_session_on_priority_received(nghttp2_session *session,
                                         nghttp2_frame *frame);

/*
 * Called when RST_STREAM is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The read_callback failed
 */
int nghttp2_session_on_rst_stream_received(nghttp2_session *session,
                                           nghttp2_frame *frame);

/*
 * Called when SETTINGS is received, assuming |frame| is properly
 * initialized. If |noack| is non-zero, SETTINGS with ACK will not be
 * submitted. If |frame| has NGHTTP2_FLAG_ACK flag set, no SETTINGS
 * with ACK will not be submitted regardless of |noack|.
 *
 * This function returns 0 if it succeeds, or one the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The read_callback failed
 * NGHTTP2_ERR_FLOODED
 *     There are too many items in outbound queue, and this is most
 *     likely caused by misbehaviour of peer.
 */
int nghttp2_session_on_settings_received(nghttp2_session *session,
                                         nghttp2_frame *frame, int noack);

/*
 * Called when PUSH_PROMISE is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_IGN_HEADER_BLOCK
 *     Frame was rejected and header block must be decoded but
 *     result must be ignored.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The read_callback failed
 */
int nghttp2_session_on_push_promise_received(nghttp2_session *session,
                                             nghttp2_frame *frame);

/*
 * Called when PING is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 * NGHTTP2_ERR_FLOODED
 *     There are too many items in outbound queue, and this is most
 *     likely caused by misbehaviour of peer.
 */
int nghttp2_session_on_ping_received(nghttp2_session *session,
                                     nghttp2_frame *frame);

/*
 * Called when GOAWAY is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 */
int nghttp2_session_on_goaway_received(nghttp2_session *session,
                                       nghttp2_frame *frame);

/*
 * Called when WINDOW_UPDATE is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 */
int nghttp2_session_on_window_update_received(nghttp2_session *session,
                                              nghttp2_frame *frame);

/*
 * Called when ALTSVC is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 */
int nghttp2_session_on_altsvc_received(nghttp2_session *session,
                                       nghttp2_frame *frame);

/*
 * Called when ORIGIN is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 */
int nghttp2_session_on_origin_received(nghttp2_session *session,
                                       nghttp2_frame *frame);

/*
 * Called when PRIORITY_UPDATE is received, assuming |frame| is
 * properly initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 */
int nghttp2_session_on_priority_update_received(nghttp2_session *session,
                                                nghttp2_frame *frame);

/*
 * Called when DATA is received, assuming |frame| is properly
 * initialized.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The callback function failed.
 */
int nghttp2_session_on_data_received(nghttp2_session *session,
                                     nghttp2_frame *frame);

/*
 * Returns nghttp2_stream* object whose stream ID is |stream_id|.  It
 * could be NULL if such stream does not exist.  This function returns
 * NULL if stream is marked as closed.
 */
nghttp2_stream *nghttp2_session_get_stream(nghttp2_session *session,
                                           int32_t stream_id);

/*
 * This function behaves like nghttp2_session_get_stream(), but it
 * returns stream object even if it is marked as closed or in
 * NGHTTP2_STREAM_IDLE state.
 */
nghttp2_stream *nghttp2_session_get_stream_raw(nghttp2_session *session,
                                               int32_t stream_id);

/*
 * Packs DATA frame |frame| in wire frame format and stores it in
 * |bufs|.  Payload will be read using |aux_data->data_prd|.  The
 * length of payload is at most |datamax| bytes.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_DEFERRED
 *     The DATA frame is postponed.
 * NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE
 *     The read_callback failed (stream error).
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_CALLBACK_FAILURE
 *     The read_callback failed (session error).
 */
int nghttp2_session_pack_data(nghttp2_session *session, nghttp2_bufs *bufs,
                              size_t datamax, nghttp2_frame *frame,
                              nghttp2_data_aux_data *aux_data,
                              nghttp2_stream *stream);

/*
 * Pops and returns next item to send.  If there is no such item,
 * returns NULL.  This function takes into account max concurrent
 * streams.  That means if session->ob_syn has item and max concurrent
 * streams is reached, the even if other queues contain items, then
 * this function returns NULL.
 */
nghttp2_outbound_item *
nghttp2_session_pop_next_ob_item(nghttp2_session *session);

/*
 * Returns next item to send.  If there is no such item, this function
 * returns NULL.  This function takes into account max concurrent
 * streams.  That means if session->ob_syn has item and max concurrent
 * streams is reached, the even if other queues contain items, then
 * this function returns NULL.
 */
nghttp2_outbound_item *
nghttp2_session_get_next_ob_item(nghttp2_session *session);

/*
 * Updates local settings with the |iv|. The number of elements in the
 * array pointed by the |iv| is given by the |niv|.  This function
 * assumes that the all settings_id member in |iv| are in range 1 to
 * NGHTTP2_SETTINGS_MAX, inclusive.
 *
 * While updating individual stream's local window size, if the window
 * size becomes strictly larger than NGHTTP2_MAX_WINDOW_SIZE,
 * RST_STREAM is issued against such a stream.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_session_update_local_settings(nghttp2_session *session,
                                          nghttp2_settings_entry *iv,
                                          size_t niv);

/*
 * Terminates current |session| with the |error_code|.  The |reason|
 * is NULL-terminated debug string.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_INVALID_ARGUMENT
 *     The |reason| is too long.
 */
int nghttp2_session_terminate_session_with_reason(nghttp2_session *session,
                                                  uint32_t error_code,
                                                  const char *reason);

/*
 * Accumulates received bytes |delta_size| for connection-level flow
 * control and decides whether to send WINDOW_UPDATE to the
 * connection.  If NGHTTP2_OPT_NO_AUTO_WINDOW_UPDATE is set,
 * WINDOW_UPDATE will not be sent.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
int nghttp2_session_update_recv_connection_window_size(nghttp2_session *session,
                                                       size_t delta_size);

/*
 * Accumulates received bytes |delta_size| for stream-level flow
 * control and decides whether to send WINDOW_UPDATE to that stream.
 * If NGHTTP2_OPT_NO_AUTO_WINDOW_UPDATE is set, WINDOW_UPDATE will not
 * be sent.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
int nghttp2_session_update_recv_stream_window_size(nghttp2_session *session,
                                                   nghttp2_stream *stream,
                                                   size_t delta_size,
                                                   int send_window_update);

#endif /* NGHTTP2_SESSION_H */
