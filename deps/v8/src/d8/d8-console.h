// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_D8_D8_CONSOLE_H_
#define V8_D8_D8_CONSOLE_H_

#include <map>

#include "src/base/platform/time.h"
#include "src/debug/interface-types.h"

namespace v8 {

class CpuProfiler;

class D8Console : public debug::ConsoleDelegate {
 public:
  explicit D8Console(Isolate* isolate);
  ~D8Console() override;

  CpuProfiler* profiler() const { return profiler_; }

  void DisposeProfiler();

 private:
  void Assert(const debug::ConsoleCallArguments& args,
              const v8::debug::ConsoleContext&) override;
  void Log(const debug::ConsoleCallArguments& args,
           const v8::debug::ConsoleContext&) override;
  void Error(const debug::ConsoleCallArguments& args,
             const v8::debug::ConsoleContext&) override;
  void Warn(const debug::ConsoleCallArguments& args,
            const v8::debug::ConsoleContext&) override;
  void Info(const debug::ConsoleCallArguments& args,
            const v8::debug::ConsoleContext&) override;
  void Debug(const debug::ConsoleCallArguments& args,
             const v8::debug::ConsoleContext&) override;
  void Profile(const debug::ConsoleCallArguments& args,
               const v8::debug::ConsoleContext& context) override;
  void ProfileEnd(const debug::ConsoleCallArguments& args,
                  const v8::debug::ConsoleContext& context) override;
  void Time(const debug::ConsoleCallArguments& args,
            const v8::debug::ConsoleContext&) override;
  void TimeLog(const debug::ConsoleCallArguments& args,
               const v8::debug::ConsoleContext&) override;
  void TimeEnd(const debug::ConsoleCallArguments& args,
               const v8::debug::ConsoleContext&) override;
  void TimeStamp(const debug::ConsoleCallArguments& args,
                 const v8::debug::ConsoleContext&) override;
  void Trace(const debug::ConsoleCallArguments& args,
             const v8::debug::ConsoleContext&) override;

  Isolate* isolate_;
  // Start times for the named timers created with console.time('foo') calls.
  // Calling console.time() and console.timeEnd() without an explicit timer
  // name will use the 'default' timer (similar to what the browser does).
  // See https://console.spec.whatwg.org/#timer-table for the specification.
  std::map<std::string, base::TimeTicks> timers_;
  // Origin for the timer used by console.timeStamp() calls.
  base::TimeTicks origin_;
  CpuProfiler* profiler_{nullptr};
  bool profiler_active_{false};
};

}  // namespace v8

#endif  // V8_D8_D8_CONSOLE_H_
