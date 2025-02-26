// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/log/internal/vlog_config.h"

#include <stddef.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/internal/fnmatch.h"
#include "absl/memory/memory.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

namespace {
bool ModuleIsPath(absl::string_view module_pattern) {
#ifdef _WIN32
  return module_pattern.find_first_of("/\\") != module_pattern.npos;
#else
  return module_pattern.find('/') != module_pattern.npos;
#endif
}
}  // namespace

bool VLogSite::SlowIsEnabled(int stale_v, int level) {
  if (ABSL_PREDICT_TRUE(stale_v != kUninitialized)) {
    // Because of the prerequisites to this function, we know that stale_v is
    // either uninitialized or >= level. If it's not uninitialized, that means
    // it must be >= level, thus we should log.
    return true;
  }
  stale_v = log_internal::RegisterAndInitialize(this);
  return ABSL_PREDICT_FALSE(stale_v >= level);
}

bool VLogSite::SlowIsEnabled0(int stale_v) { return SlowIsEnabled(stale_v, 0); }
bool VLogSite::SlowIsEnabled1(int stale_v) { return SlowIsEnabled(stale_v, 1); }
bool VLogSite::SlowIsEnabled2(int stale_v) { return SlowIsEnabled(stale_v, 2); }
bool VLogSite::SlowIsEnabled3(int stale_v) { return SlowIsEnabled(stale_v, 3); }
bool VLogSite::SlowIsEnabled4(int stale_v) { return SlowIsEnabled(stale_v, 4); }
bool VLogSite::SlowIsEnabled5(int stale_v) { return SlowIsEnabled(stale_v, 5); }

namespace {
struct VModuleInfo final {
  std::string module_pattern;
  bool module_is_path;  // i.e. it contains a path separator.
  int vlog_level;

  // Allocates memory.
  VModuleInfo(absl::string_view module_pattern_arg, bool module_is_path_arg,
              int vlog_level_arg)
      : module_pattern(std::string(module_pattern_arg)),
        module_is_path(module_is_path_arg),
        vlog_level(vlog_level_arg) {}
};

// `mutex` guards all of the data structures that aren't lock-free.
// To avoid problems with the heap checker which calls into `VLOG`, `mutex` must
// be a `SpinLock` that prevents fiber scheduling instead of a `Mutex`.
ABSL_CONST_INIT absl::base_internal::SpinLock mutex(
    absl::kConstInit, absl::base_internal::SCHEDULE_KERNEL_ONLY);

// `GetUpdateSitesMutex()` serializes updates to all of the sites (i.e. those in
// `site_list_head`) themselves.
absl::Mutex* GetUpdateSitesMutex() {
  // Chromium requires no global destructors, so we can't use the
  // absl::kConstInit idiom since absl::Mutex as a non-trivial destructor.
  static absl::NoDestructor<absl::Mutex> update_sites_mutex ABSL_ACQUIRED_AFTER(
      mutex);
  return update_sites_mutex.get();
}

ABSL_CONST_INIT int global_v ABSL_GUARDED_BY(mutex) = 0;
// `site_list_head` is the head of a singly-linked list.  Traversal, insertion,
// and reads are atomic, so no locks are required, but updates to existing
// elements are guarded by `GetUpdateSitesMutex()`.
ABSL_CONST_INIT std::atomic<VLogSite*> site_list_head{nullptr};
ABSL_CONST_INIT std::vector<VModuleInfo>* vmodule_info ABSL_GUARDED_BY(mutex)
    ABSL_PT_GUARDED_BY(mutex){nullptr};

// Only used for lisp.
ABSL_CONST_INIT std::vector<std::function<void()>>* update_callbacks
    ABSL_GUARDED_BY(GetUpdateSitesMutex())
        ABSL_PT_GUARDED_BY(GetUpdateSitesMutex()){nullptr};

// Allocates memory.
std::vector<VModuleInfo>& get_vmodule_info()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
  if (!vmodule_info) vmodule_info = new std::vector<VModuleInfo>;
  return *vmodule_info;
}

// Does not allocate or take locks.
int VLogLevel(absl::string_view file, const std::vector<VModuleInfo>* infos,
              int current_global_v) {
  // `infos` is null during a call to `VLOG` prior to setting `vmodule` (e.g. by
  // parsing flags).  We can't allocate in `VLOG`, so we treat null as empty
  // here and press on.
  if (!infos || infos->empty()) return current_global_v;
  // Get basename for file
  absl::string_view basename = file;
  {
    const size_t sep = basename.rfind('/');
    if (sep != basename.npos) {
      basename.remove_prefix(sep + 1);
#ifdef _WIN32
    } else {
      const size_t sep = basename.rfind('\\');
      if (sep != basename.npos) basename.remove_prefix(sep + 1);
#endif
    }
  }

  absl::string_view stem = file, stem_basename = basename;
  {
    const size_t sep = stem_basename.find('.');
    if (sep != stem_basename.npos) {
      stem.remove_suffix(stem_basename.size() - sep);
      stem_basename.remove_suffix(stem_basename.size() - sep);
    }
    if (absl::ConsumeSuffix(&stem_basename, "-inl")) {
      stem.remove_suffix(absl::string_view("-inl").size());
    }
  }
  for (const auto& info : *infos) {
    if (info.module_is_path) {
      // If there are any slashes in the pattern, try to match the full
      // name.
      if (FNMatch(info.module_pattern, stem)) {
        return info.vlog_level == kUseFlag ? current_global_v : info.vlog_level;
      }
    } else if (FNMatch(info.module_pattern, stem_basename)) {
      return info.vlog_level == kUseFlag ? current_global_v : info.vlog_level;
    }
  }

  return current_global_v;
}

// Allocates memory.
int AppendVModuleLocked(absl::string_view module_pattern, int log_level)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
  for (const auto& info : get_vmodule_info()) {
    if (FNMatch(info.module_pattern, module_pattern)) {
      // This is a memory optimization to avoid storing patterns that will never
      // match due to exit early semantics. Primarily optimized for our own unit
      // tests.
      return info.vlog_level;
    }
  }
  bool module_is_path = ModuleIsPath(module_pattern);
  get_vmodule_info().emplace_back(std::string(module_pattern), module_is_path,
                                  log_level);
  return global_v;
}

// Allocates memory.
int PrependVModuleLocked(absl::string_view module_pattern, int log_level)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
  absl::optional<int> old_log_level;
  for (const auto& info : get_vmodule_info()) {
    if (FNMatch(info.module_pattern, module_pattern)) {
      old_log_level = info.vlog_level;
      break;
    }
  }
  bool module_is_path = ModuleIsPath(module_pattern);
  auto iter = get_vmodule_info().emplace(get_vmodule_info().cbegin(),
                                         std::string(module_pattern),
                                         module_is_path, log_level);

  // This is a memory optimization to avoid storing patterns that will never
  // match due to exit early semantics. Primarily optimized for our own unit
  // tests.
  get_vmodule_info().erase(
      std::remove_if(++iter, get_vmodule_info().end(),
                     [module_pattern](const VModuleInfo& info) {
                       // Remove the previous pattern if it is less generic than
                       // the new one. For example, if the new pattern
                       // `module_pattern` is "foo*" and the previous pattern
                       // `info.module_pattern` is "foo", we should remove the
                       // previous pattern. Because the new pattern "foo*" will
                       // match all the files that the previous pattern "foo"
                       // matches.
                       return FNMatch(module_pattern, info.module_pattern);
                     }),
      get_vmodule_info().cend());
  return old_log_level.value_or(global_v);
}
}  // namespace

int VLogLevel(absl::string_view file) ABSL_LOCKS_EXCLUDED(mutex) {
  absl::base_internal::SpinLockHolder l(&mutex);
  return VLogLevel(file, vmodule_info, global_v);
}

int RegisterAndInitialize(VLogSite* v) ABSL_LOCKS_EXCLUDED(mutex) {
  // std::memory_order_seq_cst is overkill in this function, but given that this
  // path is intended to be slow, it's not worth the brain power to relax that.
  VLogSite* h = site_list_head.load(std::memory_order_seq_cst);

  VLogSite* old = nullptr;
  if (v->next_.compare_exchange_strong(old, h, std::memory_order_seq_cst,
                                       std::memory_order_seq_cst)) {
    // Multiple threads may attempt to register this site concurrently.
    // By successfully setting `v->next` this thread commits to being *the*
    // thread that installs `v` in the list.
    while (!site_list_head.compare_exchange_weak(
        h, v, std::memory_order_seq_cst, std::memory_order_seq_cst)) {
      v->next_.store(h, std::memory_order_seq_cst);
    }
  }

  int old_v = VLogSite::kUninitialized;
  int new_v = VLogLevel(v->file_);
  // No loop, if someone else set this, we should respect their evaluation of
  // `VLogLevel`. This may mean we return a stale `v`, but `v` itself will
  // always arrive at the freshest value.  Otherwise, we could be writing a
  // stale value and clobbering the fresher one.
  if (v->v_.compare_exchange_strong(old_v, new_v, std::memory_order_seq_cst,
                                    std::memory_order_seq_cst)) {
    return new_v;
  }
  return old_v;
}

void UpdateVLogSites() ABSL_UNLOCK_FUNCTION(mutex)
    ABSL_LOCKS_EXCLUDED(GetUpdateSitesMutex()) {
  std::vector<VModuleInfo> infos = get_vmodule_info();
  int current_global_v = global_v;
  // We need to grab `GetUpdateSitesMutex()` before we release `mutex` to ensure
  // that updates are not interleaved (resulting in an inconsistent final state)
  // and to ensure that the final state in the sites matches the final state of
  // `vmodule_info`. We unlock `mutex` to ensure that uninitialized sites don't
  // have to wait on all updates in order to acquire `mutex` and initialize
  // themselves.
  absl::MutexLock ul(GetUpdateSitesMutex());
  mutex.Unlock();
  VLogSite* n = site_list_head.load(std::memory_order_seq_cst);
  // Because sites are added to the list in the order they are executed, there
  // tend to be clusters of entries with the same file.
  const char* last_file = nullptr;
  int last_file_level = 0;
  while (n != nullptr) {
    if (n->file_ != last_file) {
      last_file = n->file_;
      last_file_level = VLogLevel(n->file_, &infos, current_global_v);
    }
    n->v_.store(last_file_level, std::memory_order_seq_cst);
    n = n->next_.load(std::memory_order_seq_cst);
  }
  if (update_callbacks) {
    for (auto& cb : *update_callbacks) {
      cb();
    }
  }
}

void UpdateVModule(absl::string_view vmodule)
    ABSL_LOCKS_EXCLUDED(mutex, GetUpdateSitesMutex()) {
  std::vector<std::pair<absl::string_view, int>> glob_levels;
  for (absl::string_view glob_level : absl::StrSplit(vmodule, ',')) {
    const size_t eq = glob_level.rfind('=');
    if (eq == glob_level.npos) continue;
    const absl::string_view glob = glob_level.substr(0, eq);
    int level;
    if (!absl::SimpleAtoi(glob_level.substr(eq + 1), &level)) continue;
    glob_levels.emplace_back(glob, level);
  }
  mutex.Lock();  // Unlocked by UpdateVLogSites().
  get_vmodule_info().clear();
  for (const auto& it : glob_levels) {
    const absl::string_view glob = it.first;
    const int level = it.second;
    AppendVModuleLocked(glob, level);
  }
  UpdateVLogSites();
}

int UpdateGlobalVLogLevel(int v)
    ABSL_LOCKS_EXCLUDED(mutex, GetUpdateSitesMutex()) {
  mutex.Lock();  // Unlocked by UpdateVLogSites().
  const int old_global_v = global_v;
  if (v == global_v) {
    mutex.Unlock();
    return old_global_v;
  }
  global_v = v;
  UpdateVLogSites();
  return old_global_v;
}

int PrependVModule(absl::string_view module_pattern, int log_level)
    ABSL_LOCKS_EXCLUDED(mutex, GetUpdateSitesMutex()) {
  mutex.Lock();  // Unlocked by UpdateVLogSites().
  int old_v = PrependVModuleLocked(module_pattern, log_level);
  UpdateVLogSites();
  return old_v;
}

void OnVLogVerbosityUpdate(std::function<void()> cb)
    ABSL_LOCKS_EXCLUDED(GetUpdateSitesMutex()) {
  absl::MutexLock ul(GetUpdateSitesMutex());
  if (!update_callbacks)
    update_callbacks = new std::vector<std::function<void()>>;
  update_callbacks->push_back(std::move(cb));
}

VLogSite* SetVModuleListHeadForTestOnly(VLogSite* v) {
  return site_list_head.exchange(v, std::memory_order_seq_cst);
}

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
