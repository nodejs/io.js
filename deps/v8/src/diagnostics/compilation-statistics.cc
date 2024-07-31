// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/compilation-statistics.h"

#include <iomanip>
#include <ostream>
#include <vector>

#include "src/base/platform/platform.h"

namespace v8 {
namespace internal {

void CompilationStatistics::RecordPhaseStats(const char* phase_kind_name,
                                             const char* phase_name,
                                             const BasicStats& stats) {
  base::MutexGuard guard(&record_mutex_);

  std::string phase_name_str(phase_name);
  auto it = phase_map_.find(phase_name_str);
  if (it == phase_map_.end()) {
    PhaseStats phase_stats(phase_map_.size(), phase_kind_name);
    it = phase_map_.insert(std::make_pair(phase_name_str, phase_stats)).first;
  }
  it->second.Accumulate(stats);
}

void CompilationStatistics::RecordPhaseKindStats(const char* phase_kind_name,
                                                 const BasicStats& stats) {
  base::MutexGuard guard(&record_mutex_);

  std::string phase_kind_name_str(phase_kind_name);
  auto it = phase_kind_map_.find(phase_kind_name_str);
  if (it == phase_kind_map_.end()) {
    PhaseKindStats phase_kind_stats(phase_kind_map_.size());
    it = phase_kind_map_
             .insert(std::make_pair(phase_kind_name_str, phase_kind_stats))
             .first;
  }
  it->second.Accumulate(stats);
}

void CompilationStatistics::RecordTotalStats(const BasicStats& stats) {
  base::MutexGuard guard(&record_mutex_);
  total_stats_.Accumulate(stats);
  total_stats_.count_++;
}

void CompilationStatistics::BasicStats::Accumulate(const BasicStats& stats) {
  delta_ += stats.delta_;
  total_allocated_bytes_ += stats.total_allocated_bytes_;
  if (stats.absolute_max_allocated_bytes_ > absolute_max_allocated_bytes_) {
    absolute_max_allocated_bytes_ = stats.absolute_max_allocated_bytes_;
    max_allocated_bytes_ = stats.max_allocated_bytes_;
    function_name_ = stats.function_name_;
  }
  input_graph_size_ += stats.input_graph_size_;
  output_graph_size_ += stats.output_graph_size_;
}

std::string CompilationStatistics::BasicStats::AsJSON() {
// clang-format off
#define DICT(s) "{" << s << "}"
#define QUOTE(s) "\"" << s << "\""
#define MEMBER(s) QUOTE(s) << ":"

  DCHECK_EQ(function_name_.find("\""), std::string::npos);

  std::stringstream stream;
  stream << DICT(
    MEMBER("function_name") << QUOTE(function_name_) << ","
    MEMBER("total_allocated_bytes") << total_allocated_bytes_ << ","
    MEMBER("max_allocated_bytes") << max_allocated_bytes_ << ","
    MEMBER("absolute_max_allocated_bytes") << absolute_max_allocated_bytes_);

  return stream.str();

#undef DICT
#undef QUOTE
#undef MEMBER
  // clang-format on
}

static void WriteLine(std::ostream& os, bool machine_format, const char* name,
                      const char* compiler,
                      const CompilationStatistics::BasicStats& stats,
                      const CompilationStatistics::BasicStats& total_stats) {
  const size_t kBufferSize = 128;
  char buffer[kBufferSize];

  double ms = stats.delta_.InMillisecondsF();
  double percent = stats.delta_.PercentOf(total_stats.delta_);
  double size_percent =
      static_cast<double>(stats.total_allocated_bytes_ * 100) /
      static_cast<double>(total_stats.total_allocated_bytes_);
  double growth =
      static_cast<double>(stats.output_graph_size_) / stats.input_graph_size_;
  double mops_per_s = (stats.output_graph_size_ / 1000000.0) / (ms / 1000.0);

  if (machine_format) {
    base::OS::SNPrintF(buffer, kBufferSize,
                       "\"%s_%s_time\"=%.3f\n\"%s_%s_space\"=%zu", compiler,
                       name, ms, compiler, name, stats.total_allocated_bytes_);
    os << buffer;
  } else {
    if (stats.output_graph_size_ != 0) {
      base::OS::SNPrintF(
          buffer, kBufferSize,
          "%34s %10.3f (%4.1f%%)  %10zu (%4.1f%%) %10zu %10zu   %5.3f %6.2f",
          name, ms, percent, stats.total_allocated_bytes_, size_percent,
          stats.max_allocated_bytes_, stats.absolute_max_allocated_bytes_,
          growth, mops_per_s);
    } else {
      base::OS::SNPrintF(
          buffer, kBufferSize,
          "%34s %10.3f (%4.1f%%)  %10zu (%4.1f%%) %10zu %10zu               ",
          name, ms, percent, stats.total_allocated_bytes_, size_percent,
          stats.max_allocated_bytes_, stats.absolute_max_allocated_bytes_);
    }

    os << buffer;
    if (!stats.function_name_.empty()) {
      os << "  " << stats.function_name_.c_str();
    }
    os << '\n';
  }
}

static void WriteFullLine(std::ostream& os) {
  os << "-----------------------------------------------------------"
        "-----------------------------------------------------------\n";
}

static void WriteHeader(std::ostream& os, const char* compiler) {
  WriteFullLine(os);
  os << std::setw(24) << compiler << " phase            Time (ms)   "
     << "                   Space (bytes)            Growth MOps/s Function\n"
     << "                                                       "
     << "         Total         Max.     Abs. max.\n";
  WriteFullLine(os);
}

static void WritePhaseKindBreak(std::ostream& os) {
  os << "                                   ------------------------"
        "-----------------------------------------------------------\n";
}

std::ostream& operator<<(std::ostream& os, const AsPrintableStatistics& ps) {
  // phase_kind_map_ and phase_map_ don't get mutated, so store a bunch of
  // pointers into them.
  const CompilationStatistics& s = ps.s;

  using SortedPhaseKinds =
      std::vector<CompilationStatistics::PhaseKindMap::const_iterator>;
  SortedPhaseKinds sorted_phase_kinds(s.phase_kind_map_.size());
  for (auto it = s.phase_kind_map_.begin(); it != s.phase_kind_map_.end();
       ++it) {
    sorted_phase_kinds[it->second.insert_order_] = it;
  }

  using SortedPhases =
      std::vector<CompilationStatistics::PhaseMap::const_iterator>;
  SortedPhases sorted_phases(s.phase_map_.size());
  for (auto it = s.phase_map_.begin(); it != s.phase_map_.end(); ++it) {
    sorted_phases[it->second.insert_order_] = it;
  }

  if (!ps.machine_output) WriteHeader(os, ps.compiler);
  for (const auto& phase_kind_it : sorted_phase_kinds) {
    const auto& phase_kind_name = phase_kind_it->first;
    if (!ps.machine_output) {
      for (const auto& phase_it : sorted_phases) {
        const auto& phase_stats = phase_it->second;
        if (phase_stats.phase_kind_name_ != phase_kind_name) continue;
        const auto& phase_name = phase_it->first;
        WriteLine(os, ps.machine_output, phase_name.c_str(), ps.compiler,
                  phase_stats, s.total_stats_);
      }
      WritePhaseKindBreak(os);
    }
    const auto& phase_kind_stats = phase_kind_it->second;
    WriteLine(os, ps.machine_output, phase_kind_name.c_str(), ps.compiler,
              phase_kind_stats, s.total_stats_);
    os << '\n';
  }

  if (!ps.machine_output) WriteFullLine(os);
  WriteLine(os, ps.machine_output, "totals", ps.compiler, s.total_stats_,
            s.total_stats_);

  if (ps.machine_output) {
    os << '\n';
    os << "\"" << ps.compiler << "_totals_count\"=" << s.total_stats_.count_;
  }
  return os;
}

}  // namespace internal
}  // namespace v8
