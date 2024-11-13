// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/source-position.h"

#include "src/codegen/optimized-compilation-info.h"
#include "src/common/assert-scope.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

std::ostream& operator<<(std::ostream& out, const SourcePositionInfo& pos) {
  out << "<";
  if (!pos.script.is_null() && IsString(pos.script->name())) {
    out << Cast<String>(pos.script->name())->ToCString(DISALLOW_NULLS).get();
  } else {
    out << "unknown";
  }
  out << ":" << pos.line + 1 << ":" << pos.column + 1 << ">";
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const std::vector<SourcePositionInfo>& stack) {
  bool first = true;
  for (const SourcePositionInfo& pos : stack) {
    if (!first) out << " inlined at ";
    out << pos;
    first = false;
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const SourcePosition& pos) {
  if (pos.isInlined()) {
    out << "<inlined(" << pos.InliningId() << "):";
  } else {
    out << "<not inlined:";
  }

  if (pos.IsExternal()) {
    out << pos.ExternalLine() << ", " << pos.ExternalFileId() << ">";
  } else {
    out << pos.ScriptOffset() << ">";
  }
  return out;
}

std::vector<SourcePositionInfo> SourcePosition::InliningStack(
    Isolate* isolate, OptimizedCompilationInfo* cinfo) const {
  SourcePosition pos = *this;
  std::vector<SourcePositionInfo> stack;
  while (pos.isInlined()) {
    const auto& inl = cinfo->inlined_functions()[pos.InliningId()];
    stack.push_back(SourcePositionInfo(isolate, pos, inl.shared_info));
    pos = inl.position.position;
  }
  stack.push_back(SourcePositionInfo(isolate, pos, cinfo->shared_info()));
  return stack;
}

std::vector<SourcePositionInfo> SourcePosition::InliningStack(
    Isolate* isolate, Tagged<Code> code) const {
  Tagged<DeoptimizationData> deopt_data =
      Cast<DeoptimizationData>(code->deoptimization_data());
  SourcePosition pos = *this;
  std::vector<SourcePositionInfo> stack;
  while (pos.isInlined()) {
    InliningPosition inl =
        deopt_data->InliningPositions()->get(pos.InliningId());
    Handle<SharedFunctionInfo> function(
        deopt_data->GetInlinedFunction(inl.inlined_function_id), isolate);
    stack.push_back(SourcePositionInfo(isolate, pos, function));
    pos = inl.position;
  }
  Handle<SharedFunctionInfo> function(
      Cast<SharedFunctionInfo>(deopt_data->SharedFunctionInfo()), isolate);
  stack.push_back(SourcePositionInfo(isolate, pos, function));
  return stack;
}

SourcePositionInfo SourcePosition::FirstInfo(Isolate* isolate,
                                             Tagged<Code> code) const {
  DisallowGarbageCollection no_gc;
  Tagged<DeoptimizationData> deopt_data =
      Cast<DeoptimizationData>(code->deoptimization_data());
  SourcePosition pos = *this;
  if (pos.isInlined()) {
    InliningPosition inl =
        deopt_data->InliningPositions()->get(pos.InliningId());
    Handle<SharedFunctionInfo> function(
        deopt_data->GetInlinedFunction(inl.inlined_function_id), isolate);
    return SourcePositionInfo(isolate, pos, function);
  }
  Handle<SharedFunctionInfo> function(
      Cast<SharedFunctionInfo>(deopt_data->SharedFunctionInfo()), isolate);
  return SourcePositionInfo(isolate, pos, function);
}

void SourcePosition::Print(std::ostream& out,
                           Tagged<SharedFunctionInfo> function) const {
  Script::PositionInfo pos;
  Tagged<Object> source_name;
  if (IsScript(function->script())) {
    Tagged<Script> script = Cast<Script>(function->script());
    source_name = script->name();
    script->GetPositionInfo(ScriptOffset(), &pos);
  }
  out << "<";
  if (IsString(source_name)) {
    out << Cast<String>(source_name)
               ->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL)
               .get();
  } else {
    out << "unknown";
  }
  out << ":" << pos.line + 1 << ":" << pos.column + 1 << ">";
}

void SourcePosition::PrintJson(std::ostream& out) const {
  if (IsExternal()) {
    out << "{ \"line\" : " << ExternalLine() << ", "
        << "  \"fileId\" : " << ExternalFileId() << ", "
        << "  \"inliningId\" : " << InliningId() << "}";
  } else {
    out << "{ \"scriptOffset\" : " << ScriptOffset() << ", "
        << "  \"inliningId\" : " << InliningId() << "}";
  }
}

void SourcePosition::Print(std::ostream& out, Tagged<Code> code) const {
  Tagged<DeoptimizationData> deopt_data =
      Cast<DeoptimizationData>(code->deoptimization_data());
  if (!isInlined()) {
    Tagged<SharedFunctionInfo> function(
        Cast<SharedFunctionInfo>(deopt_data->SharedFunctionInfo()));
    Print(out, function);
  } else {
    InliningPosition inl = deopt_data->InliningPositions()->get(InliningId());
    if (inl.inlined_function_id == -1) {
      out << *this;
    } else {
      Tagged<SharedFunctionInfo> function =
          deopt_data->GetInlinedFunction(inl.inlined_function_id);
      Print(out, function);
    }
    out << " inlined at ";
    inl.position.Print(out, code);
  }
}

SourcePositionInfo::SourcePositionInfo(Isolate* isolate, SourcePosition pos,
                                       Handle<SharedFunctionInfo> sfi)
    : position(pos), shared(sfi), script(Handle<Script>::null()) {
  {
    DisallowGarbageCollection no_gc;
    if (sfi.is_null()) return;
    Tagged<Object> maybe_script = sfi->script();
    if (!IsScript(maybe_script)) return;
    script = handle(Cast<Script>(maybe_script), isolate);
  }
  Script::PositionInfo info;
  if (Script::GetPositionInfo(script, pos.ScriptOffset(), &info)) {
    line = info.line;
    column = info.column;
  }
}

}  // namespace internal
}  // namespace v8
