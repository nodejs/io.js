// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8_CONSOLE_MESSAGE_H_
#define V8_INSPECTOR_V8_CONSOLE_MESSAGE_H_

#include <deque>
#include <map>
#include <memory>
#include <set>

#include "include/v8-local-handle.h"
#include "include/v8-persistent-handle.h"
#include "src/inspector/protocol/Console.h"
#include "src/inspector/protocol/Forward.h"
#include "src/inspector/protocol/Runtime.h"

namespace v8_inspector {

class InspectedContext;
class V8InspectorImpl;
class V8InspectorSessionImpl;
class V8StackTraceImpl;

enum class V8MessageOrigin { kConsole, kException, kRevokedException };

enum class ConsoleAPIType {
  kLog,
  kDebug,
  kInfo,
  kError,
  kWarning,
  kDir,
  kDirXML,
  kTable,
  kTrace,
  kStartGroup,
  kStartGroupCollapsed,
  kEndGroup,
  kClear,
  kAssert,
  kTimeEnd,
  kCount
};

class V8ConsoleMessage {
 public:
  ~V8ConsoleMessage();

  static std::unique_ptr<V8ConsoleMessage> createForConsoleAPI(
      v8::Local<v8::Context> v8Context, int contextId, int groupId,
      V8InspectorImpl* inspector, double timestamp, ConsoleAPIType,
      v8::MemorySpan<const v8::Local<v8::Value>> arguments,
      const String16& consoleContext, std::unique_ptr<V8StackTraceImpl>);

  static std::unique_ptr<V8ConsoleMessage> createForException(
      double timestamp, const String16& detailedMessage, const String16& url,
      unsigned lineNumber, unsigned columnNumber,
      std::unique_ptr<V8StackTraceImpl>, int scriptId, v8::Isolate*,
      const String16& message, int contextId, v8::Local<v8::Value> exception,
      unsigned exceptionId);

  static std::unique_ptr<V8ConsoleMessage> createForRevokedException(
      double timestamp, const String16& message, unsigned revokedExceptionId);

  V8MessageOrigin origin() const;
  void reportToFrontend(protocol::Console::Frontend*) const;
  void reportToFrontend(protocol::Runtime::Frontend*, V8InspectorSessionImpl*,
                        bool generatePreview) const;
  ConsoleAPIType type() const;
  void contextDestroyed(int contextId);

  int estimatedSize() const {
    return m_v8Size + static_cast<int>(m_message.length() * sizeof(UChar));
  }

 private:
  V8ConsoleMessage(V8MessageOrigin, double timestamp, const String16& message);

  using Arguments = std::vector<std::unique_ptr<v8::Global<v8::Value>>>;
  std::unique_ptr<protocol::Array<protocol::Runtime::RemoteObject>>
  wrapArguments(V8InspectorSessionImpl*, bool generatePreview) const;
  std::unique_ptr<protocol::Runtime::RemoteObject> wrapException(
      V8InspectorSessionImpl*, bool generatePreview) const;
  void setLocation(const String16& url, unsigned lineNumber,
                   unsigned columnNumber, std::unique_ptr<V8StackTraceImpl>,
                   int scriptId);
  std::unique_ptr<protocol::DictionaryValue> getAssociatedExceptionData(
      V8InspectorImpl* inspector, V8InspectorSessionImpl* session) const;

  V8MessageOrigin m_origin;
  double m_timestamp;
  String16 m_message;
  String16 m_url;
  unsigned m_lineNumber;
  unsigned m_columnNumber;
  std::unique_ptr<V8StackTraceImpl> m_stackTrace;
  int m_scriptId;
  int m_contextId;
  ConsoleAPIType m_type;
  unsigned m_exceptionId;
  unsigned m_revokedExceptionId;
  int m_v8Size = 0;
  Arguments m_arguments;
  String16 m_detailedMessage;
  String16 m_consoleContext;
};

class V8ConsoleMessageStorage {
 public:
  V8ConsoleMessageStorage(V8InspectorImpl*, int contextGroupId);
  ~V8ConsoleMessageStorage();

  int contextGroupId() { return m_contextGroupId; }
  const std::deque<std::unique_ptr<V8ConsoleMessage>>& messages() const {
    return m_messages;
  }

  void addMessage(std::unique_ptr<V8ConsoleMessage>);
  void contextDestroyed(int contextId);
  void clear();

  bool shouldReportDeprecationMessage(int contextId, const String16& method);
  int count(int contextId, const String16& id);
  bool countReset(int contextId, const String16& id);
  void time(int contextId, const String16& id);
  double timeLog(int contextId, const String16& id);
  double timeEnd(int contextId, const String16& id);
  bool hasTimer(int contextId, const String16& id);

 private:
  V8InspectorImpl* m_inspector;
  int m_contextGroupId;
  int m_estimatedSize = 0;
  std::deque<std::unique_ptr<V8ConsoleMessage>> m_messages;

  struct PerContextData {
    std::set<String16> m_reportedDeprecationMessages;
    // Corresponds to https://console.spec.whatwg.org/#count-map
    std::map<String16, int> m_count;
    // Corresponds to https://console.spec.whatwg.org/#timer-table
    std::map<String16, double> m_time;
  };
  std::map<int, PerContextData> m_data;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8_CONSOLE_MESSAGE_H_
