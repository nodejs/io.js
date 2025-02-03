// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-compiler.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <type_traits>
#include <unordered_map>

#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/threaded-list.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/register.h"
#include "src/codegen/reglist.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/bytecode-liveness-map.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/heap-refs.h"
#include "src/compiler/js-heap-broker.h"
#include "src/deoptimizer/frame-translation-builder.h"
#include "src/execution/frames.h"
#include "src/ic/handler-configuration.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-code-generator.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph-verifier.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-phi-representation-selector.h"
#include "src/maglev/maglev-post-hoc-optimizations-processors.h"
#include "src/maglev/maglev-pre-regalloc-codegen-processors.h"
#include "src/maglev/maglev-regalloc-data.h"
#include "src/maglev/maglev-regalloc.h"
#include "src/objects/code-inl.h"
#include "src/objects/js-function.h"
#include "src/utils/identity-map.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace maglev {

// static
bool MaglevCompiler::Compile(LocalIsolate* local_isolate,
                             MaglevCompilationInfo* compilation_info) {
  compiler::CurrentHeapBrokerScope current_broker(compilation_info->broker());
  Graph* graph =
      Graph::New(compilation_info->zone(),
                 compilation_info->toplevel_compilation_unit()->is_osr());

  // Build graph.
  if (v8_flags.print_maglev_code || v8_flags.code_comments ||
      v8_flags.print_maglev_graph || v8_flags.print_maglev_graphs ||
      v8_flags.trace_maglev_graph_building ||
      v8_flags.trace_maglev_escape_analysis ||
      v8_flags.trace_maglev_phi_untagging || v8_flags.trace_maglev_regalloc ||
      v8_flags.trace_maglev_object_tracking) {
    compilation_info->set_graph_labeller(new MaglevGraphLabeller());
  }

  {
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());

    if (v8_flags.print_maglev_code || v8_flags.print_maglev_graph ||
        v8_flags.print_maglev_graphs || v8_flags.trace_maglev_graph_building ||
        v8_flags.trace_maglev_phi_untagging || v8_flags.trace_maglev_regalloc) {
      MaglevCompilationUnit* top_level_unit =
          compilation_info->toplevel_compilation_unit();
      std::cout << "Compiling " << Brief(*compilation_info->toplevel_function())
                << " with Maglev\n";
      BytecodeArray::Disassemble(top_level_unit->bytecode().object(),
                                 std::cout);
      if (v8_flags.maglev_print_feedback) {
        Print(*top_level_unit->feedback().object(), std::cout);
      }
    }

    MaglevGraphBuilder graph_builder(
        local_isolate, compilation_info->toplevel_compilation_unit(), graph);

    {
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                   "V8.Maglev.GraphBuilding");
      graph_builder.Build();

      if (v8_flags.print_maglev_graphs) {
        std::cout << "\nAfter graph building" << std::endl;
        PrintGraph(std::cout, compilation_info, graph);
      }
    }

    if (v8_flags.maglev_licm) {
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                   "V8.Maglev.LoopOptimizations");

      GraphProcessor<LoopOptimizationProcessor> loop_optimizations(
          &graph_builder);
      loop_optimizations.ProcessGraph(graph);

      if (v8_flags.print_maglev_graphs) {
        std::cout << "\nAfter loop optimizations" << std::endl;
        PrintGraph(std::cout, compilation_info, graph);
      }
    }

    if (v8_flags.maglev_untagged_phis) {
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                   "V8.Maglev.PhiUntagging");

      GraphProcessor<MaglevPhiRepresentationSelector> representation_selector(
          &graph_builder);
      representation_selector.ProcessGraph(graph);

      if (v8_flags.print_maglev_graphs) {
        std::cout << "\nAfter Phi untagging" << std::endl;
        PrintGraph(std::cout, compilation_info, graph);
      }
    }
  }

#ifdef DEBUG
  {
    GraphProcessor<MaglevGraphVerifier> verifier(compilation_info);
    verifier.ProcessGraph(graph);
  }
#endif

  {
    // Post-hoc optimisation:
    //   - Dead node marking
    //   - Cleaning up identity nodes
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.DeadCodeMarking");
    GraphMultiProcessor<AnyUseMarkingProcessor> processor;
    processor.ProcessGraph(graph);
  }

  if (v8_flags.print_maglev_graphs) {
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
    std::cout << "After use marking" << std::endl;
    PrintGraph(std::cout, compilation_info, graph);
  }

#ifdef DEBUG
  {
    GraphProcessor<MaglevGraphVerifier> verifier(compilation_info);
    verifier.ProcessGraph(graph);
  }
#endif

  {
    // Preprocessing for register allocation and code gen:
    //   - Remove dead nodes
    //   - Collect input/output location constraints
    //   - Find the maximum number of stack arguments passed to calls
    //   - Collect use information, for SSA liveness and next-use distance.
    //   - Mark
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.NodeProcessing");
    GraphMultiProcessor<DeadNodeSweepingProcessor,
                        ValueLocationConstraintProcessor, MaxCallDepthProcessor,
                        LiveRangeAndNextUseProcessor,
                        DecompressedUseMarkingProcessor>
        processor(DeadNodeSweepingProcessor{compilation_info},
                  LiveRangeAndNextUseProcessor{compilation_info});
    processor.ProcessGraph(graph);
  }

  if (v8_flags.print_maglev_graphs) {
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
    std::cout << "After register allocation pre-processing" << std::endl;
    PrintGraph(std::cout, compilation_info, graph);
  }

  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.RegisterAllocation");
    StraightForwardRegisterAllocator allocator(compilation_info, graph);

    if (v8_flags.print_maglev_graph || v8_flags.print_maglev_graphs) {
      UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
      std::cout << "After register allocation" << std::endl;
      PrintGraph(std::cout, compilation_info, graph);
    }
  }

  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.CodeAssembly");
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
    std::unique_ptr<MaglevCodeGenerator> code_generator =
        std::make_unique<MaglevCodeGenerator>(local_isolate, compilation_info,
                                              graph);
    bool success = code_generator->Assemble();
    if (!success) {
      return false;
    }

    // Stash the compiled code_generator on the compilation info.
    compilation_info->set_code_generator(std::move(code_generator));
  }

  return true;
}

// static
MaybeHandle<Code> MaglevCompiler::GenerateCode(
    Isolate* isolate, MaglevCompilationInfo* compilation_info) {
  compiler::CurrentHeapBrokerScope current_broker(compilation_info->broker());
  MaglevCodeGenerator* const code_generator =
      compilation_info->code_generator();
  DCHECK_NOT_NULL(code_generator);

  Handle<Code> code;
  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.CodeGeneration");
    if (compilation_info->is_detached() ||
        !code_generator->Generate(isolate).ToHandle(&code)) {
      compilation_info->toplevel_compilation_unit()
          ->shared_function_info()
          .object()
          ->set_maglev_compilation_failed(true);
      return {};
    }
  }

  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.CommittingDependencies");
    if (!compilation_info->broker()->dependencies()->Commit(code)) {
      // Don't `set_maglev_compilation_failed` s.t. we may reattempt
      // compilation.
      // TODO(v8:7700): Make this more robust, i.e.: don't recompile endlessly,
      // and possibly attempt to recompile as early as possible.
      return {};
    }
  }

  if (v8_flags.print_maglev_code) {
#ifdef OBJECT_PRINT
    std::unique_ptr<char[]> debug_name =
        compilation_info->toplevel_function()->shared()->DebugNameCStr();
    CodeTracer::StreamScope tracing_scope(isolate->GetCodeTracer());
    auto& os = tracing_scope.stream();
    code->CodePrint(os, debug_name.get());
#else
    Print(*code);
#endif
  }

  return code;
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
