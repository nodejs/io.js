/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2013 nghttp2 contributors
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
#ifndef NGHTTP3_QPACK_H
#define NGHTTP3_QPACK_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

#include "nghttp3_rcbuf.h"
#include "nghttp3_map.h"
#include "nghttp3_pq.h"
#include "nghttp3_ringbuf.h"
#include "nghttp3_buf.h"
#include "nghttp3_ksl.h"
#include "nghttp3_qpack_huffman.h"

#define NGHTTP3_QPACK_INT_MAX ((1ull << 62) - 1)

/* NGHTTP3_QPACK_MAX_NAMELEN is the maximum (compressed) length of
   header name this library can decode. */
#define NGHTTP3_QPACK_MAX_NAMELEN 256
/* NGHTTP3_QPACK_MAX_VALUELEN is the maximum (compressed) length of
   header value this library can decode. */
#define NGHTTP3_QPACK_MAX_VALUELEN 65536
/* NGHTTP3_QPACK_MAX_ENCODERLEN is the maximum encoder stream length
   that a decoder accepts without completely processing a single field
   section. */
#define NGHTTP3_QPACK_MAX_ENCODERLEN (128 * 1024)
/* NGHTTP3_QPACK_MAX_DECODERLEN is the maximum decoder stream length
   that an encoder accepts without completely encoding a single field
   section. */
#define NGHTTP3_QPACK_MAX_DECODERLEN (4 * 1024)

/* nghttp3_qpack_indexing_mode is a indexing strategy. */
typedef enum nghttp3_qpack_indexing_mode {
  /* NGHTTP3_QPACK_INDEXING_MODE_LITERAL means that header field
     should not be inserted into dynamic table. */
  NGHTTP3_QPACK_INDEXING_MODE_LITERAL,
  /* NGHTTP3_QPACK_INDEXING_MODE_STORE means that header field can be
     inserted into dynamic table. */
  NGHTTP3_QPACK_INDEXING_MODE_STORE,
  /* NGHTTP3_QPACK_INDEXING_MODE_NEVER means that header field should
     not be inserted into dynamic table and this must be true for all
     forwarding paths. */
  NGHTTP3_QPACK_INDEXING_MODE_NEVER,
} nghttp3_qpack_indexing_mode;

typedef struct nghttp3_qpack_entry nghttp3_qpack_entry;

struct nghttp3_qpack_entry {
  /* The header field name/value pair */
  nghttp3_qpack_nv nv;
  /* map_next points to the entry which shares same bucket in hash
     table. */
  nghttp3_qpack_entry *map_next;
  /* sum is the sum of all entries inserted up to this entry.  This
     value does not contain the space required for this entry. */
  size_t sum;
  /* absidx is the absolute index of this entry. */
  uint64_t absidx;
  /* The hash value for header name (nv.name). */
  uint32_t hash;
};

/* The entry used for static table. */
typedef struct nghttp3_qpack_static_entry {
  uint64_t absidx;
  int32_t token;
  uint32_t hash;
} nghttp3_qpack_static_entry;

typedef struct nghttp3_qpack_static_header {
  nghttp3_rcbuf name;
  nghttp3_rcbuf value;
  int32_t token;
} nghttp3_qpack_static_header;

/*
 * nghttp3_qpack_header_block_ref is created per encoded header block
 * and includes the required insert count and the minimum insert count
 * of dynamic table entry it refers to.
 */
typedef struct nghttp3_qpack_header_block_ref {
  nghttp3_pq_entry max_cnts_pe;
  nghttp3_pq_entry min_cnts_pe;
  /* max_cnt is the required insert count. */
  uint64_t max_cnt;
  /* min_cnt is the minimum insert count of dynamic table entry it
     refers to.  In other words, this is the minimum absolute index of
     dynamic header table entry this encoded block refers to plus
     1. */
  uint64_t min_cnt;
} nghttp3_qpack_header_block_ref;

int nghttp3_qpack_header_block_ref_new(nghttp3_qpack_header_block_ref **pref,
                                       uint64_t max_cnt, uint64_t min_cnt,
                                       const nghttp3_mem *mem);

void nghttp3_qpack_header_block_ref_del(nghttp3_qpack_header_block_ref *ref,
                                        const nghttp3_mem *mem);

typedef struct nghttp3_qpack_stream {
  int64_t stream_id;
  /* refs is an array of pointer to nghttp3_qpack_header_block_ref in
     the order of the time they are encoded.  HTTP/3 allows multiple
     header blocks (e.g., non-final response headers, final response
     headers, trailers, and push promises) per stream. */
  nghttp3_ringbuf refs;
  /* max_cnts is a priority queue sorted by descending order of
     max_cnt of nghttp3_qpack_header_block_ref. */
  nghttp3_pq max_cnts;
} nghttp3_qpack_stream;

int nghttp3_qpack_stream_new(nghttp3_qpack_stream **pstream, int64_t stream_id,
                             const nghttp3_mem *mem);

void nghttp3_qpack_stream_del(nghttp3_qpack_stream *stream,
                              const nghttp3_mem *mem);

uint64_t nghttp3_qpack_stream_get_max_cnt(const nghttp3_qpack_stream *stream);

int nghttp3_qpack_stream_add_ref(nghttp3_qpack_stream *stream,
                                 nghttp3_qpack_header_block_ref *ref);

void nghttp3_qpack_stream_pop_ref(nghttp3_qpack_stream *stream);

#define NGHTTP3_QPACK_ENTRY_OVERHEAD 32

typedef struct nghttp3_qpack_context {
  /* dtable is a dynamic table */
  nghttp3_ringbuf dtable;
  /* mem is memory allocator */
  const nghttp3_mem *mem;
  /* dtable_size is abstracted buffer size of dtable as described in
     the spec. This is the sum of length of name/value in dtable +
     NGHTTP3_QPACK_ENTRY_OVERHEAD bytes overhead per each entry. */
  size_t dtable_size;
  size_t dtable_sum;
  /* hard_max_dtable_capacity is the upper bound of
     max_dtable_capacity. */
  size_t hard_max_dtable_capacity;
  /* max_dtable_capacity is the maximum capacity of the dynamic
     table. */
  size_t max_dtable_capacity;
  /* max_blocked_streams is the maximum number of stream which can be
     blocked. */
  size_t max_blocked_streams;
  /* next_absidx is the next absolute index for nghttp3_qpack_entry.
     It is equivalent to insert count. */
  uint64_t next_absidx;
  /* If inflate/deflate error occurred, this value is set to 1 and
     further invocation of inflate/deflate will fail with
     NGHTTP3_ERR_QPACK_FATAL. */
  uint8_t bad;
} nghttp3_qpack_context;

typedef struct nghttp3_qpack_read_state {
  nghttp3_qpack_huffman_decode_context huffman_ctx;
  nghttp3_buf namebuf;
  nghttp3_buf valuebuf;
  nghttp3_rcbuf *name;
  nghttp3_rcbuf *value;
  uint64_t left;
  size_t prefix;
  size_t shift;
  uint64_t absidx;
  int never;
  int dynamic;
  int huffman_encoded;
} nghttp3_qpack_read_state;

void nghttp3_qpack_read_state_free(nghttp3_qpack_read_state *rstate);

void nghttp3_qpack_read_state_reset(nghttp3_qpack_read_state *rstate);

#define NGHTTP3_QPACK_MAP_SIZE 64

typedef struct nghttp3_qpack_map {
  nghttp3_qpack_entry *table[NGHTTP3_QPACK_MAP_SIZE];
} nghttp3_qpack_map;

/* nghttp3_qpack_decoder_stream_state is a set of states when decoding
   decoder stream. */
typedef enum nghttp3_qpack_decoder_stream_state {
  NGHTTP3_QPACK_DS_STATE_OPCODE,
  NGHTTP3_QPACK_DS_STATE_READ_NUMBER,
} nghttp3_qpack_decoder_stream_state;

/* nghttp3_qpack_decoder_stream_opcode is opcode used in decoder
   stream. */
typedef enum nghttp3_qpack_decoder_stream_opcode {
  NGHTTP3_QPACK_DS_OPCODE_ICNT_INCREMENT,
  NGHTTP3_QPACK_DS_OPCODE_SECTION_ACK,
  NGHTTP3_QPACK_DS_OPCODE_STREAM_CANCEL,
} nghttp3_qpack_decoder_stream_opcode;

/* QPACK encoder flags */

/* NGHTTP3_QPACK_ENCODER_FLAG_NONE indicates that no flag is set. */
#define NGHTTP3_QPACK_ENCODER_FLAG_NONE 0x00u
/* NGHTTP3_QPACK_ENCODER_FLAG_PENDING_SET_DTABLE_CAP indicates that
   Set Dynamic Table Capacity is required. */
#define NGHTTP3_QPACK_ENCODER_FLAG_PENDING_SET_DTABLE_CAP 0x01u

struct nghttp3_qpack_encoder {
  nghttp3_qpack_context ctx;
  /* dtable_map is a map of hash to nghttp3_qpack_entry to provide
     fast access to an entry in dynamic table. */
  nghttp3_qpack_map dtable_map;
  /* streams is a map of stream ID to nghttp3_qpack_stream to keep
     track of unacknowledged streams. */
  nghttp3_map streams;
  /* blocked_streams is an ordered list of nghttp3_qpack_stream, in
     descending order of max_cnt, to search the unblocked streams by
     received known count. */
  nghttp3_ksl blocked_streams;
  /* min_cnts is a priority queue of nghttp3_qpack_header_block_ref
     sorted by ascending order of min_cnt to know that an entry can be
     evicted from dynamic table.  */
  nghttp3_pq min_cnts;
  /* krcnt is Known Received Count. */
  uint64_t krcnt;
  /* state is a current state of reading decoder stream. */
  nghttp3_qpack_decoder_stream_state state;
  /* opcode is a decoder stream opcode being processed. */
  nghttp3_qpack_decoder_stream_opcode opcode;
  /* rstate is a set of intermediate state which are used to process
     decoder stream. */
  nghttp3_qpack_read_state rstate;
  /* min_dtable_update is the minimum dynamic table size required. */
  size_t min_dtable_update;
  /* last_max_dtable_update is the dynamic table size last
     requested. */
  size_t last_max_dtable_update;
  /* uninterrupted_decoderlen is the number of bytes read from decoder
     stream without encoding a single field section. */
  size_t uninterrupted_decoderlen;
  /* flags is bitwise OR of zero or more of
     NGHTTP3_QPACK_ENCODER_FLAG_*. */
  uint8_t flags;
};

/*
 * nghttp3_qpack_encoder_init initializes |encoder|.
 * |hard_max_dtable_capacity| is the upper bound of the dynamic table
 * capacity.  |mem| is a memory allocator.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_init(nghttp3_qpack_encoder *encoder,
                               size_t hard_max_dtable_capacity,
                               const nghttp3_mem *mem);

/*
 * nghttp3_qpack_encoder_free frees memory allocated for |encoder|.
 * This function does not free memory pointed by |encoder|.
 */
void nghttp3_qpack_encoder_free(nghttp3_qpack_encoder *encoder);

/*
 * nghttp3_qpack_encoder_encode_nv encodes |nv|.  It writes request
 * stream into |rbuf| and writes encoder stream into |ebuf|.  |nv| is
 * a header field to encode.  |base| is base.  |allow_blocking| is
 * nonzero if this stream can be blocked (or it has been blocked
 * already).
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_encode_nv(nghttp3_qpack_encoder *encoder,
                                    uint64_t *pmax_cnt, uint64_t *pmin_cnt,
                                    nghttp3_buf *rbuf, nghttp3_buf *ebuf,
                                    const nghttp3_nv *nv, uint64_t base,
                                    int allow_blocking);

/* nghttp3_qpack_lookup_result stores a result of table lookup. */
typedef struct nghttp3_qpack_lookup_result {
  /* index is an index of matched entry.  -1 if no match is made. */
  nghttp3_ssize index;
  /* name_value_match is nonzero if both name and value are
     matched. */
  int name_value_match;
  /* pb_index is the absolute index of matched post-based dynamic
     table entry.  -1 if no such entry exists. */
  nghttp3_ssize pb_index;
} nghttp3_qpack_lookup_result;

/*
 * nghttp3_qpack_lookup_stable searches |nv| in static table.  |token|
 * is a token of nv->name and it is -1 if there is no corresponding
 * token defined.  |indexing_mode| provides indexing strategy.
 */
nghttp3_qpack_lookup_result
nghttp3_qpack_lookup_stable(const nghttp3_nv *nv, int32_t token,
                            nghttp3_qpack_indexing_mode indexing_mode);

/*
 * nghttp3_qpack_encoder_lookup_dtable searches |nv| in dynamic table.
 * |token| is a token of nv->name and it is -1 if there is no
 * corresponding token defined.  |hash| is a hash of nv->name.
 * |indexing_mode| provides indexing strategy.  |krcnt| is Known
 * Received Count.  |allow_blocking| is nonzero if this stream can be
 * blocked (or it has been blocked already).
 */
nghttp3_qpack_lookup_result nghttp3_qpack_encoder_lookup_dtable(
  nghttp3_qpack_encoder *encoder, const nghttp3_nv *nv, int32_t token,
  uint32_t hash, nghttp3_qpack_indexing_mode indexing_mode, uint64_t krcnt,
  int allow_blocking);

/*
 * nghttp3_qpack_encoder_write_field_section_prefix writes Encoded
 * Field Section Prefix into |pbuf|.  |ricnt| is Required Insert
 * Count.  |base| is Base.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_field_section_prefix(
  nghttp3_qpack_encoder *encoder, nghttp3_buf *pbuf, uint64_t ricnt,
  uint64_t base);

/*
 * nghttp3_qpack_encoder_write_static_indexed writes Indexed Header
 * Field to |rbuf|.  |absidx| is an absolute index into static table.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_static_indexed(nghttp3_qpack_encoder *encoder,
                                               nghttp3_buf *rbuf,
                                               uint64_t absidx);

/*
 * nghttp3_qpack_encoder_write_dynamic_indexed writes Indexed Header
 * Field to |rbuf|.  |absidx| is an absolute index into dynamic table.
 * |base| is base.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_dynamic_indexed(nghttp3_qpack_encoder *encoder,
                                                nghttp3_buf *rbuf,
                                                uint64_t absidx, uint64_t base);

/*
 * nghttp3_qpack_encoder_write_static_indexed writes Literal Header
 * Field With Name Reference to |rbuf|.  |absidx| is an absolute index
 * into static table to reference a name.  |nv| is a header field to
 * encode.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_static_indexed_name(
  nghttp3_qpack_encoder *encoder, nghttp3_buf *rbuf, uint64_t absidx,
  const nghttp3_nv *nv);

/*
 * nghttp3_qpack_encoder_write_dynamic_indexed writes Literal Header
 * Field With Name Reference to |rbuf|.  |absidx| is an absolute index
 * into dynamic table to reference a name.  |base| is a base.  |nv| is
 * a header field to encode.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_dynamic_indexed_name(
  nghttp3_qpack_encoder *encoder, nghttp3_buf *rbuf, uint64_t absidx,
  uint64_t base, const nghttp3_nv *nv);

/*
 * nghttp3_qpack_encoder_write_literal writes Literal Header Field
 * With Literal Name to |rbuf|.  |nv| is a header field to encode.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_literal(nghttp3_qpack_encoder *encoder,
                                        nghttp3_buf *rbuf,
                                        const nghttp3_nv *nv);

/*
 * nghttp3_qpack_encoder_write_static_insert writes Insert With Name
 * Reference to |ebuf|.  |absidx| is an absolute index into static
 * table to reference a name.  |nv| is a header field to insert.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_static_insert(nghttp3_qpack_encoder *encoder,
                                              nghttp3_buf *ebuf,
                                              uint64_t absidx,
                                              const nghttp3_nv *nv);

/*
 * nghttp3_qpack_encoder_write_dynamic_insert writes Insert With Name
 * Reference to |ebuf|.  |absidx| is an absolute index into dynamic
 * table to reference a name.  |nv| is a header field to insert.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_dynamic_insert(nghttp3_qpack_encoder *encoder,
                                               nghttp3_buf *ebuf,
                                               uint64_t absidx,
                                               const nghttp3_nv *nv);

/*
 * nghttp3_qpack_encoder_write_duplicate_insert writes Duplicate to
 * |ebuf|.  |absidx| is an absolute index into dynamic table to
 * reference an entry.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_duplicate_insert(nghttp3_qpack_encoder *encoder,
                                                 nghttp3_buf *ebuf,
                                                 uint64_t absidx);

/*
 * nghttp3_qpack_encoder_write_literal_insert writes Insert With
 * Literal Name to |ebuf|.  |nv| is a header field to insert.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_literal_insert(nghttp3_qpack_encoder *encoder,
                                               nghttp3_buf *ebuf,
                                               const nghttp3_nv *nv);

int nghttp3_qpack_encoder_stream_is_blocked(nghttp3_qpack_encoder *encoder,
                                            nghttp3_qpack_stream *stream);

/*
 * nghttp3_qpack_encoder_block_stream blocks |stream|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_block_stream(nghttp3_qpack_encoder *encoder,
                                       nghttp3_qpack_stream *stream);

/*
 * nghttp3_qpack_encoder_unblock_stream unblocks |stream|.
 */
void nghttp3_qpack_encoder_unblock_stream(nghttp3_qpack_encoder *encoder,
                                          nghttp3_qpack_stream *stream);

/*
 * nghttp3_qpack_encoder_unblock unblocks stream whose max_cnt is less
 * than or equal to |max_cnt|.
 */
void nghttp3_qpack_encoder_unblock(nghttp3_qpack_encoder *encoder,
                                   uint64_t max_cnt);

/*
 * nghttp3_qpack_encoder_find_stream returns stream whose stream ID is
 * |stream_id|.  This function returns NULL if there is no such
 * stream.
 */
nghttp3_qpack_stream *
nghttp3_qpack_encoder_find_stream(nghttp3_qpack_encoder *encoder,
                                  int64_t stream_id);

uint64_t nghttp3_qpack_encoder_get_min_cnt(nghttp3_qpack_encoder *encoder);

/*
 * nghttp3_qpack_encoder_shrink_dtable shrinks dynamic table so that
 * the dynamic table size is less than or equal to maximum size.
 */
void nghttp3_qpack_encoder_shrink_dtable(nghttp3_qpack_encoder *encoder);

/*
 * nghttp3_qpack_encoder_process_dtable_update processes pending
 * dynamic table size update.  It might write encoder stream into
 * |ebuf|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_process_dtable_update(nghttp3_qpack_encoder *encoder,
                                                nghttp3_buf *ebuf);

/*
 * nghttp3_qpack_encoder_write_set_dtable_cap writes Set Dynamic Table
 * Capacity. to |ebuf|.  |cap| is the capacity of dynamic table.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_write_set_dtable_cap(nghttp3_qpack_encoder *encoder,
                                               nghttp3_buf *ebuf, size_t cap);

/*
 * nghttp3_qpack_context_dtable_add adds |qnv| to dynamic table.  If
 * |ctx| is a part of encoder, |dtable_map| is not NULL.  |hash| is a
 * hash value of name.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_context_dtable_add(nghttp3_qpack_context *ctx,
                                     nghttp3_qpack_nv *qnv,
                                     nghttp3_qpack_map *dtable_map,
                                     uint32_t hash);

/*
 * nghttp3_qpack_encoder_dtable_static_add adds |nv| to dynamic table
 * by referencing static table entry at an absolute index |absidx|.
 * The hash of name is given as |hash|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_dtable_static_add(nghttp3_qpack_encoder *encoder,
                                            uint64_t absidx,
                                            const nghttp3_nv *nv,
                                            uint32_t hash);

/*
 * nghttp3_qpack_encoder_dtable_dynamic_add adds |nv| to dynamic table
 * by referencing dynamic table entry at an absolute index |absidx|.
 * The hash of name is given as |hash|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_dtable_dynamic_add(nghttp3_qpack_encoder *encoder,
                                             uint64_t absidx,
                                             const nghttp3_nv *nv,
                                             uint32_t hash);

/*
 * nghttp3_qpack_encoder_dtable_duplicate_add duplicates dynamic table
 * entry at an absolute index |absidx|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_encoder_dtable_duplicate_add(nghttp3_qpack_encoder *encoder,
                                               uint64_t absidx);

/*
 * nghttp3_qpack_encoder_dtable_literal_add adds |nv| to dynamic
 * table.  |token| is a token of name and is -1 if it has no token
 * value defined.  |hash| is a hash of name.
 *
 * NGHTTP3_ERR_NOMEM Out of memory.
 */
int nghttp3_qpack_encoder_dtable_literal_add(nghttp3_qpack_encoder *encoder,
                                             const nghttp3_nv *nv,
                                             int32_t token, uint32_t hash);

/*
 * `nghttp3_qpack_encoder_ack_header` tells |encoder| that header
 * block for a stream denoted by |stream_id| was acknowledged by
 * decoder.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * :macro:`NGHTTP3_ERR_QPACK_DECODER_STREAM_ERROR`
 *     Section Acknowledgement for a stream denoted by |stream_id| is
 *     unexpected.
 */
int nghttp3_qpack_encoder_ack_header(nghttp3_qpack_encoder *encoder,
                                     int64_t stream_id);

/*
 * `nghttp3_qpack_encoder_add_icnt` increments known received count of
 * |encoder| by |n|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * :macro:`NGHTTP3_ERR_NOMEM`
 *     Out of memory.
 * :macro:`NGHTTP3_ERR_QPACK_DECODER_STREAM_ERROR`
 *     |n| is too large.
 */
int nghttp3_qpack_encoder_add_icnt(nghttp3_qpack_encoder *encoder, uint64_t n);

/*
 * `nghttp3_qpack_encoder_cancel_stream` tells |encoder| that stream
 * denoted by |stream_id| is cancelled.  This function is provided for
 * debugging purpose only.  In HTTP/3, |encoder| knows this by reading
 * decoder stream with `nghttp3_qpack_encoder_read_decoder()`.
 */
void nghttp3_qpack_encoder_cancel_stream(nghttp3_qpack_encoder *encoder,
                                         int64_t stream_id);

/*
 * nghttp3_qpack_context_dtable_get returns dynamic table entry whose
 * absolute index is |absidx|.  This function assumes that such entry
 * exists.
 */
nghttp3_qpack_entry *
nghttp3_qpack_context_dtable_get(nghttp3_qpack_context *ctx, uint64_t absidx);

/*
 * nghttp3_qpack_context_dtable_top returns latest dynamic table
 * entry.  This function assumes dynamic table is not empty.
 */
nghttp3_qpack_entry *
nghttp3_qpack_context_dtable_top(nghttp3_qpack_context *ctx);

/*
 * nghttp3_qpack_entry_init initializes |ent|.  |qnv| is a header
 * field.  |sum| is the sum of table space occupied by all entries
 * inserted so far.  It does not include this entry.  |absidx| is an
 * absolute index of this entry.  |hash| is a hash of header field
 * name.  This function increases reference count of qnv->nv.name and
 * qnv->nv.value.
 */
void nghttp3_qpack_entry_init(nghttp3_qpack_entry *ent, nghttp3_qpack_nv *qnv,
                              size_t sum, uint64_t absidx, uint32_t hash);

/*
 * nghttp3_qpack_entry_free frees memory allocated for |ent|.
 */
void nghttp3_qpack_entry_free(nghttp3_qpack_entry *ent);

/*
 * nghttp3_qpack_put_varint_len returns the required number of bytes
 * to encode |n| with |prefix| bits.
 */
size_t nghttp3_qpack_put_varint_len(uint64_t n, size_t prefix);

/*
 * nghttp3_qpack_put_varint encodes |n| using variable integer
 * encoding with |prefix| bits into |buf|.  This function assumes the
 * buffer pointed by |buf| has enough space.  This function returns
 * the one byte beyond the last write (buf +
 * nghttp3_qpack_put_varint_len(n, prefix)).
 */
uint8_t *nghttp3_qpack_put_varint(uint8_t *buf, uint64_t n, size_t prefix);

/* nghttp3_qpack_encoder_stream_state is a set of states for encoder
   stream decoding. */
typedef enum nghttp3_qpack_encoder_stream_state {
  NGHTTP3_QPACK_ES_STATE_OPCODE,
  NGHTTP3_QPACK_ES_STATE_READ_INDEX,
  NGHTTP3_QPACK_ES_STATE_CHECK_NAME_HUFFMAN,
  NGHTTP3_QPACK_ES_STATE_READ_NAMELEN,
  NGHTTP3_QPACK_ES_STATE_READ_NAME_HUFFMAN,
  NGHTTP3_QPACK_ES_STATE_READ_NAME,
  NGHTTP3_QPACK_ES_STATE_CHECK_VALUE_HUFFMAN,
  NGHTTP3_QPACK_ES_STATE_READ_VALUELEN,
  NGHTTP3_QPACK_ES_STATE_READ_VALUE_HUFFMAN,
  NGHTTP3_QPACK_ES_STATE_READ_VALUE,
} nghttp3_qpack_encoder_stream_state;

/* nghttp3_qpack_encoder_stream_opcode is a set of opcodes used in
   encoder stream. */
typedef enum nghttp3_qpack_encoder_stream_opcode {
  NGHTTP3_QPACK_ES_OPCODE_INSERT_INDEXED,
  NGHTTP3_QPACK_ES_OPCODE_INSERT,
  NGHTTP3_QPACK_ES_OPCODE_DUPLICATE,
  NGHTTP3_QPACK_ES_OPCODE_SET_DTABLE_CAP,
} nghttp3_qpack_encoder_stream_opcode;

/* nghttp3_qpack_request_stream_state is a set of states for request
   stream decoding. */
typedef enum nghttp3_qpack_request_stream_state {
  NGHTTP3_QPACK_RS_STATE_RICNT,
  NGHTTP3_QPACK_RS_STATE_DBASE_SIGN,
  NGHTTP3_QPACK_RS_STATE_DBASE,
  NGHTTP3_QPACK_RS_STATE_OPCODE,
  NGHTTP3_QPACK_RS_STATE_READ_INDEX,
  NGHTTP3_QPACK_RS_STATE_CHECK_NAME_HUFFMAN,
  NGHTTP3_QPACK_RS_STATE_READ_NAMELEN,
  NGHTTP3_QPACK_RS_STATE_READ_NAME_HUFFMAN,
  NGHTTP3_QPACK_RS_STATE_READ_NAME,
  NGHTTP3_QPACK_RS_STATE_CHECK_VALUE_HUFFMAN,
  NGHTTP3_QPACK_RS_STATE_READ_VALUELEN,
  NGHTTP3_QPACK_RS_STATE_READ_VALUE_HUFFMAN,
  NGHTTP3_QPACK_RS_STATE_READ_VALUE,
  NGHTTP3_QPACK_RS_STATE_BLOCKED,
} nghttp3_qpack_request_stream_state;

/* nghttp3_qpack_request_stream_opcode is a set of opcodes used in
   request stream. */
typedef enum nghttp3_qpack_request_stream_opcode {
  NGHTTP3_QPACK_RS_OPCODE_INDEXED,
  NGHTTP3_QPACK_RS_OPCODE_INDEXED_PB,
  NGHTTP3_QPACK_RS_OPCODE_INDEXED_NAME,
  NGHTTP3_QPACK_RS_OPCODE_INDEXED_NAME_PB,
  NGHTTP3_QPACK_RS_OPCODE_LITERAL,
} nghttp3_qpack_request_stream_opcode;

struct nghttp3_qpack_decoder {
  nghttp3_qpack_context ctx;
  /* state is a current state of reading encoder stream. */
  nghttp3_qpack_encoder_stream_state state;
  /* opcode is an encoder stream opcode being processed. */
  nghttp3_qpack_encoder_stream_opcode opcode;
  /* rstate is a set of intermediate state which are used to process
     encoder stream. */
  nghttp3_qpack_read_state rstate;
  /* dbuf is decoder stream. */
  nghttp3_buf dbuf;
  /* written_icnt is Insert Count written to decoder stream so far. */
  uint64_t written_icnt;
  /* max_concurrent_streams is the number of concurrent streams that a
     remote endpoint can open, including both bidirectional and
     unidirectional streams which potentially receives QPACK encoded
     HEADER frame. */
  size_t max_concurrent_streams;
  /* uninterrupted_encoderlen is the number of bytes read from encoder
     stream without completing a single field section. */
  size_t uninterrupted_encoderlen;
};

/*
 * nghttp3_qpack_decoder_init initializes |decoder|.
 * |hard_max_dtable_capacity| is the upper bound of the dynamic table
 * capacity.  |max_blocked_streams| is the maximum number of stream
 * which can be blocked.  |mem| is a memory allocator.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 */
int nghttp3_qpack_decoder_init(nghttp3_qpack_decoder *decoder,
                               size_t hard_max_dtable_capacity,
                               size_t max_blocked_streams,
                               const nghttp3_mem *mem);

/*
 * nghttp3_qpack_decoder_free frees memory allocated for |decoder|.
 * This function does not free memory pointed by |decoder|.
 */
void nghttp3_qpack_decoder_free(nghttp3_qpack_decoder *decoder);

/*
 * nghttp3_qpack_decoder_dtable_indexed_add adds entry received in
 * Insert With Name Reference to dynamic table.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 * NGHTTP3_ERR_QPACK_ENCODER_STREAM
 *     Space required for a decoded entry exceeds max dynamic table
 *     size.
 */
int nghttp3_qpack_decoder_dtable_indexed_add(nghttp3_qpack_decoder *decoder);

/*
 * nghttp3_qpack_decoder_dtable_static_add adds entry received in
 * Insert With Name Reference (static) to dynamic table.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 * NGHTTP3_ERR_QPACK_ENCODER_STREAM
 *     Space required for a decoded entry exceeds max dynamic table
 *     size.
 */
int nghttp3_qpack_decoder_dtable_static_add(nghttp3_qpack_decoder *decoder);

/*
 * nghttp3_qpack_decoder_dtable_dynamic_add adds entry received in
 * Insert With Name Reference (dynamic) to dynamic table.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 * NGHTTP3_ERR_QPACK_ENCODER_STREAM
 *     Space required for a decoded entry exceeds max dynamic table
 *     size.
 */
int nghttp3_qpack_decoder_dtable_dynamic_add(nghttp3_qpack_decoder *decoder);

/*
 * nghttp3_qpack_decoder_dtable_duplicate_add adds entry received in
 * Duplicate to dynamic table.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 * NGHTTP3_ERR_QPACK_ENCODER_STREAM
 *     Space required for a decoded entry exceeds max dynamic table
 *     size.
 */
int nghttp3_qpack_decoder_dtable_duplicate_add(nghttp3_qpack_decoder *decoder);

/*
 * nghttp3_qpack_decoder_dtable_literal_add adds entry received in
 * Insert With Literal Name to dynamic table.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 * NGHTTP3_ERR_QPACK_ENCODER_STREAM
 *     Space required for a decoded entry exceeds max dynamic table
 *     size.
 */
int nghttp3_qpack_decoder_dtable_literal_add(nghttp3_qpack_decoder *decoder);

struct nghttp3_qpack_stream_context {
  /* rstate is a set of intermediate state which are used to process
     request stream. */
  nghttp3_qpack_read_state rstate;
  const nghttp3_mem *mem;
  int64_t stream_id;
  /* ricnt is Required Insert Count to decode this header block. */
  uint64_t ricnt;
  /* base is Base in Header Block Prefix. */
  uint64_t base;
  /* state is a current state of reading request stream. */
  nghttp3_qpack_request_stream_state state;
  /* opcode is a request stream opcode being processed. */
  nghttp3_qpack_request_stream_opcode opcode;
  /* dbase_sign is the delta base sign in Header Block Prefix. */
  int dbase_sign;
};

/*
 * nghttp3_qpack_stream_context_init initializes |sctx|.
 */
void nghttp3_qpack_stream_context_init(nghttp3_qpack_stream_context *sctx,
                                       int64_t stream_id,
                                       const nghttp3_mem *mem);

/*
 * nghttp3_qpack_stream_context_free frees memory allocated for
 * |sctx|.  This function does not free memory pointed by |sctx|.
 */
void nghttp3_qpack_stream_context_free(nghttp3_qpack_stream_context *sctx);

/*
 * nghttp3_qpack_decoder_reconstruct_ricnt reconstructs Required
 * Insert Count from the encoded form |encricnt| and stores Required
 * Insert Count in |*dest|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_QPACK_DECOMPRESSION_FAILED
 *     Unable to reconstruct Required Insert Count.
 */
int nghttp3_qpack_decoder_reconstruct_ricnt(nghttp3_qpack_decoder *decoder,
                                            uint64_t *dest, uint64_t encricnt);

/*
 * nghttp3_qpack_decoder_rel2abs converts relative index rstate->left
 * received in encoder stream to absolute index and stores it in
 * rstate->absidx.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_QPACK_ENCODER_STREAM
 *     Relative index is invalid.
 */
int nghttp3_qpack_decoder_rel2abs(nghttp3_qpack_decoder *decoder,
                                  nghttp3_qpack_read_state *rstate);

/*
 * nghttp3_qpack_decoder_brel2abs converts Base relative index
 * rstate->left received in request stream to absolute index and
 * stores it in rstate->absidx.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_QPACK_DECOMPRESSION_FAILED
 *     Base relative index is invalid.
 */
int nghttp3_qpack_decoder_brel2abs(nghttp3_qpack_decoder *decoder,
                                   nghttp3_qpack_stream_context *sctx);

/*
 * nghttp3_qpack_decoder_pbrel2abs converts Post-Base relative index
 * rstate->left received in request stream to absolute index and
 * stores it in rstate->absidx.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_QPACK_DECOMPRESSION_FAILED
 *     Post-Base relative index is invalid.
 */
int nghttp3_qpack_decoder_pbrel2abs(nghttp3_qpack_decoder *decoder,
                                    nghttp3_qpack_stream_context *sctx);

void nghttp3_qpack_decoder_emit_indexed(nghttp3_qpack_decoder *decoder,
                                        nghttp3_qpack_stream_context *sctx,
                                        nghttp3_qpack_nv *nv);

int nghttp3_qpack_decoder_emit_indexed_name(nghttp3_qpack_decoder *decoder,
                                            nghttp3_qpack_stream_context *sctx,
                                            nghttp3_qpack_nv *nv);

void nghttp3_qpack_decoder_emit_literal(nghttp3_qpack_decoder *decoder,
                                        nghttp3_qpack_stream_context *sctx,
                                        nghttp3_qpack_nv *nv);

/*
 * nghttp3_qpack_decoder_write_section_ack writes Section
 * Acknowledgement to decoder stream.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 * NGHTTP3_ERR_QPACK_FATAL
 *     Decoder stream overflow.
 */
int nghttp3_qpack_decoder_write_section_ack(
  nghttp3_qpack_decoder *decoder, const nghttp3_qpack_stream_context *sctx);

#endif /* !defined(NGHTTP3_QPACK_H) */
