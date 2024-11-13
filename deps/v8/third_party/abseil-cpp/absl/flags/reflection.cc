//
//  Copyright 2020 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/flags/reflection.h"

#include <assert.h>

#include <atomic>
#include <string>

#include "absl/base/config.h"
#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/internal/private_handle_accessor.h"
#include "absl/flags/internal/registry.h"
#include "absl/flags/usage_config.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

// --------------------------------------------------------------------
// FlagRegistry
//    A FlagRegistry singleton object holds all flag objects indexed by their
//    names so that if you know a flag's name, you can access or set it. If the
//    function is named FooLocked(), you must own the registry lock before
//    calling the function; otherwise, you should *not* hold the lock, and the
//    function will acquire it itself if needed.
// --------------------------------------------------------------------

class FlagRegistry {
 public:
  FlagRegistry() = default;
  ~FlagRegistry() = default;

  // Store a flag in this registry. Takes ownership of *flag.
  void RegisterFlag(CommandLineFlag& flag, const char* filename);

  void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION(lock_) { lock_.Lock(); }
  void Unlock() ABSL_UNLOCK_FUNCTION(lock_) { lock_.Unlock(); }

  // Returns the flag object for the specified name, or nullptr if not found.
  // Will emit a warning if a 'retired' flag is specified.
  CommandLineFlag* FindFlag(absl::string_view name);

  static FlagRegistry& GlobalRegistry();  // returns a singleton registry

 private:
  friend class flags_internal::FlagSaverImpl;  // reads all the flags in order
                                               // to copy them
  friend void ForEachFlag(std::function<void(CommandLineFlag&)> visitor);
  friend void FinalizeRegistry();

  // The map from name to flag, for FindFlag().
  using FlagMap = absl::flat_hash_map<absl::string_view, CommandLineFlag*>;
  using FlagIterator = FlagMap::iterator;
  using FlagConstIterator = FlagMap::const_iterator;
  FlagMap flags_;
  std::vector<CommandLineFlag*> flat_flags_;
  std::atomic<bool> finalized_flags_{false};

  absl::Mutex lock_;

  // Disallow
  FlagRegistry(const FlagRegistry&);
  FlagRegistry& operator=(const FlagRegistry&);
};

namespace {

class FlagRegistryLock {
 public:
  explicit FlagRegistryLock(FlagRegistry& fr) : fr_(fr) { fr_.Lock(); }
  ~FlagRegistryLock() { fr_.Unlock(); }

 private:
  FlagRegistry& fr_;
};

}  // namespace

CommandLineFlag* FlagRegistry::FindFlag(absl::string_view name) {
  if (finalized_flags_.load(std::memory_order_acquire)) {
    // We could save some gcus here if we make `Name()` be non-virtual.
    // We could move the `const char*` name to the base class.
    auto it = std::partition_point(
        flat_flags_.begin(), flat_flags_.end(),
        [=](CommandLineFlag* f) { return f->Name() < name; });
    if (it != flat_flags_.end() && (*it)->Name() == name) return *it;
  }

  FlagRegistryLock frl(*this);
  auto it = flags_.find(name);
  return it != flags_.end() ? it->second : nullptr;
}

void FlagRegistry::RegisterFlag(CommandLineFlag& flag, const char* filename) {
  if (filename != nullptr &&
      flag.Filename() != GetUsageConfig().normalize_filename(filename)) {
    flags_internal::ReportUsageError(
        absl::StrCat(
            "Inconsistency between flag object and registration for flag '",
            flag.Name(),
            "', likely due to duplicate flags or an ODR violation. Relevant "
            "files: ",
            flag.Filename(), " and ", filename),
        true);
    std::exit(1);
  }

  FlagRegistryLock registry_lock(*this);

  std::pair<FlagIterator, bool> ins =
      flags_.insert(FlagMap::value_type(flag.Name(), &flag));
  if (ins.second == false) {  // means the name was already in the map
    CommandLineFlag& old_flag = *ins.first->second;
    if (flag.IsRetired() != old_flag.IsRetired()) {
      // All registrations must agree on the 'retired' flag.
      flags_internal::ReportUsageError(
          absl::StrCat(
              "Retired flag '", flag.Name(), "' was defined normally in file '",
              (flag.IsRetired() ? old_flag.Filename() : flag.Filename()), "'."),
          true);
    } else if (flags_internal::PrivateHandleAccessor::TypeId(flag) !=
               flags_internal::PrivateHandleAccessor::TypeId(old_flag)) {
      flags_internal::ReportUsageError(
          absl::StrCat("Flag '", flag.Name(),
                       "' was defined more than once but with "
                       "differing types. Defined in files '",
                       old_flag.Filename(), "' and '", flag.Filename(), "'."),
          true);
    } else if (old_flag.IsRetired()) {
      return;
    } else if (old_flag.Filename() != flag.Filename()) {
      flags_internal::ReportUsageError(
          absl::StrCat("Flag '", flag.Name(),
                       "' was defined more than once (in files '",
                       old_flag.Filename(), "' and '", flag.Filename(), "')."),
          true);
    } else {
      flags_internal::ReportUsageError(
          absl::StrCat(
              "Something is wrong with flag '", flag.Name(), "' in file '",
              flag.Filename(), "'. One possibility: file '", flag.Filename(),
              "' is being linked both statically and dynamically into this "
              "executable. e.g. some files listed as srcs to a test and also "
              "listed as srcs of some shared lib deps of the same test."),
          true);
    }
    // All cases above are fatal, except for the retired flags.
    std::exit(1);
  }
}

FlagRegistry& FlagRegistry::GlobalRegistry() {
  static absl::NoDestructor<FlagRegistry> global_registry;
  return *global_registry;
}

// --------------------------------------------------------------------

void ForEachFlag(std::function<void(CommandLineFlag&)> visitor) {
  FlagRegistry& registry = FlagRegistry::GlobalRegistry();

  if (registry.finalized_flags_.load(std::memory_order_acquire)) {
    for (const auto& i : registry.flat_flags_) visitor(*i);
  }

  FlagRegistryLock frl(registry);
  for (const auto& i : registry.flags_) visitor(*i.second);
}

// --------------------------------------------------------------------

bool RegisterCommandLineFlag(CommandLineFlag& flag, const char* filename) {
  FlagRegistry::GlobalRegistry().RegisterFlag(flag, filename);
  return true;
}

void FinalizeRegistry() {
  auto& registry = FlagRegistry::GlobalRegistry();
  FlagRegistryLock frl(registry);
  if (registry.finalized_flags_.load(std::memory_order_relaxed)) {
    // Was already finalized. Ignore the second time.
    return;
  }
  registry.flat_flags_.reserve(registry.flags_.size());
  for (const auto& f : registry.flags_) {
    registry.flat_flags_.push_back(f.second);
  }
  std::sort(std::begin(registry.flat_flags_), std::end(registry.flat_flags_),
            [](const CommandLineFlag* lhs, const CommandLineFlag* rhs) {
              return lhs->Name() < rhs->Name();
            });
  registry.flags_.clear();
  registry.finalized_flags_.store(true, std::memory_order_release);
}

// --------------------------------------------------------------------

namespace {

// These are only used as constexpr global objects.
// They do not use a virtual destructor to simplify their implementation.
// They are not destroyed except at program exit, so leaks do not matter.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
class RetiredFlagObj final : public CommandLineFlag {
 public:
  constexpr RetiredFlagObj(const char* name, FlagFastTypeId type_id)
      : name_(name), type_id_(type_id) {}

 private:
  absl::string_view Name() const override { return name_; }
  std::string Filename() const override {
    OnAccess();
    return "RETIRED";
  }
  FlagFastTypeId TypeId() const override { return type_id_; }
  std::string Help() const override {
    OnAccess();
    return "";
  }
  bool IsRetired() const override { return true; }
  bool IsSpecifiedOnCommandLine() const override {
    OnAccess();
    return false;
  }
  std::string DefaultValue() const override {
    OnAccess();
    return "";
  }
  std::string CurrentValue() const override {
    OnAccess();
    return "";
  }

  // Any input is valid
  bool ValidateInputValue(absl::string_view) const override {
    OnAccess();
    return true;
  }

  std::unique_ptr<flags_internal::FlagStateInterface> SaveState() override {
    return nullptr;
  }

  bool ParseFrom(absl::string_view, flags_internal::FlagSettingMode,
                 flags_internal::ValueSource, std::string&) override {
    OnAccess();
    return false;
  }

  void CheckDefaultValueParsingRoundtrip() const override { OnAccess(); }

  void Read(void*) const override { OnAccess(); }

  void OnAccess() const {
    flags_internal::ReportUsageError(
        absl::StrCat("Accessing retired flag '", name_, "'"), false);
  }

  // Data members
  const char* const name_;
  const FlagFastTypeId type_id_;
};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace

void Retire(const char* name, FlagFastTypeId type_id, char* buf) {
  static_assert(sizeof(RetiredFlagObj) == kRetiredFlagObjSize, "");
  static_assert(alignof(RetiredFlagObj) == kRetiredFlagObjAlignment, "");
  auto* flag = ::new (static_cast<void*>(buf))
      flags_internal::RetiredFlagObj(name, type_id);
  FlagRegistry::GlobalRegistry().RegisterFlag(*flag, nullptr);
}

// --------------------------------------------------------------------

class FlagSaverImpl {
 public:
  FlagSaverImpl() = default;
  FlagSaverImpl(const FlagSaverImpl&) = delete;
  void operator=(const FlagSaverImpl&) = delete;

  // Saves the flag states from the flag registry into this object.
  // It's an error to call this more than once.
  void SaveFromRegistry() {
    assert(backup_registry_.empty());  // call only once!
    flags_internal::ForEachFlag([&](CommandLineFlag& flag) {
      if (auto flag_state =
              flags_internal::PrivateHandleAccessor::SaveState(flag)) {
        backup_registry_.emplace_back(std::move(flag_state));
      }
    });
  }

  // Restores the saved flag states into the flag registry.
  void RestoreToRegistry() {
    for (const auto& flag_state : backup_registry_) {
      flag_state->Restore();
    }
  }

 private:
  std::vector<std::unique_ptr<flags_internal::FlagStateInterface>>
      backup_registry_;
};

}  // namespace flags_internal

FlagSaver::FlagSaver() : impl_(new flags_internal::FlagSaverImpl) {
  impl_->SaveFromRegistry();
}

FlagSaver::~FlagSaver() {
  if (!impl_) return;

  impl_->RestoreToRegistry();
  delete impl_;
}

// --------------------------------------------------------------------

CommandLineFlag* FindCommandLineFlag(absl::string_view name) {
  if (name.empty()) return nullptr;
  flags_internal::FlagRegistry& registry =
      flags_internal::FlagRegistry::GlobalRegistry();
  return registry.FindFlag(name);
}

// --------------------------------------------------------------------

absl::flat_hash_map<absl::string_view, absl::CommandLineFlag*> GetAllFlags() {
  absl::flat_hash_map<absl::string_view, absl::CommandLineFlag*> res;
  flags_internal::ForEachFlag([&](CommandLineFlag& flag) {
    if (!flag.IsRetired()) res.insert({flag.Name(), &flag});
  });
  return res;
}

ABSL_NAMESPACE_END
}  // namespace absl
