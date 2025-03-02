// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/tracing/trace-buffer.h"

namespace v8 {
namespace platform {
namespace tracing {

TraceBufferRingBuffer::TraceBufferRingBuffer(size_t max_chunks,
                                             TraceWriter* trace_writer)
    : max_chunks_(max_chunks) {
  trace_writer_.reset(trace_writer);
  chunks_.resize(max_chunks);
}

TraceObject* TraceBufferRingBuffer::AddTraceEvent(uint64_t* handle) {
  base::SpinningMutexGuard guard(&mutex_);
  if (is_empty_ || chunks_[chunk_index_]->IsFull()) {
    chunk_index_ = is_empty_ ? 0 : NextChunkIndex(chunk_index_);
    is_empty_ = false;
    auto& chunk = chunks_[chunk_index_];
    if (chunk) {
      chunk->Reset(current_chunk_seq_++);
    } else {
      chunk.reset(new TraceBufferChunk(current_chunk_seq_++));
    }
  }
  auto& chunk = chunks_[chunk_index_];
  size_t event_index;
  TraceObject* trace_object = chunk->AddTraceEvent(&event_index);
  *handle = MakeHandle(chunk_index_, chunk->seq(), event_index);
  return trace_object;
}

TraceObject* TraceBufferRingBuffer::GetEventByHandle(uint64_t handle) {
  base::SpinningMutexGuard guard(&mutex_);
  size_t chunk_index, event_index;
  uint32_t chunk_seq;
  ExtractHandle(handle, &chunk_index, &chunk_seq, &event_index);
  if (chunk_index >= chunks_.size()) return nullptr;
  auto& chunk = chunks_[chunk_index];
  if (!chunk || chunk->seq() != chunk_seq) return nullptr;
  return chunk->GetEventAt(event_index);
}

bool TraceBufferRingBuffer::Flush() {
  base::SpinningMutexGuard guard(&mutex_);
  // This flushes all the traces stored in the buffer.
  if (!is_empty_) {
    for (size_t i = NextChunkIndex(chunk_index_);; i = NextChunkIndex(i)) {
      if (auto& chunk = chunks_[i]) {
        for (size_t j = 0; j < chunk->size(); ++j) {
          trace_writer_->AppendTraceEvent(chunk->GetEventAt(j));
        }
      }
      if (i == chunk_index_) break;
    }
  }
  trace_writer_->Flush();
  // This resets the trace buffer.
  is_empty_ = true;
  return true;
}

uint64_t TraceBufferRingBuffer::MakeHandle(size_t chunk_index,
                                           uint32_t chunk_seq,
                                           size_t event_index) const {
  return static_cast<uint64_t>(chunk_seq) * Capacity() +
         chunk_index * TraceBufferChunk::kChunkSize + event_index;
}

void TraceBufferRingBuffer::ExtractHandle(uint64_t handle, size_t* chunk_index,
                                          uint32_t* chunk_seq,
                                          size_t* event_index) const {
  *chunk_seq = static_cast<uint32_t>(handle / Capacity());
  size_t indices = handle % Capacity();
  *chunk_index = indices / TraceBufferChunk::kChunkSize;
  *event_index = indices % TraceBufferChunk::kChunkSize;
}

size_t TraceBufferRingBuffer::NextChunkIndex(size_t index) const {
  if (++index >= max_chunks_) index = 0;
  return index;
}

TraceBufferChunk::TraceBufferChunk(uint32_t seq) : seq_(seq) {}

void TraceBufferChunk::Reset(uint32_t new_seq) {
  next_free_ = 0;
  seq_ = new_seq;
}

TraceObject* TraceBufferChunk::AddTraceEvent(size_t* event_index) {
  *event_index = next_free_++;
  return &chunk_[*event_index];
}

TraceBuffer* TraceBuffer::CreateTraceBufferRingBuffer(
    size_t max_chunks, TraceWriter* trace_writer) {
  return new TraceBufferRingBuffer(max_chunks, trace_writer);
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
