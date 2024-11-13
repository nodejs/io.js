// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/flags/flags.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>

#include "src/base/functional.h"
#include "src/base/lazy-instance.h"
#include "src/base/platform/platform.h"
#include "src/codegen/cpu-features.h"
#include "src/flags/flags-impl.h"
#include "src/logging/tracing-flags.h"
#include "src/tracing/tracing-category-observer.h"
#include "src/utils/allocation.h"
#include "src/utils/memcopy.h"
#include "src/utils/ostreams.h"
#include "src/utils/utils.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-limits.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8::internal {

// Define {v8_flags}, declared in flags.h.
FlagValues v8_flags;

// {v8_flags} needs to be aligned to a memory page, and the size needs to be a
// multiple of a page size. This is required for memory-protection of the memory
// holding the {v8_flags} struct.
// Both is guaranteed by the {alignas(kMinimumOSPageSize)} annotation on
// {FlagValues}.
static_assert(alignof(FlagValues) == kMinimumOSPageSize);
static_assert(sizeof(FlagValues) % kMinimumOSPageSize == 0);

// Define all of our flags default values.
#define FLAG_MODE_DEFINE_DEFAULTS
#include "src/flags/flag-definitions.h"  // NOLINT(build/include)
#undef FLAG_MODE_DEFINE_DEFAULTS

char FlagHelpers::NormalizeChar(char ch) { return ch == '_' ? '-' : ch; }

int FlagHelpers::FlagNamesCmp(const char* a, const char* b) {
  int i = 0;
  char ac, bc;
  do {
    ac = NormalizeChar(a[i]);
    bc = NormalizeChar(b[i]);
    if (ac < bc) return -1;
    if (ac > bc) return 1;
    i++;
  } while (ac != '\0');
  DCHECK(bc == '\0');
  return 0;
}

bool FlagHelpers::EqualNames(const char* a, const char* b) {
  return FlagNamesCmp(a, b) == 0;
}

// Checks if two flag names are equal, allowing for the second name to have a
// suffix starting with a white space character, e.g. "max_opt < 3". This is
// used in flag implications.
bool FlagHelpers::EqualNameWithSuffix(const char* a, const char* b) {
  char ac, bc;
  for (int i = 0; true; ++i) {
    ac = NormalizeChar(a[i]);
    bc = NormalizeChar(b[i]);
    if (ac == '\0') break;
    if (ac != bc) return false;
  }
  return bc == '\0' || std::isspace(bc);
}

std::ostream& operator<<(std::ostream& os, FlagName flag_name) {
  os << (flag_name.negated ? "--no-" : "--");
  for (const char* p = flag_name.name; *p; ++p) {
    os << FlagHelpers::NormalizeChar(*p);
  }
  return os;
}

void Flag::set_string_value(const char* new_value, bool owns_new_value,
                            SetBy set_by) {
  DCHECK_EQ(TYPE_STRING, type_);
  DCHECK_IMPLIES(owns_new_value, new_value != nullptr);
  auto* flag_value = reinterpret_cast<FlagValue<const char*>*>(valptr_);
  const char* old_value = *flag_value;
  DCHECK_IMPLIES(owns_ptr_, old_value != nullptr);
  bool change_flag = old_value
                         ? !new_value || std::strcmp(old_value, new_value) != 0
                         : !!new_value;
  change_flag = CheckFlagChange(set_by, change_flag);
  if (change_flag) {
    if (owns_ptr_) DeleteArray(old_value);
    *flag_value = new_value;
    owns_ptr_ = owns_new_value;
  } else {
    if (owns_new_value) DeleteArray(new_value);
  }
}

bool Flag::ShouldCheckFlagContradictions() {
  if (v8_flags.allow_overwriting_for_next_flag) {
    // Setting the flag manually to false before calling Reset() avoids this
    // becoming re-entrant.
    v8_flags.allow_overwriting_for_next_flag = false;
    FindFlagByPointer(&v8_flags.allow_overwriting_for_next_flag)->Reset();
    return false;
  }
  return v8_flags.abort_on_contradictory_flags && !v8_flags.fuzzing;
}

bool Flag::CheckFlagChange(SetBy new_set_by, bool change_flag,
                           const char* implied_by) {
  if (new_set_by == SetBy::kWeakImplication &&
      (set_by_ == SetBy::kImplication || set_by_ == SetBy::kCommandLine)) {
    return false;
  }
  if (ShouldCheckFlagContradictions()) {
    static constexpr const char kHint[] =
        "If a test variant caused this, it might be necessary to specify "
        "additional contradictory flags in "
        "tools/testrunner/local/variants.py.";
    struct FatalError : public std::ostringstream {
      // MSVC complains about non-returning destructor; disable that.
      MSVC_SUPPRESS_WARNING(4722)
      ~FatalError() { FATAL("%s.\n%s", str().c_str(), kHint); }
    };
    // Readonly flags cannot change value.
    if (change_flag && IsReadOnly()) {
      // Exit instead of abort for certain testing situations.
      if (v8_flags.exit_on_contradictory_flags) base::OS::ExitProcess(0);
      if (implied_by == nullptr) {
        FatalError{} << "Contradictory value for readonly flag "
                     << FlagName{name()};
      } else {
        DCHECK(IsAnyImplication(new_set_by));
        FatalError{} << "Contradictory value for readonly flag "
                     << FlagName{name()} << " implied by " << implied_by;
      }
    }
    // For bool flags, we only check for a conflict if the value actually
    // changes. So specifying the same flag with the same value multiple times
    // is allowed.
    // For other flags, we disallow specifying them explicitly or in the
    // presence of an implication if the value is not the same.
    // This is to simplify the rules describing conflicts in variants.py: A
    // repeated non-boolean flag is considered an error.
    bool is_bool_flag = type_ == TYPE_MAYBE_BOOL || type_ == TYPE_BOOL;
    bool check_implications = change_flag;
    switch (set_by_) {
      case SetBy::kDefault:
        break;
      case SetBy::kWeakImplication:
        if (new_set_by == SetBy::kWeakImplication && check_implications) {
          FatalError{} << "Contradictory weak flag implications from "
                       << FlagName{implied_by_} << " and "
                       << FlagName{implied_by} << " for flag "
                       << FlagName{name()};
        }
        break;
      case SetBy::kImplication:
        if (new_set_by == SetBy::kImplication && check_implications) {
          FatalError{} << "Contradictory flag implications from "
                       << FlagName{implied_by_} << " and "
                       << FlagName{implied_by} << " for flag "
                       << FlagName{name()};
        }
        break;
      case SetBy::kCommandLine:
        if (new_set_by == SetBy::kImplication && check_implications) {
          // Exit instead of abort for certain testing situations.
          if (v8_flags.exit_on_contradictory_flags) base::OS::ExitProcess(0);
          if (is_bool_flag) {
            FatalError{} << "Flag " << FlagName{name()} << ": value implied by "
                         << FlagName{implied_by}
                         << " conflicts with explicit specification";
          } else {
            FatalError{} << "Flag " << FlagName{name()} << " is implied by "
                         << FlagName{implied_by}
                         << " but also specified explicitly";
          }
        } else if (new_set_by == SetBy::kCommandLine && check_implications) {
          // Exit instead of abort for certain testing situations.
          if (v8_flags.exit_on_contradictory_flags) base::OS::ExitProcess(0);
          if (is_bool_flag) {
            FatalError{} << "Command-line provided flag " << FlagName{name()}
                         << " specified as both true and false";
          } else {
            FatalError{} << "Command-line provided flag " << FlagName{name()}
                         << " specified multiple times";
          }
        }
        break;
    }
  }
  if (change_flag && IsReadOnly()) {
    // Readonly flags must never change value.
    return false;
  }
  set_by_ = new_set_by;
  if (IsAnyImplication(new_set_by)) {
    DCHECK_NOT_NULL(implied_by);
    implied_by_ = implied_by;
#ifdef DEBUG
    // This only works when implied_by is a flag_name or !flag_name, but it
    // can also be a condition e.g. flag_name > 3. Since this is only used for
    // checks in DEBUG mode, we will just ignore the more complex conditions
    // for now - that will just lead to a nullptr which won't be followed.
    implied_by_ptr_ = static_cast<Flag*>(FindImplicationFlagByName(
        implied_by[0] == '!' ? implied_by + 1 : implied_by));
    DCHECK_NE(implied_by_ptr_, this);
#endif
  }
  return change_flag;
}

bool Flag::IsDefault() const {
  switch (type_) {
    case TYPE_BOOL:
      return bool_variable() == bool_default();
    case TYPE_MAYBE_BOOL:
      return maybe_bool_variable().has_value() == false;
    case TYPE_INT:
      return int_variable() == int_default();
    case TYPE_UINT:
      return uint_variable() == uint_default();
    case TYPE_UINT64:
      return uint64_variable() == uint64_default();
    case TYPE_FLOAT:
      return float_variable() == float_default();
    case TYPE_SIZE_T:
      return size_t_variable() == size_t_default();
    case TYPE_STRING: {
      const char* str1 = string_value();
      const char* str2 = string_default();
      if (str2 == nullptr) return str1 == nullptr;
      if (str1 == nullptr) return str2 == nullptr;
      return strcmp(str1, str2) == 0;
    }
  }
  UNREACHABLE();
}

void Flag::ReleaseDynamicAllocations() {
  if (type_ != TYPE_STRING) return;
  if (owns_ptr_) DeleteArray(string_value());
}

void Flag::Reset() {
  switch (type_) {
    case TYPE_BOOL:
      set_bool_variable(bool_default(), SetBy::kDefault);
      break;
    case TYPE_MAYBE_BOOL:
      set_maybe_bool_variable(std::nullopt, SetBy::kDefault);
      break;
    case TYPE_INT:
      set_int_variable(int_default(), SetBy::kDefault);
      break;
    case TYPE_UINT:
      set_uint_variable(uint_default(), SetBy::kDefault);
      break;
    case TYPE_UINT64:
      set_uint64_variable(uint64_default(), SetBy::kDefault);
      break;
    case TYPE_FLOAT:
      set_float_variable(float_default(), SetBy::kDefault);
      break;
    case TYPE_SIZE_T:
      set_size_t_variable(size_t_default(), SetBy::kDefault);
      break;
    case TYPE_STRING:
      set_string_value(string_default(), false, SetBy::kDefault);
      break;
  }
}

Flag flags[] = {
#define FLAG_MODE_META
#include "src/flags/flag-definitions.h"  // NOLINT(build/include)
#undef FLAG_MODE_META
};

constexpr size_t kNumFlags = arraysize(flags);

base::Vector<Flag> Flags() { return base::ArrayVector(flags); }

struct FlagLess {
  bool operator()(const Flag* a, const Flag* b) const {
    return FlagHelpers::FlagNamesCmp(a->name(), b->name()) < 0;
  }
};

struct FlagNameGreater {
  bool operator()(const Flag* a, const char* b) const {
    return FlagHelpers::FlagNamesCmp(a->name(), b) > 0;
  }
};

// Optimized look-up of flags by name using binary search. Works only for flags
// that can be found. If the looked-up flag might not exit in the list, an
// additional name check of the returned flag is required.
class FlagMapByName {
 public:
  FlagMapByName() {
    for (size_t i = 0; i < kNumFlags; ++i) {
      flags_[i] = &flags[i];
    }
    std::sort(flags_.begin(), flags_.end(), FlagLess());
  }

  // Returns the greatest flag whose name is less than or equal to the given
  // name (lexicographically). This allows for finding the right flag even if
  // there is a suffix, as in the case of implications, e.g. "max_opt < 3".
  Flag* GetFlag(const char* name) {
    auto it = std::lower_bound(flags_.rbegin(), flags_.rend(), name,
                               FlagNameGreater());
    if (it == flags_.rend()) return nullptr;
    return *it;
  }

 private:
  std::array<Flag*, kNumFlags> flags_;
};

DEFINE_LAZY_LEAKY_OBJECT_GETTER(FlagMapByName, GetFlagMap)

// This should be used to look up flags that we know were defined.
// It allows for suffixes used in implications, e.g. "max_opt < 3",
Flag* FindImplicationFlagByName(const char* name) {
  Flag* flag = GetFlagMap()->GetFlag(name);
  CHECK(flag != nullptr);
  DCHECK(FlagHelpers::EqualNameWithSuffix(flag->name(), name));
  return flag;
}

// This can be used to look up flags that might not exist (e.g. invalid command
// line flags).
Flag* FindFlagByName(const char* name) {
  Flag* flag = GetFlagMap()->GetFlag(name);
  // GetFlag returns an invalid lower bound for flags not in the list. So
  // we need to verify the name again.
  if (flag != nullptr && FlagHelpers::EqualNames(flag->name(), name)) {
    return flag;
  }
#ifdef DEBUG
  // Ensure the flag is not in the global list.
  for (size_t i = 0; i < kNumFlags; ++i) {
    DCHECK(!FlagHelpers::EqualNames(name, flags[i].name()));
  }
#endif
  return nullptr;
}

Flag* FindFlagByPointer(const void* ptr) {
  for (size_t i = 0; i < kNumFlags; ++i) {
    if (flags[i].PointsTo(ptr)) return &flags[i];
  }
  return nullptr;
}

static const char* Type2String(Flag::FlagType type) {
  switch (type) {
    case Flag::TYPE_BOOL:
      return "bool";
    case Flag::TYPE_MAYBE_BOOL:
      return "maybe_bool";
    case Flag::TYPE_INT:
      return "int";
    case Flag::TYPE_UINT:
      return "uint";
    case Flag::TYPE_UINT64:
      return "uint64";
    case Flag::TYPE_FLOAT:
      return "float";
    case Flag::TYPE_SIZE_T:
      return "size_t";
    case Flag::TYPE_STRING:
      return "string";
  }
}

// Helper for printing flag values.
struct PrintFlagValue {
  const Flag& flag;
};

std::ostream& operator<<(std::ostream& os, PrintFlagValue flag_value) {
  const Flag& flag = flag_value.flag;
  switch (flag.type()) {
    case Flag::TYPE_BOOL:
      os << (flag.bool_variable() ? "true" : "false");
      break;
    case Flag::TYPE_MAYBE_BOOL:
      os << (flag.maybe_bool_variable().has_value()
                 ? (flag.maybe_bool_variable().value() ? "true" : "false")
                 : "unset");
      break;
    case Flag::TYPE_INT:
      os << flag.int_variable();
      break;
    case Flag::TYPE_UINT:
      os << flag.uint_variable();
      break;
    case Flag::TYPE_UINT64:
      os << flag.uint64_variable();
      break;
    case Flag::TYPE_FLOAT:
      os << flag.float_variable();
      break;
    case Flag::TYPE_SIZE_T:
      os << flag.size_t_variable();
      break;
    case Flag::TYPE_STRING: {
      const char* str = flag.string_value();
      os << std::quoted(str ? str : "");
      break;
    }
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const Flag& flag) {
  if (flag.type() == Flag::TYPE_BOOL) {
    os << FlagName{flag.name(), !flag.bool_variable()};
  } else {
    os << FlagName{flag.name()} << "=" << PrintFlagValue{flag};
  }
  return os;
}

static std::atomic<uint32_t> flag_hash{0};
static std::atomic<bool> flags_frozen{false};

uint32_t ComputeFlagListHash() {
  std::ostringstream modified_args_as_string;
  if (COMPRESS_POINTERS_BOOL) modified_args_as_string << "ptr-compr";
  if (DEBUG_BOOL) modified_args_as_string << "debug";

#ifdef DEBUG
  // These two sets are used to check that we don't leave out any flags
  // implied by --predictable in the list below.
  std::set<const char*> flags_implied_by_predictable;
  std::set<const char*> flags_ignored_because_of_predictable;
#endif

  for (const Flag& flag : flags) {
    if (flag.IsDefault()) continue;
#ifdef DEBUG
    if (flag.ImpliedBy(&v8_flags.predictable)) {
      flags_implied_by_predictable.insert(flag.name());
    }
#endif
    // We want to be able to flip --profile-deserialization without
    // causing the code cache to get invalidated by this hash.
    if (flag.PointsTo(&v8_flags.profile_deserialization)) continue;
    // Skip v8_flags.random_seed and v8_flags.predictable to allow predictable
    // code caching.
    if (flag.PointsTo(&v8_flags.random_seed)) continue;
    if (flag.PointsTo(&v8_flags.predictable)) continue;

    // The following flags are implied by --predictable (some negated).
    if (flag.PointsTo(&v8_flags.concurrent_sparkplug) ||
        flag.PointsTo(&v8_flags.concurrent_recompilation) ||
        flag.PointsTo(&v8_flags.lazy_feedback_allocation) ||
#ifdef V8_ENABLE_MAGLEV
        flag.PointsTo(&v8_flags.maglev_deopt_data_on_background) ||
        flag.PointsTo(&v8_flags.maglev_build_code_on_background) ||
#endif
        flag.PointsTo(&v8_flags.parallel_scavenge) ||
        flag.PointsTo(&v8_flags.concurrent_marking) ||
        flag.PointsTo(&v8_flags.concurrent_minor_ms_marking) ||
        flag.PointsTo(&v8_flags.concurrent_array_buffer_sweeping) ||
        flag.PointsTo(&v8_flags.parallel_marking) ||
        flag.PointsTo(&v8_flags.concurrent_sweeping) ||
        flag.PointsTo(&v8_flags.parallel_compaction) ||
        flag.PointsTo(&v8_flags.parallel_pointer_update) ||
        flag.PointsTo(&v8_flags.parallel_weak_ref_clearing) ||
        flag.PointsTo(&v8_flags.memory_reducer) ||
        flag.PointsTo(&v8_flags.cppheap_concurrent_marking) ||
        flag.PointsTo(&v8_flags.cppheap_incremental_marking) ||
        flag.PointsTo(&v8_flags.single_threaded_gc) ||
        flag.PointsTo(&v8_flags.fuzzing_and_concurrent_recompilation) ||
        flag.PointsTo(&v8_flags.predictable_and_random_seed_is_0)) {
#ifdef DEBUG
      if (flag.ImpliedBy(&v8_flags.predictable)) {
        flags_ignored_because_of_predictable.insert(flag.name());
      }
#endif
      continue;
    }
    modified_args_as_string << flag;
  }

#ifdef DEBUG
  // Disable the check for fuzzing. This check is only here
  // to ensure that we can generate reproducible code cache
  // for production builds, we don't care as much about the
  // reproducibility in the case of fuzzing.
  if (!v8_flags.fuzzing) {
    for (const char* name : flags_implied_by_predictable) {
      if (flags_ignored_because_of_predictable.find(name) ==
          flags_ignored_because_of_predictable.end()) {
        PrintF(
            "%s should be added to the list of "
            "flags_ignored_because_of_predictable\n",
            name);
        UNREACHABLE();
      }
    }
  }
#endif

  std::string args(modified_args_as_string.str());
  // Generate a hash that is not 0.
  uint32_t hash = static_cast<uint32_t>(base::hash_range(
                      args.c_str(), args.c_str() + args.length())) |
                  1;
  DCHECK_NE(hash, 0);
  return hash;
}

// Helper function to parse flags: Takes an argument arg and splits it into
// a flag name and flag value (or nullptr if they are missing). negated is set
// if the arg started with "-no" or "--no". The buffer may be used to NUL-
// terminate the name, it must be large enough to hold any possible name.
static void SplitArgument(const char* arg, char* buffer, int buffer_size,
                          const char** name, const char** value,
                          bool* negated) {
  *name = nullptr;
  *value = nullptr;
  *negated = false;

  if (arg[0] != '-') return;

  // Find the begin of the flag name.
  arg++;  // remove 1st '-'
  if (*arg == '-') {
    arg++;                    // remove 2nd '-'
    DCHECK_NE('\0', arg[0]);  // '--' arguments are handled in the caller.
  }
  if (arg[0] == 'n' && arg[1] == 'o') {
    arg += 2;  // remove "no"
    if (FlagHelpers::NormalizeChar(arg[0]) == '-') {
      arg++;  // remove dash after "no".
    }
    *negated = true;
  }
  *name = arg;

  // Find the end of the flag name.
  while (*arg != '\0' && *arg != '=') arg++;

  // Get the value if any.
  if (*arg == '=') {
    // Make a copy so we can NUL-terminate the flag name.
    size_t n = arg - *name;
    CHECK(n < static_cast<size_t>(buffer_size));  // buffer is too small
    MemCopy(buffer, *name, n);
    buffer[n] = '\0';
    *name = buffer;
    *value = arg + 1;
  }
}

template <typename T>
bool TryParseUnsigned(Flag* flag, const char* arg, const char* value,
                      char** endp, T* out_val) {
  // We do not use strtoul because it accepts negative numbers.
  // Rejects values >= 2**63 when T is 64 bits wide but that
  // seems like an acceptable trade-off.
  uint64_t max = static_cast<uint64_t>(std::numeric_limits<T>::max());
  errno = 0;
  int64_t val = static_cast<int64_t>(strtoll(value, endp, 10));
  if (val < 0 || static_cast<uint64_t>(val) > max || errno != 0) {
    PrintF(stderr,
           "Error: Value for flag %s of type %s is out of bounds "
           "[0-%" PRIu64 "]\n",
           arg, Type2String(flag->type()), max);
    return false;
  }
  *out_val = static_cast<T>(val);
  return true;
}

// static
int FlagList::SetFlagsFromCommandLine(int* argc, char** argv, bool remove_flags,
                                      HelpOptions help_options) {
  int return_code = 0;
  // Parse arguments.
  for (int i = 1; i < *argc;) {
    int j = i;  // j > 0
    const char* arg = argv[i++];
    if (arg == nullptr) continue;

    // Stop processing flags on '--'.
    if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0') break;

    // Split arg into flag components.
    char buffer[1 * KB];
    const char* name;
    const char* value;
    bool negated;
    SplitArgument(arg, buffer, sizeof buffer, &name, &value, &negated);

    if (name == nullptr) continue;

    // Lookup the flag.
    Flag* flag = FindFlagByName(name);
    if (flag == nullptr) {
      if (remove_flags) {
        // We don't recognize this flag but since we're removing
        // the flags we recognize we assume that the remaining flags
        // will be processed somewhere else so this flag might make
        // sense there.
        continue;
      } else {
        PrintF(stderr, "Error: unrecognized flag %s\n", arg);
        return_code = j;
        break;
      }
    }

    // If we still need a flag value, use the next argument if available.
    if (flag->type() != Flag::TYPE_BOOL &&
        flag->type() != Flag::TYPE_MAYBE_BOOL && value == nullptr) {
      if (i < *argc) {
        value = argv[i++];
      }
      if (!value) {
        PrintF(stderr, "Error: missing value for flag %s of type %s\n", arg,
               Type2String(flag->type()));
        return_code = j;
        break;
      }
    }

    // Set the flag.
    char* endp = const_cast<char*>("");  // *endp is only read
    switch (flag->type()) {
      case Flag::TYPE_BOOL:
        flag->set_bool_variable(!negated, Flag::SetBy::kCommandLine);
        break;
      case Flag::TYPE_MAYBE_BOOL:
        flag->set_maybe_bool_variable(!negated, Flag::SetBy::kCommandLine);
        break;
      case Flag::TYPE_INT:
        flag->set_int_variable(static_cast<int>(strtol(value, &endp, 10)),
                               Flag::SetBy::kCommandLine);
        break;
      case Flag::TYPE_UINT: {
        unsigned int parsed_value;
        if (TryParseUnsigned(flag, arg, value, &endp, &parsed_value)) {
          flag->set_uint_variable(parsed_value, Flag::SetBy::kCommandLine);
        } else {
          return_code = j;
        }
        break;
      }
      case Flag::TYPE_UINT64: {
        uint64_t parsed_value;
        if (TryParseUnsigned(flag, arg, value, &endp, &parsed_value)) {
          flag->set_uint64_variable(parsed_value, Flag::SetBy::kCommandLine);
        } else {
          return_code = j;
        }
        break;
      }
      case Flag::TYPE_FLOAT:
        flag->set_float_variable(strtod(value, &endp),
                                 Flag::SetBy::kCommandLine);
        break;
      case Flag::TYPE_SIZE_T: {
        size_t parsed_value;
        if (TryParseUnsigned(flag, arg, value, &endp, &parsed_value)) {
          flag->set_size_t_variable(parsed_value, Flag::SetBy::kCommandLine);
        } else {
          return_code = j;
        }
        break;
      }
      case Flag::TYPE_STRING:
        flag->set_string_value(value ? StrDup(value) : nullptr, true,
                               Flag::SetBy::kCommandLine);
        break;
    }

    // Handle errors.
    bool is_bool_type = flag->type() == Flag::TYPE_BOOL ||
                        flag->type() == Flag::TYPE_MAYBE_BOOL;
    if ((is_bool_type && value != nullptr) || (!is_bool_type && negated) ||
        *endp != '\0') {
      // TODO(neis): TryParseUnsigned may return with {*endp == '\0'} even in
      // an error case.
      PrintF(stderr, "Error: illegal value for flag %s of type %s\n", arg,
             Type2String(flag->type()));
      if (is_bool_type) {
        PrintF(stderr,
               "To set or unset a boolean flag, use --flag or --no-flag.\n");
      }
      return_code = j;
      break;
    }

    // Remove the flag & value from the command.
    if (remove_flags) {
      while (j < i) {
        argv[j++] = nullptr;
      }
    }
  }

  if (v8_flags.help) {
    if (help_options.HasUsage()) {
      PrintF(stdout, "%s", help_options.usage());
    }
    PrintHelp();
    if (help_options.ShouldExit()) {
      exit(0);
    }
  }

  if (remove_flags) {
    // Shrink the argument list.
    int j = 1;
    for (int i = 1; i < *argc; i++) {
      if (argv[i] != nullptr) argv[j++] = argv[i];
    }
    *argc = j;
  } else if (return_code != 0) {
    if (return_code + 1 < *argc) {
      PrintF(stderr, "The remaining arguments were ignored:");
      for (int i = return_code + 1; i < *argc; ++i) {
        PrintF(stderr, " %s", argv[i]);
      }
      PrintF(stderr, "\n");
    }
  }
  if (return_code != 0) PrintF(stderr, "Try --help for options\n");

  return return_code;
}

static char* SkipWhiteSpace(char* p) {
  while (*p != '\0' && isspace(*p) != 0) p++;
  return p;
}

static char* SkipBlackSpace(char* p) {
  while (*p != '\0' && isspace(*p) == 0) p++;
  return p;
}

// static
int FlagList::SetFlagsFromString(const char* str, size_t len) {
  // Make a 0-terminated copy of str.
  std::unique_ptr<char[]> copy0{NewArray<char>(len + 1)};
  MemCopy(copy0.get(), str, len);
  copy0[len] = '\0';

  // Strip leading white space.
  char* copy = SkipWhiteSpace(copy0.get());

  // Count the number of 'arguments'.
  int argc = 1;  // be compatible with SetFlagsFromCommandLine()
  for (char* p = copy; *p != '\0'; argc++) {
    p = SkipBlackSpace(p);
    p = SkipWhiteSpace(p);
  }

  // Allocate argument array.
  base::ScopedVector<char*> argv(argc);

  // Split the flags string into arguments.
  argc = 1;  // be compatible with SetFlagsFromCommandLine()
  for (char* p = copy; *p != '\0'; argc++) {
    argv[argc] = p;
    p = SkipBlackSpace(p);
    if (*p != '\0') *p++ = '\0';  // 0-terminate argument
    p = SkipWhiteSpace(p);
  }

  return SetFlagsFromCommandLine(&argc, argv.begin(), false);
}

// static
void FlagList::FreezeFlags() {
  // Disallow changes via the API by setting {flags_frozen}.
  flags_frozen.store(true, std::memory_order_relaxed);
  // Also memory-protect the memory that holds the flag values. This makes it
  // impossible for attackers to overwrite values, except if they find a way to
  // first unprotect the memory again.
  // Note that for string flags we only protect the pointer itself, but not the
  // string storage. TODO(12887): Fix this.
  base::OS::SetDataReadOnly(&v8_flags, sizeof(v8_flags));
}

// static
bool FlagList::IsFrozen() {
  return flags_frozen.load(std::memory_order_relaxed);
}

// static
void FlagList::ReleaseDynamicAllocations() {
  flag_hash = 0;
  for (size_t i = 0; i < kNumFlags; ++i) {
    flags[i].ReleaseDynamicAllocations();
  }
}

// static
void FlagList::PrintHelp() {
  CpuFeatures::Probe(false);
  CpuFeatures::PrintTarget();
  CpuFeatures::PrintFeatures();

  StdoutStream os;
  os << "The following syntax for options is accepted (both '-' and '--' are "
        "ok):\n"
        "  --flag        (bool flags only)\n"
        "  --no-flag     (bool flags only)\n"
        "  --flag=value  (non-bool flags only, no spaces around '=')\n"
        "  --flag value  (non-bool flags only)\n"
        "  --            (captures all remaining args in JavaScript)\n\n";
  os << "Options:\n";

  for (const Flag& f : flags) {
    os << "  " << FlagName{f.name()} << " (" << f.comment() << ")\n"
       << "        type: " << Type2String(f.type()) << "  default: " << f
       << "\n";
  }
}

// static
void FlagList::PrintValues() {
  StdoutStream os;
  for (const Flag& f : flags) {
    os << f << "\n";
  }
}

namespace {

class ImplicationProcessor {
 public:
  // Returns {true} if any flag value was changed.
  bool EnforceImplications() {
    bool changed = false;
#define FLAG_MODE_DEFINE_IMPLICATIONS
#include "src/flags/flag-definitions.h"  // NOLINT(build/include)
#undef FLAG_MODE_DEFINE_IMPLICATIONS
    CheckForCycle();
    return changed;
  }

 private:
  // Called from {DEFINE_*_IMPLICATION} in flag-definitions.h.
  template <class T>
  bool TriggerImplication(bool premise, const char* premise_name,
                          FlagValue<T>* conclusion_value,
                          const char* conclusion_name, T value,
                          bool weak_implication) {
    if (!premise) return false;
    Flag* conclusion_flag = FindImplicationFlagByName(conclusion_name);
    if (!conclusion_flag->CheckFlagChange(
            weak_implication ? Flag::SetBy::kWeakImplication
                             : Flag::SetBy::kImplication,
            conclusion_value->value() != value, premise_name)) {
      return false;
    }
    if (V8_UNLIKELY(num_iterations_ >= kMaxNumIterations)) {
      cycle_ << "\n" << FlagName{premise_name} << " -> ";
      if constexpr (std::is_same_v<T, bool>) {
        cycle_ << FlagName{conclusion_flag->name(), !value};
      } else {
        cycle_ << FlagName{conclusion_flag->name()} << " = " << value;
      }
    }
    *conclusion_value = value;
    return true;
  }

  // Called from {DEFINE_*_IMPLICATION} in flag-definitions.h, when the
  // conclusion flag is read-only (note this is the const overload of the
  // function just above).
  template <class T>
  bool TriggerImplication(bool premise, const char* premise_name,
                          const FlagValue<T>* conclusion_value,
                          const char* conclusion_name, T value,
                          bool weak_implication) {
    if (!premise) return false;
    Flag* conclusion_flag = FindImplicationFlagByName(conclusion_name);
    // Because this is the `const FlagValue*` overload:
    DCHECK(conclusion_flag->IsReadOnly());
    if (!conclusion_flag->CheckFlagChange(
            weak_implication ? Flag::SetBy::kWeakImplication
                             : Flag::SetBy::kImplication,
            conclusion_value->value() != value, premise_name)) {
      return false;
    }
    // Must equal the default value, otherwise CheckFlagChange should've
    // returned false.
    DCHECK_EQ(value, conclusion_flag->GetDefaultValue<T>());
    return true;
  }

  void CheckForCycle() {
    // Make sure flag implications reach a fixed point within
    // {kMaxNumIterations} iterations.
    if (++num_iterations_ < kMaxNumIterations) return;

    if (num_iterations_ == kMaxNumIterations) {
      // Start cycle detection.
      DCHECK(cycle_.str().empty());
      cycle_start_hash_ = ComputeFlagListHash();
      return;
    }

    DCHECK_NE(0, cycle_start_hash_);
    // We accept spurious but highly unlikely hash collisions here. This is
    // only a debug output anyway.
    if (ComputeFlagListHash() == cycle_start_hash_) {
      DCHECK(!cycle_.str().empty());
      // {cycle_} starts with a newline.
      FATAL("Cycle in flag implications:%s", cycle_.str().c_str());
    }
    // We must have found a cycle within another {kMaxNumIterations}.
    DCHECK_GE(2 * kMaxNumIterations, num_iterations_);
  }

  static constexpr size_t kMaxNumIterations = kNumFlags;
  size_t num_iterations_ = 0;
  // After {kMaxNumIterations} we use the following two fields for finding
  // cycles in flags.
  uint32_t cycle_start_hash_;
  std::ostringstream cycle_;
};

}  // namespace

#define CONTRADICTION(flag1, flag2)                         \
  (v8_flags.flag1 && v8_flags.flag2)                        \
      ? std::make_tuple(FindFlagByPointer(&v8_flags.flag1), \
                        FindFlagByPointer(&v8_flags.flag2)) \
      : std::make_tuple(nullptr, nullptr)

// static
void FlagList::ResolveContradictionsWhenFuzzing() {
  if (!i::v8_flags.fuzzing) return;

  // List flags that lead to known contradictory cycles when both are passed
  // on the command line. One of them will be reset with precedence left to
  // right.
  std::tuple<Flag*, Flag*> contradictions[] = {
      CONTRADICTION(jitless, maglev_future),
      CONTRADICTION(jitless, stress_maglev),
      CONTRADICTION(jitless, stress_concurrent_inlining),
      CONTRADICTION(jitless, stress_concurrent_inlining_attach_code),
      CONTRADICTION(predictable, stress_concurrent_inlining_attach_code),
      CONTRADICTION(stress_concurrent_inlining, assert_types),
      CONTRADICTION(stress_concurrent_inlining_attach_code, assert_types),
  };
  for (auto [flag1, flag2] : contradictions) {
    if (!flag1 || !flag2) continue;
    // Check values again, since a flag might have already been reset by
    // another contradiction.
    if (!flag1->bool_variable() || !flag2->bool_variable()) continue;

    Flag* flag = flag1;
    if (flag->IsDefault()) {
      flag = flag2;
    }
    if (flag->IsDefault()) {
      FATAL("Multiple flags with contradictory default values");
    }

    std::cerr << "Warning: resetting flag --" << flag->name()
              << " due to conflicting flags" << std::endl;
    flag->Reset();
  }
}

#undef CONTRADICTION

// static
void FlagList::EnforceFlagImplications() {
  for (ImplicationProcessor proc; proc.EnforceImplications();) {
    // Continue processing (recursive) implications. The processor has an
    // internal limit to avoid endless recursion.
  }
}

// static
uint32_t FlagList::Hash() {
  if (uint32_t hash = flag_hash.load(std::memory_order_relaxed)) return hash;
  uint32_t hash = ComputeFlagListHash();
  flag_hash.store(hash, std::memory_order_relaxed);
  return hash;
}

// static
void FlagList::ResetFlagHash() {
  // If flags are frozen, we should not need to reset the hash since we cannot
  // change flag values anyway.
  CHECK(!IsFrozen());
  flag_hash = 0;
}

}  // namespace v8::internal
