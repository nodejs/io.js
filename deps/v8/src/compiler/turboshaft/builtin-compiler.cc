// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/builtin-compiler.h"

#include "src/builtins/profile-data-reader.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/pipelines.h"
#include "src/compiler/turboshaft/zone-with-name.h"
#include "src/compiler/zone-stats.h"
#include "src/execution/isolate.h"

namespace v8::internal::compiler::turboshaft {

inline constexpr char kBuiltinCompilationZoneName[] =
    "builtin-compilation-zone";

DirectHandle<Code> BuildWithTurboshaftAssemblerImpl(
    Isolate* isolate, Builtin builtin, TurboshaftAssemblerGenerator generator,
    std::function<compiler::CallDescriptor*(Zone*)> call_descriptor_builder,
    const char* name, const AssemblerOptions& options, CodeKind code_kind,
    std::optional<BytecodeHandlerData> bytecode_handler_data) {
  using namespace compiler::turboshaft;  // NOLINT(build/namespaces)
  DCHECK_EQ(code_kind == CodeKind::BYTECODE_HANDLER,
            bytecode_handler_data.has_value());

  compiler::ZoneStats zone_stats(isolate->allocator());
  ZoneWithName<kBuiltinCompilationZoneName> zone(&zone_stats,
                                                 kBuiltinCompilationZoneName);
  OptimizedCompilationInfo info(base::CStrVector(name), zone, code_kind,
                                builtin);
  compiler::CallDescriptor* call_descriptor = call_descriptor_builder(zone);

  PipelineData data(&zone_stats, TurboshaftPipelineKind::kTSABuiltin, isolate,
                    &info, options);
  data.InitializeBuiltinComponent(call_descriptor,
                                  std::move(bytecode_handler_data));
  data.InitializeGraphComponent(nullptr);
  ZoneWithName<kTempZoneName> temp_zone(&zone_stats, kTempZoneName);
  generator(&data, isolate, data.graph(), temp_zone);

  DirectHandle<Code> code =
      compiler::Pipeline::GenerateCodeForTurboshaftBuiltin(
          &data, call_descriptor, builtin, name,
          ProfileDataFromFile::TryRead(name))
          .ToHandleChecked();
  return code;
}

}  // namespace v8::internal::compiler::turboshaft
