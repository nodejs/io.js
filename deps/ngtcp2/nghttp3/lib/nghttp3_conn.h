/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
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
#ifndef NGHTTP3_CONN_H
#define NGHTTP3_CONN_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

#include "nghttp3_stream.h"
#include "nghttp3_map.h"
#include "nghttp3_qpack.h"
#include "nghttp3_tnode.h"
#include "nghttp3_idtr.h"
#include "nghttp3_gaptr.h"

#define NGHTTP3_VARINT_MAX ((1ull << 62) - 1)

/* NGHTTP3_QPACK_ENCODER_MAX_TABLE_CAPACITY is the maximum dynamic
   table size for QPACK encoder. */
#define NGHTTP3_QPACK_ENCODER_MAX_TABLE_CAPACITY 16384

/* NGHTTP3_QPACK_ENCODER_MAX_BLOCK_STREAMS is the maximum number of
   blocked streams for QPACK encoder. */
#define NGHTTP3_QPACK_ENCODER_MAX_BLOCK_STREAMS 100

/* NGHTTP3_CONN_FLAG_NONE indicates that no flag is set. */
#define NGHTTP3_CONN_FLAG_NONE 0x0000u
/* NGHTTP3_CONN_FLAG_SETTINGS_RECVED is set when SETTINGS frame has
   been received. */
#define NGHTTP3_CONN_FLAG_SETTINGS_RECVED 0x0001u
/* NGHTTP3_CONN_FLAG_CONTROL_OPENED is set when a control stream has
   opened. */
#define NGHTTP3_CONN_FLAG_CONTROL_OPENED 0x0002u
/* NGHTTP3_CONN_FLAG_QPACK_ENCODER_OPENED is set when a QPACK encoder
   stream has opened. */
#define NGHTTP3_CONN_FLAG_QPACK_ENCODER_OPENED 0x0004u
/* NGHTTP3_CONN_FLAG_QPACK_DECODER_OPENED is set when a QPACK decoder
   stream has opened. */
#define NGHTTP3_CONN_FLAG_QPACK_DECODER_OPENED 0x0008u
/* NGHTTP3_CONN_FLAG_SHUTDOWN_COMMENCED is set when graceful shutdown
   has started. */
#define NGHTTP3_CONN_FLAG_SHUTDOWN_COMMENCED 0x0010u
/* NGHTTP3_CONN_FLAG_GOAWAY_RECVED indicates that GOAWAY frame has
   received. */
#define NGHTTP3_CONN_FLAG_GOAWAY_RECVED 0x0020u
/* NGHTTP3_CONN_FLAG_GOAWAY_QUEUED indicates that GOAWAY frame has
   been submitted for transmission. */
#define NGHTTP3_CONN_FLAG_GOAWAY_QUEUED 0x0040u

typedef struct nghttp3_chunk {
  nghttp3_opl_entry oplent;
} nghttp3_chunk;

nghttp3_objalloc_decl(chunk, nghttp3_chunk, oplent);

struct nghttp3_conn {
  nghttp3_objalloc out_chunk_objalloc;
  nghttp3_objalloc stream_objalloc;
  nghttp3_callbacks callbacks;
  nghttp3_map streams;
  nghttp3_qpack_decoder qdec;
  nghttp3_qpack_encoder qenc;
  nghttp3_pq qpack_blocked_streams;
  struct {
    nghttp3_pq spq;
  } sched[NGHTTP3_URGENCY_LEVELS];
  const nghttp3_mem *mem;
  void *user_data;
  int server;
  uint16_t flags;

  struct {
    nghttp3_settings settings;
    struct {
      /* max_pushes is the number of push IDs that local endpoint can
         issue.  This field is used by server only and used just for
         validation */
      uint64_t max_pushes;
    } uni;
  } local;

  struct {
    struct {
      nghttp3_idtr idtr;
      /* max_client_streams is the cumulative number of client
         initiated bidirectional stream ID the remote endpoint can
         issue.  This field is used on server side only. */
      uint64_t max_client_streams;
      /* num_streams is the number of client initiated bidirectional
         streams that are currently open.  This field is for server
         use only. */
      size_t num_streams;
    } bidi;
    nghttp3_settings settings;
  } remote;

  struct {
    /* goaway_id is the latest ID received in GOAWAY frame. */
    int64_t goaway_id;

    int64_t max_stream_id_bidi;

    /* pri_fieldbuf is a buffer to store incoming Priority Field Value
       in PRIORITY_UPDATE frame. */
    uint8_t pri_fieldbuf[8];
    /* pri_fieldlen is the number of bytes written into
       pri_fieldbuf. */
    size_t pri_fieldbuflen;
  } rx;

  struct {
    struct {
      nghttp3_buf rbuf;
      nghttp3_buf ebuf;
    } qpack;
    nghttp3_stream *ctrl;
    nghttp3_stream *qenc;
    nghttp3_stream *qdec;
    /* goaway_id is the latest ID sent in GOAWAY frame. */
    int64_t goaway_id;
  } tx;
};

nghttp3_stream *nghttp3_conn_find_stream(nghttp3_conn *conn, int64_t stream_id);

int nghttp3_conn_create_stream(nghttp3_conn *conn, nghttp3_stream **pstream,
                               int64_t stream_id);

nghttp3_ssize nghttp3_conn_read_bidi(nghttp3_conn *conn, size_t *pnproc,
                                     nghttp3_stream *stream, const uint8_t *src,
                                     size_t srclen, int fin);

nghttp3_ssize nghttp3_conn_read_uni(nghttp3_conn *conn, nghttp3_stream *stream,
                                    const uint8_t *src, size_t srclen, int fin);

nghttp3_ssize nghttp3_conn_read_control(nghttp3_conn *conn,
                                        nghttp3_stream *stream,
                                        const uint8_t *src, size_t srclen);

nghttp3_ssize nghttp3_conn_read_qpack_encoder(nghttp3_conn *conn,
                                              const uint8_t *src,
                                              size_t srclen);

nghttp3_ssize nghttp3_conn_read_qpack_decoder(nghttp3_conn *conn,
                                              const uint8_t *src,
                                              size_t srclen);

int nghttp3_conn_on_data(nghttp3_conn *conn, nghttp3_stream *stream,
                         const uint8_t *data, size_t datalen);

int nghttp3_conn_on_priority_update(nghttp3_conn *conn,
                                    const nghttp3_frame_priority_update *fr);

nghttp3_ssize nghttp3_conn_on_headers(nghttp3_conn *conn,
                                      nghttp3_stream *stream,
                                      const uint8_t *data, size_t datalen,
                                      int fin);

int nghttp3_conn_on_settings_entry_received(nghttp3_conn *conn,
                                            const nghttp3_frame_settings *fr);

int nghttp3_conn_qpack_blocked_streams_push(nghttp3_conn *conn,
                                            nghttp3_stream *stream);

void nghttp3_conn_qpack_blocked_streams_pop(nghttp3_conn *conn);

int nghttp3_conn_schedule_stream(nghttp3_conn *conn, nghttp3_stream *stream);

int nghttp3_conn_ensure_stream_scheduled(nghttp3_conn *conn,
                                         nghttp3_stream *stream);

void nghttp3_conn_unschedule_stream(nghttp3_conn *conn, nghttp3_stream *stream);

int nghttp3_conn_reject_stream(nghttp3_conn *conn, nghttp3_stream *stream);

/*
 * nghttp3_conn_get_next_tx_stream returns next stream to send.  It
 * returns NULL if there is no such stream.
 */
nghttp3_stream *nghttp3_conn_get_next_tx_stream(nghttp3_conn *conn);

#endif /* !defined(NGHTTP3_CONN_H) */
