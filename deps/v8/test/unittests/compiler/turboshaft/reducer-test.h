// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "src/compiler/backend/instruction.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::compiler::turboshaft {

class TestInstance {
 public:
  using Assembler = TSAssembler<VariableReducer>;

  struct CapturedOperation {
    TestInstance* instance;
    OpIndex input;
    std::set<OpIndex> generated_output;

    bool IsEmpty() const { return generated_output.empty(); }

    template <typename Op>
    bool Contains() const {
      for (OpIndex o : generated_output) {
        if (instance->graph().Get(o).Is<Op>()) return true;
      }
      return false;
    }

    template <typename Op>
    const underlying_operation_t<Op>* GetFirst() const {
      for (OpIndex o : generated_output) {
        if (auto result = instance->graph().Get(o).TryCast<Op>()) {
          return result;
        }
      }
      return nullptr;
    }

    template <typename Op>
    const underlying_operation_t<Op>* GetAs() const {
      DCHECK_EQ(generated_output.size(), 1);
      return GetFirst<Op>();
    }

    const Operation* Get() const {
      DCHECK_EQ(generated_output.size(), 1);
      return &instance->graph().Get(*generated_output.begin());
    }
  };

  template <typename Builder>
  static TestInstance CreateFromGraph(PipelineData* data, int parameter_count,
                                      const Builder& builder, Isolate* isolate,
                                      Zone* zone) {
    auto graph = std::make_unique<Graph>(zone);
    TestInstance instance(data, std::move(graph), isolate, zone);
    // Generate a function prolog
    Block* start_block = instance.Asm().NewBlock();
    instance.Asm().Bind(start_block);
    instance.Asm().Parameter(3, RegisterRepresentation::Tagged(), "%context");
    instance.Asm().Parameter(0, RegisterRepresentation::Tagged(), "%this");
    for (int i = 0; i < parameter_count; ++i) {
      instance.parameters_.push_back(
          instance.Asm().Parameter(1 + i, RegisterRepresentation::Tagged()));
    }
    builder(instance);
    return instance;
  }

  Assembler& Asm() { return assembler_; }
  Graph& graph() { return *graph_; }
  Factory& factory() { return *isolate_->factory(); }
  Zone* zone() { return zone_; }

  Assembler& operator()() { return Asm(); }

  template <template <typename> typename... Reducers>
  void Run(bool trace_reductions = v8_flags.turboshaft_trace_reduction) {
    TSAssembler<GraphVisitor, Reducers...> phase(
        data_, graph(), graph().GetOrCreateCompanion(), zone_);
#ifdef DEBUG
    if (trace_reductions) {
      phase.template VisitGraph<true>();
    } else {
      phase.template VisitGraph<false>();
    }
#else
    phase.template VisitGraph<false>();
#endif
    // Map all captured inputs.
    for (auto& [key, captured] : captured_operations_) {
      std::set<OpIndex> temp = std::move(captured.generated_output);
      for (OpIndex index : graph_->AllOperationIndices()) {
        OpIndex origin = graph_->operation_origins()[index];
        if (temp.contains(origin)) captured.generated_output.insert(index);
      }
    }
  }

  V<Object> GetParameter(int index) {
    DCHECK_LE(0, index);
    DCHECK_LT(index, parameters_.size());
    return parameters_[index];
  }
  OpIndex BuildFrameState() {
    FrameStateData::Builder builder;
    // Closure
    builder.AddInput(MachineType::AnyTagged(),
                     Asm().SmiConstant(Smi::FromInt(0)));
    // TODO(nicohartmann@): Parameters, Context, Locals, Accumulator if
    // necessary.

    FrameStateFunctionInfo* function_info =
        zone_->template New<FrameStateFunctionInfo>(
            FrameStateType::kUnoptimizedFunction, 0, 0, 0,
            Handle<SharedFunctionInfo>{});
    const FrameStateInfo* frame_state_info =
        zone_->template New<FrameStateInfo>(BytecodeOffset(0),
                                            OutputFrameStateCombine::Ignore(),
                                            function_info);

    return Asm().FrameState(
        builder.Inputs(), builder.inlined(),
        builder.AllocateFrameStateData(*frame_state_info, zone_));
  }

  OpIndex Capture(OpIndex input, const std::string& key) {
    captured_operations_[key] =
        CapturedOperation{this, input, std::set<OpIndex>{input}};
    return input;
  }
  template <typename T>
  V<T> Capture(V<T> input, const std::string& key) {
    return V<T>::Cast(Capture(static_cast<OpIndex>(input), key));
  }
  const CapturedOperation& GetCapture(const std::string& key) const {
    auto it = captured_operations_.find(key);
    DCHECK_NE(it, captured_operations_.end());
    return it->second;
  }
  const Operation* GetCaptured(const std::string& key) const {
    return GetCapture(key).Get();
  }
  template <typename Op>
  const underlying_operation_t<Op>* GetCapturedAs(
      const std::string& key) const {
    return GetCapture(key).GetAs<Op>();
  }

  size_t CountOp(Opcode opcode) {
    auto operations = graph().AllOperations();
    return std::count_if(
        operations.begin(), operations.end(),
        [opcode](const Operation& op) { return op.opcode == opcode; });
  }

  struct CaptureHelper {
    TestInstance* instance;
    std::string key;
    OpIndex operator=(OpIndex value) { return instance->Capture(value, key); }
  };
  CaptureHelper CaptureHelperForMacro(const std::string& key) {
    return CaptureHelper{this, std::move(key)};
  }

  void PrintGraphForTurbolizer(const char* phase_name) {
    if (!stream_) {
      const testing::TestInfo* test_info =
          testing::UnitTest::GetInstance()->current_test_info();
      std::stringstream file_name;
      file_name << "turbo-" << test_info->test_suite_name() << "_"
                << test_info->name() << ".json";
      stream_ = std::make_unique<std::ofstream>(file_name.str(),
                                                std::ios_base::trunc);
      *stream_ << "{\"function\" : ";
      size_t len = strlen("test_generated_function") + 1;
      auto name = std::make_unique<char[]>(len);
      snprintf(name.get(), len, "test_generated_function");
      JsonPrintFunctionSource(*stream_, -1, std::move(name), Handle<Script>{},
                              isolate_, Handle<SharedFunctionInfo>{});
      *stream_ << ",\n\"phases\":[";
    }
    PrintTurboshaftGraphForTurbolizer(*stream_, graph(), phase_name, nullptr,
                                      zone_);
  }

 private:
  TestInstance(PipelineData* data, std::unique_ptr<Graph> graph,
               Isolate* isolate, Zone* zone)
      : data_(data),
        assembler_(data, *graph, *graph, zone),
        graph_(std::move(graph)),
        isolate_(isolate),
        zone_(zone) {}

  PipelineData* data_;
  Assembler assembler_;
  std::unique_ptr<Graph> graph_;
  std::unique_ptr<std::ofstream> stream_;
  Isolate* isolate_;
  Zone* zone_;
  base::SmallMap<std::map<std::string, CapturedOperation>> captured_operations_;
  base::SmallVector<OpIndex, 4> parameters_;
};

class ReducerTest : public TestWithNativeContextAndZone {
 public:
  template <typename Builder>
  TestInstance CreateFromGraph(int parameter_count, const Builder& builder) {
    return TestInstance::CreateFromGraph(pipeline_data_.get(), parameter_count,
                                         builder, isolate(), zone());
  }

  void SetUp() override {
    pipeline_data_.reset(new turboshaft::PipelineData(
        &zone_stats_, TurboshaftPipelineKind::kJS, this->isolate(), nullptr,
        AssemblerOptions::Default(this->isolate())));
  }
  void TearDown() override { pipeline_data_.reset(); }

  ZoneStats zone_stats_{this->zone()->allocator()};
  std::unique_ptr<turboshaft::PipelineData> pipeline_data_;
};

}  // namespace v8::internal::compiler::turboshaft
