/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Sliding window over the input data. */

#ifndef BROTLI_ENC_RINGBUFFER_H_
#define BROTLI_ENC_RINGBUFFER_H_

#include <string.h>  /* memcpy */

#include <brotli/types.h>

#include "../common/platform.h"
#include "memory.h"
#include "quality.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* A RingBuffer(window_bits, tail_bits) contains `1 << window_bits' bytes of
   data in a circular manner: writing a byte writes it to:
     `position() % (1 << window_bits)'.
   For convenience, the RingBuffer array contains another copy of the
   first `1 << tail_bits' bytes:
     buffer_[i] == buffer_[i + (1 << window_bits)], if i < (1 << tail_bits),
   and another copy of the last two bytes:
     buffer_[-1] == buffer_[(1 << window_bits) - 1] and
     buffer_[-2] == buffer_[(1 << window_bits) - 2]. */
typedef struct RingBuffer {
  /* Size of the ring-buffer is (1 << window_bits) + tail_size_. */
  const uint32_t size_;
  const uint32_t mask_;
  const uint32_t tail_size_;
  const uint32_t total_size_;

  uint32_t cur_size_;
  /* Position to write in the ring buffer. */
  uint32_t pos_;
  /* The actual ring buffer containing the copy of the last two bytes, the data,
     and the copy of the beginning as a tail. */
  uint8_t* data_;
  /* The start of the ring-buffer. */
  uint8_t* buffer_;
} RingBuffer;

static BROTLI_INLINE void RingBufferInit(RingBuffer* rb) {
  rb->cur_size_ = 0;
  rb->pos_ = 0;
  rb->data_ = 0;
  rb->buffer_ = 0;
}

static BROTLI_INLINE void RingBufferSetup(
    const BrotliEncoderParams* params, RingBuffer* rb) {
  int window_bits = ComputeRbBits(params);
  int tail_bits = params->lgblock;
  *(uint32_t*)&rb->size_ = 1u << window_bits;
  *(uint32_t*)&rb->mask_ = (1u << window_bits) - 1;
  *(uint32_t*)&rb->tail_size_ = 1u << tail_bits;
  *(uint32_t*)&rb->total_size_ = rb->size_ + rb->tail_size_;
}

static BROTLI_INLINE void RingBufferFree(MemoryManager* m, RingBuffer* rb) {
  BROTLI_FREE(m, rb->data_);
}

/* Allocates or re-allocates data_ to the given length + plus some slack
   region before and after. Fills the slack regions with zeros. */
static BROTLI_INLINE void RingBufferInitBuffer(
    MemoryManager* m, const uint32_t buflen, RingBuffer* rb) {
  static const size_t kSlackForEightByteHashingEverywhere = 7;
  uint8_t* new_data = BROTLI_ALLOC(
      m, uint8_t, 2 + buflen + kSlackForEightByteHashingEverywhere);
  size_t i;
  if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(new_data)) return;
  if (rb->data_) {
    memcpy(new_data, rb->data_,
        2 + rb->cur_size_ + kSlackForEightByteHashingEverywhere);
    BROTLI_FREE(m, rb->data_);
  }
  rb->data_ = new_data;
  rb->cur_size_ = buflen;
  rb->buffer_ = rb->data_ + 2;
  rb->buffer_[-2] = rb->buffer_[-1] = 0;
  for (i = 0; i < kSlackForEightByteHashingEverywhere; ++i) {
    rb->buffer_[rb->cur_size_ + i] = 0;
  }
}

static BROTLI_INLINE void RingBufferWriteTail(
    const uint8_t* bytes, size_t n, RingBuffer* rb) {
  const size_t masked_pos = rb->pos_ & rb->mask_;
  if (BROTLI_PREDICT_FALSE(masked_pos < rb->tail_size_)) {
    /* Just fill the tail buffer with the beginning data. */
    const size_t p = rb->size_ + masked_pos;
    memcpy(&rb->buffer_[p], bytes,
        BROTLI_MIN(size_t, n, rb->tail_size_ - masked_pos));
  }
}

/* Push bytes into the ring buffer. */
static BROTLI_INLINE void RingBufferWrite(
    MemoryManager* m, const uint8_t* bytes, size_t n, RingBuffer* rb) {
  if (rb->pos_ == 0 && n < rb->tail_size_) {
    /* Special case for the first write: to process the first block, we don't
       need to allocate the whole ring-buffer and we don't need the tail
       either. However, we do this memory usage optimization only if the
       first write is less than the tail size, which is also the input block
       size, otherwise it is likely that other blocks will follow and we
       will need to reallocate to the full size anyway. */
    rb->pos_ = (uint32_t)n;
    RingBufferInitBuffer(m, rb->pos_, rb);
    if (BROTLI_IS_OOM(m)) return;
    memcpy(rb->buffer_, bytes, n);
    return;
  }
  if (rb->cur_size_ < rb->total_size_) {
    /* Lazily allocate the full buffer. */
    RingBufferInitBuffer(m, rb->total_size_, rb);
    if (BROTLI_IS_OOM(m)) return;
    /* Initialize the last two bytes to zero, so that we don't have to worry
       later when we copy the last two bytes to the first two positions. */
    rb->buffer_[rb->size_ - 2] = 0;
    rb->buffer_[rb->size_ - 1] = 0;
    /* Initialize tail; might be touched by "best_len++" optimization when
       ring buffer is "full". */
    rb->buffer_[rb->size_] = 241;
  }
  {
    const size_t masked_pos = rb->pos_ & rb->mask_;
    /* The length of the writes is limited so that we do not need to worry
       about a write */
    RingBufferWriteTail(bytes, n, rb);
    if (BROTLI_PREDICT_TRUE(masked_pos + n <= rb->size_)) {
      /* A single write fits. */
      memcpy(&rb->buffer_[masked_pos], bytes, n);
    } else {
      /* Split into two writes.
         Copy into the end of the buffer, including the tail buffer. */
      memcpy(&rb->buffer_[masked_pos], bytes,
             BROTLI_MIN(size_t, n, rb->total_size_ - masked_pos));
      /* Copy into the beginning of the buffer */
      memcpy(&rb->buffer_[0], bytes + (rb->size_ - masked_pos),
             n - (rb->size_ - masked_pos));
    }
  }
  {
    BROTLI_BOOL not_first_lap = (rb->pos_ & (1u << 31)) != 0;
    uint32_t rb_pos_mask = (1u << 31) - 1;
    rb->buffer_[-2] = rb->buffer_[rb->size_ - 2];
    rb->buffer_[-1] = rb->buffer_[rb->size_ - 1];
    rb->pos_ = (rb->pos_ & rb_pos_mask) + (uint32_t)(n & rb_pos_mask);
    if (not_first_lap) {
      /* Wrap, but preserve not-a-first-lap feature. */
      rb->pos_ |= 1u << 31;
    }
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_RINGBUFFER_H_ */
