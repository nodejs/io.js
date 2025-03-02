// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_INTERFACE_TYPES_H_
#define V8_DEBUG_INTERFACE_TYPES_H_

#include <cstdint>

#include "include/v8-function-callback.h"
#include "include/v8-local-handle.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "v8-isolate.h"

namespace v8 {

class String;

namespace internal {
class BuiltinArguments;
}  // namespace internal

namespace debug {

/**
 * Defines location inside script.
 * Lines and columns are 0-based.
 */
class V8_EXPORT_PRIVATE Location {
 public:
  Location(int line_number, int column_number);
  /**
   * Create empty location.
   */
  Location();

  int GetLineNumber() const;
  int GetColumnNumber() const;
  bool IsEmpty() const;

 private:
  int line_number_;
  int column_number_;
  bool is_empty_;
};

enum DebugAsyncActionType {
  kDebugAwait,
  kDebugPromiseThen,
  kDebugPromiseCatch,
  kDebugPromiseFinally,
  kDebugWillHandle,
  kDebugDidHandle,
  kDebugStackTraceCaptured
};

enum BreakLocationType {
  kCallBreakLocation,
  kReturnBreakLocation,
  kDebuggerStatementBreakLocation,
  kCommonBreakLocation
};

enum class CoverageMode {
  // Make use of existing information in feedback vectors on the heap.
  // Only return a yes/no result. Optimization and GC are not affected.
  // Collecting best effort coverage does not reset counters.
  kBestEffort,
  // Disable optimization and prevent feedback vectors from being garbage
  // collected in order to preserve precise invocation counts. Collecting
  // precise count coverage resets counters to get incremental updates.
  kPreciseCount,
  // We are only interested in a yes/no result for the function. Optimization
  // and GC can be allowed once a function has been invoked. Collecting
  // precise binary coverage resets counters for incremental updates.
  kPreciseBinary,
  // Similar to the precise coverage modes but provides coverage at a
  // lower granularity. Design doc: goo.gl/lA2swZ.
  kBlockCount,
  kBlockBinary,
};

class V8_EXPORT_PRIVATE BreakLocation : public Location {
 public:
  BreakLocation(int line_number, int column_number, BreakLocationType type)
      : Location(line_number, column_number), type_(type) {}

  BreakLocationType type() const { return type_; }

 private:
  BreakLocationType type_;
};

class ConsoleCallArguments {
 public:
  int Length() const { return length_; }
  /**
   * Accessor for the available arguments. Returns `undefined` if the index
   * is out of bounds.
   */
  V8_INLINE v8::Local<v8::Value> operator[](int i) const {
    // values_ points to the first argument.
    if (i < 0 || length_ <= i) return Undefined(GetIsolate());
    DCHECK_NOT_NULL(values_);
    return Local<Value>::FromSlot(values_ + i);
  }

  V8_INLINE v8::Isolate* GetIsolate() const { return isolate_; }

  explicit ConsoleCallArguments(const v8::FunctionCallbackInfo<v8::Value>&);
  explicit ConsoleCallArguments(internal::Isolate* isolate,
                                const internal::BuiltinArguments&);

 private:
  v8::Isolate* isolate_;
  internal::Address* values_;
  int length_;
};

class ConsoleContext {
 public:
  ConsoleContext(int id, v8::Local<v8::String> name) : id_(id), name_(name) {}
  ConsoleContext() : id_(0) {}

  int id() const { return id_; }
  v8::Local<v8::String> name() const { return name_; }

 private:
  int id_;
  v8::Local<v8::String> name_;
};

class ConsoleDelegate {
 public:
  virtual void Debug(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Error(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Info(const ConsoleCallArguments& args,
                    const ConsoleContext& context) {}
  virtual void Log(const ConsoleCallArguments& args,
                   const ConsoleContext& context) {}
  virtual void Warn(const ConsoleCallArguments& args,
                    const ConsoleContext& context) {}
  virtual void Dir(const ConsoleCallArguments& args,
                   const ConsoleContext& context) {}
  virtual void DirXml(const ConsoleCallArguments& args,
                      const ConsoleContext& context) {}
  virtual void Table(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Trace(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Group(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void GroupCollapsed(const ConsoleCallArguments& args,
                              const ConsoleContext& context) {}
  virtual void GroupEnd(const ConsoleCallArguments& args,
                        const ConsoleContext& context) {}
  virtual void Clear(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void Count(const ConsoleCallArguments& args,
                     const ConsoleContext& context) {}
  virtual void CountReset(const ConsoleCallArguments& args,
                          const ConsoleContext& context) {}
  virtual void Assert(const ConsoleCallArguments& args,
                      const ConsoleContext& context) {}
  virtual void Profile(const ConsoleCallArguments& args,
                       const ConsoleContext& context) {}
  virtual void ProfileEnd(const ConsoleCallArguments& args,
                          const ConsoleContext& context) {}
  virtual void Time(const ConsoleCallArguments& args,
                    const ConsoleContext& context) {}
  virtual void TimeLog(const ConsoleCallArguments& args,
                       const ConsoleContext& context) {}
  virtual void TimeEnd(const ConsoleCallArguments& args,
                       const ConsoleContext& context) {}
  virtual void TimeStamp(const ConsoleCallArguments& args,
                         const ConsoleContext& context) {}
  virtual ~ConsoleDelegate() = default;
};

using BreakpointId = int;

}  // namespace debug
}  // namespace v8

#endif  // V8_DEBUG_INTERFACE_TYPES_H_
