// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/decompression-optimization-phase.h"

#include "src/compiler/turboshaft/decompression-optimization.h"

namespace v8::internal::compiler::turboshaft {

void DecompressionOptimizationPhase::Run(PipelineData* data, Zone* temp_zone) {
  if (!COMPRESS_POINTERS_BOOL) return;
  RunDecompressionOptimization(data->graph(), temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
