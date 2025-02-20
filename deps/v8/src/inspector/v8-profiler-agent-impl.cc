// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-profiler-agent-impl.h"

#include <vector>

#include "include/v8-profiler.h"
#include "src/base/atomicops.h"
#include "src/base/platform/time.h"
#include "src/debug/debug-interface.h"
#include "src/inspector/protocol/Protocol.h"
#include "src/inspector/string-util.h"
#include "src/inspector/v8-debugger.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/inspector/v8-inspector-session-impl.h"
#include "src/inspector/v8-stack-trace-impl.h"

namespace v8_inspector {

namespace ProfilerAgentState {
static const char samplingInterval[] = "samplingInterval";
static const char userInitiatedProfiling[] = "userInitiatedProfiling";
static const char profilerEnabled[] = "profilerEnabled";
static const char preciseCoverageStarted[] = "preciseCoverageStarted";
static const char preciseCoverageCallCount[] = "preciseCoverageCallCount";
static const char preciseCoverageDetailed[] = "preciseCoverageDetailed";
static const char preciseCoverageAllowTriggeredUpdates[] =
    "preciseCoverageAllowTriggeredUpdates";
}  // namespace ProfilerAgentState

namespace {

String16 resourceNameToUrl(V8InspectorImpl* inspector,
                           v8::Local<v8::String> v8Name) {
  String16 name = toProtocolString(inspector->isolate(), v8Name);
  if (!inspector) return name;
  std::unique_ptr<StringBuffer> url =
      inspector->client()->resourceNameToUrl(toStringView(name));
  return url ? toString16(url->string()) : name;
}

std::unique_ptr<protocol::Array<protocol::Profiler::PositionTickInfo>>
buildInspectorObjectForPositionTicks(const v8::CpuProfileNode* node) {
  unsigned lineCount = node->GetHitLineCount();
  if (!lineCount) return nullptr;
  auto array =
      std::make_unique<protocol::Array<protocol::Profiler::PositionTickInfo>>();
  std::vector<v8::CpuProfileNode::LineTick> entries(lineCount);
  if (node->GetLineTicks(&entries[0], lineCount)) {
    for (unsigned i = 0; i < lineCount; i++) {
      std::unique_ptr<protocol::Profiler::PositionTickInfo> line =
          protocol::Profiler::PositionTickInfo::create()
              .setLine(entries[i].line)
              .setTicks(entries[i].hit_count)
              .build();
      array->emplace_back(std::move(line));
    }
  }
  return array;
}

std::unique_ptr<protocol::Profiler::ProfileNode> buildInspectorObjectFor(
    V8InspectorImpl* inspector, const v8::CpuProfileNode* node) {
  v8::Isolate* isolate = inspector->isolate();
  v8::HandleScope handleScope(isolate);
  auto callFrame =
      protocol::Runtime::CallFrame::create()
          .setFunctionName(toProtocolString(isolate, node->GetFunctionName()))
          .setScriptId(String16::fromInteger(node->GetScriptId()))
          .setUrl(resourceNameToUrl(inspector, node->GetScriptResourceName()))
          .setLineNumber(node->GetLineNumber() - 1)
          .setColumnNumber(node->GetColumnNumber() - 1)
          .build();
  auto result = protocol::Profiler::ProfileNode::create()
                    .setCallFrame(std::move(callFrame))
                    .setHitCount(node->GetHitCount())
                    .setId(node->GetNodeId())
                    .build();

  const int childrenCount = node->GetChildrenCount();
  if (childrenCount) {
    auto children = std::make_unique<protocol::Array<int>>();
    for (int i = 0; i < childrenCount; i++)
      children->emplace_back(node->GetChild(i)->GetNodeId());
    result->setChildren(std::move(children));
  }

  const char* deoptReason = node->GetBailoutReason();
  if (deoptReason && deoptReason[0] && strcmp(deoptReason, "no reason"))
    result->setDeoptReason(deoptReason);

  auto positionTicks = buildInspectorObjectForPositionTicks(node);
  if (positionTicks) result->setPositionTicks(std::move(positionTicks));

  return result;
}

std::unique_ptr<protocol::Array<int>> buildInspectorObjectForSamples(
    v8::CpuProfile* v8profile) {
  auto array = std::make_unique<protocol::Array<int>>();
  int count = v8profile->GetSamplesCount();
  for (int i = 0; i < count; i++)
    array->emplace_back(v8profile->GetSample(i)->GetNodeId());
  return array;
}

std::unique_ptr<protocol::Array<int>> buildInspectorObjectForTimestamps(
    v8::CpuProfile* v8profile) {
  auto array = std::make_unique<protocol::Array<int>>();
  int count = v8profile->GetSamplesCount();
  uint64_t lastTime = v8profile->GetStartTime();
  for (int i = 0; i < count; i++) {
    uint64_t ts = v8profile->GetSampleTimestamp(i);
    array->emplace_back(static_cast<int>(ts - lastTime));
    lastTime = ts;
  }
  return array;
}

void flattenNodesTree(V8InspectorImpl* inspector,
                      const v8::CpuProfileNode* node,
                      protocol::Array<protocol::Profiler::ProfileNode>* list) {
  list->emplace_back(buildInspectorObjectFor(inspector, node));
  const int childrenCount = node->GetChildrenCount();
  for (int i = 0; i < childrenCount; i++)
    flattenNodesTree(inspector, node->GetChild(i), list);
}

std::unique_ptr<protocol::Profiler::Profile> createCPUProfile(
    V8InspectorImpl* inspector, v8::CpuProfile* v8profile) {
  auto nodes =
      std::make_unique<protocol::Array<protocol::Profiler::ProfileNode>>();
  flattenNodesTree(inspector, v8profile->GetTopDownRoot(), nodes.get());
  return protocol::Profiler::Profile::create()
      .setNodes(std::move(nodes))
      .setStartTime(static_cast<double>(v8profile->GetStartTime()))
      .setEndTime(static_cast<double>(v8profile->GetEndTime()))
      .setSamples(buildInspectorObjectForSamples(v8profile))
      .setTimeDeltas(buildInspectorObjectForTimestamps(v8profile))
      .build();
}

std::unique_ptr<protocol::Debugger::Location> currentDebugLocation(
    V8InspectorImpl* inspector) {
  auto stackTrace = V8StackTraceImpl::capture(inspector->debugger(), 1);
  CHECK(stackTrace);
  CHECK(!stackTrace->isEmpty());
  return protocol::Debugger::Location::create()
      .setScriptId(String16::fromInteger(stackTrace->topScriptId()))
      .setLineNumber(stackTrace->topLineNumber())
      .setColumnNumber(stackTrace->topColumnNumber())
      .build();
}

volatile int s_lastProfileId = 0;

}  // namespace

class V8ProfilerAgentImpl::ProfileDescriptor {
 public:
  ProfileDescriptor(const String16& id, const String16& title)
      : m_id(id), m_title(title) {}
  String16 m_id;
  String16 m_title;
};

V8ProfilerAgentImpl::V8ProfilerAgentImpl(
    V8InspectorSessionImpl* session, protocol::FrontendChannel* frontendChannel,
    protocol::DictionaryValue* state)
    : m_session(session),
      m_isolate(m_session->inspector()->isolate()),
      m_state(state),
      m_frontend(frontendChannel) {}

V8ProfilerAgentImpl::~V8ProfilerAgentImpl() {
  if (m_profiler) m_profiler->Dispose();
}

void V8ProfilerAgentImpl::consoleProfile(const String16& title) {
  if (!m_enabled) return;
  String16 id = nextProfileId();
  m_startedProfiles.push_back(ProfileDescriptor(id, title));
  startProfiling(id);
  m_frontend.consoleProfileStarted(
      id, currentDebugLocation(m_session->inspector()), title);
}

void V8ProfilerAgentImpl::consoleProfileEnd(const String16& title) {
  if (!m_enabled) return;
  String16 id;
  String16 resolvedTitle;
  // Take last started profile if no title was passed.
  if (title.isEmpty()) {
    if (m_startedProfiles.empty()) return;
    id = m_startedProfiles.back().m_id;
    resolvedTitle = m_startedProfiles.back().m_title;
    m_startedProfiles.pop_back();
  } else {
    for (size_t i = 0; i < m_startedProfiles.size(); i++) {
      if (m_startedProfiles[i].m_title == title) {
        resolvedTitle = title;
        id = m_startedProfiles[i].m_id;
        m_startedProfiles.erase(m_startedProfiles.begin() + i);
        break;
      }
    }
    if (id.isEmpty()) return;
  }
  std::unique_ptr<protocol::Profiler::Profile> profile =
      stopProfiling(id, true);
  if (!profile) return;
  m_frontend.consoleProfileFinished(
      id, currentDebugLocation(m_session->inspector()), std::move(profile),
      resolvedTitle);
}

Response V8ProfilerAgentImpl::enable() {
  if (!m_enabled) {
    m_enabled = true;
    m_state->setBoolean(ProfilerAgentState::profilerEnabled, true);
  }

  return Response::Success();
}

Response V8ProfilerAgentImpl::disable() {
  if (m_enabled) {
    for (size_t i = m_startedProfiles.size(); i > 0; --i)
      stopProfiling(m_startedProfiles[i - 1].m_id, false);
    m_startedProfiles.clear();
    stop(nullptr);
    stopPreciseCoverage();
    DCHECK(!m_profiler);
    m_enabled = false;
    m_state->setBoolean(ProfilerAgentState::profilerEnabled, false);
  }

  return Response::Success();
}

Response V8ProfilerAgentImpl::setSamplingInterval(int interval) {
  if (m_profiler) {
    return Response::ServerError(
        "Cannot change sampling interval when profiling.");
  }
  m_state->setInteger(ProfilerAgentState::samplingInterval, interval);
  return Response::Success();
}

void V8ProfilerAgentImpl::restore() {
  DCHECK(!m_enabled);
  if (m_state->booleanProperty(ProfilerAgentState::profilerEnabled, false)) {
    m_enabled = true;
    DCHECK(!m_profiler);
    if (m_state->booleanProperty(ProfilerAgentState::userInitiatedProfiling,
                                 false)) {
      start();
    }
    if (m_state->booleanProperty(ProfilerAgentState::preciseCoverageStarted,
                                 false)) {
      bool callCount = m_state->booleanProperty(
          ProfilerAgentState::preciseCoverageCallCount, false);
      bool detailed = m_state->booleanProperty(
          ProfilerAgentState::preciseCoverageDetailed, false);
      bool updatesAllowed = m_state->booleanProperty(
          ProfilerAgentState::preciseCoverageAllowTriggeredUpdates, false);
      double timestamp;
      startPreciseCoverage(std::optional<bool>(callCount),
                           std::optional<bool>(detailed),
                           std::optional<bool>(updatesAllowed), &timestamp);
    }
  }
}

Response V8ProfilerAgentImpl::start() {
  if (m_recordingCPUProfile) return Response::Success();
  if (!m_enabled) return Response::ServerError("Profiler is not enabled");
  m_recordingCPUProfile = true;
  m_frontendInitiatedProfileId = nextProfileId();
  startProfiling(m_frontendInitiatedProfileId);
  m_state->setBoolean(ProfilerAgentState::userInitiatedProfiling, true);
  return Response::Success();
}

Response V8ProfilerAgentImpl::stop(
    std::unique_ptr<protocol::Profiler::Profile>* profile) {
  if (!m_recordingCPUProfile) {
    return Response::ServerError("No recording profiles found");
  }
  m_recordingCPUProfile = false;
  std::unique_ptr<protocol::Profiler::Profile> cpuProfile =
      stopProfiling(m_frontendInitiatedProfileId, !!profile);
  if (profile) {
    *profile = std::move(cpuProfile);
    if (!*profile) return Response::ServerError("Profile is not found");
  }
  m_frontendInitiatedProfileId = String16();
  m_state->setBoolean(ProfilerAgentState::userInitiatedProfiling, false);
  return Response::Success();
}

Response V8ProfilerAgentImpl::startPreciseCoverage(
    std::optional<bool> callCount, std::optional<bool> detailed,
    std::optional<bool> allowTriggeredUpdates, double* out_timestamp) {
  if (!m_enabled) return Response::ServerError("Profiler is not enabled");
  *out_timestamp = v8::base::TimeTicks::Now().since_origin().InSecondsF();
  bool callCountValue = callCount.value_or(false);
  bool detailedValue = detailed.value_or(false);
  bool allowTriggeredUpdatesValue = allowTriggeredUpdates.value_or(false);
  m_state->setBoolean(ProfilerAgentState::preciseCoverageStarted, true);
  m_state->setBoolean(ProfilerAgentState::preciseCoverageCallCount,
                      callCountValue);
  m_state->setBoolean(ProfilerAgentState::preciseCoverageDetailed,
                      detailedValue);
  m_state->setBoolean(ProfilerAgentState::preciseCoverageAllowTriggeredUpdates,
                      allowTriggeredUpdatesValue);
  // BlockCount is a superset of PreciseCount. It includes block-granularity
  // coverage data if it exists (at the time of writing, that's the case for
  // each function recompiled after the BlockCount mode has been set); and
  // function-granularity coverage data otherwise.
  using C = v8::debug::Coverage;
  using Mode = v8::debug::CoverageMode;
  Mode mode = callCountValue
                  ? (detailedValue ? Mode::kBlockCount : Mode::kPreciseCount)
                  : (detailedValue ? Mode::kBlockBinary : Mode::kPreciseBinary);
  C::SelectMode(m_isolate, mode);
  return Response::Success();
}

Response V8ProfilerAgentImpl::stopPreciseCoverage() {
  if (!m_enabled) return Response::ServerError("Profiler is not enabled");
  m_state->setBoolean(ProfilerAgentState::preciseCoverageStarted, false);
  m_state->setBoolean(ProfilerAgentState::preciseCoverageCallCount, false);
  m_state->setBoolean(ProfilerAgentState::preciseCoverageDetailed, false);
  v8::debug::Coverage::SelectMode(m_isolate,
                                  v8::debug::CoverageMode::kBestEffort);
  return Response::Success();
}

namespace {
std::unique_ptr<protocol::Profiler::CoverageRange> createCoverageRange(
    int start, int end, int count) {
  return protocol::Profiler::CoverageRange::create()
      .setStartOffset(start)
      .setEndOffset(end)
      .setCount(count)
      .build();
}

Response coverageToProtocol(
    V8InspectorImpl* inspector, const v8::debug::Coverage& coverage,
    std::unique_ptr<protocol::Array<protocol::Profiler::ScriptCoverage>>*
        out_result) {
  auto result =
      std::make_unique<protocol::Array<protocol::Profiler::ScriptCoverage>>();
  v8::Isolate* isolate = inspector->isolate();
  for (size_t i = 0; i < coverage.ScriptCount(); i++) {
    v8::debug::Coverage::ScriptData script_data = coverage.GetScriptData(i);
    v8::Local<v8::debug::Script> script = script_data.GetScript();
    auto functions = std::make_unique<
        protocol::Array<protocol::Profiler::FunctionCoverage>>();
    for (size_t j = 0; j < script_data.FunctionCount(); j++) {
      v8::debug::Coverage::FunctionData function_data =
          script_data.GetFunctionData(j);
      auto ranges = std::make_unique<
          protocol::Array<protocol::Profiler::CoverageRange>>();

      // Add function range.
      ranges->emplace_back(createCoverageRange(function_data.StartOffset(),
                                               function_data.EndOffset(),
                                               function_data.Count()));

      // Process inner blocks.
      for (size_t k = 0; k < function_data.BlockCount(); k++) {
        v8::debug::Coverage::BlockData block_data =
            function_data.GetBlockData(k);
        ranges->emplace_back(createCoverageRange(block_data.StartOffset(),
                                                 block_data.EndOffset(),
                                                 block_data.Count()));
      }

      functions->emplace_back(
          protocol::Profiler::FunctionCoverage::create()
              .setFunctionName(toProtocolString(
                  isolate,
                  function_data.Name().FromMaybe(v8::Local<v8::String>())))
              .setRanges(std::move(ranges))
              .setIsBlockCoverage(function_data.HasBlockCoverage())
              .build());
    }
    String16 url;
    v8::Local<v8::String> name;
    if (script->SourceURL().ToLocal(&name) && name->Length()) {
      url = toProtocolString(isolate, name);
    } else if (script->Name().ToLocal(&name) && name->Length()) {
      url = resourceNameToUrl(inspector, name);
    }
    result->emplace_back(protocol::Profiler::ScriptCoverage::create()
                             .setScriptId(String16::fromInteger(script->Id()))
                             .setUrl(url)
                             .setFunctions(std::move(functions))
                             .build());
  }
  *out_result = std::move(result);
  return Response::Success();
}
}  // anonymous namespace

Response V8ProfilerAgentImpl::takePreciseCoverage(
    std::unique_ptr<protocol::Array<protocol::Profiler::ScriptCoverage>>*
        out_result,
    double* out_timestamp) {
  if (!m_state->booleanProperty(ProfilerAgentState::preciseCoverageStarted,
                                false)) {
    return Response::ServerError("Precise coverage has not been started.");
  }
  v8::HandleScope handle_scope(m_isolate);
  v8::debug::Coverage coverage = v8::debug::Coverage::CollectPrecise(m_isolate);
  *out_timestamp = v8::base::TimeTicks::Now().since_origin().InSecondsF();
  return coverageToProtocol(m_session->inspector(), coverage, out_result);
}

void V8ProfilerAgentImpl::triggerPreciseCoverageDeltaUpdate(
    const String16& occasion) {
  if (!m_state->booleanProperty(ProfilerAgentState::preciseCoverageStarted,
                                false)) {
    return;
  }
  if (!m_state->booleanProperty(
          ProfilerAgentState::preciseCoverageAllowTriggeredUpdates, false)) {
    return;
  }
  v8::HandleScope handle_scope(m_isolate);
  v8::debug::Coverage coverage = v8::debug::Coverage::CollectPrecise(m_isolate);
  std::unique_ptr<protocol::Array<protocol::Profiler::ScriptCoverage>>
      out_result;
  coverageToProtocol(m_session->inspector(), coverage, &out_result);
  double now = v8::base::TimeTicks::Now().since_origin().InSecondsF();
  m_frontend.preciseCoverageDeltaUpdate(now, occasion, std::move(out_result));
}

Response V8ProfilerAgentImpl::getBestEffortCoverage(
    std::unique_ptr<protocol::Array<protocol::Profiler::ScriptCoverage>>*
        out_result) {
  v8::HandleScope handle_scope(m_isolate);
  v8::debug::Coverage coverage =
      v8::debug::Coverage::CollectBestEffort(m_isolate);
  return coverageToProtocol(m_session->inspector(), coverage, out_result);
}

String16 V8ProfilerAgentImpl::nextProfileId() {
  return String16::fromInteger(
      v8::base::Relaxed_AtomicIncrement(&s_lastProfileId, 1));
}

void V8ProfilerAgentImpl::startProfiling(const String16& title) {
  v8::HandleScope handleScope(m_isolate);
  if (!m_startedProfilesCount) {
    DCHECK(!m_profiler);
    m_profiler = v8::CpuProfiler::New(m_isolate);
    int interval =
        m_state->integerProperty(ProfilerAgentState::samplingInterval, 0);
    if (interval) m_profiler->SetSamplingInterval(interval);
  }
  ++m_startedProfilesCount;
  m_profiler->StartProfiling(toV8String(m_isolate, title), true);
}

std::unique_ptr<protocol::Profiler::Profile> V8ProfilerAgentImpl::stopProfiling(
    const String16& title, bool serialize) {
  v8::HandleScope handleScope(m_isolate);
  v8::CpuProfile* profile =
      m_profiler->StopProfiling(toV8String(m_isolate, title));
  std::unique_ptr<protocol::Profiler::Profile> result;
  if (profile) {
    if (serialize) result = createCPUProfile(m_session->inspector(), profile);
    profile->Delete();
  }
  --m_startedProfilesCount;
  if (!m_startedProfilesCount) {
    m_profiler->Dispose();
    m_profiler = nullptr;
  }
  return result;
}

}  // namespace v8_inspector
