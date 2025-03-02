// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TRACING_TRACE_BUFFER_H_
#define V8_LIBPLATFORM_TRACING_TRACE_BUFFER_H_

#include <memory>
#include <vector>

#include "include/libplatform/v8-tracing.h"
#include "src/base/platform/mutex.h"

namespace v8 {
namespace platform {
namespace tracing {

class TraceBufferRingBuffer : public TraceBuffer {
 public:
  // Takes ownership of |trace_writer|.
  TraceBufferRingBuffer(size_t max_chunks, TraceWriter* trace_writer);
  ~TraceBufferRingBuffer() override = default;

  TraceObject* AddTraceEvent(uint64_t* handle) override;
  TraceObject* GetEventByHandle(uint64_t handle) override;
  bool Flush() override;

 private:
  uint64_t MakeHandle(size_t chunk_index, uint32_t chunk_seq,
                      size_t event_index) const;
  void ExtractHandle(uint64_t handle, size_t* chunk_index, uint32_t* chunk_seq,
                     size_t* event_index) const;
  size_t Capacity() const { return max_chunks_ * TraceBufferChunk::kChunkSize; }
  size_t NextChunkIndex(size_t index) const;

  mutable base::SpinningMutex mutex_;
  size_t max_chunks_;
  std::unique_ptr<TraceWriter> trace_writer_;
  std::vector<std::unique_ptr<TraceBufferChunk>> chunks_;
  size_t chunk_index_;
  bool is_empty_ = true;
  uint32_t current_chunk_seq_ = 1;
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_TRACE_BUFFER_H_
