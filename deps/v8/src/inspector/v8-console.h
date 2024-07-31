// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8_CONSOLE_H_
#define V8_INSPECTOR_V8_CONSOLE_H_

#include <map>

#include "include/v8-array-buffer.h"
#include "include/v8-external.h"
#include "include/v8-local-handle.h"
#include "src/base/macros.h"
#include "src/debug/interface-types.h"

namespace v8 {
class ObjectTemplate;
class Set;
}  // namespace v8

namespace v8_inspector {

class InspectedContext;
class TaskInfo;
class V8InspectorImpl;

// Console API
// https://console.spec.whatwg.org/#console-namespace
class V8Console : public v8::debug::ConsoleDelegate {
 public:
  v8::Local<v8::Object> createCommandLineAPI(v8::Local<v8::Context> context,
                                             int sessionId);
  void installMemoryGetter(v8::Local<v8::Context> context,
                           v8::Local<v8::Object> console);
  void installAsyncStackTaggingAPI(v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> console);
  void cancelConsoleTask(TaskInfo* taskInfo);

  std::map<void*, std::unique_ptr<TaskInfo>>& AllConsoleTasksForTest() {
    return m_tasks;
  }

  class V8_NODISCARD CommandLineAPIScope {
   public:
    CommandLineAPIScope(v8::Local<v8::Context>,
                        v8::Local<v8::Object> commandLineAPI,
                        v8::Local<v8::Object> global);
    ~CommandLineAPIScope();
    CommandLineAPIScope(const CommandLineAPIScope&) = delete;
    CommandLineAPIScope& operator=(const CommandLineAPIScope&) = delete;

   private:
    static void accessorGetterCallback(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>&);
    static void accessorSetterCallback(v8::Local<v8::Name>,
                                       v8::Local<v8::Value>,
                                       const v8::PropertyCallbackInfo<void>&);

    v8::Local<v8::Context> context() const { return m_context.Get(m_isolate); }
    v8::Local<v8::Object> commandLineAPI() const {
      return m_commandLineAPI.Get(m_isolate);
    }
    v8::Local<v8::Object> global() const { return m_global.Get(m_isolate); }
    v8::Local<v8::PrimitiveArray> installedMethods() const {
      return m_installedMethods.Get(m_isolate);
    }
    v8::Local<v8::ArrayBuffer> thisReference() const {
      return m_thisReference.Get(m_isolate);
    }

    v8::Isolate* m_isolate;
    v8::Global<v8::Context> m_context;
    v8::Global<v8::Object> m_commandLineAPI;
    v8::Global<v8::Object> m_global;
    v8::Global<v8::PrimitiveArray> m_installedMethods;
    v8::Global<v8::ArrayBuffer> m_thisReference;
  };

  explicit V8Console(V8InspectorImpl* inspector);

 private:
  friend class TaskInfo;

  void Debug(const v8::debug::ConsoleCallArguments&,
             const v8::debug::ConsoleContext& consoleContext) override;
  void Error(const v8::debug::ConsoleCallArguments&,
             const v8::debug::ConsoleContext& consoleContext) override;
  void Info(const v8::debug::ConsoleCallArguments&,
            const v8::debug::ConsoleContext& consoleContext) override;
  void Log(const v8::debug::ConsoleCallArguments&,
           const v8::debug::ConsoleContext& consoleContext) override;
  void Warn(const v8::debug::ConsoleCallArguments&,
            const v8::debug::ConsoleContext& consoleContext) override;
  void Dir(const v8::debug::ConsoleCallArguments&,
           const v8::debug::ConsoleContext& consoleContext) override;
  void DirXml(const v8::debug::ConsoleCallArguments&,
              const v8::debug::ConsoleContext& consoleContext) override;
  void Table(const v8::debug::ConsoleCallArguments&,
             const v8::debug::ConsoleContext& consoleContext) override;
  void Trace(const v8::debug::ConsoleCallArguments&,
             const v8::debug::ConsoleContext& consoleContext) override;
  void Group(const v8::debug::ConsoleCallArguments&,
             const v8::debug::ConsoleContext& consoleContext) override;
  void GroupCollapsed(const v8::debug::ConsoleCallArguments&,
                      const v8::debug::ConsoleContext& consoleContext) override;
  void GroupEnd(const v8::debug::ConsoleCallArguments&,
                const v8::debug::ConsoleContext& consoleContext) override;
  void Clear(const v8::debug::ConsoleCallArguments&,
             const v8::debug::ConsoleContext& consoleContext) override;
  void Count(const v8::debug::ConsoleCallArguments&,
             const v8::debug::ConsoleContext& consoleContext) override;
  void CountReset(const v8::debug::ConsoleCallArguments&,
                  const v8::debug::ConsoleContext& consoleContext) override;
  void Assert(const v8::debug::ConsoleCallArguments&,
              const v8::debug::ConsoleContext& consoleContext) override;
  void Profile(const v8::debug::ConsoleCallArguments&,
               const v8::debug::ConsoleContext& consoleContext) override;
  void ProfileEnd(const v8::debug::ConsoleCallArguments&,
                  const v8::debug::ConsoleContext& consoleContext) override;
  void Time(const v8::debug::ConsoleCallArguments&,
            const v8::debug::ConsoleContext& consoleContext) override;
  void TimeLog(const v8::debug::ConsoleCallArguments&,
               const v8::debug::ConsoleContext& consoleContext) override;
  void TimeEnd(const v8::debug::ConsoleCallArguments&,
               const v8::debug::ConsoleContext& consoleContext) override;
  void TimeStamp(const v8::debug::ConsoleCallArguments&,
                 const v8::debug::ConsoleContext& consoleContext) override;

  template <void (V8Console::*func)(const v8::FunctionCallbackInfo<v8::Value>&)>
  static void call(const v8::FunctionCallbackInfo<v8::Value>& info) {
    V8Console* console =
        static_cast<V8Console*>(info.Data().As<v8::External>()->Value());
    (console->*func)(info);
  }
  using CommandLineAPIData = std::pair<V8Console*, int>;
  template <void (V8Console::*func)(const v8::FunctionCallbackInfo<v8::Value>&,
                                    int)>
  static void call(const v8::FunctionCallbackInfo<v8::Value>& info) {
    CommandLineAPIData* data = static_cast<CommandLineAPIData*>(
        info.Data().As<v8::ArrayBuffer>()->GetBackingStore()->Data());
    (data->first->*func)(info, data->second);
  }
  template <void (V8Console::*func)(const v8::debug::ConsoleCallArguments&,
                                    const v8::debug::ConsoleContext&)>
  static void call(const v8::FunctionCallbackInfo<v8::Value>& info) {
    CommandLineAPIData* data = static_cast<CommandLineAPIData*>(
        info.Data().As<v8::ArrayBuffer>()->GetBackingStore()->Data());
    v8::debug::ConsoleCallArguments args(info);
    (data->first->*func)(args, v8::debug::ConsoleContext());
  }

  // TODO(foolip): There is no spec for the Memory Info API, see blink-dev:
  // https://groups.google.com/a/chromium.org/d/msg/blink-dev/g5YRCGpC9vs/b4OJz71NmPwJ
  void memoryGetterCallback(const v8::FunctionCallbackInfo<v8::Value>&);
  void memorySetterCallback(const v8::FunctionCallbackInfo<v8::Value>&);

  void createTask(const v8::FunctionCallbackInfo<v8::Value>&);
  void runTask(const v8::FunctionCallbackInfo<v8::Value>&);

  // CommandLineAPI
  void keysCallback(const v8::FunctionCallbackInfo<v8::Value>&, int sessionId);
  void valuesCallback(const v8::FunctionCallbackInfo<v8::Value>&,
                      int sessionId);
  void debugFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>&,
                             int sessionId);
  void undebugFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>&,
                               int sessionId);
  void monitorFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>&,
                               int sessionId);
  void unmonitorFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>&,
                                 int sessionId);
  void lastEvaluationResultCallback(const v8::FunctionCallbackInfo<v8::Value>&,
                                    int sessionId);
  void inspectCallback(const v8::FunctionCallbackInfo<v8::Value>&,
                       int sessionId);
  void copyCallback(const v8::FunctionCallbackInfo<v8::Value>&, int sessionId);
  void inspectedObject(const v8::FunctionCallbackInfo<v8::Value>&,
                       int sessionId, unsigned num);
  void inspectedObject0(const v8::FunctionCallbackInfo<v8::Value>& info,
                        int sessionId) {
    inspectedObject(info, sessionId, 0);
  }
  void inspectedObject1(const v8::FunctionCallbackInfo<v8::Value>& info,
                        int sessionId) {
    inspectedObject(info, sessionId, 1);
  }
  void inspectedObject2(const v8::FunctionCallbackInfo<v8::Value>& info,
                        int sessionId) {
    inspectedObject(info, sessionId, 2);
  }
  void inspectedObject3(const v8::FunctionCallbackInfo<v8::Value>& info,
                        int sessionId) {
    inspectedObject(info, sessionId, 3);
  }
  void inspectedObject4(const v8::FunctionCallbackInfo<v8::Value>& info,
                        int sessionId) {
    inspectedObject(info, sessionId, 4);
  }
  void queryObjectsCallback(const v8::FunctionCallbackInfo<v8::Value>& info,
                            int sessionId);

  // Lazily creates m_taskInfoKey and returns a local handle to it. We can't
  // initialize m_taskInfoKey in the constructor as it would be part of
  // Chromium's context snapshot.
  v8::Local<v8::Private> taskInfoKey();

  // Lazily creates m_taskTemplate and returns a local handle to it.
  // Similarly to m_taskInfoKey, we can't create the template upfront as to not
  // be part of Chromium's context snapshot.
  v8::Local<v8::ObjectTemplate> taskTemplate();

  V8InspectorImpl* m_inspector;

  // All currently alive tasks. We mark tasks immediately as weak when created
  // but we need the finalizer to cancel the task when GC cleans them up.
  std::map<void*, std::unique_ptr<TaskInfo>> m_tasks;

  // We use a private symbol to stash the `TaskInfo` as an v8::External on the
  // JS task objects created by `console.createTask`.
  v8::Global<v8::Private> m_taskInfoKey;

  // We cache the task template for the async stack tagging API for faster
  // instantiation. Use `taskTemplate()` to retrieve the lazily created
  // template.
  v8::Global<v8::ObjectTemplate> m_taskTemplate;
};

/**
 * Each JS task object created via `console.createTask` has a corresponding
 * `TaskInfo` object on the C++ side (in a 1:1 relationship).
 *
 * The `TaskInfo` holds on weakly to the JS task object.
 * The JS task objects uses a private symbol to store a pointer to the
 * `TaskInfo` object (via v8::External).
 *
 * The `TaskInfo` objects holds all the necessary information we need to
 * properly cancel the corresponding async task then the JS task object
 * gets GC'ed.
 */
class TaskInfo {
 public:
  TaskInfo(v8::Isolate* isolate, V8Console* console,
           v8::Local<v8::Object> task);

  // For these task IDs we duplicate the ID logic from blink and use even
  // pointers compared to the odd IDs we use for promises. This guarantees that
  // we don't have any conflicts between task IDs.
  void* Id() const {
    return reinterpret_cast<void*>(reinterpret_cast<intptr_t>(this) << 1);
  }

  // After calling `Cancel` the `TaskInfo` instance is destroyed.
  void Cancel() { m_console->cancelConsoleTask(this); }

 private:
  v8::Global<v8::Object> m_task;
  V8Console* m_console = nullptr;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8_CONSOLE_H_
