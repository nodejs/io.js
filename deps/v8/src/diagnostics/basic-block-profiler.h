// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_BASIC_BLOCK_PROFILER_H_
#define V8_DIAGNOSTICS_BASIC_BLOCK_PROFILER_H_

#include <iosfwd>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {

class OnHeapBasicBlockProfilerData;

class BasicBlockProfilerData {
 public:
  explicit BasicBlockProfilerData(size_t n_blocks);
  V8_EXPORT_PRIVATE BasicBlockProfilerData(
      DirectHandle<OnHeapBasicBlockProfilerData> js_heap_data,
      Isolate* isolate);
  V8_EXPORT_PRIVATE BasicBlockProfilerData(
      Tagged<OnHeapBasicBlockProfilerData> js_heap_data);

  BasicBlockProfilerData(const BasicBlockProfilerData&) = delete;
  BasicBlockProfilerData& operator=(const BasicBlockProfilerData&) = delete;

  size_t n_blocks() const {
    DCHECK_EQ(block_ids_.size(), counts_.size());
    return block_ids_.size();
  }
  const uint32_t* counts() const { return &counts_[0]; }

  void SetCode(const std::ostringstream& os);
  void SetFunctionName(std::unique_ptr<char[]> name);
  void SetSchedule(const std::ostringstream& os);
  void SetBlockId(size_t offset, int32_t id);
  void SetHash(int hash);
  void AddBranch(int32_t true_block_id, int32_t false_block_id);

  // Copy the data from this object into an equivalent object stored on the JS
  // heap, so that it can survive snapshotting and relocation. This must
  // happen on the main thread during finalization of the compilation.
  Handle<OnHeapBasicBlockProfilerData> CopyToJSHeap(Isolate* isolate);

  void Log(Isolate* isolate, std::ostream& os);

 private:
  friend class BasicBlockProfiler;
  friend std::ostream& operator<<(std::ostream& os,
                                  const BasicBlockProfilerData& s);

  V8_EXPORT_PRIVATE void ResetCounts();

  void CopyFromJSHeap(Tagged<OnHeapBasicBlockProfilerData> js_heap_data);

  // These vectors are indexed by reverse post-order block number.
  std::vector<int32_t> block_ids_;
  std::vector<uint32_t> counts_;
  std::vector<std::pair<int32_t, int32_t>> branches_;
  std::string function_name_;
  std::string schedule_;
  std::string code_;
  int hash_ = 0;
};

class BasicBlockProfiler {
 public:
  using DataList = std::list<std::unique_ptr<BasicBlockProfilerData>>;

  BasicBlockProfiler() = default;
  ~BasicBlockProfiler() = default;
  BasicBlockProfiler(const BasicBlockProfiler&) = delete;
  BasicBlockProfiler& operator=(const BasicBlockProfiler&) = delete;

  V8_EXPORT_PRIVATE static BasicBlockProfiler* Get();
  BasicBlockProfilerData* NewData(size_t n_blocks);
  V8_EXPORT_PRIVATE void ResetCounts(Isolate* isolate);
  V8_EXPORT_PRIVATE bool HasData(Isolate* isolate);
  V8_EXPORT_PRIVATE void Print(Isolate* isolate, std::ostream& os);
  V8_EXPORT_PRIVATE void Log(Isolate* isolate, std::ostream& os);

  // Coverage bitmap in this context includes only on heap BasicBlockProfiler
  // data. It is used to export coverage of builtins function loaded from
  // snapshot.
  V8_EXPORT_PRIVATE std::vector<bool> GetCoverageBitmap(Isolate* isolate);

  const DataList* data_list() { return &data_list_; }

 private:
  DataList data_list_;
  base::Mutex data_list_mutex_;
};

std::ostream& operator<<(std::ostream& os, const BasicBlockProfilerData& s);

// This struct comprises all callee inside a block.
using BlockCallees = std::set<Builtin>;
// This struct describes a call inside a caller, the key is block id, the value
// is a set of callee builtins.
using BuiltinCallees = std::unordered_map<int32_t, BlockCallees>;
// This struct describes a map for all builtins which will call other builtin.
using BuiltinCallMap = std::unordered_map<Builtin, BuiltinCallees>;

class BuiltinsCallGraph {
 public:
  BuiltinsCallGraph();
  ~BuiltinsCallGraph() = default;
  BuiltinsCallGraph(const BuiltinsCallGraph&) = delete;
  BuiltinsCallGraph& operator=(const BuiltinsCallGraph&) = delete;

  static BuiltinsCallGraph* Get();
  void AddBuiltinCall(Builtin caller, Builtin callee, int32_t block_id);
  const BuiltinCallees* GetBuiltinCallees(Builtin builtin);
  V8_INLINE bool all_hash_matched() const { return all_hash_matched_; }
  V8_INLINE void set_all_hash_matched(bool all_hash_matched) {
    all_hash_matched_ = all_hash_matched;
  }

 private:
  BuiltinCallMap builtin_call_map_;
  bool all_hash_matched_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_BASIC_BLOCK_PROFILER_H_
