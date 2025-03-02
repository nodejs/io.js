// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_BASE_BYTES_H_
#define V8_HEAP_BASE_BYTES_H_

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>

#include "src/base/platform/time.h"
#include "src/base/ring-buffer.h"

namespace heap::base {

struct BytesAndDuration final {
  constexpr BytesAndDuration() = default;
  constexpr BytesAndDuration(size_t bytes, v8::base::TimeDelta duration)
      : bytes(bytes), duration(duration) {}

  size_t bytes = 0;
  v8::base::TimeDelta duration;
};

using BytesAndDurationBuffer = v8::base::RingBuffer<BytesAndDuration>;

// Returns the average speed of events recorded in `buffer` including an
// `initial` event in Bytes/ms. If provided, `selected_duration` will bound the
// events considered (which uses the order of events in
// `BytesAndDurationBuffer`). The bounds are in Bytes/ms and can be used to
// bound non-zero speeds.
inline std::optional<double> AverageSpeed(
    const BytesAndDurationBuffer& buffer, const BytesAndDuration& initial,
    std::optional<v8::base::TimeDelta> selected_duration,
    size_t min_non_empty_speed = 0,
    size_t max_speed = std::numeric_limits<size_t>::max()) {
  const BytesAndDuration sum = buffer.Reduce(
      [selected_duration](const BytesAndDuration& a,
                          const BytesAndDuration& b) {
        if (selected_duration.has_value() &&
            a.duration >= selected_duration.value()) {
          return a;
        }
        return BytesAndDuration(a.bytes + b.bytes, a.duration + b.duration);
      },
      initial);
  const auto duration = sum.duration;
  if (duration.IsZero()) {
    return std::nullopt;
  }
  return std::max(
      std::min(static_cast<double>(sum.bytes) / duration.InMillisecondsF(),
               static_cast<double>(max_speed)),
      static_cast<double>(min_non_empty_speed));
}

class SmoothedBytesAndDuration {
 public:
  explicit SmoothedBytesAndDuration(v8::base::TimeDelta decay)
      : decay_(decay) {}

  void Update(BytesAndDuration bytes_and_duration) {
    if (bytes_and_duration.duration.IsZero()) {
      return;
    }
    const double new_throughput = bytes_and_duration.bytes /
                                  bytes_and_duration.duration.InMillisecondsF();
    throughput_ = new_throughput + Decay(throughput_ - new_throughput,
                                         bytes_and_duration.duration);
  }
  // Return throughput of memory (in bytes) over time (in millis).
  double GetThroughput() const { return throughput_; }

  // Returns throughput decayed as if `delay` passed.
  double GetThroughput(v8::base::TimeDelta delay) const {
    return Decay(throughput_, delay);
  }

 private:
  double Decay(double throughput, v8::base::TimeDelta delay) const {
    return throughput *
           exp2(-delay.InMillisecondsF() / decay_.InMillisecondsF());
  }

  double throughput_ = 0.0;
  const v8::base::TimeDelta decay_;
};

}  // namespace heap::base

#endif  // V8_HEAP_BASE_BYTES_H_
