/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
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
#include "ngtcp2_strm.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_rtb.h"
#include "ngtcp2_pkt.h"
#include "ngtcp2_vec.h"
#include "ngtcp2_frame_chain.h"

void ngtcp2_strm_init(ngtcp2_strm *strm, int64_t stream_id, uint32_t flags,
                      uint64_t max_rx_offset, uint64_t max_tx_offset,
                      void *stream_user_data, ngtcp2_objalloc *frc_objalloc,
                      const ngtcp2_mem *mem) {
  strm->pe.index = NGTCP2_PQ_BAD_INDEX;
  strm->cycle = 0;
  strm->frc_objalloc = frc_objalloc;
  strm->tx.acked_offset = NULL;
  strm->tx.cont_acked_offset = 0;
  strm->tx.streamfrq = NULL;
  strm->tx.offset = 0;
  strm->tx.max_offset = max_tx_offset;
  strm->tx.last_blocked_offset = UINT64_MAX;
  strm->tx.last_max_stream_data_ts = UINT64_MAX;
  strm->tx.loss_count = 0;
  strm->tx.last_lost_pkt_num = -1;
  strm->tx.stop_sending_app_error_code = 0;
  strm->tx.reset_stream_app_error_code = 0;
  strm->rx.rob = NULL;
  strm->rx.cont_offset = 0;
  strm->rx.last_offset = 0;
  strm->rx.max_offset = strm->rx.unsent_max_offset = strm->rx.window =
    max_rx_offset;
  strm->mem = mem;
  strm->stream_id = stream_id;
  strm->stream_user_data = stream_user_data;
  strm->flags = flags;
  strm->app_error_code = 0;
}

void ngtcp2_strm_free(ngtcp2_strm *strm) {
  ngtcp2_ksl_it it;

  if (strm == NULL) {
    return;
  }

  if (strm->tx.streamfrq) {
    for (it = ngtcp2_ksl_begin(strm->tx.streamfrq); !ngtcp2_ksl_it_end(&it);
         ngtcp2_ksl_it_next(&it)) {
      ngtcp2_frame_chain_objalloc_del(ngtcp2_ksl_it_get(&it),
                                      strm->frc_objalloc, strm->mem);
    }

    ngtcp2_ksl_free(strm->tx.streamfrq);
    ngtcp2_mem_free(strm->mem, strm->tx.streamfrq);
  }

  if (strm->rx.rob) {
    ngtcp2_rob_free(strm->rx.rob);
    ngtcp2_mem_free(strm->mem, strm->rx.rob);
  }

  if (strm->tx.acked_offset) {
    ngtcp2_gaptr_free(strm->tx.acked_offset);
    ngtcp2_mem_free(strm->mem, strm->tx.acked_offset);
  }
}

static int strm_rob_init(ngtcp2_strm *strm) {
  int rv;
  ngtcp2_rob *rob = ngtcp2_mem_malloc(strm->mem, sizeof(*rob));

  if (rob == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  rv = ngtcp2_rob_init(rob, 8 * 1024, strm->mem);
  if (rv != 0) {
    ngtcp2_mem_free(strm->mem, rob);
    return rv;
  }

  strm->rx.rob = rob;

  return 0;
}

uint64_t ngtcp2_strm_rx_offset(const ngtcp2_strm *strm) {
  if (strm->rx.rob == NULL) {
    return strm->rx.cont_offset;
  }
  return ngtcp2_rob_first_gap_offset(strm->rx.rob);
}

/* strm_rob_heavily_fragmented returns nonzero if the number of gaps
   in |rob| exceeds the limit. */
static int strm_rob_heavily_fragmented(const ngtcp2_rob *rob) {
  return ngtcp2_ksl_len(&rob->gapksl) >= 5000;
}

int ngtcp2_strm_recv_reordering(ngtcp2_strm *strm, const uint8_t *data,
                                size_t datalen, uint64_t offset) {
  int rv;

  if (strm->rx.rob == NULL) {
    rv = strm_rob_init(strm);
    if (rv != 0) {
      return rv;
    }

    if (strm->rx.cont_offset) {
      ngtcp2_rob_remove_prefix(strm->rx.rob, strm->rx.cont_offset);
    }
  }

  if (strm_rob_heavily_fragmented(strm->rx.rob)) {
    return NGTCP2_ERR_INTERNAL;
  }

  return ngtcp2_rob_push(strm->rx.rob, offset, data, datalen);
}

void ngtcp2_strm_update_rx_offset(ngtcp2_strm *strm, uint64_t offset) {
  if (strm->rx.rob == NULL) {
    strm->rx.cont_offset = offset;
    return;
  }

  ngtcp2_rob_remove_prefix(strm->rx.rob, offset);
}

void ngtcp2_strm_discard_reordered_data(ngtcp2_strm *strm) {
  if (strm->rx.rob == NULL) {
    return;
  }

  strm->rx.cont_offset = ngtcp2_strm_rx_offset(strm);

  ngtcp2_rob_free(strm->rx.rob);
  ngtcp2_mem_free(strm->mem, strm->rx.rob);
  strm->rx.rob = NULL;
}

void ngtcp2_strm_shutdown(ngtcp2_strm *strm, uint32_t flags) {
  strm->flags |= flags & NGTCP2_STRM_FLAG_SHUT_RDWR;
}

static int strm_streamfrq_init(ngtcp2_strm *strm) {
  ngtcp2_ksl *streamfrq = ngtcp2_mem_malloc(strm->mem, sizeof(*streamfrq));
  if (streamfrq == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_ksl_init(streamfrq, ngtcp2_ksl_uint64_less,
                  ngtcp2_ksl_uint64_less_search, sizeof(uint64_t), strm->mem);

  strm->tx.streamfrq = streamfrq;

  return 0;
}

int ngtcp2_strm_streamfrq_push(ngtcp2_strm *strm, ngtcp2_frame_chain *frc) {
  int rv;

  assert(frc->fr.type == NGTCP2_FRAME_STREAM ||
         frc->fr.type == NGTCP2_FRAME_CRYPTO);
  assert(frc->next == NULL);

  if (strm->tx.streamfrq == NULL) {
    rv = strm_streamfrq_init(strm);
    if (rv != 0) {
      return rv;
    }
  }

  return ngtcp2_ksl_insert(strm->tx.streamfrq, NULL, &frc->fr.stream.offset,
                           frc);
}

static int strm_streamfrq_unacked_pop(ngtcp2_strm *strm,
                                      ngtcp2_frame_chain **pfrc) {
  ngtcp2_frame_chain *frc, *nfrc;
  ngtcp2_stream *fr, *nfr;
  uint64_t offset, end_offset;
  size_t idx, end_idx;
  uint64_t base_offset, end_base_offset;
  ngtcp2_range gap;
  ngtcp2_vec *v;
  int rv;
  ngtcp2_ksl_it it;

  *pfrc = NULL;

  assert(strm->tx.streamfrq);
  assert(ngtcp2_ksl_len(strm->tx.streamfrq));

  for (it = ngtcp2_ksl_begin(strm->tx.streamfrq); !ngtcp2_ksl_it_end(&it);) {
    frc = ngtcp2_ksl_it_get(&it);
    fr = &frc->fr.stream;

    ngtcp2_ksl_remove_hint(strm->tx.streamfrq, &it, &it, &fr->offset);

    idx = 0;
    offset = fr->offset;
    base_offset = 0;

    gap = ngtcp2_strm_get_unacked_range_after(strm, offset);
    if (gap.begin < offset) {
      gap.begin = offset;
    }

    for (; idx < fr->datacnt && offset < gap.begin; ++idx) {
      v = &fr->data[idx];
      if (offset + v->len > gap.begin) {
        base_offset = gap.begin - offset;
        break;
      }

      offset += v->len;
    }

    if (idx == fr->datacnt) {
      if (fr->fin) {
        if (strm->flags & NGTCP2_STRM_FLAG_FIN_ACKED) {
          ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
          assert(ngtcp2_ksl_len(strm->tx.streamfrq) == 0);
          return 0;
        }

        fr->offset += ngtcp2_vec_len(fr->data, fr->datacnt);
        fr->datacnt = 0;

        *pfrc = frc;

        return 0;
      }

      if (fr->offset == 0 && fr->datacnt == 0 && strm->tx.offset == 0 &&
          !(strm->flags & NGTCP2_STRM_FLAG_ANY_ACKED)) {
        *pfrc = frc;

        return 0;
      }

      ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);

      continue;
    }

    assert(gap.begin == offset + base_offset);

    end_idx = idx;
    end_offset = offset;
    end_base_offset = 0;

    for (; end_idx < fr->datacnt; ++end_idx) {
      v = &fr->data[end_idx];
      if (end_offset + v->len > gap.end) {
        end_base_offset = gap.end - end_offset;
        break;
      }

      end_offset += v->len;
    }

    if (fr->offset == offset && base_offset == 0 && fr->datacnt == end_idx) {
      *pfrc = frc;
      return 0;
    }

    if (fr->datacnt == end_idx) {
      memmove(fr->data, fr->data + idx, sizeof(fr->data[0]) * (end_idx - idx));

      assert(fr->data[0].len > base_offset);

      fr->offset = offset + base_offset;
      fr->datacnt = end_idx - idx;
      fr->data[0].base += base_offset;
      fr->data[0].len -= (size_t)base_offset;

      *pfrc = frc;

      return 0;
    }

    rv = ngtcp2_frame_chain_stream_datacnt_objalloc_new(
      &nfrc, fr->datacnt - end_idx, strm->frc_objalloc, strm->mem);
    if (rv != 0) {
      ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
      return rv;
    }

    nfr = &nfrc->fr.stream;
    memcpy(nfr->data, fr->data + end_idx,
           sizeof(nfr->data[0]) * (fr->datacnt - end_idx));

    assert(nfr->data[0].len > end_base_offset);

    nfr->type = fr->type;
    nfr->flags = 0;
    nfr->fin = fr->fin;
    nfr->stream_id = fr->stream_id;
    nfr->offset = end_offset + end_base_offset;
    nfr->datacnt = fr->datacnt - end_idx;
    nfr->data[0].base += end_base_offset;
    nfr->data[0].len -= (size_t)end_base_offset;

    rv = ngtcp2_ksl_insert(strm->tx.streamfrq, NULL, &nfr->offset, nfrc);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      ngtcp2_frame_chain_objalloc_del(nfrc, strm->frc_objalloc, strm->mem);
      ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);

      return rv;
    }

    if (end_base_offset) {
      ++end_idx;
    }

    memmove(fr->data, fr->data + idx, sizeof(fr->data[0]) * (end_idx - idx));

    assert(fr->data[0].len > base_offset);

    fr->fin = 0;
    fr->offset = offset + base_offset;
    fr->datacnt = end_idx - idx;

    if (end_base_offset) {
      assert(fr->data[fr->datacnt - 1].len > end_base_offset);
      fr->data[fr->datacnt - 1].len = (size_t)end_base_offset;
    }

    fr->data[0].base += base_offset;
    fr->data[0].len -= (size_t)base_offset;

    *pfrc = frc;

    return 0;
  }

  return 0;
}

int ngtcp2_strm_streamfrq_pop(ngtcp2_strm *strm, ngtcp2_frame_chain **pfrc,
                              size_t left) {
  ngtcp2_stream *fr, *nfr;
  ngtcp2_frame_chain *frc, *nfrc, *sfrc;
  int rv;
  size_t nmerged;
  uint64_t datalen;
  ngtcp2_vec a[NGTCP2_MAX_STREAM_DATACNT];
  ngtcp2_vec b[NGTCP2_MAX_STREAM_DATACNT];
  size_t acnt, bcnt;
  uint64_t unacked_offset;

  if (strm->tx.streamfrq == NULL || ngtcp2_ksl_len(strm->tx.streamfrq) == 0) {
    *pfrc = NULL;
    return 0;
  }

  rv = strm_streamfrq_unacked_pop(strm, &frc);
  if (rv != 0) {
    return rv;
  }

  if (frc == NULL) {
    *pfrc = NULL;
    return 0;
  }

  fr = &frc->fr.stream;
  datalen = ngtcp2_vec_len(fr->data, fr->datacnt);

  /* datalen could be zero if 0 length STREAM has been sent */
  if (left == 0 && datalen) {
    rv = ngtcp2_ksl_insert(strm->tx.streamfrq, NULL, &fr->offset, frc);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
      return rv;
    }

    *pfrc = NULL;

    return 0;
  }

  if (datalen > left) {
    ngtcp2_vec_copy(a, fr->data, fr->datacnt);
    acnt = fr->datacnt;

    bcnt = 0;
    ngtcp2_vec_split(b, &bcnt, a, &acnt, left, NGTCP2_MAX_STREAM_DATACNT);

    assert(acnt > 0);
    assert(bcnt > 0);

    rv = ngtcp2_frame_chain_stream_datacnt_objalloc_new(
      &nfrc, bcnt, strm->frc_objalloc, strm->mem);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
      return rv;
    }

    nfr = &nfrc->fr.stream;
    nfr->type = fr->type;
    nfr->flags = 0;
    nfr->fin = fr->fin;
    nfr->stream_id = fr->stream_id;
    nfr->offset = fr->offset + left;
    nfr->datacnt = bcnt;
    ngtcp2_vec_copy(nfr->data, b, bcnt);

    rv = ngtcp2_ksl_insert(strm->tx.streamfrq, NULL, &nfr->offset, nfrc);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      ngtcp2_frame_chain_objalloc_del(nfrc, strm->frc_objalloc, strm->mem);
      ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);

      return rv;
    }

    rv = ngtcp2_frame_chain_stream_datacnt_objalloc_new(
      &nfrc, acnt, strm->frc_objalloc, strm->mem);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
      return rv;
    }

    nfr = &nfrc->fr.stream;
    *nfr = *fr;
    nfr->fin = 0;
    nfr->datacnt = acnt;
    ngtcp2_vec_copy(nfr->data, a, acnt);

    ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);

    *pfrc = nfrc;

    return 0;
  }

  left -= (size_t)datalen;

  ngtcp2_vec_copy(a, fr->data, fr->datacnt);
  acnt = fr->datacnt;

  for (; left && ngtcp2_ksl_len(strm->tx.streamfrq);) {
    unacked_offset = ngtcp2_strm_streamfrq_unacked_offset(strm);
    if (unacked_offset != fr->offset + datalen) {
      assert(fr->offset + datalen < unacked_offset);
      break;
    }

    rv = strm_streamfrq_unacked_pop(strm, &nfrc);
    if (rv != 0) {
      assert(ngtcp2_err_is_fatal(rv));
      ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
      return rv;
    }
    if (nfrc == NULL) {
      break;
    }

    nfr = &nfrc->fr.stream;

    if (nfr->fin && nfr->datacnt == 0) {
      fr->fin = 1;
      ngtcp2_frame_chain_objalloc_del(nfrc, strm->frc_objalloc, strm->mem);
      break;
    }

    bcnt = nfr->datacnt;

    nmerged = ngtcp2_vec_merge(a, &acnt, nfr->data, &bcnt, left,
                               NGTCP2_MAX_STREAM_DATACNT);
    if (nmerged == 0) {
      rv = ngtcp2_ksl_insert(strm->tx.streamfrq, NULL, &nfr->offset, nfrc);
      if (rv != 0) {
        assert(ngtcp2_err_is_fatal(rv));
        ngtcp2_frame_chain_objalloc_del(nfrc, strm->frc_objalloc, strm->mem);
        ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);

        return rv;
      }

      break;
    }

    datalen += nmerged;
    left -= nmerged;

    if (bcnt == 0) {
      fr->fin = nfr->fin;
      ngtcp2_frame_chain_objalloc_del(nfrc, strm->frc_objalloc, strm->mem);
      continue;
    }

    if (nfr->datacnt <= NGTCP2_FRAME_CHAIN_STREAM_DATACNT_THRES ||
        bcnt > NGTCP2_FRAME_CHAIN_STREAM_DATACNT_THRES) {
      nfr->offset += nmerged;
      nfr->datacnt = bcnt;

      rv = ngtcp2_ksl_insert(strm->tx.streamfrq, NULL, &nfr->offset, nfrc);
      if (rv != 0) {
        ngtcp2_frame_chain_objalloc_del(nfrc, strm->frc_objalloc, strm->mem);
        ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
        return rv;
      }
    } else {
      rv = ngtcp2_frame_chain_stream_datacnt_objalloc_new(
        &sfrc, bcnt, strm->frc_objalloc, strm->mem);
      if (rv != 0) {
        ngtcp2_frame_chain_objalloc_del(nfrc, strm->frc_objalloc, strm->mem);
        ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
        return rv;
      }

      sfrc->fr.stream = nfrc->fr.stream;
      sfrc->fr.stream.offset += nmerged;
      sfrc->fr.stream.datacnt = bcnt;
      ngtcp2_vec_copy(sfrc->fr.stream.data, nfrc->fr.stream.data, bcnt);

      ngtcp2_frame_chain_objalloc_del(nfrc, strm->frc_objalloc, strm->mem);

      rv = ngtcp2_ksl_insert(strm->tx.streamfrq, NULL, &sfrc->fr.stream.offset,
                             sfrc);
      if (rv != 0) {
        ngtcp2_frame_chain_objalloc_del(sfrc, strm->frc_objalloc, strm->mem);
        ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
        return rv;
      }
    }

    break;
  }

  if (acnt == fr->datacnt) {
    if (acnt > 0) {
      fr->data[acnt - 1] = a[acnt - 1];
    }

    *pfrc = frc;

    return 0;
  }

  assert(acnt > fr->datacnt);

  rv = ngtcp2_frame_chain_stream_datacnt_objalloc_new(
    &nfrc, acnt, strm->frc_objalloc, strm->mem);
  if (rv != 0) {
    ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
    return rv;
  }

  nfr = &nfrc->fr.stream;
  *nfr = *fr;
  nfr->datacnt = acnt;
  ngtcp2_vec_copy(nfr->data, a, acnt);

  ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);

  *pfrc = nfrc;

  return 0;
}

uint64_t ngtcp2_strm_streamfrq_unacked_offset(const ngtcp2_strm *strm) {
  ngtcp2_frame_chain *frc;
  ngtcp2_stream *fr;
  ngtcp2_range gap;
  ngtcp2_ksl_it it;
  uint64_t datalen;

  assert(strm->tx.streamfrq);
  assert(ngtcp2_ksl_len(strm->tx.streamfrq));

  for (it = ngtcp2_ksl_begin(strm->tx.streamfrq); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    frc = ngtcp2_ksl_it_get(&it);
    fr = &frc->fr.stream;

    gap = ngtcp2_strm_get_unacked_range_after(strm, fr->offset);

    datalen = ngtcp2_vec_len(fr->data, fr->datacnt);

    if (gap.begin <= fr->offset) {
      return fr->offset;
    }

    if (gap.begin < fr->offset + datalen) {
      return gap.begin;
    }

    if (fr->offset + datalen == gap.begin && fr->fin &&
        !(strm->flags & NGTCP2_STRM_FLAG_FIN_ACKED)) {
      return fr->offset + datalen;
    }
  }

  return (uint64_t)-1;
}

ngtcp2_frame_chain *ngtcp2_strm_streamfrq_top(const ngtcp2_strm *strm) {
  ngtcp2_ksl_it it;

  assert(strm->tx.streamfrq);
  assert(ngtcp2_ksl_len(strm->tx.streamfrq));

  it = ngtcp2_ksl_begin(strm->tx.streamfrq);

  return ngtcp2_ksl_it_get(&it);
}

int ngtcp2_strm_streamfrq_empty(const ngtcp2_strm *strm) {
  return strm->tx.streamfrq == NULL || ngtcp2_ksl_len(strm->tx.streamfrq) == 0;
}

void ngtcp2_strm_streamfrq_clear(ngtcp2_strm *strm) {
  ngtcp2_frame_chain *frc;
  ngtcp2_ksl_it it;

  if (strm->tx.streamfrq == NULL) {
    return;
  }

  for (it = ngtcp2_ksl_begin(strm->tx.streamfrq); !ngtcp2_ksl_it_end(&it);
       ngtcp2_ksl_it_next(&it)) {
    frc = ngtcp2_ksl_it_get(&it);
    ngtcp2_frame_chain_objalloc_del(frc, strm->frc_objalloc, strm->mem);
  }

  ngtcp2_ksl_clear(strm->tx.streamfrq);
}

int ngtcp2_strm_is_tx_queued(const ngtcp2_strm *strm) {
  return strm->pe.index != NGTCP2_PQ_BAD_INDEX;
}

int ngtcp2_strm_is_all_tx_data_acked(const ngtcp2_strm *strm) {
  if (strm->tx.acked_offset == NULL) {
    return strm->tx.cont_acked_offset == strm->tx.offset;
  }

  return ngtcp2_gaptr_first_gap_offset(strm->tx.acked_offset) ==
         strm->tx.offset;
}

int ngtcp2_strm_is_all_tx_data_fin_acked(const ngtcp2_strm *strm) {
  return (strm->flags & NGTCP2_STRM_FLAG_FIN_ACKED) &&
         ngtcp2_strm_is_all_tx_data_acked(strm);
}

ngtcp2_range ngtcp2_strm_get_unacked_range_after(const ngtcp2_strm *strm,
                                                 uint64_t offset) {
  ngtcp2_range gap;

  if (strm->tx.acked_offset == NULL) {
    gap.begin = strm->tx.cont_acked_offset;
    gap.end = UINT64_MAX;
    return gap;
  }

  return ngtcp2_gaptr_get_first_gap_after(strm->tx.acked_offset, offset);
}

uint64_t ngtcp2_strm_get_acked_offset(const ngtcp2_strm *strm) {
  if (strm->tx.acked_offset == NULL) {
    return strm->tx.cont_acked_offset;
  }

  return ngtcp2_gaptr_first_gap_offset(strm->tx.acked_offset);
}

static int strm_acked_offset_init(ngtcp2_strm *strm) {
  ngtcp2_gaptr *acked_offset =
    ngtcp2_mem_malloc(strm->mem, sizeof(*acked_offset));

  if (acked_offset == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_gaptr_init(acked_offset, strm->mem);

  strm->tx.acked_offset = acked_offset;

  return 0;
}

int ngtcp2_strm_ack_data(ngtcp2_strm *strm, uint64_t offset, uint64_t len) {
  int rv;

  if (strm->tx.acked_offset == NULL) {
    if (strm->tx.cont_acked_offset == offset) {
      strm->tx.cont_acked_offset += len;
      return 0;
    }

    rv = strm_acked_offset_init(strm);
    if (rv != 0) {
      return rv;
    }

    rv =
      ngtcp2_gaptr_push(strm->tx.acked_offset, 0, strm->tx.cont_acked_offset);
    if (rv != 0) {
      return rv;
    }
  }

  return ngtcp2_gaptr_push(strm->tx.acked_offset, offset, len);
}

void ngtcp2_strm_set_app_error_code(ngtcp2_strm *strm,
                                    uint64_t app_error_code) {
  if (strm->flags & NGTCP2_STRM_FLAG_APP_ERROR_CODE_SET) {
    return;
  }

  assert(0 == strm->app_error_code);

  strm->flags |= NGTCP2_STRM_FLAG_APP_ERROR_CODE_SET;
  strm->app_error_code = app_error_code;
}

int ngtcp2_strm_require_retransmit_reset_stream(const ngtcp2_strm *strm) {
  return !ngtcp2_strm_is_all_tx_data_fin_acked(strm);
}

int ngtcp2_strm_require_retransmit_stop_sending(const ngtcp2_strm *strm) {
  return !(strm->flags & NGTCP2_STRM_FLAG_SHUT_RD) ||
         ngtcp2_strm_rx_offset(strm) != strm->rx.last_offset;
}

int ngtcp2_strm_require_retransmit_max_stream_data(
  const ngtcp2_strm *strm, const ngtcp2_max_stream_data *fr) {
  return fr->max_stream_data == strm->rx.max_offset &&
         !(strm->flags &
           (NGTCP2_STRM_FLAG_SHUT_RD | NGTCP2_STRM_FLAG_STOP_SENDING));
}

int ngtcp2_strm_require_retransmit_stream_data_blocked(
  const ngtcp2_strm *strm, const ngtcp2_stream_data_blocked *fr) {
  return fr->offset == strm->tx.max_offset &&
         !(strm->flags & NGTCP2_STRM_FLAG_SHUT_WR);
}
