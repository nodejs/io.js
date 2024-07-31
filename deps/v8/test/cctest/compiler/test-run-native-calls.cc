// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "src/base/overflowing-math.h"
#include "src/codegen/assembler.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/register-configuration.h"
#include "src/compiler/linkage.h"
#include "src/compiler/raw-machine-assembler.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/wasm-linkage.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/graph-and-builders.h"
#include "test/common/value-helper.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace test_run_native_calls {

namespace {
using float32 = float;
using float64 = double;

// Picks a representative pair of integers from the given range.
// If there are less than {max_pairs} possible pairs, do them all, otherwise try
// to select a representative set.
class Pairs {
 public:
  Pairs(int max_pairs, int range, const int* codes)
      : range_(range),
        codes_(codes),
        max_pairs_(std::min(max_pairs, range_ * range_)),
        counter_(0) {}

  bool More() { return counter_ < max_pairs_; }

  void Next(int* r0, int* r1, bool same_is_ok) {
    do {
      // Find the next pair.
      if (exhaustive()) {
        *r0 = codes_[counter_ % range_];
        *r1 = codes_[counter_ / range_];
      } else {
        // Try each integer at least once for both r0 and r1.
        int index = counter_ / 2;
        if (counter_ & 1) {
          *r0 = codes_[index % range_];
          *r1 = codes_[index / range_];
        } else {
          *r1 = codes_[index % range_];
          *r0 = codes_[index / range_];
        }
      }
      counter_++;
      if ((same_is_ok) || (*r0 != *r1)) break;
      if (counter_ == max_pairs_) {
        // For the last hurrah, reg#0 with reg#n-1
        *r0 = codes_[0];
        *r1 = codes_[range_ - 1];
        break;
      }
    } while (true);
  }

 private:
  int range_;
  const int* codes_;
  int max_pairs_;
  int counter_;
  bool exhaustive() { return max_pairs_ == (range_ * range_); }
};


// Pairs of general purpose registers.
class RegisterPairs : public Pairs {
 public:
  RegisterPairs()
      : Pairs(100, GetRegConfig()->num_allocatable_general_registers(),
              GetRegConfig()->allocatable_general_codes()) {}
};

// Pairs of float registers.
class Float32RegisterPairs : public Pairs {
 public:
  Float32RegisterPairs()
      : Pairs(100, GetRegConfig()->num_allocatable_float_registers(),
              GetRegConfig()->allocatable_float_codes()) {}
};


// Pairs of double registers.
class Float64RegisterPairs : public Pairs {
 public:
  Float64RegisterPairs()
      : Pairs(100, GetRegConfig()->num_allocatable_double_registers(),
              GetRegConfig()->allocatable_double_codes()) {}
};


// Helper for allocating either an GP or FP reg, or the next stack slot.
class Allocator {
 public:
  Allocator(int* gp, int gpc, int* fp, int fpc) : stack_offset_(0) {
    for (int i = 0; i < gpc; ++i) {
      gp_.push_back(Register::from_code(gp[i]));
    }
    for (int i = 0; i < fpc; ++i) {
      fp_.push_back(DoubleRegister::from_code(fp[i]));
    }
    Reset();
  }

  int stack_offset() const { return stack_offset_; }

  LinkageLocation Next(MachineType type) {
    if (IsFloatingPoint(type.representation())) {
      // Allocate a floating point register/stack location.
      if (reg_allocator_->CanAllocateFP(type.representation())) {
        int code = reg_allocator_->NextFpReg(type.representation());
        return LinkageLocation::ForRegister(code, type);
      } else {
        int offset = -1 - stack_offset_;
        stack_offset_ += StackWords(type);
        return LinkageLocation::ForCallerFrameSlot(offset, type);
      }
    } else {
      // Allocate a general purpose register/stack location.
      if (reg_allocator_->CanAllocateGP()) {
        int code = reg_allocator_->NextGpReg();
        return LinkageLocation::ForRegister(code, type);
      } else {
        int offset = -1 - stack_offset_;
        stack_offset_ += StackWords(type);
        return LinkageLocation::ForCallerFrameSlot(offset, type);
      }
    }
  }
  int StackWords(MachineType type) {
    int size = 1 << ElementSizeLog2Of(type.representation());
    return size <= kSystemPointerSize ? 1 : size / kSystemPointerSize;
  }
  void Reset() {
    stack_offset_ = 0;
    reg_allocator_.reset(
        new wasm::LinkageAllocator(gp_.data(), static_cast<int>(gp_.size()),
                                   fp_.data(), static_cast<int>(fp_.size())));
  }

 private:
  std::vector<Register> gp_;
  std::vector<DoubleRegister> fp_;
  std::unique_ptr<wasm::LinkageAllocator> reg_allocator_;
  int stack_offset_;
};


class RegisterConfig {
 public:
  RegisterConfig(Allocator& p, Allocator& r) : params(p), rets(r) {}

  CallDescriptor* Create(Zone* zone, MachineSignature* msig) {
    rets.Reset();
    params.Reset();

    LocationSignature::Builder locations(zone, msig->return_count(),
                                         msig->parameter_count());
    // Add return location(s).
    const int return_count = static_cast<int>(locations.return_count_);
    for (int i = 0; i < return_count; i++) {
      locations.AddReturn(rets.Next(msig->GetReturn(i)));
    }

    // Add register and/or stack parameter(s).
    const int parameter_count = static_cast<int>(msig->parameter_count());
    for (int i = 0; i < parameter_count; i++) {
      locations.AddParam(params.Next(msig->GetParam(i)));
    }

    const RegList kCalleeSaveRegisters;
    const DoubleRegList kCalleeSaveFPRegisters;

    MachineType target_type = MachineType::AnyTagged();
    LinkageLocation target_loc = LinkageLocation::ForAnyRegister();
    int stack_param_count = params.stack_offset();
    return zone->New<CallDescriptor>(       // --
        CallDescriptor::kCallCodeObject,    // kind
        kDefaultCodeEntrypointTag,          // tag
        target_type,                        // target MachineType
        target_loc,                         // target location
        locations.Build(),                  // location_sig
        stack_param_count,                  // stack_parameter_count
        compiler::Operator::kNoProperties,  // properties
        kCalleeSaveRegisters,               // callee-saved registers
        kCalleeSaveFPRegisters,             // callee-saved fp regs
        CallDescriptor::kNoFlags,           // flags
        "c-call");
  }

 private:
  Allocator& params;
  Allocator& rets;
};

const int kMaxParamCount = 64;

MachineType kIntTypes[kMaxParamCount + 1] = {
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32(), MachineType::Int32(),
    MachineType::Int32(), MachineType::Int32()};


// For making uniform int32 signatures shorter.
class Int32Signature : public MachineSignature {
 public:
  explicit Int32Signature(int param_count)
      : MachineSignature(1, param_count, kIntTypes) {
    CHECK_GE(kMaxParamCount, param_count);
  }
};

Handle<Code> CompileGraph(const char* name, CallDescriptor* call_descriptor,
                          Graph* graph, Schedule* schedule = nullptr) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  OptimizedCompilationInfo info(base::ArrayVector("testing"), graph->zone(),
                                CodeKind::FOR_TESTING);
  Handle<Code> code = Pipeline::GenerateCodeForTesting(
                          &info, isolate, call_descriptor, graph,
                          AssemblerOptions::Default(isolate), schedule)
                          .ToHandleChecked();
#ifdef ENABLE_DISASSEMBLER
  if (v8_flags.print_opt_code) {
    StdoutStream os;
    code->Disassemble(name, os, isolate);
  }
#endif
  return code;
}

Handle<Code> WrapWithCFunction(Isolate* isolate, Handle<Code> inner,
                               CallDescriptor* call_descriptor) {
  Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
  int param_count = static_cast<int>(call_descriptor->ParameterCount());
  GraphAndBuilders caller(&zone);
  {
    GraphAndBuilders& b = caller;
    Node* start = b.graph()->NewNode(b.common()->Start(param_count + 3));
    b.graph()->SetStart(start);
    Node* target = b.graph()->NewNode(b.common()->HeapConstant(inner));

    // Add arguments to the call.
    Node** args = zone.AllocateArray<Node*>(param_count + 3);
    int index = 0;
    args[index++] = target;
    for (int i = 0; i < param_count; i++) {
      args[index] = b.graph()->NewNode(b.common()->Parameter(i), start);
      index++;
    }
    args[index++] = start;  // effect.
    args[index++] = start;  // control.

    // Build the call and return nodes.
    Node* call = b.graph()->NewNode(b.common()->Call(call_descriptor),
                                    param_count + 3, args);
    Node* zero = b.graph()->NewNode(b.common()->Int32Constant(0));
    Node* ret =
        b.graph()->NewNode(b.common()->Return(), zero, call, call, start);
    b.graph()->SetEnd(ret);
  }

  MachineSignature* msig = call_descriptor->GetMachineSignature(&zone);
  CallDescriptor* cdesc = Linkage::GetSimplifiedCDescriptor(&zone, msig);

  return CompileGraph("wrapper", cdesc, caller.graph());
}

template <typename CType>
class ArgsBuffer {
 public:
  static const int kMaxParamCount = 64;

  explicit ArgsBuffer(int count, int seed = 1) : count_(count), seed_(seed) {
    // initialize the buffer with "seed 0"
    seed_ = 0;
    Mutate();
    seed_ = seed;
  }

  class Sig : public MachineSignature {
   public:
    explicit Sig(int param_count)
        : MachineSignature(1, param_count, MachTypes()) {
      CHECK_GE(kMaxParamCount, param_count);
    }
  };

  static MachineType* MachTypes() {
    MachineType t = MachineTypeForC<CType>();
    static MachineType kTypes[kMaxParamCount + 1] = {
        t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t,
        t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t,
        t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t, t};
    return kTypes;
  }

  Node* MakeConstant(RawMachineAssembler* raw, int32_t value) {
    return raw->Int32Constant(value);
  }

  Node* MakeConstant(RawMachineAssembler* raw, int64_t value) {
    return raw->Int64Constant(value);
  }

  Node* MakeConstant(RawMachineAssembler* raw, float32 value) {
    return raw->Float32Constant(value);
  }

  Node* MakeConstant(RawMachineAssembler* raw, float64 value) {
    return raw->Float64Constant(value);
  }

  Node* LoadInput(RawMachineAssembler* raw, Node* base, int index) {
    Node* offset = raw->Int32Constant(index * sizeof(CType));
    return raw->Load(MachineTypeForC<CType>(), base, offset);
  }

  Node* StoreOutput(RawMachineAssembler* raw, Node* value) {
    Node* base = raw->PointerConstant(&output);
    Node* offset = raw->Int32Constant(0);
    return raw->Store(MachineTypeForC<CType>().representation(), base, offset,
                      value, kNoWriteBarrier);
  }

  // Computes the next set of inputs by updating the {input} array.
  void Mutate();

  void Reset() { memset(input, 0, sizeof(input)); }

  int count_;
  int seed_;
  CType input[kMaxParamCount];
  CType output;
};


template <>
void ArgsBuffer<int32_t>::Mutate() {
  uint32_t base = 1111111111u * seed_;
  for (int j = 0; j < count_ && j < kMaxParamCount; j++) {
    input[j] = static_cast<int32_t>(256 + base + j + seed_ * 13);
  }
  output = -1;
  seed_++;
}


template <>
void ArgsBuffer<int64_t>::Mutate() {
  uint64_t base = 11111111111111111ull * seed_;
  for (int j = 0; j < count_ && j < kMaxParamCount; j++) {
    input[j] = static_cast<int64_t>(256 + base + j + seed_ * 13);
  }
  output = -1;
  seed_++;
}


template <>
void ArgsBuffer<float32>::Mutate() {
  float64 base = -33.25 * seed_;
  for (int j = 0; j < count_ && j < kMaxParamCount; j++) {
    input[j] = 256 + base + j + seed_ * 13;
  }
  output = std::numeric_limits<float32>::quiet_NaN();
  seed_++;
}


template <>
void ArgsBuffer<float64>::Mutate() {
  float64 base = -111.25 * seed_;
  for (int j = 0; j < count_ && j < kMaxParamCount; j++) {
    input[j] = 256 + base + j + seed_ * 13;
  }
  output = std::numeric_limits<float64>::quiet_NaN();
  seed_++;
}

int ParamCount(CallDescriptor* call_descriptor) {
  return static_cast<int>(call_descriptor->ParameterCount());
}


template <typename CType>
class Computer {
 public:
  static void Run(CallDescriptor* desc,
                  void (*build)(CallDescriptor*, RawMachineAssembler*),
                  CType (*compute)(CallDescriptor*, CType* inputs),
                  int seed = 1) {
    int num_params = ParamCount(desc);
    CHECK_LE(num_params, kMaxParamCount);
    Isolate* isolate = CcTest::InitIsolateOnce();
    HandleScope scope(isolate);
    Handle<Code> inner;
    {
      // Build the graph for the computation.
      Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
      Graph graph(&zone);
      RawMachineAssembler raw(isolate, &graph, desc);
      build(desc, &raw);
      inner = CompileGraph("Compute", desc, &graph, raw.ExportForTest());
    }

    CSignatureOf<int32_t> csig;
    ArgsBuffer<CType> io(num_params, seed);

    {
      // constant mode.
      Handle<Code> wrapper;
      {
        // Wrap the above code with a callable function that passes constants.
        Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
        Graph graph(&zone);
        CallDescriptor* cdesc = Linkage::GetSimplifiedCDescriptor(&zone, &csig);
        RawMachineAssembler raw(isolate, &graph, cdesc);
        Node* target = raw.HeapConstant(inner);
        Node** inputs = zone.AllocateArray<Node*>(num_params + 1);
        int input_count = 0;
        inputs[input_count++] = target;
        for (int i = 0; i < num_params; i++) {
          inputs[input_count++] = io.MakeConstant(&raw, io.input[i]);
        }

        Node* call = raw.CallN(desc, input_count, inputs);
        Node* store = io.StoreOutput(&raw, call);
        USE(store);
        raw.Return(raw.Int32Constant(seed));
        wrapper = CompileGraph("Compute-wrapper-const", cdesc, &graph,
                               raw.ExportForTest());
      }

      CodeRunner<int32_t> runnable(isolate, wrapper, &csig);

      // Run the code, checking it against the reference.
      CType expected = compute(desc, io.input);
      int32_t check_seed = runnable.Call();
      CHECK_EQ(seed, check_seed);
      CHECK_EQ(expected, io.output);
    }

    {
      // buffer mode.
      Handle<Code> wrapper;
      {
        // Wrap the above code with a callable function that loads from {input}.
        Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
        Graph graph(&zone);
        CallDescriptor* cdesc = Linkage::GetSimplifiedCDescriptor(&zone, &csig);
        RawMachineAssembler raw(isolate, &graph, cdesc);
        Node* base = raw.PointerConstant(io.input);
        Node* target = raw.HeapConstant(inner);
        Node** inputs = zone.AllocateArray<Node*>(kMaxParamCount + 1);
        int input_count = 0;
        inputs[input_count++] = target;
        for (int i = 0; i < num_params; i++) {
          inputs[input_count++] = io.LoadInput(&raw, base, i);
        }

        Node* call = raw.CallN(desc, input_count, inputs);
        Node* store = io.StoreOutput(&raw, call);
        USE(store);
        raw.Return(raw.Int32Constant(seed));
        wrapper =
            CompileGraph("Compute-wrapper", cdesc, &graph, raw.ExportForTest());
      }

      CodeRunner<int32_t> runnable(isolate, wrapper, &csig);

      // Run the code, checking it against the reference.
      for (int i = 0; i < 5; i++) {
        CType expected = compute(desc, io.input);
        int32_t check_seed = runnable.Call();
        CHECK_EQ(seed, check_seed);
        CHECK_EQ(expected, io.output);
        io.Mutate();
      }
    }
  }
};

}  // namespace


static void TestInt32Sub(CallDescriptor* desc) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
  GraphAndBuilders inner(&zone);
  {
    // Build the add function.
    GraphAndBuilders& b = inner;
    Node* start = b.graph()->NewNode(b.common()->Start(5));
    b.graph()->SetStart(start);
    Node* p0 = b.graph()->NewNode(b.common()->Parameter(0), start);
    Node* p1 = b.graph()->NewNode(b.common()->Parameter(1), start);
    Node* add = b.graph()->NewNode(b.machine()->Int32Sub(), p0, p1);
    Node* zero = b.graph()->NewNode(b.common()->Int32Constant(0));
    Node* ret =
        b.graph()->NewNode(b.common()->Return(), zero, add, start, start);
    b.graph()->SetEnd(ret);
  }

  Handle<Code> inner_code = CompileGraph("Int32Sub", desc, inner.graph());
  Handle<Code> wrapper = WrapWithCFunction(isolate, inner_code, desc);
  MachineSignature* msig = desc->GetMachineSignature(&zone);
  CodeRunner<int32_t> runnable(isolate, wrapper,
                               CSignature::FromMachine(&zone, msig));

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected = static_cast<int32_t>(static_cast<uint32_t>(i) -
                                              static_cast<uint32_t>(j));
      int32_t result = runnable.Call(i, j);
      CHECK_EQ(expected, result);
    }
  }
}


static void CopyTwentyInt32(CallDescriptor* desc) {
  const int kNumParams = 20;
  int32_t input[kNumParams];
  int32_t output[kNumParams];
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  Handle<Code> inner;
  {
    // Writes all parameters into the output buffer.
    Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
    Graph graph(&zone);
    RawMachineAssembler raw(isolate, &graph, desc);
    Node* base = raw.PointerConstant(output);
    for (int i = 0; i < kNumParams; i++) {
      Node* offset = raw.Int32Constant(i * sizeof(int32_t));
      raw.Store(MachineRepresentation::kWord32, base, offset, raw.Parameter(i),
                kNoWriteBarrier);
    }
    raw.Return(raw.Int32Constant(42));
    inner = CompileGraph("CopyTwentyInt32", desc, &graph, raw.ExportForTest());
  }

  CSignatureOf<int32_t> csig;
  Handle<Code> wrapper;
  {
    // Loads parameters from the input buffer and calls the above code.
    Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
    Graph graph(&zone);
    CallDescriptor* cdesc = Linkage::GetSimplifiedCDescriptor(&zone, &csig);
    RawMachineAssembler raw(isolate, &graph, cdesc);
    Node* base = raw.PointerConstant(input);
    Node* target = raw.HeapConstant(inner);
    Node** inputs = zone.AllocateArray<Node*>(JSParameterCount(kNumParams));
    int input_count = 0;
    inputs[input_count++] = target;
    for (int i = 0; i < kNumParams; i++) {
      Node* offset = raw.Int32Constant(i * sizeof(int32_t));
      inputs[input_count++] = raw.Load(MachineType::Int32(), base, offset);
    }

    Node* call = raw.CallN(desc, input_count, inputs);
    raw.Return(call);
    wrapper = CompileGraph("CopyTwentyInt32-wrapper", cdesc, &graph,
                           raw.ExportForTest());
  }

  CodeRunner<int32_t> runnable(isolate, wrapper, &csig);

  // Run the code, checking it correctly implements the memcpy.
  for (int i = 0; i < 5; i++) {
    uint32_t base = 1111111111u * i;
    for (int j = 0; j < kNumParams; j++) {
      input[j] = static_cast<int32_t>(base + 13 * j);
    }

    memset(output, 0, sizeof(output));
    CHECK_EQ(42, runnable.Call());

    for (int j = 0; j < kNumParams; j++) {
      CHECK_EQ(input[j], output[j]);
    }
  }
}


static void Test_RunInt32SubWithRet(int retreg) {
  Int32Signature sig(2);
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  RegisterPairs pairs;
  while (pairs.More()) {
    int parray[2];
    int rarray[] = {retreg};
    pairs.Next(&parray[0], &parray[1], false);
    Allocator params(parray, 2, nullptr, 0);
    Allocator rets(rarray, 1, nullptr, 0);
    RegisterConfig config(params, rets);
    TestInt32Sub(config.Create(&zone, &sig));
  }
}


// Separate tests for parallelization.
#define TEST_INT32_SUB_WITH_RET(x)                     \
  TEST(Run_Int32Sub_all_allocatable_pairs_##x) {       \
    if (x < Register::kNumRegisters &&                 \
        GetRegConfig()->IsAllocatableGeneralCode(x)) { \
      Test_RunInt32SubWithRet(x);                      \
    }                                                  \
  }

TEST_INT32_SUB_WITH_RET(0)
TEST_INT32_SUB_WITH_RET(1)
TEST_INT32_SUB_WITH_RET(2)
TEST_INT32_SUB_WITH_RET(3)
TEST_INT32_SUB_WITH_RET(4)
TEST_INT32_SUB_WITH_RET(5)
TEST_INT32_SUB_WITH_RET(6)
TEST_INT32_SUB_WITH_RET(7)
TEST_INT32_SUB_WITH_RET(8)
TEST_INT32_SUB_WITH_RET(9)
TEST_INT32_SUB_WITH_RET(10)
TEST_INT32_SUB_WITH_RET(11)
TEST_INT32_SUB_WITH_RET(12)
TEST_INT32_SUB_WITH_RET(13)
TEST_INT32_SUB_WITH_RET(14)
TEST_INT32_SUB_WITH_RET(15)
TEST_INT32_SUB_WITH_RET(16)
TEST_INT32_SUB_WITH_RET(17)
TEST_INT32_SUB_WITH_RET(18)
TEST_INT32_SUB_WITH_RET(19)


TEST(Run_Int32Sub_all_allocatable_single) {
  Int32Signature sig(2);
  RegisterPairs pairs;
  while (pairs.More()) {
    v8::internal::AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    int parray[1];
    int rarray[1];
    pairs.Next(&rarray[0], &parray[0], true);
    Allocator params(parray, 1, nullptr, 0);
    Allocator rets(rarray, 1, nullptr, 0);
    RegisterConfig config(params, rets);
    TestInt32Sub(config.Create(&zone, &sig));
  }
}


TEST(Run_CopyTwentyInt32_all_allocatable_pairs) {
  Int32Signature sig(20);
  RegisterPairs pairs;
  while (pairs.More()) {
    v8::internal::AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    int parray[2];
    int rarray[] = {GetRegConfig()->GetAllocatableGeneralCode(0)};
    pairs.Next(&parray[0], &parray[1], false);
    Allocator params(parray, 2, nullptr, 0);
    Allocator rets(rarray, 1, nullptr, 0);
    RegisterConfig config(params, rets);
    CopyTwentyInt32(config.Create(&zone, &sig));
  }
}

template <typename CType>
static void Run_Computation(
    CallDescriptor* desc, void (*build)(CallDescriptor*, RawMachineAssembler*),
    CType (*compute)(CallDescriptor*, CType* inputs), int seed = 1) {
  Computer<CType>::Run(desc, build, compute, seed);
}

static uint32_t coeff[] = {1,  2,  3,  5,  7,   11,  13,  17,  19, 23, 29,
                           31, 37, 41, 43, 47,  53,  59,  61,  67, 71, 73,
                           79, 83, 89, 97, 101, 103, 107, 109, 113};

static void Build_Int32_WeightedSum(CallDescriptor* desc,
                                    RawMachineAssembler* raw) {
  Node* result = raw->Int32Constant(0);
  for (int i = 0; i < ParamCount(desc); i++) {
    Node* term = raw->Int32Mul(raw->Parameter(i), raw->Int32Constant(coeff[i]));
    result = raw->Int32Add(result, term);
  }
  raw->Return(result);
}

static int32_t Compute_Int32_WeightedSum(CallDescriptor* desc, int32_t* input) {
  uint32_t result = 0;
  for (int i = 0; i < ParamCount(desc); i++) {
    result += static_cast<uint32_t>(input[i]) * coeff[i];
  }
  return static_cast<int32_t>(result);
}


static void Test_Int32_WeightedSum_of_size(int count) {
  Int32Signature sig(count);
  for (int p0 = 0; p0 < Register::kNumRegisters; p0++) {
    if (GetRegConfig()->IsAllocatableGeneralCode(p0)) {
      v8::internal::AccountingAllocator allocator;
      Zone zone(&allocator, ZONE_NAME);

      int parray[] = {p0};
      int rarray[] = {GetRegConfig()->GetAllocatableGeneralCode(0)};
      Allocator params(parray, 1, nullptr, 0);
      Allocator rets(rarray, 1, nullptr, 0);
      RegisterConfig config(params, rets);
      CallDescriptor* desc = config.Create(&zone, &sig);
      Run_Computation<int32_t>(desc, Build_Int32_WeightedSum,
                               Compute_Int32_WeightedSum, 257 + count);
    }
  }
}


// Separate tests for parallelization.
#define TEST_INT32_WEIGHTEDSUM(x) \
  TEST(Run_Int32_WeightedSum_##x) { Test_Int32_WeightedSum_of_size(x); }


TEST_INT32_WEIGHTEDSUM(1)
TEST_INT32_WEIGHTEDSUM(2)
TEST_INT32_WEIGHTEDSUM(3)
TEST_INT32_WEIGHTEDSUM(4)
TEST_INT32_WEIGHTEDSUM(5)
TEST_INT32_WEIGHTEDSUM(7)
TEST_INT32_WEIGHTEDSUM(9)
TEST_INT32_WEIGHTEDSUM(11)
TEST_INT32_WEIGHTEDSUM(17)
TEST_INT32_WEIGHTEDSUM(19)

template <int which>
static void Build_Select(CallDescriptor* desc, RawMachineAssembler* raw) {
  raw->Return(raw->Parameter(which));
}

template <typename CType, int which>
static CType Compute_Select(CallDescriptor* desc, CType* inputs) {
  return inputs[which];
}


template <typename CType, int which>
static void RunSelect(CallDescriptor* desc) {
  int count = ParamCount(desc);
  if (count <= which) return;
  Run_Computation<CType>(desc, Build_Select<which>,
                         Compute_Select<CType, which>,
                         1044 + which + 3 * sizeof(CType));
}


template <int which>
void Test_Int32_Select() {
  int parray[] = {GetRegConfig()->GetAllocatableGeneralCode(0)};
  int rarray[] = {GetRegConfig()->GetAllocatableGeneralCode(0)};
  Allocator params(parray, 1, nullptr, 0);
  Allocator rets(rarray, 1, nullptr, 0);
  RegisterConfig config(params, rets);

  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  for (int i = which + 1; i <= 64; i++) {
    Int32Signature sig(i);
    CallDescriptor* desc = config.Create(&zone, &sig);
    RunSelect<int32_t, which>(desc);
  }
}


// Separate tests for parallelization.
#define TEST_INT32_SELECT(x) \
  TEST(Run_Int32_Select_##x) { Test_Int32_Select<x>(); }


TEST_INT32_SELECT(0)
TEST_INT32_SELECT(1)
TEST_INT32_SELECT(2)
TEST_INT32_SELECT(3)
TEST_INT32_SELECT(4)
TEST_INT32_SELECT(5)
TEST_INT32_SELECT(6)
TEST_INT32_SELECT(11)
TEST_INT32_SELECT(15)
TEST_INT32_SELECT(19)
TEST_INT32_SELECT(45)
TEST_INT32_SELECT(62)
TEST_INT32_SELECT(63)


TEST(Int64Select_registers) {
  if (GetRegConfig()->num_allocatable_general_registers() < 2) return;
  // TODO(titzer): int64 on 32-bit platforms
  if (kSystemPointerSize < 8) return;

  int rarray[] = {GetRegConfig()->GetAllocatableGeneralCode(0)};
  ArgsBuffer<int64_t>::Sig sig(2);

  RegisterPairs pairs;
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  while (pairs.More()) {
    int parray[2];
    pairs.Next(&parray[0], &parray[1], false);
    Allocator params(parray, 2, nullptr, 0);
    Allocator rets(rarray, 1, nullptr, 0);
    RegisterConfig config(params, rets);

    CallDescriptor* desc = config.Create(&zone, &sig);
    RunSelect<int64_t, 0>(desc);
    RunSelect<int64_t, 1>(desc);
  }
}


TEST(Float32Select_registers) {
  if (GetRegConfig()->num_allocatable_double_registers() < 2) {
    return;
  }

  int rarray[] = {GetRegConfig()->GetAllocatableFloatCode(0)};
  ArgsBuffer<float32>::Sig sig(2);

  Float32RegisterPairs pairs;
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  while (pairs.More()) {
    int parray[2];
    pairs.Next(&parray[0], &parray[1], false);
    Allocator params(nullptr, 0, parray, 2);
    Allocator rets(nullptr, 0, rarray, 1);
    RegisterConfig config(params, rets);

    CallDescriptor* desc = config.Create(&zone, &sig);
    RunSelect<float32, 0>(desc);
    RunSelect<float32, 1>(desc);
  }
}


TEST(Float64Select_registers) {
  if (GetRegConfig()->num_allocatable_double_registers() < 2) return;
  if (GetRegConfig()->num_allocatable_general_registers() < 2) return;
  int rarray[] = {GetRegConfig()->GetAllocatableDoubleCode(0)};
  ArgsBuffer<float64>::Sig sig(2);

  Float64RegisterPairs pairs;
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  while (pairs.More()) {
    int parray[2];
    pairs.Next(&parray[0], &parray[1], false);
    Allocator params(nullptr, 0, parray, 2);
    Allocator rets(nullptr, 0, rarray, 1);
    RegisterConfig config(params, rets);

    CallDescriptor* desc = config.Create(&zone, &sig);
    RunSelect<float64, 0>(desc);
    RunSelect<float64, 1>(desc);
  }
}


TEST(Float32Select_stack_params_return_reg) {
  int rarray[] = {GetRegConfig()->GetAllocatableFloatCode(0)};
  Allocator params(nullptr, 0, nullptr, 0);
  Allocator rets(nullptr, 0, rarray, 1);
  RegisterConfig config(params, rets);

  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  for (int count = 1; count < 6; count++) {
    ArgsBuffer<float32>::Sig sig(count);
    CallDescriptor* desc = config.Create(&zone, &sig);
    RunSelect<float32, 0>(desc);
    RunSelect<float32, 1>(desc);
    RunSelect<float32, 2>(desc);
    RunSelect<float32, 3>(desc);
    RunSelect<float32, 4>(desc);
    RunSelect<float32, 5>(desc);
  }
}


TEST(Float64Select_stack_params_return_reg) {
  int rarray[] = {GetRegConfig()->GetAllocatableDoubleCode(0)};
  Allocator params(nullptr, 0, nullptr, 0);
  Allocator rets(nullptr, 0, rarray, 1);
  RegisterConfig config(params, rets);

  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  for (int count = 1; count < 6; count++) {
    ArgsBuffer<float64>::Sig sig(count);
    CallDescriptor* desc = config.Create(&zone, &sig);
    RunSelect<float64, 0>(desc);
    RunSelect<float64, 1>(desc);
    RunSelect<float64, 2>(desc);
    RunSelect<float64, 3>(desc);
    RunSelect<float64, 4>(desc);
    RunSelect<float64, 5>(desc);
  }
}

template <typename CType, int which>
static void Build_Select_With_Call(CallDescriptor* desc,
                                   RawMachineAssembler* raw) {
  Handle<Code> inner;
  int num_params = ParamCount(desc);
  CHECK_LE(num_params, kMaxParamCount);
  {
    Isolate* isolate = CcTest::InitIsolateOnce();
    // Build the actual select.
    Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
    Graph graph(&zone);
    RawMachineAssembler r(isolate, &graph, desc);
    r.Return(r.Parameter(which));
    inner = CompileGraph("Select-indirection", desc, &graph, r.ExportForTest());
    CHECK(!inner.is_null());
    CHECK(IsCode(*inner));
  }

  {
    // Build a call to the function that does the select.
    Node* target = raw->HeapConstant(inner);
    Node** inputs = raw->zone()->AllocateArray<Node*>(num_params + 1);
    int input_count = 0;
    inputs[input_count++] = target;
    for (int i = 0; i < num_params; i++) {
      inputs[input_count++] = raw->Parameter(i);
    }

    Node* call = raw->CallN(desc, input_count, inputs);
    raw->Return(call);
  }
}

TEST(Float64StackParamsToStackParams) {
  int rarray[] = {GetRegConfig()->GetAllocatableDoubleCode(0)};
  Allocator params(nullptr, 0, nullptr, 0);
  Allocator rets(nullptr, 0, rarray, 1);

  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  ArgsBuffer<float64>::Sig sig(2);
  RegisterConfig config(params, rets);
  CallDescriptor* desc = config.Create(&zone, &sig);

  Run_Computation<float64>(desc, Build_Select_With_Call<float64, 0>,
                           Compute_Select<float64, 0>, 1098);

  Run_Computation<float64>(desc, Build_Select_With_Call<float64, 1>,
                           Compute_Select<float64, 1>, 1099);
}


void MixedParamTest(int start) {
  if (GetRegConfig()->num_double_registers() < 2) return;

// TODO(titzer): mix in 64-bit types on all platforms when supported.
#if V8_TARGET_ARCH_32_BIT
  static MachineType types[] = {
      MachineType::Int32(),   MachineType::Float32(), MachineType::Float64(),
      MachineType::Int32(),   MachineType::Float64(), MachineType::Float32(),
      MachineType::Float32(), MachineType::Float64(), MachineType::Int32(),
      MachineType::Float32(), MachineType::Int32(),   MachineType::Float64(),
      MachineType::Float64(), MachineType::Float32(), MachineType::Int32(),
      MachineType::Float64(), MachineType::Int32(),   MachineType::Float32()};
#else
  static MachineType types[] = {
      MachineType::Int32(),   MachineType::Int64(),   MachineType::Float32(),
      MachineType::Float64(), MachineType::Int32(),   MachineType::Float64(),
      MachineType::Float32(), MachineType::Int64(),   MachineType::Int64(),
      MachineType::Float32(), MachineType::Float32(), MachineType::Int32(),
      MachineType::Float64(), MachineType::Float64(), MachineType::Int64(),
      MachineType::Int32(),   MachineType::Float64(), MachineType::Int32(),
      MachineType::Float32()};
#endif

  Isolate* isolate = CcTest::InitIsolateOnce();

  // Build machine signature
  MachineType* params = &types[start];
  const int num_params = static_cast<int>(arraysize(types) - start);

  // Build call descriptor
  int parray_gp[] = {GetRegConfig()->GetAllocatableGeneralCode(0),
                     GetRegConfig()->GetAllocatableGeneralCode(1)};
  int rarray_gp[] = {GetRegConfig()->GetAllocatableGeneralCode(0)};
  int parray_fp[] = {GetRegConfig()->GetAllocatableDoubleCode(0),
                     GetRegConfig()->GetAllocatableDoubleCode(1)};
  int rarray_fp[] = {GetRegConfig()->GetAllocatableDoubleCode(0)};
  Allocator palloc(parray_gp, 2, parray_fp, 2);
  Allocator ralloc(rarray_gp, 1, rarray_fp, 1);
  RegisterConfig config(palloc, ralloc);

  for (int which = 0; which < num_params; which++) {
    v8::internal::AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    HandleScope scope(isolate);
    MachineSignature::Builder builder(&zone, 1, num_params);
    builder.AddReturn(params[which]);
    for (int j = 0; j < num_params; j++) builder.AddParam(params[j]);
    MachineSignature* sig = builder.Build();
    CallDescriptor* desc = config.Create(&zone, sig);

    Handle<Code> select;
    {
      // build the select.
      Zone select_zone(&allocator, ZONE_NAME, kCompressGraphZone);
      Graph graph(&select_zone);
      RawMachineAssembler raw(isolate, &graph, desc);
      raw.Return(raw.Parameter(which));
      select = CompileGraph("Compute", desc, &graph, raw.ExportForTest());
    }

    {
      // call the select.
      Handle<Code> wrapper;
      int32_t expected_ret;
      char bytes[kDoubleSize];
      alignas(8) char output[kDoubleSize];
      int expected_size = 0;
      CSignatureOf<int32_t> csig;
      {
        // Wrap the select code with a callable function that passes constants.
        Zone wrap_zone(&allocator, ZONE_NAME, kCompressGraphZone);
        Graph graph(&wrap_zone);
        CallDescriptor* cdesc =
            Linkage::GetSimplifiedCDescriptor(&wrap_zone, &csig);
        RawMachineAssembler raw(isolate, &graph, cdesc);
        Node* target = raw.HeapConstant(select);
        Node** inputs = wrap_zone.AllocateArray<Node*>(num_params + 1);
        int input_count = 0;
        inputs[input_count++] = target;
        int64_t constant = 0x0102030405060708;
        for (int i = 0; i < num_params; i++) {
          MachineType param_type = sig->GetParam(i);
          Node* konst = nullptr;
          if (param_type == MachineType::Int32()) {
            int32_t value[] = {static_cast<int32_t>(constant)};
            konst = raw.Int32Constant(value[0]);
            if (i == which) memcpy(bytes, value, expected_size = 4);
          }
          if (param_type == MachineType::Int64()) {
            int64_t value[] = {static_cast<int64_t>(constant)};
            konst = raw.Int64Constant(value[0]);
            if (i == which) memcpy(bytes, value, expected_size = 8);
          }
          if (param_type == MachineType::Float32()) {
            float32 value[] = {static_cast<float32>(constant)};
            konst = raw.Float32Constant(value[0]);
            if (i == which) memcpy(bytes, value, expected_size = 4);
          }
          if (param_type == MachineType::Float64()) {
            float64 value[] = {static_cast<float64>(constant)};
            konst = raw.Float64Constant(value[0]);
            if (i == which) memcpy(bytes, value, expected_size = 8);
          }
          CHECK_NOT_NULL(konst);

          inputs[input_count++] = konst;
          const int64_t kIncrement = 0x1010101010101010;
          constant = base::AddWithWraparound(constant, kIncrement);
        }

        Node* call = raw.CallN(desc, input_count, inputs);
        Node* store =
            raw.StoreToPointer(output, sig->GetReturn().representation(), call);
        USE(store);
        expected_ret = static_cast<int32_t>(constant);
        raw.Return(raw.Int32Constant(expected_ret));
        wrapper = CompileGraph("Select-mixed-wrapper-const", cdesc, &graph,
                               raw.ExportForTest());
      }

      CodeRunner<int32_t> runnable(isolate, wrapper, &csig);
      CHECK_EQ(expected_ret, runnable.Call());
      for (int i = 0; i < expected_size; i++) {
        CHECK_EQ(static_cast<int>(bytes[i]), static_cast<int>(output[i]));
      }
    }
  }
}


TEST(MixedParams_0) { MixedParamTest(0); }
TEST(MixedParams_1) { MixedParamTest(1); }
TEST(MixedParams_2) { MixedParamTest(2); }
TEST(MixedParams_3) { MixedParamTest(3); }

template <typename T>
void TestStackSlot(MachineType slot_type, T expected) {
  // Test: Generate with a function f which reserves a stack slot, call an inner
  // function g from f which writes into the stack slot of f.

  if (GetRegConfig()->num_allocatable_double_registers() < 2) return;

  Isolate* isolate = CcTest::InitIsolateOnce();

  // Lots of code to generate the build descriptor for the inner function.
  int parray_gp[] = {GetRegConfig()->GetAllocatableGeneralCode(0),
                     GetRegConfig()->GetAllocatableGeneralCode(1)};
  int rarray_gp[] = {GetRegConfig()->GetAllocatableGeneralCode(0)};
  int parray_fp[] = {GetRegConfig()->GetAllocatableDoubleCode(0),
                     GetRegConfig()->GetAllocatableDoubleCode(1)};
  int rarray_fp[] = {GetRegConfig()->GetAllocatableDoubleCode(0)};
  Allocator palloc(parray_gp, 2, parray_fp, 2);
  Allocator ralloc(rarray_gp, 1, rarray_fp, 1);
  RegisterConfig config(palloc, ralloc);

  Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
  HandleScope scope(isolate);
  MachineSignature::Builder builder(&zone, 1, 12);
  builder.AddReturn(MachineType::Int32());
  for (int i = 0; i < 10; i++) {
    builder.AddParam(MachineType::Int32());
  }
  builder.AddParam(slot_type);
  builder.AddParam(MachineType::Pointer());
  MachineSignature* sig = builder.Build();
  CallDescriptor* desc = config.Create(&zone, sig);

  // Create inner function g. g has lots of parameters so that they are passed
  // over the stack.
  Handle<Code> inner;
  Graph graph(&zone);
  RawMachineAssembler g(isolate, &graph, desc);

  g.Store(slot_type.representation(), g.Parameter(11), g.Parameter(10),
          WriteBarrierKind::kNoWriteBarrier);
  g.Return(g.Parameter(9));
  inner = CompileGraph("Compute", desc, &graph, g.ExportForTest());

  // Create function f with a stack slot which calls the inner function g.
  BufferedRawMachineAssemblerTester<T> f(slot_type);
  Node* target = f.HeapConstant(inner);
  Node* stack_slot = f.StackSlot(slot_type.representation());
  Node* nodes[14];
  int input_count = 0;
  nodes[input_count++] = target;
  for (int i = 0; i < 10; i++) {
    nodes[input_count++] = f.Int32Constant(i);
  }
  nodes[input_count++] = f.Parameter(0);
  nodes[input_count++] = stack_slot;

  f.CallN(desc, input_count, nodes);
  f.Return(f.Load(slot_type, stack_slot, f.IntPtrConstant(0)));

  CHECK_EQ(expected, f.Call(expected));
}

TEST(RunStackSlotInt32) {
  int32_t magic = 0x12345678;
  TestStackSlot(MachineType::Int32(), magic);
}

#if !V8_TARGET_ARCH_32_BIT
TEST(RunStackSlotInt64) {
  int64_t magic = 0x123456789ABCDEF0;
  TestStackSlot(MachineType::Int64(), magic);
}
#endif

TEST(RunStackSlotFloat32) {
  float magic = 1234.125f;
  TestStackSlot(MachineType::Float32(), magic);
}

TEST(RunStackSlotFloat64) {
  double magic = 3456.375;
  TestStackSlot(MachineType::Float64(), magic);
}

}  // namespace test_run_native_calls
}  // namespace compiler
}  // namespace internal
}  // namespace v8
