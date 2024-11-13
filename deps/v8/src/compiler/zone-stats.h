// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ZONE_STATS_H_
#define V8_COMPILER_ZONE_STATS_H_

#include <map>
#include <vector>

#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

class V8_EXPORT_PRIVATE ZoneStats final {
 public:
  class V8_NODISCARD Scope final {
   public:
    explicit Scope(ZoneStats* zone_stats, const char* zone_name,
                   bool support_zone_compression = false)
        : zone_name_(zone_name),
          zone_stats_(zone_stats),
          zone_(nullptr),
          support_zone_compression_(support_zone_compression) {}
    ~Scope() { Destroy(); }

    Scope(const Scope&) = delete;
    Scope(Scope&& other) V8_NOEXCEPT
        : zone_name_(other.zone_name_),
          zone_stats_(other.zone_stats_),
          zone_(nullptr),
          support_zone_compression_(other.support_zone_compression_) {
      std::swap(zone_, other.zone_);
    }
    Scope& operator=(const Scope&) = delete;
    Scope& operator=(Scope&& other) V8_NOEXCEPT {
      Destroy();
      zone_name_ = other.zone_name_;
      zone_stats_ = other.zone_stats_;
      support_zone_compression_ = other.support_zone_compression_;
      DCHECK_NULL(zone_);
      std::swap(zone_, other.zone_);
      return *this;
    }

    Zone* zone() {
      if (zone_ == nullptr)
        zone_ =
            zone_stats_->NewEmptyZone(zone_name_, support_zone_compression_);
      return zone_;
    }
    void Destroy() {
      if (zone_ != nullptr) zone_stats_->ReturnZone(zone_);
      zone_ = nullptr;
    }

    ZoneStats* zone_stats() const { return zone_stats_; }

   private:
    const char* zone_name_;
    ZoneStats* zone_stats_;
    Zone* zone_;
    bool support_zone_compression_;
  };

  class V8_EXPORT_PRIVATE V8_NODISCARD StatsScope final {
   public:
    explicit StatsScope(ZoneStats* zone_stats);
    ~StatsScope();
    StatsScope(const StatsScope&) = delete;
    StatsScope& operator=(const StatsScope&) = delete;

    size_t GetMaxAllocatedBytes();
    size_t GetCurrentAllocatedBytes();
    size_t GetTotalAllocatedBytes();

   private:
    friend class ZoneStats;
    void ZoneReturned(Zone* zone);

    using InitialValues = std::map<Zone*, size_t>;

    ZoneStats* const zone_stats_;
    InitialValues initial_values_;
    size_t total_allocated_bytes_at_start_;
    size_t max_allocated_bytes_;
  };

  explicit ZoneStats(AccountingAllocator* allocator);
  ~ZoneStats();
  ZoneStats(const ZoneStats&) = delete;
  ZoneStats& operator=(const ZoneStats&) = delete;

  size_t GetMaxAllocatedBytes() const;
  size_t GetTotalAllocatedBytes() const;
  size_t GetCurrentAllocatedBytes() const;

 private:
  Zone* NewEmptyZone(const char* zone_name, bool support_zone_compression);
  void ReturnZone(Zone* zone);

  static const size_t kMaxUnusedSize = 3;
  using Zones = std::vector<Zone*>;
  using Stats = std::vector<StatsScope*>;

  Zones zones_;
  Stats stats_;
  size_t max_allocated_bytes_;
  size_t total_deleted_bytes_;
  AccountingAllocator* allocator_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ZONE_STATS_H_
