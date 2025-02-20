// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8_HEAP_PROFILER_AGENT_IMPL_H_
#define V8_INSPECTOR_V8_HEAP_PROFILER_AGENT_IMPL_H_

#include <memory>

#include "src/base/macros.h"
#include "src/inspector/protocol/Forward.h"
#include "src/inspector/protocol/HeapProfiler.h"

namespace v8 {
class Isolate;
}

namespace v8_inspector {

class V8InspectorSessionImpl;

using protocol::Response;

class V8HeapProfilerAgentImpl : public protocol::HeapProfiler::Backend {
 public:
  V8HeapProfilerAgentImpl(V8InspectorSessionImpl*, protocol::FrontendChannel*,
                          protocol::DictionaryValue* state);
  ~V8HeapProfilerAgentImpl() override;
  V8HeapProfilerAgentImpl(const V8HeapProfilerAgentImpl&) = delete;
  V8HeapProfilerAgentImpl& operator=(const V8HeapProfilerAgentImpl&) = delete;
  void restore();

  void collectGarbage(
      std::unique_ptr<CollectGarbageCallback> callback) override;

  Response enable() override;
  Response startTrackingHeapObjects(
      std::optional<bool> trackAllocations) override;
  Response stopTrackingHeapObjects(
      std::optional<bool> reportProgress,
      std::optional<bool> treatGlobalObjectsAsRoots,
      std::optional<bool> captureNumericValue,
      std::optional<bool> exposeInternals) override;

  Response disable() override;

  void takeHeapSnapshot(
      std::optional<bool> reportProgress,
      std::optional<bool> treatGlobalObjectsAsRoots,
      std::optional<bool> captureNumericValue,
      std::optional<bool> exposeInternals,
      std::unique_ptr<TakeHeapSnapshotCallback> callback) override;

  Response getObjectByHeapObjectId(
      const String16& heapSnapshotObjectId, std::optional<String16> objectGroup,
      std::unique_ptr<protocol::Runtime::RemoteObject>* result) override;
  Response addInspectedHeapObject(
      const String16& inspectedHeapObjectId) override;
  Response getHeapObjectId(const String16& objectId,
                           String16* heapSnapshotObjectId) override;

  Response startSampling(
      std::optional<double> samplingInterval,
      std::optional<bool> includeObjectsCollectedByMajorGC,
      std::optional<bool> includeObjectsCollectedByMinorGC) override;
  Response stopSampling(
      std::unique_ptr<protocol::HeapProfiler::SamplingHeapProfile>*) override;
  Response getSamplingProfile(
      std::unique_ptr<protocol::HeapProfiler::SamplingHeapProfile>*) override;

  // If any heap snapshot requests have been deferred, run them now. This is
  // called by the debugger when pausing execution on this thread.
  void takePendingHeapSnapshots();

 private:
  struct AsyncCallbacks;
  class GCTask;
  class HeapSnapshotTask;
  struct HeapSnapshotProtocolOptions;

  Response takeHeapSnapshotNow(
      const HeapSnapshotProtocolOptions& protocolOptions,
      cppgc::EmbedderStackState stackState);
  void startTrackingHeapObjectsInternal(bool trackAllocations);
  void stopTrackingHeapObjectsInternal();
  void requestHeapStatsUpdate();
  static void onTimer(void*);
  void onTimerImpl();

  V8InspectorSessionImpl* m_session;
  v8::Isolate* m_isolate;
  protocol::HeapProfiler::Frontend m_frontend;
  protocol::DictionaryValue* m_state;
  bool m_hasTimer;
  double m_timerDelayInSeconds;
  std::shared_ptr<AsyncCallbacks> m_asyncCallbacks;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8_HEAP_PROFILER_AGENT_IMPL_H_
