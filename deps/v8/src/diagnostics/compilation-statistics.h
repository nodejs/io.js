// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_COMPILATION_STATISTICS_H_
#define V8_DIAGNOSTICS_COMPILATION_STATISTICS_H_

#include <map>
#include <string>

#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

class OptimizedCompilationInfo;
class CompilationStatistics;

struct AsPrintableStatistics {
  const char* compiler;
  CompilationStatistics& s;
  const bool machine_output;
};

class CompilationStatistics final : public Malloced {
 public:
  CompilationStatistics() = default;
  CompilationStatistics(const CompilationStatistics&) = delete;
  CompilationStatistics& operator=(const CompilationStatistics&) = delete;

  class BasicStats {
   public:
    void Accumulate(const BasicStats& stats);

    std::string AsJSON();

    base::TimeDelta delta_;
    size_t total_allocated_bytes_ = 0;
    size_t max_allocated_bytes_ = 0;
    size_t absolute_max_allocated_bytes_ = 0;
    size_t input_graph_size_ = 0;
    size_t output_graph_size_ = 0;
    std::string function_name_;
  };

  void RecordPhaseStats(const char* phase_kind_name, const char* phase_name,
                        const BasicStats& stats);

  void RecordPhaseKindStats(const char* phase_kind_name,
                            const BasicStats& stats);

  void RecordTotalStats(const BasicStats& stats);

 private:
  class TotalStats : public BasicStats {
   public:
    TotalStats() : source_size_(0), count_(0) {}
    uint64_t source_size_;
    size_t count_;
  };

  class OrderedStats : public BasicStats {
   public:
    explicit OrderedStats(size_t insert_order) : insert_order_(insert_order) {}
    size_t insert_order_;
  };

  class PhaseStats : public OrderedStats {
   public:
    PhaseStats(size_t insert_order, const char* phase_kind_name)
        : OrderedStats(insert_order), phase_kind_name_(phase_kind_name) {}
    std::string phase_kind_name_;
  };

  friend std::ostream& operator<<(std::ostream& os,
                                  const AsPrintableStatistics& s);

  using PhaseKindStats = OrderedStats;
  using PhaseKindMap = std::map<std::string, PhaseKindStats>;
  using PhaseMap = std::map<std::string, PhaseStats>;

  TotalStats total_stats_;
  PhaseKindMap phase_kind_map_;
  PhaseMap phase_map_;
  base::SpinningMutex access_mutex_;
};

std::ostream& operator<<(std::ostream& os, const AsPrintableStatistics& s);

}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_COMPILATION_STATISTICS_H_
