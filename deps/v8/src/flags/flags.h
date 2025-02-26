// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FLAGS_FLAGS_H_
#define V8_FLAGS_FLAGS_H_

#include <optional>

#include "src/common/globals.h"

#if V8_ENABLE_WEBASSEMBLY
// Include the wasm-limits.h header for some default values of Wasm flags.
// This can be reverted once we can use designated initializations (C++20) for
// {v8_flags} (defined in flags.cc) instead of specifying the default values in
// the header and using the default constructor.
#include "src/wasm/wasm-limits.h"
#endif

namespace v8::internal {

// The value of a single flag (this is the type of all v8_flags.* fields).
template <typename T>
class FlagValue {
  // {FlagValue} types will be memory-protected in {FlagList::FreezeFlags}.
  // We currently allow the following types to be used for flags:
  // - Arithmetic types like bool, int, size_t, double; those will trivially be
  //   protected.
  // - std::optional<bool>, which is basically a POD, and can also be
  //   protected.
  // - const char*, for which we currently do not protect the actual string
  //   value. TODO(12887): Also protect the string storage.
  //
  // Other types can be added as needed, after checking that memory protection
  // works for them.
  static_assert(std::is_same_v<std::decay_t<T>, T>);
  static_assert(std::is_arithmetic_v<T> ||
                std::is_same_v<std::optional<bool>, T> ||
                std::is_same_v<const char*, T>);

 public:
  using underlying_type = T;
  explicit constexpr FlagValue(T value) : value_(value) {}

  // Implicitly convert to a {T}. Not marked {constexpr} so we do not get
  // compiler warnings about dead code (when checking readonly flags).
  operator T() const { return value_; }

  // Explicitly convert to a {T} via {value()}. This is {constexpr} so we can
  // use it for computing other constants.
  constexpr T value() const { return value_; }

  // Assign a new value (defined below).
  inline FlagValue<T>& operator=(T new_value);

 private:
  T value_;
};

// Declare a struct to hold all of our flags.
struct alignas(kMinimumOSPageSize) FlagValues {
  FlagValues() = default;
  // No copying, moving, or assigning. This is a singleton struct.
  FlagValues(const FlagValues&) = delete;
  FlagValues(FlagValues&&) = delete;
  FlagValues& operator=(const FlagValues&) = delete;
  FlagValues& operator=(FlagValues&&) = delete;

#define FLAG_MODE_DECLARE
#include "src/flags/flag-definitions.h"  // NOLINT(build/include)
#undef FLAG_MODE_DECLARE
};

V8_EXPORT_PRIVATE extern FlagValues v8_flags;

// The global list of all flags.
class V8_EXPORT_PRIVATE FlagList {
 public:
  class HelpOptions {
   public:
    enum ExitBehavior : bool { kExit = true, kDontExit = false };

    explicit HelpOptions(ExitBehavior exit_behavior = kExit,
                         const char* usage = nullptr)
        : exit_behavior_(exit_behavior), usage_(usage) {}

    bool ShouldExit() { return exit_behavior_ == kExit; }
    bool HasUsage() { return usage_ != nullptr; }
    const char* usage() { return usage_; }

   private:
    ExitBehavior exit_behavior_;
    const char* usage_;
  };

  // Set the flag values by parsing the command line. If remove_flags is
  // set, the recognized flags and associated values are removed from (argc,
  // argv) and only unknown arguments remain. Returns 0 if no error occurred.
  // Otherwise, returns the argv index > 0 for the argument where an error
  // occurred. In that case, (argc, argv) will remain unchanged independent of
  // the remove_flags value, and no assumptions about flag settings should be
  // made. If exit_behavior is set to Exit and --help has been specified on the
  // command line, then the usage string will be printed, if it was specified,
  // followed by the help flag and then the process will exit. Otherwise the
  // flag help will be displayed but execution will continue.
  //
  // The following syntax for flags is accepted (both '-' and '--' are ok):
  //
  //   --flag        (bool flags only)
  //   --no-flag     (bool flags only)
  //   --flag=value  (non-bool flags only, no spaces around '=')
  //   --flag value  (non-bool flags only)
  //   --            (capture all remaining args in JavaScript)
  static int SetFlagsFromCommandLine(
      int* argc, char** argv, bool remove_flags,
      FlagList::HelpOptions help_options = FlagList::HelpOptions());

  // Set the flag values by parsing the string str. Splits string into argc
  // substrings argv[], each of which consisting of non-white-space chars,
  // and then calls SetFlagsFromCommandLine() and returns its result.
  static int SetFlagsFromString(const char* str, size_t len);

  // Freeze the current flag values (disallow changes via the API).
  static void FreezeFlags();

  // Returns true if the flags are currently frozen.
  static bool IsFrozen();

  // Free dynamically allocated memory of strings. This is called during
  // teardown; flag values cannot be used afterwards any more.
  static void ReleaseDynamicAllocations();

  // Print help to stdout with flags, types, and default values.
  static void PrintHelp();

  static void PrintValues();

  // Prints JS and Wasm feature flags, categorized by in-progress, staging, and
  // shipping, as JSON. Used by scripts to clean up flags in test files.
  static void PrintFeatureFlagsJSON();

  // Reset some contradictory flags provided on the command line during
  // fuzzing.
  static void ResolveContradictionsWhenFuzzing();

  // Set flags as consequence of being implied by another flag.
  static void EnforceFlagImplications();

  // Hash of flags (to quickly determine mismatching flag expectations).
  // This hash is calculated during V8::Initialize and cached.
  static uint32_t Hash();

 private:
  // Reset the flag hash on flag changes. This is a private method called from
  // {FlagValue<T>::operator=}; there should be no need to call it from any
  // other place.
  static void ResetFlagHash();

  // Make {FlagValue<T>} a friend, so it can call {ResetFlagHash()}.
  template <typename T>
  friend class FlagValue;
};

template <typename T>
FlagValue<T>& FlagValue<T>::operator=(T new_value) {
  if (new_value != value_) {
    FlagList::ResetFlagHash();
    value_ = new_value;
  }
  return *this;
}

}  // namespace v8::internal

#endif  // V8_FLAGS_FLAGS_H_
