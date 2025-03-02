// Copyright 2016 the V8 project authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(V8_OS_STARBOARD)
#include "starboard/system.h"
#define __builtin_abort SbSystemBreakIntoDebugger
#endif

#include "src/inspector/v8-stack-trace-impl.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "../../third_party/inspector_protocol/crdtp/json.h"
#include "src/debug/debug-interface.h"
#include "src/inspector/v8-debugger.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/tracing/trace-event.h"

using v8_crdtp::json::ConvertJSONToCBOR;

namespace v8_inspector {
namespace {

static const char kId[] = "id";
static const char kDebuggerId[] = "debuggerId";
static const char kShouldPause[] = "shouldPause";

static const v8::StackTrace::StackTraceOptions stackTraceOptions =
    static_cast<v8::StackTrace::StackTraceOptions>(
        v8::StackTrace::kDetailed |
        v8::StackTrace::kExposeFramesAcrossSecurityOrigins);

std::vector<std::shared_ptr<StackFrame>> toFramesVector(
    V8Debugger* debugger, v8::Local<v8::StackTrace> v8StackTrace,
    int maxStackSize) {
  DCHECK(debugger->isolate()->InContext());
  int frameCount = std::min(v8StackTrace->GetFrameCount(), maxStackSize);

  TRACE_EVENT1(
      TRACE_DISABLED_BY_DEFAULT("v8.inspector") "," TRACE_DISABLED_BY_DEFAULT(
          "v8.stack_trace"),
      "toFramesVector", "frameCount", frameCount);

  std::vector<std::shared_ptr<StackFrame>> frames(frameCount);
  for (int i = 0; i < frameCount; ++i) {
    frames[i] =
        debugger->symbolize(v8StackTrace->GetFrame(debugger->isolate(), i));
  }
  return frames;
}

std::unique_ptr<protocol::Runtime::StackTrace> buildInspectorObjectCommon(
    V8Debugger* debugger,
    const std::vector<std::shared_ptr<StackFrame>>& frames,
    const String16& description,
    const std::shared_ptr<AsyncStackTrace>& asyncParent,
    const V8StackTraceId& externalParent, int maxAsyncDepth) {
  if (asyncParent && frames.empty() &&
      description == asyncParent->description()) {
    return asyncParent->buildInspectorObject(debugger, maxAsyncDepth);
  }

  auto inspectorFrames =
      std::make_unique<protocol::Array<protocol::Runtime::CallFrame>>();
  for (const std::shared_ptr<StackFrame>& frame : frames) {
    V8InspectorClient* client = nullptr;
    if (debugger && debugger->inspector())
      client = debugger->inspector()->client();
    inspectorFrames->emplace_back(frame->buildInspectorObject(client));
  }
  std::unique_ptr<protocol::Runtime::StackTrace> stackTrace =
      protocol::Runtime::StackTrace::create()
          .setCallFrames(std::move(inspectorFrames))
          .build();
  if (!description.isEmpty()) stackTrace->setDescription(description);
  if (asyncParent) {
    if (maxAsyncDepth > 0) {
      stackTrace->setParent(
          asyncParent->buildInspectorObject(debugger, maxAsyncDepth - 1));
    } else if (debugger) {
      stackTrace->setParentId(
          protocol::Runtime::StackTraceId::create()
              .setId(stackTraceIdToString(
                  AsyncStackTrace::store(debugger, asyncParent)))
              .build());
    }
  }
  if (!externalParent.IsInvalid()) {
    stackTrace->setParentId(
        protocol::Runtime::StackTraceId::create()
            .setId(stackTraceIdToString(externalParent.id))
            .setDebuggerId(
                internal::V8DebuggerId(externalParent.debugger_id).toString())
            .build());
  }
  return stackTrace;
}

}  // namespace

V8StackTraceId::V8StackTraceId()
    : id(0), debugger_id(internal::V8DebuggerId().pair()) {}

V8StackTraceId::V8StackTraceId(uintptr_t id,
                               const std::pair<int64_t, int64_t> debugger_id)
    : id(id), debugger_id(debugger_id) {}

V8StackTraceId::V8StackTraceId(uintptr_t id,
                               const std::pair<int64_t, int64_t> debugger_id,
                               bool should_pause)
    : id(id), debugger_id(debugger_id), should_pause(should_pause) {}

V8StackTraceId::V8StackTraceId(StringView json)
    : id(0), debugger_id(internal::V8DebuggerId().pair()) {
  if (json.length() == 0) return;
  std::vector<uint8_t> cbor;
  if (json.is8Bit()) {
    ConvertJSONToCBOR(
        v8_crdtp::span<uint8_t>(json.characters8(), json.length()), &cbor);
  } else {
    ConvertJSONToCBOR(
        v8_crdtp::span<uint16_t>(json.characters16(), json.length()), &cbor);
  }
  auto dict = protocol::DictionaryValue::cast(
      protocol::Value::parseBinary(cbor.data(), cbor.size()));
  if (!dict) return;
  String16 s;
  if (!dict->getString(kId, &s)) return;
  bool isOk = false;
  int64_t parsedId = s.toInteger64(&isOk);
  if (!isOk || !parsedId) return;
  if (!dict->getString(kDebuggerId, &s)) return;
  internal::V8DebuggerId debuggerId(s);
  if (!debuggerId.isValid()) return;
  if (!dict->getBoolean(kShouldPause, &should_pause)) return;
  id = parsedId;
  debugger_id = debuggerId.pair();
}

bool V8StackTraceId::IsInvalid() const { return !id; }

std::unique_ptr<StringBuffer> V8StackTraceId::ToString() {
  if (IsInvalid()) return nullptr;
  auto dict = protocol::DictionaryValue::create();
  dict->setString(kId, String16::fromInteger64(id));
  dict->setString(kDebuggerId, internal::V8DebuggerId(debugger_id).toString());
  dict->setBoolean(kShouldPause, should_pause);
  std::vector<uint8_t> json;
  v8_crdtp::json::ConvertCBORToJSON(v8_crdtp::SpanFrom(dict->Serialize()),
                                    &json);
  return StringBufferFrom(std::move(json));
}

StackFrame::StackFrame(String16&& functionName, int scriptId,
                       String16&& sourceURL, int lineNumber, int columnNumber,
                       bool hasSourceURLComment)
    : m_functionName(std::move(functionName)),
      m_scriptId(scriptId),
      m_sourceURL(std::move(sourceURL)),
      m_lineNumber(lineNumber),
      m_columnNumber(columnNumber),
      m_hasSourceURLComment(hasSourceURLComment) {
  DCHECK_NE(v8::Message::kNoLineNumberInfo, m_lineNumber + 1);
  DCHECK_NE(v8::Message::kNoColumnInfo, m_columnNumber + 1);
}

const String16& StackFrame::functionName() const { return m_functionName; }

int StackFrame::scriptId() const { return m_scriptId; }

const String16& StackFrame::sourceURL() const { return m_sourceURL; }

int StackFrame::lineNumber() const { return m_lineNumber; }

int StackFrame::columnNumber() const { return m_columnNumber; }

std::unique_ptr<protocol::Runtime::CallFrame> StackFrame::buildInspectorObject(
    V8InspectorClient* client) const {
  String16 frameUrl;
  const char* dataURIPrefix = "data:";
  if (m_sourceURL.substring(0, strlen(dataURIPrefix)) != dataURIPrefix) {
    frameUrl = m_sourceURL;
  }

  if (client && !m_hasSourceURLComment && frameUrl.length() > 0) {
    std::unique_ptr<StringBuffer> url =
        client->resourceNameToUrl(toStringView(m_sourceURL));
    if (url) {
      frameUrl = toString16(url->string());
    }
  }
  return protocol::Runtime::CallFrame::create()
      .setFunctionName(m_functionName)
      .setScriptId(String16::fromInteger(m_scriptId))
      .setUrl(frameUrl)
      .setLineNumber(m_lineNumber)
      .setColumnNumber(m_columnNumber)
      .build();
}

bool StackFrame::isEqual(StackFrame* frame) const {
  return m_scriptId == frame->m_scriptId &&
         m_lineNumber == frame->m_lineNumber &&
         m_columnNumber == frame->m_columnNumber;
}

// static
std::unique_ptr<V8StackTraceImpl> V8StackTraceImpl::create(
    V8Debugger* debugger, v8::Local<v8::StackTrace> v8StackTrace,
    int maxStackSize) {
  DCHECK(debugger);

  v8::Isolate* isolate = debugger->isolate();
  v8::HandleScope scope(isolate);

  std::vector<std::shared_ptr<StackFrame>> frames;
  if (!v8StackTrace.IsEmpty() && v8StackTrace->GetFrameCount()) {
    frames = toFramesVector(debugger, v8StackTrace, maxStackSize);
  }

  int maxAsyncDepth = debugger->maxAsyncCallChainDepth();
  std::shared_ptr<AsyncStackTrace> asyncParent;
  V8StackTraceId externalParent;
  if (!v8StackTrace.IsEmpty()) {
    debugger->asyncParentFor(v8StackTrace->GetID(), &asyncParent,
                             &externalParent);
  }
  if (frames.empty() && !asyncParent && externalParent.IsInvalid()) return {};
  return std::unique_ptr<V8StackTraceImpl>(new V8StackTraceImpl(
      std::move(frames), maxAsyncDepth, asyncParent, externalParent));
}

// static
std::unique_ptr<V8StackTraceImpl> V8StackTraceImpl::capture(
    V8Debugger* debugger, int maxStackSize) {
  DCHECK(debugger);

  TRACE_EVENT1(
      TRACE_DISABLED_BY_DEFAULT("v8.inspector") "," TRACE_DISABLED_BY_DEFAULT(
          "v8.stack_trace"),
      "V8StackTraceImpl::capture", "maxFrameCount", maxStackSize);

  v8::Isolate* isolate = debugger->isolate();
  v8::HandleScope handleScope(isolate);
  v8::Local<v8::StackTrace> v8StackTrace;
  if (isolate->InContext()) {
    v8StackTrace = v8::StackTrace::CurrentStackTrace(isolate, maxStackSize,
                                                     stackTraceOptions);
  }
  return V8StackTraceImpl::create(debugger, v8StackTrace, maxStackSize);
}

V8StackTraceImpl::V8StackTraceImpl(
    std::vector<std::shared_ptr<StackFrame>> frames, int maxAsyncDepth,
    std::shared_ptr<AsyncStackTrace> asyncParent,
    const V8StackTraceId& externalParent)
    : m_frames(std::move(frames)),
      m_maxAsyncDepth(maxAsyncDepth),
      m_asyncParent(std::move(asyncParent)),
      m_externalParent(externalParent) {}

V8StackTraceImpl::~V8StackTraceImpl() = default;

std::unique_ptr<V8StackTrace> V8StackTraceImpl::clone() {
  return std::unique_ptr<V8StackTrace>(new V8StackTraceImpl(
      m_frames, 0, std::shared_ptr<AsyncStackTrace>(), V8StackTraceId()));
}

StringView V8StackTraceImpl::firstNonEmptySourceURL() const {
  StackFrameIterator current(this);
  while (!current.done()) {
    if (current.frame()->sourceURL().length()) {
      return toStringView(current.frame()->sourceURL());
    }
    current.next();
  }
  return StringView();
}

bool V8StackTraceImpl::isEmpty() const { return m_frames.empty(); }

StringView V8StackTraceImpl::topSourceURL() const {
  return toStringView(m_frames[0]->sourceURL());
}

int V8StackTraceImpl::topLineNumber() const {
  return m_frames[0]->lineNumber() + 1;
}

int V8StackTraceImpl::topColumnNumber() const {
  return m_frames[0]->columnNumber() + 1;
}

int V8StackTraceImpl::topScriptId() const { return m_frames[0]->scriptId(); }

StringView V8StackTraceImpl::topFunctionName() const {
  return toStringView(m_frames[0]->functionName());
}

std::vector<V8StackFrame> V8StackTraceImpl::frames() const {
  std::vector<V8StackFrame> ret;
  ret.reserve(m_frames.size());

  for (const auto& frame : m_frames) {
    if (frame) {
      ret.emplace_back(V8StackFrame{
          toStringView(frame->sourceURL()), toStringView(frame->functionName()),
          frame->lineNumber() + 1, frame->columnNumber() + 1,
          frame->scriptId()});
    }
  }

  return ret;
}

std::unique_ptr<protocol::Runtime::StackTrace>
V8StackTraceImpl::buildInspectorObjectImpl(V8Debugger* debugger) const {
  return buildInspectorObjectImpl(debugger, m_maxAsyncDepth);
}

std::unique_ptr<protocol::Runtime::StackTrace>
V8StackTraceImpl::buildInspectorObjectImpl(V8Debugger* debugger,
                                           int maxAsyncDepth) const {
  return buildInspectorObjectCommon(debugger, m_frames, String16(),
                                    m_asyncParent.lock(), m_externalParent,
                                    maxAsyncDepth);
}

std::unique_ptr<protocol::Runtime::API::StackTrace>
V8StackTraceImpl::buildInspectorObject(int maxAsyncDepth) const {
  return buildInspectorObjectImpl(nullptr,
                                  std::min(maxAsyncDepth, m_maxAsyncDepth));
}

std::unique_ptr<StringBuffer> V8StackTraceImpl::toString() const {
  String16Builder stackTrace;
  for (size_t i = 0; i < m_frames.size(); ++i) {
    const StackFrame& frame = *m_frames[i];
    stackTrace.append("\n    at " + (frame.functionName().length()
                                         ? frame.functionName()
                                         : "(anonymous function)"));
    stackTrace.append(" (");
    stackTrace.append(frame.sourceURL());
    stackTrace.append(':');
    stackTrace.append(String16::fromInteger(frame.lineNumber() + 1));
    stackTrace.append(':');
    stackTrace.append(String16::fromInteger(frame.columnNumber() + 1));
    stackTrace.append(')');
  }
  return StringBufferFrom(stackTrace.toString());
}

bool V8StackTraceImpl::isEqualIgnoringTopFrame(
    V8StackTraceImpl* stackTrace) const {
  StackFrameIterator current(this);
  StackFrameIterator target(stackTrace);

  current.next();
  target.next();
  while (!current.done() && !target.done()) {
    if (!current.frame()->isEqual(target.frame())) {
      return false;
    }
    current.next();
    target.next();
  }
  return current.done() == target.done();
}

V8StackTraceImpl::StackFrameIterator::StackFrameIterator(
    const V8StackTraceImpl* stackTrace)
    : m_currentIt(stackTrace->m_frames.begin()),
      m_currentEnd(stackTrace->m_frames.end()),
      m_parent(stackTrace->m_asyncParent.lock().get()) {}

void V8StackTraceImpl::StackFrameIterator::next() {
  if (m_currentIt == m_currentEnd) return;
  ++m_currentIt;
  while (m_currentIt == m_currentEnd && m_parent) {
    const std::vector<std::shared_ptr<StackFrame>>& frames = m_parent->frames();
    m_currentIt = frames.begin();
    m_currentEnd = frames.end();
    m_parent = m_parent->parent().lock().get();
  }
}

bool V8StackTraceImpl::StackFrameIterator::done() {
  return m_currentIt == m_currentEnd;
}

StackFrame* V8StackTraceImpl::StackFrameIterator::frame() {
  return m_currentIt->get();
}

// static
std::shared_ptr<AsyncStackTrace> AsyncStackTrace::capture(
    V8Debugger* debugger, const String16& description, bool skipTopFrame) {
  DCHECK(debugger);

  int maxStackSize = debugger->maxCallStackSizeToCapture();
  TRACE_EVENT1(
      TRACE_DISABLED_BY_DEFAULT("v8.inspector") "," TRACE_DISABLED_BY_DEFAULT(
          "v8.stack_trace"),
      "AsyncStackTrace::capture", "maxFrameCount", maxStackSize);

  v8::Isolate* isolate = debugger->isolate();
  v8::HandleScope handleScope(isolate);

  std::vector<std::shared_ptr<StackFrame>> frames;
  std::shared_ptr<AsyncStackTrace> asyncParent;
  V8StackTraceId externalParent;
  if (isolate->InContext()) {
    v8::Local<v8::StackTrace> v8StackTrace = v8::StackTrace::CurrentStackTrace(
        isolate, maxStackSize, stackTraceOptions);
    frames = toFramesVector(debugger, v8StackTrace, maxStackSize);
    if (skipTopFrame && !frames.empty()) {
      frames.erase(frames.begin());
    }

    debugger->asyncParentFor(v8StackTrace->GetID(), &asyncParent,
                             &externalParent);
  }

  if (frames.empty() && !asyncParent && externalParent.IsInvalid())
    return nullptr;

  if (asyncParent && frames.empty() &&
      (asyncParent->m_description == description || description.isEmpty())) {
    return asyncParent;
  }

  return std::shared_ptr<AsyncStackTrace>(new AsyncStackTrace(
      description, std::move(frames), asyncParent, externalParent));
}

AsyncStackTrace::AsyncStackTrace(
    const String16& description,
    std::vector<std::shared_ptr<StackFrame>> frames,
    std::shared_ptr<AsyncStackTrace> asyncParent,
    const V8StackTraceId& externalParent)
    : m_id(0),
      m_description(description),
      m_frames(std::move(frames)),
      m_asyncParent(std::move(asyncParent)),
      m_externalParent(externalParent) {}

std::unique_ptr<protocol::Runtime::StackTrace>
AsyncStackTrace::buildInspectorObject(V8Debugger* debugger,
                                      int maxAsyncDepth) const {
  return buildInspectorObjectCommon(debugger, m_frames, m_description,
                                    m_asyncParent.lock(), m_externalParent,
                                    maxAsyncDepth);
}

uintptr_t AsyncStackTrace::store(V8Debugger* debugger,
                                 std::shared_ptr<AsyncStackTrace> stack) {
  if (stack->m_id) return stack->m_id;
  stack->m_id = debugger->storeStackTrace(stack);
  return stack->m_id;
}

const String16& AsyncStackTrace::description() const { return m_description; }

std::weak_ptr<AsyncStackTrace> AsyncStackTrace::parent() const {
  return m_asyncParent;
}

bool AsyncStackTrace::isEmpty() const { return m_frames.empty(); }

}  // namespace v8_inspector
