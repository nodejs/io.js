// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_IC_STATS_H_
#define V8_IC_IC_STATS_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "include/v8-internal.h"  // For Address.
#include "src/base/atomicops.h"
#include "src/base/lazy-instance.h"
#include "src/sandbox/isolate.h"

namespace v8 {

namespace tracing {
class TracedValue;
}  // namespace tracing

namespace internal {

class JSFunction;
class Script;
template <typename T>
class Tagged;

struct ICInfo {
  ICInfo();
  void Reset();
  void AppendToTracedValue(v8::tracing::TracedValue* value) const;
  std::string type;
  const char* function_name;
  int script_offset;
  const char* script_name;
  int line_num;
  int column_num;
  bool is_constructor;
  bool is_optimized;
  std::string state;
  // Address of the map.
  void* map;
  // Whether map is a dictionary map.
  bool is_dictionary_map;
  // Number of own descriptors.
  unsigned number_of_own_descriptors;
  std::string instance_type;
};

class ICStats {
 public:
  const int MAX_IC_INFO = 4096;

  ICStats();
  void Dump();
  void Begin();
  void End();
  void Reset();
  V8_INLINE ICInfo& Current() {
    DCHECK(pos_ >= 0 && pos_ < MAX_IC_INFO);
    return ic_infos_[pos_];
  }
  const char* GetOrCacheScriptName(Tagged<Script> script);
  const char* GetOrCacheFunctionName(IsolateForSandbox isolate,
                                     Tagged<JSFunction> function);
  V8_INLINE static ICStats* instance() { return instance_.Pointer(); }

 private:
  static base::LazyInstance<ICStats>::type instance_;
  base::Atomic32 enabled_;
  std::vector<ICInfo> ic_infos_;
  // Keys are Script pointers; uses raw Address to keep includes light.
  std::unordered_map<Address, std::unique_ptr<char[]>> script_name_map_;
  // Keys are JSFunction pointers; uses raw Address to keep includes light.
  std::unordered_map<Address, std::unique_ptr<char[]>> function_name_map_;
  int pos_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_IC_STATS_H_
