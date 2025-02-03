// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/fuzzing/random-module-generation.h"

#include <algorithm>
#include <array>
#include <optional>

#include "src/base/small-vector.h"
#include "src/base/utils/random-number-generator.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes-inl.h"

// This whole compilation unit should only be included in non-official builds to
// reduce binary size (it's a testing-only implementation which lives in src/ so
// that the GenerateRandomWasmModule runtime function can use it).
#ifdef OFFICIAL_BUILD
#error Exclude this compilation unit in official builds.
#endif

namespace v8::internal::wasm::fuzzing {

namespace {

constexpr int kMaxArrays = 3;
constexpr int kMaxStructs = 4;
constexpr int kMaxStructFields = 4;
constexpr int kMaxFunctions = 4;
constexpr int kMaxGlobals = 64;
constexpr uint32_t kMaxLocals = 32;
constexpr int kMaxParameters = 15;
constexpr int kMaxReturns = 15;
constexpr int kMaxExceptions = 4;
constexpr int kMaxTableSize = 32;
constexpr int kMaxTables = 4;
constexpr int kMaxMemories = 4;
constexpr int kMaxMemorySize = 32;
constexpr int kMaxArraySize = 20;
constexpr int kMaxPassiveDataSegments = 2;
constexpr uint32_t kMaxRecursionDepth = 64;
constexpr int kMaxCatchCases = 6;

struct StringImports {
  uint32_t cast;
  uint32_t test;
  uint32_t fromCharCode;
  uint32_t fromCodePoint;
  uint32_t charCodeAt;
  uint32_t codePointAt;
  uint32_t length;
  uint32_t concat;
  uint32_t substring;
  uint32_t equals;
  uint32_t compare;
  uint32_t fromCharCodeArray;
  uint32_t intoCharCodeArray;
  uint32_t measureStringAsUTF8;
  uint32_t encodeStringIntoUTF8Array;
  uint32_t encodeStringToUTF8Array;
  uint32_t decodeStringFromUTF8Array;

  // These aren't imports, but closely related, so store them here as well:
  uint32_t array_i16;
  uint32_t array_i8;
};

// Creates an array out of the arguments without hardcoding the exact number of
// arguments.
template <typename... T>
constexpr auto CreateArray(T... elements) {
  std::array result = {elements...};
  return result;
}

// Concatenate arrays into one array in compile-time.
template <typename T, size_t... N>
constexpr auto ConcatArrays(std::array<T, N>... array) {
  constexpr size_t kNumArrays = sizeof...(array);
  std::array<T*, kNumArrays> kArrays = {&array[0]...};
  constexpr size_t kLengths[kNumArrays] = {array.size()...};
  constexpr size_t kSumOfLengths = (... + array.size());

  std::array<T, kSumOfLengths> result = {0};
  size_t result_index = 0;
  for (size_t arr = 0; arr < kNumArrays; arr++) {
    for (size_t pos = 0; pos < kLengths[arr]; pos++) {
      result[result_index++] = kArrays[arr][pos];
    }
  }
  return result;
}

template <bool predicate, typename T, size_t kSize1, size_t kSize2>
constexpr auto AppendArrayIf(std::array<T, kSize1> array1,
                             std::array<T, kSize2> array2) {
  if constexpr (!predicate) {
    return array1;
  } else {
    return ConcatArrays(array1, array2);
  }
}

class DataRange {
  // data_ is used for general random values for fuzzing.
  base::Vector<const uint8_t> data_;
  // The RNG is used for generating random values (i32.consts etc.) for which
  // the quality of the input is less important.
  base::RandomNumberGenerator rng_;

 public:
  explicit DataRange(base::Vector<const uint8_t> data, int64_t seed = -1)
      : data_(data), rng_(seed == -1 ? get<int64_t>() : seed) {}
  DataRange(const DataRange&) = delete;
  DataRange& operator=(const DataRange&) = delete;

  // Don't accidentally pass DataRange by value. This will reuse bytes and might
  // lead to OOM because the end might not be reached.
  // Define move constructor and move assignment, disallow copy constructor and
  // copy assignment (below).
  DataRange(DataRange&& other) V8_NOEXCEPT : data_(other.data_),
                                             rng_(other.rng_) {
    other.data_ = {};
  }
  DataRange& operator=(DataRange&& other) V8_NOEXCEPT {
    data_ = other.data_;
    rng_ = other.rng_;
    other.data_ = {};
    return *this;
  }

  size_t size() const { return data_.size(); }

  DataRange split() {
    // As we might split many times, only use 2 bytes if the data size is large.
    uint16_t random_choice = data_.size() > std::numeric_limits<uint8_t>::max()
                                 ? get<uint16_t>()
                                 : get<uint8_t>();
    uint16_t num_bytes = random_choice % std::max(size_t{1}, data_.size());
    int64_t new_seed = rng_.initial_seed() ^ rng_.NextInt64();
    DataRange split(data_.SubVector(0, num_bytes), new_seed);
    data_ += num_bytes;
    return split;
  }

  template <typename T, size_t max_bytes = sizeof(T)>
  T getPseudoRandom() {
    static_assert(!std::is_same<T, bool>::value, "bool needs special handling");
    static_assert(max_bytes <= sizeof(T));
    // Special handling for signed integers: Calling getPseudoRandom<int32_t, 1>
    // () should be equal to getPseudoRandom<int8_t>(). (The NextBytes() below
    // does not achieve that due to depending on endianness and either never
    // generating negative values or filling in the highest significant bits
    // which would be unexpected).
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
      switch (max_bytes) {
        case 1:
          return static_cast<int8_t>(getPseudoRandom<uint8_t>());
        case 2:
          return static_cast<int16_t>(getPseudoRandom<uint16_t>());
        case 4:
          return static_cast<int32_t>(getPseudoRandom<uint32_t>());
        default:
          return static_cast<T>(
              getPseudoRandom<std::make_unsigned_t<T>, max_bytes>());
      }
    }

    T result{};
    rng_.NextBytes(&result, max_bytes);
    return result;
  }

  template <typename T>
  T get() {
    // Bool needs special handling (see template specialization below).
    static_assert(!std::is_same<T, bool>::value, "bool needs special handling");

    // We want to support the case where we have less than sizeof(T) bytes
    // remaining in the slice. We'll just use what we have, so we get a bit of
    // randomness when there are still some bytes left. If size == 0, get<T>()
    // returns the type's value-initialized value.
    const size_t num_bytes = std::min(sizeof(T), data_.size());
    T result{};
    memcpy(&result, data_.begin(), num_bytes);
    data_ += num_bytes;
    return result;
  }
};

// Explicit specialization must be defined outside of class body.
template <>
bool DataRange::get() {
  // The general implementation above is not instantiable for bool, as that
  // would cause undefinied behaviour when memcpy'ing random bytes to the
  // bool. This can result in different observable side effects when invoking
  // get<bool> between debug and release version, which eventually makes the
  // code output different as well as raising various unrecoverable errors on
  // runtime.
  // Hence we specialize get<bool> to consume a full byte and use the least
  // significant bit only (0 == false, 1 == true).
  return get<uint8_t>() % 2;
}

enum IncludeNumericTypes {
  kIncludeNumericTypes = true,
  kExcludeNumericTypes = false
};
enum IncludePackedTypes {
  kIncludePackedTypes = true,
  kExcludePackedTypes = false
};
enum IncludeAllGenerics {
  kIncludeAllGenerics = true,
  kExcludeSomeGenerics = false
};
enum IncludeS128 { kIncludeS128 = true, kExcludeS128 = false };

// Chooses one `ValueType` randomly based on `options` and the enums specified
// above.
template <WasmModuleGenerationOptions options>
ValueType GetValueTypeHelper(DataRange* data, uint32_t num_nullable_types,
                             uint32_t num_non_nullable_types,
                             IncludeNumericTypes include_numeric_types,
                             IncludePackedTypes include_packed_types,
                             IncludeAllGenerics include_all_generics,
                             IncludeS128 include_s128 = kIncludeS128) {
  // Create and fill a vector of potential types to choose from.
  base::SmallVector<ValueType, 32> types;

  // Numeric non-wasmGC types.
  if (include_numeric_types) {
    // Many "general-purpose" instructions return i32, so give that a higher
    // probability (such as 3x).
    types.insert(types.end(),
                 {kWasmI32, kWasmI32, kWasmI32, kWasmI64, kWasmF32, kWasmF64});

    // SIMD type.
    if (ShouldGenerateSIMD(options) && include_s128) {
      types.push_back(kWasmS128);
    }
  }

  // The MVP types: apart from numeric types, contains only the non-nullable
  // funcRef. We don't add externRef, because for externRef globals we generate
  // initialiser expressions where we need wasmGC types. Also, externRef is not
  // really useful for the MVP fuzzer, as there is nothing that we could
  // generate.
  types.push_back(kWasmFuncRef);

  // WasmGC types (including user-defined types).
  // Decide if the return type will be nullable or not.
  const bool nullable =
      ShouldGenerateWasmGC(options) ? data->get<bool>() : false;

  if (ShouldGenerateWasmGC(options)) {
    types.push_back(kWasmI31Ref);

    if (include_numeric_types && include_packed_types) {
      types.insert(types.end(), {kWasmI8, kWasmI16});
    }

    if (nullable) {
      types.insert(types.end(),
                   {kWasmNullRef, kWasmNullExternRef, kWasmNullFuncRef});
    }
    if (nullable || include_all_generics) {
      types.insert(types.end(), {kWasmStructRef, kWasmArrayRef, kWasmAnyRef,
                                 kWasmEqRef, kWasmExternRef});
    }
  }

  // The last index of user-defined types allowed is different based on the
  // nullability of the output. User-defined types are function signatures or
  // structs and arrays (in case of wasmGC).
  const uint32_t num_user_defined_types =
      nullable ? num_nullable_types : num_non_nullable_types;

  // Conceptually, user-defined types are added to the end of the list. Pick a
  // random one among them.
  uint32_t chosen_id =
      data->get<uint8_t>() % (types.size() + num_user_defined_types);

  Nullability nullability = nullable ? kNullable : kNonNullable;

  if (chosen_id >= types.size()) {
    // Return user-defined type.
    return ValueType::RefMaybeNull(
        chosen_id - static_cast<uint32_t>(types.size()), nullability);
  }
  // If returning a reference type, fix its nullability according to {nullable}.
  if (types[chosen_id].is_reference()) {
    return ValueType::RefMaybeNull(types[chosen_id].heap_type(), nullability);
  }
  // Otherwise, just return the picked type.
  return types[chosen_id];
}

template <WasmModuleGenerationOptions options>
ValueType GetValueType(DataRange* data, uint32_t num_types) {
  return GetValueTypeHelper<options>(data, num_types, num_types,
                                     kIncludeNumericTypes, kExcludePackedTypes,
                                     kIncludeAllGenerics);
}

void GeneratePassiveDataSegment(DataRange* range, WasmModuleBuilder* builder) {
  int length = range->get<uint8_t>() % 65;
  ZoneVector<uint8_t> data(length, builder->zone());
  for (int i = 0; i < length; ++i) {
    data[i] = range->getPseudoRandom<uint8_t>();
  }
  builder->AddPassiveDataSegment(data.data(),
                                 static_cast<uint32_t>(data.size()));
}

uint32_t GenerateRefTypeElementSegment(DataRange* range,
                                       WasmModuleBuilder* builder,
                                       ValueType element_type) {
  DCHECK(element_type.is_object_reference());
  DCHECK(element_type.has_index());
  WasmModuleBuilder::WasmElemSegment segment(
      builder->zone(), element_type, false,
      WasmInitExpr::RefNullConst(element_type.heap_representation()));
  size_t element_count = range->get<uint8_t>() % 11;
  for (size_t i = 0; i < element_count; ++i) {
    segment.entries.emplace_back(
        WasmModuleBuilder::WasmElemSegment::Entry::kRefNullEntry,
        element_type.ref_index());
  }
  return builder->AddElementSegment(std::move(segment));
}

template <WasmModuleGenerationOptions options>
std::vector<ValueType> GenerateTypes(DataRange* data, uint32_t num_ref_types) {
  std::vector<ValueType> types;
  int num_params = int{data->get<uint8_t>()} % (kMaxParameters + 1);
  types.reserve(num_params);
  for (int i = 0; i < num_params; ++i) {
    types.push_back(GetValueType<options>(data, num_ref_types));
  }
  return types;
}

FunctionSig* CreateSignature(Zone* zone,
                             base::Vector<const ValueType> param_types,
                             base::Vector<const ValueType> return_types) {
  FunctionSig::Builder builder(zone, return_types.size(), param_types.size());
  for (auto& type : param_types) {
    builder.AddParam(type);
  }
  for (auto& type : return_types) {
    builder.AddReturn(type);
  }
  return builder.Build();
}

template <WasmModuleGenerationOptions options>
class BodyGen {
  template <WasmOpcode Op, ValueKind... Args>
  void op(DataRange* data) {
    Generate<Args...>(data);
    builder_->Emit(Op);
  }

  class V8_NODISCARD BlockScope {
   public:
    BlockScope(BodyGen* gen, WasmOpcode block_type,
               base::Vector<const ValueType> param_types,
               base::Vector<const ValueType> result_types,
               base::Vector<const ValueType> br_types, bool emit_end = true)
        : gen_(gen), emit_end_(emit_end) {
      gen->blocks_.emplace_back(br_types.begin(), br_types.end());
      gen->builder_->EmitByte(block_type);

      if (param_types.size() == 0 && result_types.size() == 0) {
        gen->builder_->EmitValueType(kWasmVoid);
        return;
      }
      if (param_types.size() == 0 && result_types.size() == 1) {
        gen->builder_->EmitValueType(result_types[0]);
        return;
      }
      // Multi-value block.
      Zone* zone = gen->builder_->builder()->zone();
      FunctionSig::Builder builder(zone, result_types.size(),
                                   param_types.size());
      for (auto& type : param_types) {
        DCHECK_NE(type, kWasmVoid);
        builder.AddParam(type);
      }
      for (auto& type : result_types) {
        DCHECK_NE(type, kWasmVoid);
        builder.AddReturn(type);
      }
      FunctionSig* sig = builder.Build();
      const bool is_final = true;
      int sig_id = gen->builder_->builder()->AddSignature(sig, is_final);
      gen->builder_->EmitI32V(sig_id);
    }

    ~BlockScope() {
      if (emit_end_) gen_->builder_->Emit(kExprEnd);
      gen_->blocks_.pop_back();
    }

   private:
    BodyGen* const gen_;
    bool emit_end_;
  };

  void block(base::Vector<const ValueType> param_types,
             base::Vector<const ValueType> return_types, DataRange* data) {
    BlockScope block_scope(this, kExprBlock, param_types, return_types,
                           return_types);
    ConsumeAndGenerate(param_types, return_types, data);
  }

  template <ValueKind T>
  void block(DataRange* data) {
    if constexpr (T == kVoid) {
      block({}, {}, data);
    } else {
      block({}, base::VectorOf({ValueType::Primitive(T)}), data);
    }
  }

  void loop(base::Vector<const ValueType> param_types,
            base::Vector<const ValueType> return_types, DataRange* data) {
    BlockScope block_scope(this, kExprLoop, param_types, return_types,
                           param_types);
    ConsumeAndGenerate(param_types, return_types, data);
  }

  template <ValueKind T>
  void loop(DataRange* data) {
    if constexpr (T == kVoid) {
      loop({}, {}, data);
    } else {
      loop({}, base::VectorOf({ValueType::Primitive(T)}), data);
    }
  }

  void finite_loop(base::Vector<const ValueType> param_types,
                   base::Vector<const ValueType> return_types,
                   DataRange* data) {
    // int counter = `kLoopConstant`;
    int kLoopConstant = data->get<uint8_t>() % 8 + 1;
    uint32_t counter = builder_->AddLocal(kWasmI32);
    builder_->EmitI32Const(kLoopConstant);
    builder_->EmitSetLocal(counter);

    // begin loop {
    BlockScope loop_scope(this, kExprLoop, param_types, return_types,
                          param_types);
    //   Consume the parameters:
    //   Resetting locals in each iteration can create interesting loop-phis.
    // TODO(evih): Iterate through existing locals and try to reuse them instead
    // of creating new locals.
    for (auto it = param_types.rbegin(); it != param_types.rend(); it++) {
      uint32_t local = builder_->AddLocal(*it);
      builder_->EmitSetLocal(local);
    }

    //   Loop body.
    Generate(kWasmVoid, data);

    //   Decrement the counter.
    builder_->EmitGetLocal(counter);
    builder_->EmitI32Const(1);
    builder_->Emit(kExprI32Sub);
    builder_->EmitTeeLocal(counter);

    //   If there is another iteration, generate new parameters for the loop and
    //   go to the beginning of the loop.
    {
      BlockScope if_scope(this, kExprIf, {}, {}, {});
      Generate(param_types, data);
      builder_->EmitWithI32V(kExprBr, 1);
    }

    //   Otherwise, generate the return types.
    Generate(return_types, data);
    // } end loop
  }

  template <ValueKind T>
  void finite_loop(DataRange* data) {
    if constexpr (T == kVoid) {
      finite_loop({}, {}, data);
    } else {
      finite_loop({}, base::VectorOf({ValueType::Primitive(T)}), data);
    }
  }

  enum IfType { kIf, kIfElse };

  void if_(base::Vector<const ValueType> param_types,
           base::Vector<const ValueType> return_types, IfType type,
           DataRange* data) {
    // One-armed "if" are only valid if the input and output types are the same.
    DCHECK_IMPLIES(type == kIf, param_types == return_types);
    Generate(kWasmI32, data);
    BlockScope block_scope(this, kExprIf, param_types, return_types,
                           return_types);
    ConsumeAndGenerate(param_types, return_types, data);
    if (type == kIfElse) {
      builder_->Emit(kExprElse);
      ConsumeAndGenerate(param_types, return_types, data);
    }
  }

  template <ValueKind T, IfType type>
  void if_(DataRange* data) {
    static_assert(T == kVoid || type == kIfElse,
                  "if without else cannot produce a value");
    if_({},
        T == kVoid ? base::Vector<ValueType>{}
                   : base::VectorOf({ValueType::Primitive(T)}),
        type, data);
  }

  void try_block_helper(ValueType return_type, DataRange* data) {
    bool has_catch_all = data->get<bool>();
    uint8_t num_catch =
        data->get<uint8_t>() % (builder_->builder()->NumTags() + 1);
    bool is_delegate = num_catch == 0 && !has_catch_all && data->get<bool>();
    // Allow one more target than there are enclosing try blocks, for delegating
    // to the caller.

    base::Vector<const ValueType> return_type_vec =
        return_type.kind() == kVoid ? base::Vector<ValueType>{}
                                    : base::VectorOf(&return_type, 1);
    BlockScope block_scope(this, kExprTry, {}, return_type_vec, return_type_vec,
                           !is_delegate);
    int control_depth = static_cast<int>(blocks_.size()) - 1;
    Generate(return_type, data);
    catch_blocks_.push_back(control_depth);
    for (int i = 0; i < num_catch; ++i) {
      const FunctionSig* exception_type = builder_->builder()->GetTagType(i);
      builder_->EmitWithU32V(kExprCatch, i);
      ConsumeAndGenerate(exception_type->parameters(), return_type_vec, data);
    }
    if (has_catch_all) {
      builder_->Emit(kExprCatchAll);
      Generate(return_type, data);
    }
    if (is_delegate) {
      // The delegate target depth does not include the current try block,
      // because 'delegate' closes this scope. However it is still in the
      // {blocks_} list, so remove one to get the correct size.
      int delegate_depth = data->get<uint8_t>() % (blocks_.size() - 1);
      builder_->EmitWithU32V(kExprDelegate, delegate_depth);
    }
    catch_blocks_.pop_back();
  }

  template <ValueKind T>
  void try_block(DataRange* data) {
    try_block_helper(ValueType::Primitive(T), data);
  }

  struct CatchCase {
    int tag_index;
    CatchKind kind;
  };

  // Generates the i-th nested block for the try-table, and recursively generate
  // the blocks inside it.
  void try_table_rec(base::Vector<const ValueType> param_types,
                     base::Vector<const ValueType> return_types,
                     base::Vector<CatchCase> catch_cases, size_t i,
                     DataRange* data) {
    if (i == catch_cases.size()) {
      // Base case: emit the try-table itself.
      builder_->Emit(kExprTryTable);
      blocks_.emplace_back(return_types.begin(), return_types.end());
      const bool is_final = true;
      uint32_t try_sig_index = builder_->builder()->AddSignature(
          CreateSignature(builder_->builder()->zone(), param_types,
                          return_types),
          is_final);
      builder_->EmitI32V(try_sig_index);
      builder_->EmitU32V(static_cast<uint32_t>(catch_cases.size()));
      for (size_t j = 0; j < catch_cases.size(); ++j) {
        builder_->EmitByte(catch_cases[j].kind);
        if (catch_cases[j].kind == kCatch || catch_cases[j].kind == kCatchRef) {
          builder_->EmitByte(catch_cases[j].tag_index);
        }
        builder_->EmitByte(catch_cases.size() - j - 1);
      }
      ConsumeAndGenerate(param_types, return_types, data);
      builder_->Emit(kExprEnd);
      blocks_.pop_back();
      builder_->EmitWithI32V(kExprBr, static_cast<int32_t>(catch_cases.size()));
      return;
    }

    // Enter the i-th nested block. The signature of the block is built as
    // follows:
    // - The input types are the same for each block, the operands are forwarded
    // as-is to the inner try-table.
    // - The output types can be empty, or contain the tag types and/or an
    // exnref depending on the catch kind
    const FunctionSig* type =
        builder_->builder()->GetTagType(catch_cases[i].tag_index);
    int has_tag =
        catch_cases[i].kind == kCatchRef || catch_cases[i].kind == kCatch;
    int has_ref =
        catch_cases[i].kind == kCatchAllRef || catch_cases[i].kind == kCatchRef;
    size_t return_count =
        (has_tag ? type->parameter_count() : 0) + (has_ref ? 1 : 0);
    auto block_returns =
        builder_->builder()->zone()->AllocateVector<ValueType>(return_count);
    if (has_tag) {
      std::copy_n(type->parameters().begin(), type->parameter_count(),
                  block_returns.begin());
    }
    if (has_ref) block_returns.last() = kWasmExnRef;
    {
      BlockScope block(this, kExprBlock, param_types, block_returns,
                       block_returns);
      try_table_rec(param_types, return_types, catch_cases, i + 1, data);
    }
    // Catch label. Consume the unpacked values and exnref (if any), produce
    // values that match the outer scope, and branch to it.
    ConsumeAndGenerate(block_returns, return_types, data);
    builder_->EmitWithU32V(kExprBr, static_cast<uint32_t>(i));
  }

  void try_table_block_helper(base::Vector<const ValueType> param_types,
                              base::Vector<const ValueType> return_types,
                              DataRange* data) {
    uint8_t num_catch = data->get<uint8_t>() % kMaxCatchCases;
    auto catch_cases =
        builder_->builder()->zone()->AllocateVector<CatchCase>(num_catch);
    for (int i = 0; i < num_catch; ++i) {
      catch_cases[i].tag_index =
          data->get<uint8_t>() % builder_->builder()->NumTags();
      catch_cases[i].kind =
          static_cast<CatchKind>(data->get<uint8_t>() % (kLastCatchKind + 1));
    }

    BlockScope block_scope(this, kExprBlock, param_types, return_types,
                           return_types);
    try_table_rec(param_types, return_types, catch_cases, 0, data);
  }

  template <ValueKind T>
  void try_table_block(DataRange* data) {
    try_table_block_helper({}, base::VectorOf({ValueType::Primitive(T)}), data);
  }

  void any_block(base::Vector<const ValueType> param_types,
                 base::Vector<const ValueType> return_types, DataRange* data) {
    uint8_t block_type = data->get<uint8_t>() % 6;
    switch (block_type) {
      case 0:
        block(param_types, return_types, data);
        return;
      case 1:
        loop(param_types, return_types, data);
        return;
      case 2:
        finite_loop(param_types, return_types, data);
        return;
      case 3:
        if (param_types == return_types) {
          if_({}, {}, kIf, data);
          return;
        }
        [[fallthrough]];
      case 4:
        if_(param_types, return_types, kIfElse, data);
        return;
      case 5:
        try_table_block_helper(param_types, return_types, data);
        return;
    }
  }

  void br(DataRange* data) {
    // There is always at least the block representing the function body.
    DCHECK(!blocks_.empty());
    const uint32_t target_block = data->get<uint8_t>() % blocks_.size();
    const auto break_types = base::VectorOf(blocks_[target_block]);

    Generate(break_types, data);
    builder_->EmitWithI32V(
        kExprBr, static_cast<uint32_t>(blocks_.size()) - 1 - target_block);
  }

  template <ValueKind wanted_kind>
  void br_if(DataRange* data) {
    // There is always at least the block representing the function body.
    DCHECK(!blocks_.empty());
    const uint32_t target_block = data->get<uint8_t>() % blocks_.size();
    const auto break_types = base::VectorOf(blocks_[target_block]);

    Generate(break_types, data);
    Generate(kWasmI32, data);
    builder_->EmitWithI32V(
        kExprBrIf, static_cast<uint32_t>(blocks_.size()) - 1 - target_block);
    ConsumeAndGenerate(
        break_types,
        wanted_kind == kVoid
            ? base::Vector<ValueType>{}
            : base::VectorOf({ValueType::Primitive(wanted_kind)}),
        data);
  }

  template <ValueKind wanted_kind>
  void br_on_null(DataRange* data) {
    DCHECK(!blocks_.empty());
    const uint32_t target_block = data->get<uint8_t>() % blocks_.size();
    const auto break_types = base::VectorOf(blocks_[target_block]);
    Generate(break_types, data);
    GenerateRef(data);
    builder_->EmitWithI32V(
        kExprBrOnNull,
        static_cast<uint32_t>(blocks_.size()) - 1 - target_block);
    builder_->Emit(kExprDrop);
    ConsumeAndGenerate(
        break_types,
        wanted_kind == kVoid
            ? base::Vector<ValueType>{}
            : base::VectorOf({ValueType::Primitive(wanted_kind)}),
        data);
  }

  template <ValueKind wanted_kind>
  void br_on_non_null(DataRange* data) {
    DCHECK(!blocks_.empty());
    const uint32_t target_block = data->get<uint8_t>() % blocks_.size();
    const auto break_types = base::VectorOf(blocks_[target_block]);
    if (break_types.empty() ||
        !break_types[break_types.size() - 1].is_reference()) {
      // Invalid break_types for br_on_non_null.
      Generate<wanted_kind>(data);
      return;
    }
    Generate(break_types, data);
    builder_->EmitWithI32V(
        kExprBrOnNonNull,
        static_cast<uint32_t>(blocks_.size()) - 1 - target_block);
    ConsumeAndGenerate(
        break_types.SubVector(0, break_types.size() - 1),
        wanted_kind == kVoid
            ? base::Vector<ValueType>{}
            : base::VectorOf({ValueType::Primitive(wanted_kind)}),
        data);
  }

  void br_table(ValueType result_type, DataRange* data) {
    const uint8_t block_count = 1 + data->get<uint8_t>() % 8;
    // Generate the block entries.
    uint16_t entry_bits =
        block_count > 4 ? data->get<uint16_t>() : data->get<uint8_t>();
    for (size_t i = 0; i < block_count; ++i) {
      builder_->Emit(kExprBlock);
      builder_->EmitValueType(result_type);
      blocks_.emplace_back();
      if (result_type != kWasmVoid) {
        blocks_.back().push_back(result_type);
      }
      // There can be additional instructions in each block.
      // Only generate it with a 25% chance as it's otherwise quite unlikely to
      // have enough random bytes left for the br_table instruction.
      if ((entry_bits & 3) == 3) {
        Generate(kWasmVoid, data);
      }
      entry_bits >>= 2;
    }
    // Generate the br_table.
    Generate(result_type, data);
    Generate(kWasmI32, data);
    builder_->Emit(kExprBrTable);
    uint32_t entry_count = 1 + data->get<uint8_t>() % 8;
    builder_->EmitU32V(entry_count);
    for (size_t i = 0; i < entry_count + 1; ++i) {
      builder_->EmitU32V(data->get<uint8_t>() % block_count);
    }
    // Generate the block ends.
    uint8_t exit_bits = result_type == kWasmVoid ? 0 : data->get<uint8_t>();
    for (size_t i = 0; i < block_count; ++i) {
      if (exit_bits & 1) {
        // Drop and generate new value.
        builder_->Emit(kExprDrop);
        Generate(result_type, data);
      }
      exit_bits >>= 1;
      builder_->Emit(kExprEnd);
      blocks_.pop_back();
    }
  }

  template <ValueKind wanted_kind>
  void br_table(DataRange* data) {
    br_table(
        wanted_kind == kVoid ? kWasmVoid : ValueType::Primitive(wanted_kind),
        data);
  }

  void return_op(DataRange* data) {
    auto returns = builder_->signature()->returns();
    Generate(returns, data);
    builder_->Emit(kExprReturn);
  }

  constexpr static uint8_t max_alignment(WasmOpcode memop) {
    switch (memop) {
      case kExprS128LoadMem:
      case kExprS128StoreMem:
        return 4;
      case kExprI64LoadMem:
      case kExprF64LoadMem:
      case kExprI64StoreMem:
      case kExprF64StoreMem:
      case kExprI64AtomicStore:
      case kExprI64AtomicLoad:
      case kExprI64AtomicAdd:
      case kExprI64AtomicSub:
      case kExprI64AtomicAnd:
      case kExprI64AtomicOr:
      case kExprI64AtomicXor:
      case kExprI64AtomicExchange:
      case kExprI64AtomicCompareExchange:
      case kExprS128Load8x8S:
      case kExprS128Load8x8U:
      case kExprS128Load16x4S:
      case kExprS128Load16x4U:
      case kExprS128Load32x2S:
      case kExprS128Load32x2U:
      case kExprS128Load64Splat:
      case kExprS128Load64Zero:
        return 3;
      case kExprI32LoadMem:
      case kExprI64LoadMem32S:
      case kExprI64LoadMem32U:
      case kExprF32LoadMem:
      case kExprI32StoreMem:
      case kExprI64StoreMem32:
      case kExprF32StoreMem:
      case kExprI32AtomicStore:
      case kExprI64AtomicStore32U:
      case kExprI32AtomicLoad:
      case kExprI64AtomicLoad32U:
      case kExprI32AtomicAdd:
      case kExprI32AtomicSub:
      case kExprI32AtomicAnd:
      case kExprI32AtomicOr:
      case kExprI32AtomicXor:
      case kExprI32AtomicExchange:
      case kExprI32AtomicCompareExchange:
      case kExprI64AtomicAdd32U:
      case kExprI64AtomicSub32U:
      case kExprI64AtomicAnd32U:
      case kExprI64AtomicOr32U:
      case kExprI64AtomicXor32U:
      case kExprI64AtomicExchange32U:
      case kExprI64AtomicCompareExchange32U:
      case kExprS128Load32Splat:
      case kExprS128Load32Zero:
        return 2;
      case kExprI32LoadMem16S:
      case kExprI32LoadMem16U:
      case kExprI64LoadMem16S:
      case kExprI64LoadMem16U:
      case kExprI32StoreMem16:
      case kExprI64StoreMem16:
      case kExprI32AtomicStore16U:
      case kExprI64AtomicStore16U:
      case kExprI32AtomicLoad16U:
      case kExprI64AtomicLoad16U:
      case kExprI32AtomicAdd16U:
      case kExprI32AtomicSub16U:
      case kExprI32AtomicAnd16U:
      case kExprI32AtomicOr16U:
      case kExprI32AtomicXor16U:
      case kExprI32AtomicExchange16U:
      case kExprI32AtomicCompareExchange16U:
      case kExprI64AtomicAdd16U:
      case kExprI64AtomicSub16U:
      case kExprI64AtomicAnd16U:
      case kExprI64AtomicOr16U:
      case kExprI64AtomicXor16U:
      case kExprI64AtomicExchange16U:
      case kExprI64AtomicCompareExchange16U:
      case kExprS128Load16Splat:
        return 1;
      case kExprI32LoadMem8S:
      case kExprI32LoadMem8U:
      case kExprI64LoadMem8S:
      case kExprI64LoadMem8U:
      case kExprI32StoreMem8:
      case kExprI64StoreMem8:
      case kExprI32AtomicStore8U:
      case kExprI64AtomicStore8U:
      case kExprI32AtomicLoad8U:
      case kExprI64AtomicLoad8U:
      case kExprI32AtomicAdd8U:
      case kExprI32AtomicSub8U:
      case kExprI32AtomicAnd8U:
      case kExprI32AtomicOr8U:
      case kExprI32AtomicXor8U:
      case kExprI32AtomicExchange8U:
      case kExprI32AtomicCompareExchange8U:
      case kExprI64AtomicAdd8U:
      case kExprI64AtomicSub8U:
      case kExprI64AtomicAnd8U:
      case kExprI64AtomicOr8U:
      case kExprI64AtomicXor8U:
      case kExprI64AtomicExchange8U:
      case kExprI64AtomicCompareExchange8U:
      case kExprS128Load8Splat:
        return 0;
      default:
        return 0;
    }
  }

  template <WasmOpcode memory_op, ValueKind... arg_kinds>
  void memop(DataRange* data) {
    // Atomic operations need to be aligned exactly to their max alignment.
    const bool is_atomic = memory_op >> 8 == kAtomicPrefix;
    const uint8_t align = is_atomic ? max_alignment(memory_op)
                                    : data->getPseudoRandom<uint8_t>() %
                                          (max_alignment(memory_op) + 1);

    uint8_t memory_index =
        data->get<uint8_t>() % builder_->builder()->NumMemories();

    uint64_t offset = data->get<uint16_t>();
    // With a 1/256 chance generate potentially very large offsets.
    if ((offset & 0xff) == 0xff) {
      offset = builder_->builder()->IsMemory64(memory_index)
                   ? data->getPseudoRandom<uint64_t>() & 0x1ffffffff
                   : data->getPseudoRandom<uint32_t>();
    }

    // Generate the index and the arguments, if any.
    builder_->builder()->IsMemory64(memory_index)
        ? Generate<kI64, arg_kinds...>(data)
        : Generate<kI32, arg_kinds...>(data);

    // Format of the instruction (supports multi-memory):
    // memory_op (align | 0x40) memory_index offset
    if (WasmOpcodes::IsPrefixOpcode(static_cast<WasmOpcode>(memory_op >> 8))) {
      DCHECK(memory_op >> 8 == kAtomicPrefix || memory_op >> 8 == kSimdPrefix);
      builder_->EmitWithPrefix(memory_op);
    } else {
      builder_->Emit(memory_op);
    }
    builder_->EmitU32V(align | 0x40);
    builder_->EmitU32V(memory_index);
    builder_->EmitU64V(offset);
  }

  template <WasmOpcode Op, ValueKind... Args>
  void op_with_prefix(DataRange* data) {
    Generate<Args...>(data);
    builder_->EmitWithPrefix(Op);
  }

  void simd_const(DataRange* data) {
    builder_->EmitWithPrefix(kExprS128Const);
    for (int i = 0; i < kSimd128Size; i++) {
      builder_->EmitByte(data->getPseudoRandom<uint8_t>());
    }
  }

  template <WasmOpcode Op, int lanes, ValueKind... Args>
  void simd_lane_op(DataRange* data) {
    Generate<Args...>(data);
    builder_->EmitWithPrefix(Op);
    builder_->EmitByte(data->get<uint8_t>() % lanes);
  }

  template <WasmOpcode Op, int lanes, ValueKind... Args>
  void simd_lane_memop(DataRange* data) {
    // Simd load/store instructions that have a lane immediate.
    memop<Op, Args...>(data);
    builder_->EmitByte(data->get<uint8_t>() % lanes);
  }

  void simd_shuffle(DataRange* data) {
    Generate<kS128, kS128>(data);
    builder_->EmitWithPrefix(kExprI8x16Shuffle);
    for (int i = 0; i < kSimd128Size; i++) {
      builder_->EmitByte(static_cast<uint8_t>(data->get<uint8_t>() % 32));
    }
  }

  void drop(DataRange* data) {
    Generate(GetValueType<options>(
                 data, static_cast<uint32_t>(functions_.size() +
                                             structs_.size() + arrays_.size())),
             data);
    builder_->Emit(kExprDrop);
  }

  enum CallKind { kCallDirect, kCallIndirect, kCallRef };

  template <ValueKind wanted_kind>
  void call(DataRange* data) {
    call(data, ValueType::Primitive(wanted_kind), kCallDirect);
  }

  template <ValueKind wanted_kind>
  void call_indirect(DataRange* data) {
    call(data, ValueType::Primitive(wanted_kind), kCallIndirect);
  }

  template <ValueKind wanted_kind>
  void call_ref(DataRange* data) {
    call(data, ValueType::Primitive(wanted_kind), kCallRef);
  }

  void Convert(ValueType src, ValueType dst) {
    auto idx = [](ValueType t) -> int {
      switch (t.kind()) {
        case kI32:
          return 0;
        case kI64:
          return 1;
        case kF32:
          return 2;
        case kF64:
          return 3;
        default:
          UNREACHABLE();
      }
    };
    static constexpr WasmOpcode kConvertOpcodes[] = {
        // {i32, i64, f32, f64} -> i32
        kExprNop, kExprI32ConvertI64, kExprI32SConvertF32, kExprI32SConvertF64,
        // {i32, i64, f32, f64} -> i64
        kExprI64SConvertI32, kExprNop, kExprI64SConvertF32, kExprI64SConvertF64,
        // {i32, i64, f32, f64} -> f32
        kExprF32SConvertI32, kExprF32SConvertI64, kExprNop, kExprF32ConvertF64,
        // {i32, i64, f32, f64} -> f64
        kExprF64SConvertI32, kExprF64SConvertI64, kExprF64ConvertF32, kExprNop};
    int arr_idx = idx(dst) << 2 | idx(src);
    builder_->Emit(kConvertOpcodes[arr_idx]);
  }

  int choose_function_table_index(DataRange* data) {
    int table_count = builder_->builder()->NumTables();
    int start = data->get<uint8_t>() % table_count;
    for (int i = 0; i < table_count; ++i) {
      int index = (start + i) % table_count;
      if (builder_->builder()->GetTableType(index).is_reference_to(
              HeapType::kFunc)) {
        return index;
      }
    }
    FATAL("No funcref table found; table index 0 is expected to be funcref");
  }

  void call(DataRange* data, ValueType wanted_kind, CallKind call_kind) {
    uint8_t random_byte = data->get<uint8_t>();
    int func_index = random_byte % functions_.size();
    uint32_t sig_index = functions_[func_index];
    const FunctionSig* sig = builder_->builder()->GetSignature(sig_index);
    // Generate arguments.
    for (size_t i = 0; i < sig->parameter_count(); ++i) {
      Generate(sig->GetParam(i), data);
    }
    // Emit call.
    // If the return types of the callee happen to match the return types of the
    // caller, generate a tail call.
    bool use_return_call = random_byte > 127;
    if (use_return_call &&
        std::equal(sig->returns().begin(), sig->returns().end(),
                   builder_->signature()->returns().begin(),
                   builder_->signature()->returns().end())) {
      if (call_kind == kCallDirect) {
        builder_->EmitWithU32V(kExprReturnCall,
                               NumImportedFunctions() + func_index);
      } else if (call_kind == kCallIndirect) {
        // This will not trap because table[func_index] always contains function
        // func_index.
        builder_->EmitI32Const(func_index);
        builder_->EmitWithU32V(kExprReturnCallIndirect, sig_index);
        builder_->EmitByte(choose_function_table_index(data));  // Table index.
      } else {
        GenerateRef(HeapType(sig_index), data);
        builder_->EmitWithU32V(kExprReturnCallRef, sig_index);
      }
      return;
    } else {
      if (call_kind == kCallDirect) {
        builder_->EmitWithU32V(kExprCallFunction,
                               NumImportedFunctions() + func_index);
      } else if (call_kind == kCallIndirect) {
        // This will not trap because table[func_index] always contains function
        // func_index.
        builder_->EmitI32Const(func_index);
        builder_->EmitWithU32V(kExprCallIndirect, sig_index);
        builder_->EmitByte(choose_function_table_index(data));  // Table index.
      } else {
        GenerateRef(HeapType(sig_index), data);
        builder_->EmitWithU32V(kExprCallRef, sig_index);
      }
    }
    if (sig->return_count() == 0 && wanted_kind != kWasmVoid) {
      // The call did not generate a value. Thus just generate it here.
      Generate(wanted_kind, data);
      return;
    }
    if (wanted_kind == kWasmVoid) {
      // The call did generate values, but we did not want one.
      for (size_t i = 0; i < sig->return_count(); ++i) {
        builder_->Emit(kExprDrop);
      }
      return;
    }
    auto wanted_types =
        base::VectorOf(&wanted_kind, wanted_kind == kWasmVoid ? 0 : 1);
    ConsumeAndGenerate(sig->returns(), wanted_types, data);
  }

  struct Var {
    uint32_t index;
    ValueType type = kWasmVoid;
    Var() = default;
    Var(uint32_t index, ValueType type) : index(index), type(type) {}
    bool is_valid() const { return type != kWasmVoid; }
  };

  Var GetRandomLocal(DataRange* data) {
    uint32_t num_params =
        static_cast<uint32_t>(builder_->signature()->parameter_count());
    uint32_t num_locals = static_cast<uint32_t>(locals_.size());
    if (num_params + num_locals == 0) return {};
    uint32_t index = data->get<uint8_t>() % (num_params + num_locals);
    ValueType type = index < num_params ? builder_->signature()->GetParam(index)
                                        : locals_[index - num_params];
    return {index, type};
  }

  constexpr static bool is_convertible_kind(ValueKind kind) {
    return kind == kI32 || kind == kI64 || kind == kF32 || kind == kF64;
  }

  template <ValueKind wanted_kind>
  void local_op(DataRange* data, WasmOpcode opcode) {
    static_assert(wanted_kind == kVoid || is_convertible_kind(wanted_kind));
    Var local = GetRandomLocal(data);
    // If there are no locals and no parameters, just generate any value (if a
    // value is needed), or do nothing.
    if (!local.is_valid() || !is_convertible_kind(local.type.kind())) {
      if (wanted_kind == kVoid) return;
      return Generate<wanted_kind>(data);
    }

    if (opcode != kExprLocalGet) Generate(local.type, data);
    builder_->EmitWithU32V(opcode, local.index);
    if (wanted_kind != kVoid && local.type.kind() != wanted_kind) {
      Convert(local.type, ValueType::Primitive(wanted_kind));
    }
  }

  template <ValueKind wanted_kind>
  void get_local(DataRange* data) {
    static_assert(wanted_kind != kVoid, "illegal type");
    local_op<wanted_kind>(data, kExprLocalGet);
  }

  void set_local(DataRange* data) { local_op<kVoid>(data, kExprLocalSet); }

  template <ValueKind wanted_kind>
  void tee_local(DataRange* data) {
    local_op<wanted_kind>(data, kExprLocalTee);
  }

  template <size_t num_bytes>
  void i32_const(DataRange* data) {
    builder_->EmitI32Const(data->getPseudoRandom<int32_t, num_bytes>());
  }

  template <size_t num_bytes>
  void i64_const(DataRange* data) {
    builder_->EmitI64Const(data->getPseudoRandom<int64_t, num_bytes>());
  }

  Var GetRandomGlobal(DataRange* data, bool ensure_mutable) {
    uint32_t index;
    if (ensure_mutable) {
      if (mutable_globals_.empty()) return {};
      index = mutable_globals_[data->get<uint8_t>() % mutable_globals_.size()];
    } else {
      if (globals_.empty()) return {};
      index = data->get<uint8_t>() % globals_.size();
    }
    ValueType type = globals_[index];
    return {index, type};
  }

  template <ValueKind wanted_kind>
  void global_op(DataRange* data) {
    static_assert(wanted_kind == kVoid || is_convertible_kind(wanted_kind));
    constexpr bool is_set = wanted_kind == kVoid;
    Var global = GetRandomGlobal(data, is_set);
    // If there are no globals, just generate any value (if a value is needed),
    // or do nothing.
    if (!global.is_valid() || !is_convertible_kind(global.type.kind())) {
      if (wanted_kind == kVoid) return;
      return Generate<wanted_kind>(data);
    }

    if (is_set) Generate(global.type, data);
    builder_->EmitWithU32V(is_set ? kExprGlobalSet : kExprGlobalGet,
                           global.index);
    if (!is_set && global.type.kind() != wanted_kind) {
      Convert(global.type, ValueType::Primitive(wanted_kind));
    }
  }

  template <ValueKind wanted_kind>
  void get_global(DataRange* data) {
    static_assert(wanted_kind != kVoid, "illegal type");
    global_op<wanted_kind>(data);
  }

  template <ValueKind select_kind>
  void select_with_type(DataRange* data) {
    static_assert(select_kind != kVoid, "illegal kind for select");
    Generate<select_kind, select_kind, kI32>(data);
    // num_types is always 1.
    uint8_t num_types = 1;
    builder_->EmitWithU8U8(kExprSelectWithType, num_types,
                           ValueType::Primitive(select_kind).value_type_code());
  }

  void set_global(DataRange* data) { global_op<kVoid>(data); }

  void throw_or_rethrow(DataRange* data) {
    bool rethrow = data->get<bool>();
    if (rethrow && !catch_blocks_.empty()) {
      int control_depth = static_cast<int>(blocks_.size() - 1);
      int catch_index =
          data->get<uint8_t>() % static_cast<int>(catch_blocks_.size());
      builder_->EmitWithU32V(kExprRethrow,
                             control_depth - catch_blocks_[catch_index]);
    } else {
      int tag = data->get<uint8_t>() % builder_->builder()->NumTags();
      const FunctionSig* exception_sig = builder_->builder()->GetTagType(tag);
      Generate(exception_sig->parameters(), data);
      builder_->EmitWithU32V(kExprThrow, tag);
    }
  }

  template <ValueKind... Types>
  void sequence(DataRange* data) {
    Generate<Types...>(data);
  }

  void memory_size(DataRange* data) {
    uint8_t memory_index =
        data->get<uint8_t>() % builder_->builder()->NumMemories();

    builder_->EmitWithU8(kExprMemorySize, memory_index);
    // The `memory_size` returns an I32. However, `kExprMemorySize` for memory64
    // returns an I64, so we should convert it.
    if (builder_->builder()->IsMemory64(memory_index)) {
      builder_->Emit(kExprI32ConvertI64);
    }
  }

  void grow_memory(DataRange* data) {
    uint8_t memory_index =
        data->get<uint8_t>() % builder_->builder()->NumMemories();

    // Generate the index and the arguments, if any.
    builder_->builder()->IsMemory64(memory_index) ? Generate<kI64>(data)
                                                  : Generate<kI32>(data);
    builder_->EmitWithU8(kExprMemoryGrow, memory_index);
    // The `grow_memory` returns an I32. However, `kExprMemoryGrow` for memory64
    // returns an I64, so we should convert it.
    if (builder_->builder()->IsMemory64(memory_index)) {
      builder_->Emit(kExprI32ConvertI64);
    }
  }

  void ref_null(HeapType type, DataRange* data) {
    builder_->EmitWithI32V(kExprRefNull, type.code());
  }

  bool get_local_ref(HeapType type, DataRange* data, Nullability nullable) {
    Var local = GetRandomLocal(data);
    // TODO(14034): Ideally we would check for subtyping here over type
    // equality, but we don't have a module.
    if (local.is_valid() && local.type.is_object_reference() &&
        local.type.heap_type() == type &&
        (local.type.is_nullable()
             ? nullable == kNullable  // We check for nullability-subtyping
             : locals_initialized_    // If the local is not nullable, we cannot
                                      // use it during locals initialization
         )) {
      builder_->EmitWithU32V(kExprLocalGet, local.index);
      return true;
    }

    return false;
  }

  bool new_object(HeapType type, DataRange* data, Nullability nullable) {
    DCHECK(type.is_index());

    uint32_t index = type.ref_index();
    bool new_default = data->get<bool>();

    if (builder_->builder()->IsStructType(index)) {
      const StructType* struct_gen = builder_->builder()->GetStructType(index);
      int field_count = struct_gen->field_count();
      bool can_be_defaultable = std::all_of(
          struct_gen->fields().begin(), struct_gen->fields().end(),
          [](ValueType type) -> bool { return type.is_defaultable(); });

      if (new_default && can_be_defaultable) {
        builder_->EmitWithPrefix(kExprStructNewDefault);
        builder_->EmitU32V(index);
      } else {
        for (int i = 0; i < field_count; i++) {
          Generate(struct_gen->field(i).Unpacked(), data);
        }
        builder_->EmitWithPrefix(kExprStructNew);
        builder_->EmitU32V(index);
      }
    } else if (builder_->builder()->IsArrayType(index)) {
      ValueType element_type =
          builder_->builder()->GetArrayType(index)->element_type();
      bool can_be_defaultable = element_type.is_defaultable();
      WasmOpcode array_new_op[] = {
          kExprArrayNew,        kExprArrayNewFixed,
          kExprArrayNewData,    kExprArrayNewElem,
          kExprArrayNewDefault,  // default op has to be at the end of the list.
      };
      size_t op_size = arraysize(array_new_op);
      if (!can_be_defaultable) --op_size;
      switch (array_new_op[data->get<uint8_t>() % op_size]) {
        case kExprArrayNewElem:
        case kExprArrayNewData: {
          // This is more restrictive than it has to be.
          // TODO(14034): Also support nonnullable and non-index reference
          // types.
          if (element_type.is_reference() && element_type.is_nullable() &&
              element_type.has_index()) {
            // Add a new element segment with the corresponding type.
            uint32_t element_segment = GenerateRefTypeElementSegment(
                data, builder_->builder(), element_type);
            // Generate offset, length.
            // TODO(14034): Change the distribution here to make it more likely
            // that the numbers are in range.
            Generate(base::VectorOf({kWasmI32, kWasmI32}), data);
            // Generate array.new_elem instruction.
            builder_->EmitWithPrefix(kExprArrayNewElem);
            builder_->EmitU32V(index);
            builder_->EmitU32V(element_segment);
            break;
          } else if (!element_type.is_reference()) {
            // Lazily create a data segment if the module doesn't have one yet.
            if (builder_->builder()->NumDataSegments() == 0) {
              GeneratePassiveDataSegment(data, builder_->builder());
            }
            int data_index =
                data->get<uint8_t>() % builder_->builder()->NumDataSegments();
            // Generate offset, length.
            Generate(base::VectorOf({kWasmI32, kWasmI32}), data);
            builder_->EmitWithPrefix(kExprArrayNewData);
            builder_->EmitU32V(index);
            builder_->EmitU32V(data_index);
            break;
          }
          [[fallthrough]];  // To array.new.
        }
        case kExprArrayNew:
          Generate(element_type.Unpacked(), data);
          Generate(kWasmI32, data);
          builder_->EmitI32Const(kMaxArraySize);
          builder_->Emit(kExprI32RemS);
          builder_->EmitWithPrefix(kExprArrayNew);
          builder_->EmitU32V(index);
          break;
        case kExprArrayNewFixed: {
          size_t element_count =
              std::min(static_cast<size_t>(data->get<uint8_t>()), data->size());
          for (size_t i = 0; i < element_count; ++i) {
            Generate(element_type.Unpacked(), data);
          }
          builder_->EmitWithPrefix(kExprArrayNewFixed);
          builder_->EmitU32V(index);
          builder_->EmitU32V(static_cast<uint32_t>(element_count));
          break;
        }
        case kExprArrayNewDefault:
          Generate(kWasmI32, data);
          builder_->EmitI32Const(kMaxArraySize);
          builder_->Emit(kExprI32RemS);
          builder_->EmitWithPrefix(kExprArrayNewDefault);
          builder_->EmitU32V(index);
          break;
        default:
          FATAL("Unimplemented opcode");
      }
    } else {
      CHECK(builder_->builder()->IsSignature(index));
      // Map the type index to a function index.
      // TODO(11954. 7748): Once we have type canonicalization, choose a random
      // function from among those matching the signature (consider function
      // subtyping?).
      uint32_t declared_func_index =
          index - static_cast<uint32_t>(arrays_.size() + structs_.size());
      size_t num_functions = builder_->builder()->NumDeclaredFunctions();
      const FunctionSig* sig = builder_->builder()->GetSignature(index);
      for (size_t i = 0; i < num_functions; ++i) {
        if (sig == builder_->builder()
                       ->GetFunction(declared_func_index)
                       ->signature()) {
          uint32_t absolute_func_index =
              NumImportedFunctions() + declared_func_index;
          builder_->EmitWithU32V(kExprRefFunc, absolute_func_index);
          return true;
        }
        declared_func_index = (declared_func_index + 1) % num_functions;
      }
      // We did not find a function matching the requested signature.
      builder_->EmitWithI32V(kExprRefNull, index);
      if (!nullable) {
        builder_->Emit(kExprRefAsNonNull);
      }
    }

    return true;
  }

  template <ValueKind wanted_kind>
  void table_op(std::vector<ValueType> types, DataRange* data,
                WasmOpcode opcode) {
    DCHECK(opcode == kExprTableSet || opcode == kExprTableSize ||
           opcode == kExprTableGrow || opcode == kExprTableFill);
    int num_tables = builder_->builder()->NumTables();
    DCHECK_GT(num_tables, 0);
    int index = data->get<uint8_t>() % num_tables;
    for (size_t i = 0; i < types.size(); i++) {
      // When passing the reftype by default kWasmFuncRef is used.
      // Then the type is changed according to its table type.
      if (types[i] == kWasmFuncRef) {
        types[i] = builder_->builder()->GetTableType(index);
      }
    }
    Generate(base::VectorOf(types), data);
    if (opcode == kExprTableSet) {
      builder_->Emit(opcode);
    } else {
      builder_->EmitWithPrefix(opcode);
    }
    builder_->EmitU32V(index);
  }

  bool table_get(HeapType type, DataRange* data, Nullability nullable) {
    ValueType needed_type = ValueType::RefMaybeNull(type, nullable);
    int table_count = builder_->builder()->NumTables();
    ZoneVector<uint32_t> table(builder_->builder()->zone());
    for (int i = 0; i < table_count; i++) {
      if (builder_->builder()->GetTableType(i) == needed_type) {
        table.push_back(i);
      }
    }
    if (table.empty()) {
      return false;
    }
    int index = data->get<uint8_t>() % static_cast<int>(table.size());
    Generate(kWasmI32, data);
    builder_->Emit(kExprTableGet);
    builder_->EmitU32V(table[index]);
    return true;
  }

  void table_set(DataRange* data) {
    table_op<kVoid>({kWasmI32, kWasmFuncRef}, data, kExprTableSet);
  }
  void table_size(DataRange* data) { table_op<kI32>({}, data, kExprTableSize); }
  void table_grow(DataRange* data) {
    table_op<kI32>({kWasmFuncRef, kWasmI32}, data, kExprTableGrow);
  }
  void table_fill(DataRange* data) {
    table_op<kVoid>({kWasmI32, kWasmFuncRef, kWasmI32}, data, kExprTableFill);
  }
  void table_copy(DataRange* data) {
    ValueType needed_type = data->get<bool>() ? kWasmFuncRef : kWasmExternRef;
    int table_count = builder_->builder()->NumTables();
    ZoneVector<uint32_t> table(builder_->builder()->zone());
    for (int i = 0; i < table_count; i++) {
      if (builder_->builder()->GetTableType(i) == needed_type) {
        table.push_back(i);
      }
    }
    if (table.empty()) {
      return;
    }
    int first_index = data->get<uint8_t>() % static_cast<int>(table.size());
    int second_index = data->get<uint8_t>() % static_cast<int>(table.size());
    Generate(kWasmI32, data);
    Generate(kWasmI32, data);
    Generate(kWasmI32, data);
    builder_->EmitWithPrefix(kExprTableCopy);
    builder_->EmitU32V(table[first_index]);
    builder_->EmitU32V(table[second_index]);
  }

  bool array_get_helper(ValueType value_type, DataRange* data) {
    WasmModuleBuilder* builder = builder_->builder();
    ZoneVector<uint32_t> array_indices(builder->zone());

    for (uint32_t i : arrays_) {
      DCHECK(builder->IsArrayType(i));
      if (builder->GetArrayType(i)->element_type().Unpacked() == value_type) {
        array_indices.push_back(i);
      }
    }

    if (!array_indices.empty()) {
      int index = data->get<uint8_t>() % static_cast<int>(array_indices.size());
      GenerateRef(HeapType(array_indices[index]), data, kNullable);
      Generate(kWasmI32, data);
      if (builder->GetArrayType(array_indices[index])
              ->element_type()
              .is_packed()) {
        builder_->EmitWithPrefix(data->get<bool>() ? kExprArrayGetS
                                                   : kExprArrayGetU);

      } else {
        builder_->EmitWithPrefix(kExprArrayGet);
      }
      builder_->EmitU32V(array_indices[index]);
      return true;
    }

    return false;
  }

  template <ValueKind wanted_kind>
  void array_get(DataRange* data) {
    bool got_array_value =
        array_get_helper(ValueType::Primitive(wanted_kind), data);
    if (!got_array_value) {
      Generate<wanted_kind>(data);
    }
  }
  bool array_get_ref(HeapType type, DataRange* data, Nullability nullable) {
    ValueType needed_type = ValueType::RefMaybeNull(type, nullable);
    return array_get_helper(needed_type, data);
  }

  void i31_get(DataRange* data) {
    GenerateRef(HeapType(HeapType::kI31), data);
    if (data->get<bool>()) {
      builder_->EmitWithPrefix(kExprI31GetS);
    } else {
      builder_->EmitWithPrefix(kExprI31GetU);
    }
  }

  void array_len(DataRange* data) {
    DCHECK_NE(0, arrays_.size());  // We always emit at least one array type.
    GenerateRef(HeapType(HeapType::kArray), data);
    builder_->EmitWithPrefix(kExprArrayLen);
  }

  void array_copy(DataRange* data) {
    DCHECK_NE(0, arrays_.size());  // We always emit at least one array type.
    // TODO(14034): The source element type only has to be a subtype of the
    // destination element type. Currently this only generates copy from same
    // typed arrays.
    uint32_t array_index = arrays_[data->get<uint8_t>() % arrays_.size()];
    DCHECK(builder_->builder()->IsArrayType(array_index));
    GenerateRef(HeapType(array_index), data);  // destination
    Generate(kWasmI32, data);                  // destination index
    GenerateRef(HeapType(array_index), data);  // source
    Generate(kWasmI32, data);                  // source index
    Generate(kWasmI32, data);                  // length
    builder_->EmitWithPrefix(kExprArrayCopy);
    builder_->EmitU32V(array_index);  // destination array type index
    builder_->EmitU32V(array_index);  // source array type index
  }

  void array_fill(DataRange* data) {
    DCHECK_NE(0, arrays_.size());  // We always emit at least one array type.
    int array_index = arrays_[data->get<uint8_t>() % arrays_.size()];
    DCHECK(builder_->builder()->IsArrayType(array_index));
    ValueType element_type = builder_->builder()
                                 ->GetArrayType(array_index)
                                 ->element_type()
                                 .Unpacked();
    GenerateRef(HeapType(array_index), data);  // array
    Generate(kWasmI32, data);                  // offset
    Generate(element_type, data);              // value
    Generate(kWasmI32, data);                  // length
    builder_->EmitWithPrefix(kExprArrayFill);
    builder_->EmitU32V(array_index);
  }

  void array_init_data(DataRange* data) {
    DCHECK_NE(0, arrays_.size());  // We always emit at least one array type.
    int array_index = arrays_[data->get<uint8_t>() % arrays_.size()];
    DCHECK(builder_->builder()->IsArrayType(array_index));
    const ArrayType* array_type =
        builder_->builder()->GetArrayType(array_index);
    DCHECK(array_type->mutability());
    ValueType element_type = array_type->element_type().Unpacked();
    if (element_type.is_reference()) {
      return;
    }
    if (builder_->builder()->NumDataSegments() == 0) {
      GeneratePassiveDataSegment(data, builder_->builder());
    }

    int data_index =
        data->get<uint8_t>() % builder_->builder()->NumDataSegments();
    // Generate array, index, data_offset, length.
    Generate(base::VectorOf({ValueType::RefNull(array_index), kWasmI32,
                             kWasmI32, kWasmI32}),
             data);
    builder_->EmitWithPrefix(kExprArrayInitData);
    builder_->EmitU32V(array_index);
    builder_->EmitU32V(data_index);
  }

  void array_init_elem(DataRange* data) {
    DCHECK_NE(0, arrays_.size());  // We always emit at least one array type.
    int array_index = arrays_[data->get<uint8_t>() % arrays_.size()];
    DCHECK(builder_->builder()->IsArrayType(array_index));
    const ArrayType* array_type =
        builder_->builder()->GetArrayType(array_index);
    DCHECK(array_type->mutability());
    ValueType element_type = array_type->element_type().Unpacked();
    // This is more restrictive than it has to be.
    // TODO(14034): Also support nonnullable and non-index reference
    // types.
    if (!element_type.is_reference() || element_type.is_non_nullable() ||
        !element_type.has_index()) {
      return;
    }
    // Add a new element segment with the corresponding type.
    uint32_t element_segment =
        GenerateRefTypeElementSegment(data, builder_->builder(), element_type);
    // Generate array, index, elem_offset, length.
    // TODO(14034): Change the distribution here to make it more likely
    // that the numbers are in range.
    Generate(base::VectorOf({ValueType::RefNull(array_index), kWasmI32,
                             kWasmI32, kWasmI32}),
             data);
    // Generate array.new_elem instruction.
    builder_->EmitWithPrefix(kExprArrayInitElem);
    builder_->EmitU32V(array_index);
    builder_->EmitU32V(element_segment);
  }

  void array_set(DataRange* data) {
    WasmModuleBuilder* builder = builder_->builder();
    ZoneVector<uint32_t> array_indices(builder->zone());
    for (uint32_t i : arrays_) {
      DCHECK(builder->IsArrayType(i));
      if (builder->GetArrayType(i)->mutability()) {
        array_indices.push_back(i);
      }
    }

    if (array_indices.empty()) {
      return;
    }

    int index = data->get<uint8_t>() % static_cast<int>(array_indices.size());
    GenerateRef(HeapType(array_indices[index]), data);
    Generate(kWasmI32, data);
    Generate(
        builder->GetArrayType(array_indices[index])->element_type().Unpacked(),
        data);
    builder_->EmitWithPrefix(kExprArraySet);
    builder_->EmitU32V(array_indices[index]);
  }

  bool struct_get_helper(ValueType value_type, DataRange* data) {
    WasmModuleBuilder* builder = builder_->builder();
    ZoneVector<uint32_t> field_index(builder->zone());
    ZoneVector<uint32_t> struct_index(builder->zone());
    for (uint32_t i : structs_) {
      DCHECK(builder->IsStructType(i));
      int field_count = builder->GetStructType(i)->field_count();
      for (int index = 0; index < field_count; index++) {
        // TODO(14034): This should be a subtype check!
        if (builder->GetStructType(i)->field(index) == value_type) {
          field_index.push_back(index);
          struct_index.push_back(i);
        }
      }
    }
    if (!field_index.empty()) {
      int index = data->get<uint8_t>() % static_cast<int>(field_index.size());
      GenerateRef(HeapType(struct_index[index]), data, kNullable);
      if (builder->GetStructType(struct_index[index])
              ->field(field_index[index])
              .is_packed()) {
        builder_->EmitWithPrefix(data->get<bool>() ? kExprStructGetS
                                                   : kExprStructGetU);
      } else {
        builder_->EmitWithPrefix(kExprStructGet);
      }
      builder_->EmitU32V(struct_index[index]);
      builder_->EmitU32V(field_index[index]);
      return true;
    }
    return false;
  }

  template <ValueKind wanted_kind>
  void struct_get(DataRange* data) {
    bool got_struct_value =
        struct_get_helper(ValueType::Primitive(wanted_kind), data);
    if (!got_struct_value) {
      Generate<wanted_kind>(data);
    }
  }

  bool struct_get_ref(HeapType type, DataRange* data, Nullability nullable) {
    ValueType needed_type = ValueType::RefMaybeNull(type, nullable);
    return struct_get_helper(needed_type, data);
  }

  bool ref_cast(HeapType type, DataRange* data, Nullability nullable) {
    HeapType input_type = top_type(type);
    GenerateRef(input_type, data);
    builder_->EmitWithPrefix(nullable ? kExprRefCastNull : kExprRefCast);
    builder_->EmitI32V(type.code());
    return true;  // It always produces the desired result type.
  }

  HeapType top_type(HeapType type) {
    switch (type.representation()) {
      case HeapType::kAny:
      case HeapType::kEq:
      case HeapType::kArray:
      case HeapType::kStruct:
      case HeapType::kI31:
      case HeapType::kNone:
        return HeapType(HeapType::kAny);
      case HeapType::kExtern:
      case HeapType::kNoExtern:
        return HeapType(HeapType::kExtern);
      case HeapType::kExn:
      case HeapType::kNoExn:
        return HeapType(HeapType::kExn);
      case HeapType::kFunc:
      case HeapType::kNoFunc:
        return HeapType(HeapType::kFunc);
      default:
        DCHECK(type.is_index());
        if (builder_->builder()->IsSignature(type.ref_index())) {
          return HeapType(HeapType::kFunc);
        }
        DCHECK(builder_->builder()->IsStructType(type.ref_index()) ||
               builder_->builder()->IsArrayType(type.ref_index()));
        return HeapType(HeapType::kAny);
    }
  }

  HeapType choose_sub_type(HeapType type, DataRange* data) {
    switch (type.representation()) {
      case HeapType::kAny: {
        constexpr HeapType::Representation generic_types[] = {
            HeapType::kAny,    HeapType::kEq,  HeapType::kArray,
            HeapType::kStruct, HeapType::kI31, HeapType::kNone,
        };
        size_t choice =
            data->get<uint8_t>() %
            (arrays_.size() + structs_.size() + arraysize(generic_types));

        if (choice < arrays_.size()) return HeapType(arrays_[choice]);
        choice -= arrays_.size();
        if (choice < structs_.size()) return HeapType(structs_[choice]);
        choice -= structs_.size();
        return HeapType(generic_types[choice]);
      }
      case HeapType::kEq: {
        constexpr HeapType::Representation generic_types[] = {
            HeapType::kEq,  HeapType::kArray, HeapType::kStruct,
            HeapType::kI31, HeapType::kNone,
        };
        size_t choice =
            data->get<uint8_t>() %
            (arrays_.size() + structs_.size() + arraysize(generic_types));

        if (choice < arrays_.size()) return HeapType(arrays_[choice]);
        choice -= arrays_.size();
        if (choice < structs_.size()) return HeapType(structs_[choice]);
        choice -= structs_.size();
        return HeapType(generic_types[choice]);
      }
      case HeapType::kStruct: {
        constexpr HeapType::Representation generic_types[] = {
            HeapType::kStruct,
            HeapType::kNone,
        };
        const size_t type_count = structs_.size();
        const size_t choice =
            data->get<uint8_t>() % (type_count + arraysize(generic_types));
        return choice >= type_count
                   ? HeapType(generic_types[choice - type_count])
                   : HeapType(structs_[choice]);
      }
      case HeapType::kArray: {
        constexpr HeapType::Representation generic_types[] = {
            HeapType::kArray,
            HeapType::kNone,
        };
        const size_t type_count = arrays_.size();
        const size_t choice =
            data->get<uint8_t>() % (type_count + arraysize(generic_types));
        return choice >= type_count
                   ? HeapType(generic_types[choice - type_count])
                   : HeapType(arrays_[choice]);
      }
      case HeapType::kFunc: {
        constexpr HeapType::Representation generic_types[] = {
            HeapType::kFunc, HeapType::kNoFunc};
        const size_t type_count = functions_.size();
        const size_t choice =
            data->get<uint8_t>() % (type_count + arraysize(generic_types));
        return choice >= type_count
                   ? HeapType(generic_types[choice - type_count])
                   : HeapType(functions_[choice]);
      }
      case HeapType::kExtern:
        // About 10% of chosen subtypes will be kNoExtern.
        return HeapType(data->get<uint8_t>() > 25 ? HeapType::kExtern
                                                  : HeapType::kNoExtern);
      default:
        if (!type.is_index()) {
          // No logic implemented to find a sub-type.
          return type;
        }
        // Collect all (direct) sub types.
        // TODO(14034): Also collect indirect sub types.
        std::vector<uint32_t> subtypes;
        uint32_t type_count = builder_->builder()->NumTypes();
        for (uint32_t i = 0; i < type_count; ++i) {
          if (builder_->builder()->GetSuperType(i) == type.ref_index()) {
            subtypes.push_back(i);
          }
        }
        return subtypes.empty()
                   ? type  // no downcast possible
                   : HeapType(subtypes[data->get<uint8_t>() % subtypes.size()]);
    }
  }

  bool br_on_cast(HeapType type, DataRange* data, Nullability nullable) {
    DCHECK(!blocks_.empty());
    const uint32_t target_block = data->get<uint8_t>() % blocks_.size();
    const uint32_t block_index =
        static_cast<uint32_t>(blocks_.size()) - 1 - target_block;
    const auto break_types = base::VectorOf(blocks_[target_block]);
    if (break_types.empty()) {
      return false;
    }
    ValueType break_type = break_types.last();
    if (!break_type.is_reference()) {
      return false;
    }

    Generate(break_types.SubVector(0, break_types.size() - 1), data);
    if (data->get<bool>()) {
      // br_on_cast
      HeapType source_type = top_type(break_type.heap_type());
      const bool source_is_nullable = data->get<bool>();
      GenerateRef(source_type, data,
                  source_is_nullable ? kNullable : kNonNullable);
      const bool target_is_nullable =
          source_is_nullable && break_type.is_nullable() && data->get<bool>();
      builder_->EmitWithPrefix(kExprBrOnCast);
      builder_->EmitU32V(source_is_nullable + (target_is_nullable << 1));
      builder_->EmitU32V(block_index);
      builder_->EmitI32V(source_type.code());             // source type
      builder_->EmitI32V(break_type.heap_type().code());  // target type
      // Fallthrough: The type has been up-cast to the source type of the
      // br_on_cast instruction! (If the type on the stack was more specific,
      // this loses type information.)
      base::SmallVector<ValueType, 32> fallthrough_types(break_types);
      fallthrough_types.back() = ValueType::RefMaybeNull(
          source_type, source_is_nullable ? kNullable : kNonNullable);
      ConsumeAndGenerate(base::VectorOf(fallthrough_types), {}, data);
      // Generate the actually desired ref type.
      GenerateRef(type, data, nullable);
    } else {
      // br_on_cast_fail
      HeapType source_type = break_type.heap_type();
      const bool source_is_nullable = data->get<bool>();
      GenerateRef(source_type, data,
                  source_is_nullable ? kNullable : kNonNullable);
      const bool target_is_nullable =
          source_is_nullable &&
          (!break_type.is_nullable() || data->get<bool>());
      HeapType target_type = choose_sub_type(source_type, data);

      builder_->EmitWithPrefix(kExprBrOnCastFail);
      builder_->EmitU32V(source_is_nullable + (target_is_nullable << 1));
      builder_->EmitU32V(block_index);
      builder_->EmitI32V(source_type.code());
      builder_->EmitI32V(target_type.code());
      // Fallthrough: The type has been cast to the target type.
      base::SmallVector<ValueType, 32> fallthrough_types(break_types);
      fallthrough_types.back() = ValueType::RefMaybeNull(
          target_type, target_is_nullable ? kNullable : kNonNullable);
      ConsumeAndGenerate(base::VectorOf(fallthrough_types), {}, data);
      // Generate the actually desired ref type.
      GenerateRef(type, data, nullable);
    }
    return true;
  }

  bool any_convert_extern(HeapType type, DataRange* data,
                          Nullability nullable) {
    if (type.representation() != HeapType::kAny) {
      return false;
    }
    GenerateRef(HeapType(HeapType::kExtern), data);
    builder_->EmitWithPrefix(kExprAnyConvertExtern);
    if (nullable == kNonNullable) {
      builder_->Emit(kExprRefAsNonNull);
    }
    return true;
  }

  bool ref_as_non_null(HeapType type, DataRange* data, Nullability nullable) {
    GenerateRef(type, data, kNullable);
    builder_->Emit(kExprRefAsNonNull);
    return true;
  }

  void struct_set(DataRange* data) {
    WasmModuleBuilder* builder = builder_->builder();
    DCHECK_NE(0, structs_.size());  // We always emit at least one struct type.
    int struct_index = structs_[data->get<uint8_t>() % structs_.size()];
    DCHECK(builder->IsStructType(struct_index));
    const StructType* struct_type = builder->GetStructType(struct_index);
    ZoneVector<uint32_t> field_indices(builder->zone());
    for (uint32_t i = 0; i < struct_type->field_count(); i++) {
      if (struct_type->mutability(i)) {
        field_indices.push_back(i);
      }
    }
    if (field_indices.empty()) {
      return;
    }
    int field_index =
        field_indices[data->get<uint8_t>() % field_indices.size()];
    GenerateRef(HeapType(struct_index), data);
    Generate(struct_type->field(field_index).Unpacked(), data);
    builder_->EmitWithPrefix(kExprStructSet);
    builder_->EmitU32V(struct_index);
    builder_->EmitU32V(field_index);
  }

  void ref_is_null(DataRange* data) {
    GenerateRef(HeapType(HeapType::kAny), data);
    builder_->Emit(kExprRefIsNull);
  }

  template <WasmOpcode opcode>
  void ref_test(DataRange* data) {
    GenerateRef(HeapType(HeapType::kAny), data);
    constexpr int generic_types[] = {kAnyRefCode,    kEqRefCode, kArrayRefCode,
                                     kStructRefCode, kNoneCode,  kI31RefCode};
    size_t num_types = structs_.size() + arrays_.size();
    size_t num_all_types = num_types + arraysize(generic_types);
    size_t type_choice = data->get<uint8_t>() % num_all_types;
    builder_->EmitWithPrefix(opcode);
    if (type_choice < structs_.size()) {
      builder_->EmitU32V(structs_[type_choice]);
      return;
    }
    type_choice -= structs_.size();
    if (type_choice < arrays_.size()) {
      builder_->EmitU32V(arrays_[type_choice]);
      return;
    }
    type_choice -= arrays_.size();
    builder_->EmitU32V(generic_types[type_choice]);
  }

  void ref_eq(DataRange* data) {
    GenerateRef(HeapType(HeapType::kEq), data);
    GenerateRef(HeapType(HeapType::kEq), data);
    builder_->Emit(kExprRefEq);
  }

  void call_string_import(uint32_t index) {
    builder_->EmitWithU32V(kExprCallFunction, index);
  }

  void string_cast(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    call_string_import(string_imports_.cast);
  }

  void string_test(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    call_string_import(string_imports_.test);
  }

  void string_fromcharcode(DataRange* data) {
    Generate(kWasmI32, data);
    call_string_import(string_imports_.fromCharCode);
  }

  void string_fromcodepoint(DataRange* data) {
    Generate(kWasmI32, data);
    call_string_import(string_imports_.fromCodePoint);
  }

  void string_charcodeat(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    Generate(kWasmI32, data);
    call_string_import(string_imports_.charCodeAt);
  }

  void string_codepointat(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    Generate(kWasmI32, data);
    call_string_import(string_imports_.codePointAt);
  }

  void string_length(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    call_string_import(string_imports_.length);
  }

  void string_concat(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    GenerateRef(HeapType(HeapType::kExtern), data);
    call_string_import(string_imports_.concat);
  }

  void string_substring(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    Generate(kWasmI32, data);
    Generate(kWasmI32, data);
    call_string_import(string_imports_.substring);
  }

  void string_equals(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    GenerateRef(HeapType(HeapType::kExtern), data);
    call_string_import(string_imports_.equals);
  }

  void string_compare(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    GenerateRef(HeapType(HeapType::kExtern), data);
    call_string_import(string_imports_.compare);
  }

  void string_fromcharcodearray(DataRange* data) {
    GenerateRef(HeapType(string_imports_.array_i16), data);
    Generate(kWasmI32, data);
    Generate(kWasmI32, data);
    call_string_import(string_imports_.fromCharCodeArray);
  }

  void string_intocharcodearray(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    GenerateRef(HeapType(string_imports_.array_i16), data);
    Generate(kWasmI32, data);
    call_string_import(string_imports_.intoCharCodeArray);
  }

  void string_measureutf8(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    call_string_import(string_imports_.measureStringAsUTF8);
  }

  void string_intoutf8array(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    GenerateRef(HeapType(string_imports_.array_i8), data);
    Generate(kWasmI32, data);
    call_string_import(string_imports_.encodeStringIntoUTF8Array);
  }

  void string_toutf8array(DataRange* data) {
    GenerateRef(HeapType(HeapType::kExtern), data);
    call_string_import(string_imports_.encodeStringToUTF8Array);
  }

  void string_fromutf8array(DataRange* data) {
    GenerateRef(HeapType(string_imports_.array_i8), data);
    Generate(kWasmI32, data);
    Generate(kWasmI32, data);
    call_string_import(string_imports_.decodeStringFromUTF8Array);
  }

  using GenerateFn = void (BodyGen::*)(DataRange*);
  using GenerateFnWithHeap = bool (BodyGen::*)(HeapType, DataRange*,
                                               Nullability);

  template <size_t N>
  void GenerateOneOf(const std::array<GenerateFn, N>& alternatives,
                     DataRange* data) {
    static_assert(N < std::numeric_limits<uint8_t>::max(),
                  "Too many alternatives. Use a bigger type if needed.");
    const auto which = data->get<uint8_t>();

    GenerateFn alternate = alternatives[which % N];
    (this->*alternate)(data);
  }

  // Returns true if it had succesfully generated a randomly chosen expression
  // from the `alternatives`.
  template <size_t N>
  bool GenerateOneOf(const std::array<GenerateFnWithHeap, N>& alternatives,
                     HeapType type, DataRange* data, Nullability nullability) {
    static_assert(N < std::numeric_limits<uint8_t>::max(),
                  "Too many alternatives. Use a bigger type if needed.");

    int index = data->get<uint8_t>() % (N + 1);

    if (nullability && index == N) {
      ref_null(type, data);
      return true;
    }

    for (int i = index; i < static_cast<int>(N); i++) {
      if ((this->*alternatives[i])(type, data, nullability)) {
        return true;
      }
    }

    for (int i = 0; i < index; i++) {
      if ((this->*alternatives[i])(type, data, nullability)) {
        return true;
      }
    }

    if (nullability == kNullable) {
      ref_null(type, data);
      return true;
    }

    return false;
  }

  struct GeneratorRecursionScope {
    explicit GeneratorRecursionScope(BodyGen* gen) : gen(gen) {
      ++gen->recursion_depth;
      DCHECK_LE(gen->recursion_depth, kMaxRecursionDepth);
    }
    ~GeneratorRecursionScope() {
      DCHECK_GT(gen->recursion_depth, 0);
      --gen->recursion_depth;
    }
    BodyGen* gen;
  };

 public:
  BodyGen(WasmFunctionBuilder* fn, const std::vector<uint32_t>& functions,
          const std::vector<ValueType>& globals,
          const std::vector<uint8_t>& mutable_globals,
          const std::vector<uint32_t>& structs,
          const std::vector<uint32_t>& arrays, const StringImports& strings,
          DataRange* data)
      : builder_(fn),
        functions_(functions),
        globals_(globals),
        mutable_globals_(mutable_globals),
        structs_(structs),
        arrays_(arrays),
        string_imports_(strings),
        locals_initialized_(false) {
    const FunctionSig* sig = fn->signature();
    blocks_.emplace_back();
    for (size_t i = 0; i < sig->return_count(); ++i) {
      blocks_.back().push_back(sig->GetReturn(i));
    }
    locals_.resize(data->get<uint8_t>() % kMaxLocals);
    uint32_t num_types = static_cast<uint32_t>(
        functions_.size() + structs_.size() + arrays_.size());
    for (ValueType& local : locals_) {
      local = GetValueType<options>(data, num_types);
      fn->AddLocal(local);
    }
  }

  int NumImportedFunctions() {
    return builder_->builder()->NumImportedFunctions();
  }

  // Generator functions.
  // Implementation detail: We define non-template Generate*TYPE*() functions
  // instead of templatized Generate<TYPE>(). This is because we cannot define
  // the templatized Generate<TYPE>() functions:
  //  - outside of the class body without specializing the template of the
  //  `BodyGen` (results in partial template specialization error);
  //  - inside of the class body (gcc complains about explicit specialization in
  //  non-namespace scope).

  void GenerateVoid(DataRange* data) {
    GeneratorRecursionScope rec_scope(this);
    if (recursion_limit_reached() || data->size() == 0) return;

    constexpr auto mvp_alternatives =
        CreateArray(&BodyGen::sequence<kVoid, kVoid>,
                    &BodyGen::sequence<kVoid, kVoid, kVoid, kVoid>,
                    &BodyGen::sequence<kVoid, kVoid, kVoid, kVoid, kVoid, kVoid,
                                       kVoid, kVoid>,
                    &BodyGen::block<kVoid>,           //
                    &BodyGen::loop<kVoid>,            //
                    &BodyGen::finite_loop<kVoid>,     //
                    &BodyGen::if_<kVoid, kIf>,        //
                    &BodyGen::if_<kVoid, kIfElse>,    //
                    &BodyGen::br,                     //
                    &BodyGen::br_if<kVoid>,           //
                    &BodyGen::br_on_null<kVoid>,      //
                    &BodyGen::br_on_non_null<kVoid>,  //
                    &BodyGen::br_table<kVoid>,        //
                    &BodyGen::return_op,              //

                    &BodyGen::memop<kExprI32StoreMem, kI32>,
                    &BodyGen::memop<kExprI32StoreMem8, kI32>,
                    &BodyGen::memop<kExprI32StoreMem16, kI32>,
                    &BodyGen::memop<kExprI64StoreMem, kI64>,
                    &BodyGen::memop<kExprI64StoreMem8, kI64>,
                    &BodyGen::memop<kExprI64StoreMem16, kI64>,
                    &BodyGen::memop<kExprI64StoreMem32, kI64>,
                    &BodyGen::memop<kExprF32StoreMem, kF32>,
                    &BodyGen::memop<kExprF64StoreMem, kF64>,
                    &BodyGen::memop<kExprI32AtomicStore, kI32>,
                    &BodyGen::memop<kExprI32AtomicStore8U, kI32>,
                    &BodyGen::memop<kExprI32AtomicStore16U, kI32>,
                    &BodyGen::memop<kExprI64AtomicStore, kI64>,
                    &BodyGen::memop<kExprI64AtomicStore8U, kI64>,
                    &BodyGen::memop<kExprI64AtomicStore16U, kI64>,
                    &BodyGen::memop<kExprI64AtomicStore32U, kI64>,

                    &BodyGen::drop,

                    &BodyGen::call<kVoid>,           //
                    &BodyGen::call_indirect<kVoid>,  //
                    &BodyGen::call_ref<kVoid>,       //

                    &BodyGen::set_local,         //
                    &BodyGen::set_global,        //
                    &BodyGen::throw_or_rethrow,  //
                    &BodyGen::try_block<kVoid>,  //

                    &BodyGen::table_set,    //
                    &BodyGen::table_fill,   //
                    &BodyGen::table_copy);  //

    auto constexpr simd_alternatives =
        CreateArray(&BodyGen::memop<kExprS128StoreMem, kS128>,
                    &BodyGen::simd_lane_memop<kExprS128Store8Lane, 16, kS128>,
                    &BodyGen::simd_lane_memop<kExprS128Store16Lane, 8, kS128>,
                    &BodyGen::simd_lane_memop<kExprS128Store32Lane, 4, kS128>,
                    &BodyGen::simd_lane_memop<kExprS128Store64Lane, 2, kS128>);

    auto constexpr wasmGC_alternatives =
        CreateArray(&BodyGen::struct_set,        //
                    &BodyGen::array_set,         //
                    &BodyGen::array_copy,        //
                    &BodyGen::array_fill,        //
                    &BodyGen::array_init_data,   //
                    &BodyGen::array_init_elem);  //

    constexpr auto alternatives = AppendArrayIf<ShouldGenerateWasmGC(options)>(
        AppendArrayIf<ShouldGenerateSIMD(options)>(mvp_alternatives,
                                                   simd_alternatives),
        wasmGC_alternatives);
    GenerateOneOf(alternatives, data);
  }

  void GenerateI32(DataRange* data) {
    GeneratorRecursionScope rec_scope(this);
    if (recursion_limit_reached() || data->size() <= 1) {
      // Rather than evenly distributing values across the full 32-bit range,
      // distribute them evenly over the possible bit lengths. In particular,
      // for values used as indices into something else, smaller values are
      // more likely to be useful.
      uint8_t size = 1 + (data->getPseudoRandom<uint8_t>() & 31);
      uint32_t mask = kMaxUInt32 >> (32 - size);
      builder_->EmitI32Const(data->getPseudoRandom<uint32_t>() & mask);
      return;
    }

    constexpr auto mvp_alternatives = CreateArray(
        &BodyGen::i32_const<1>,  //
        &BodyGen::i32_const<2>,  //
        &BodyGen::i32_const<3>,  //
        &BodyGen::i32_const<4>,  //

        &BodyGen::sequence<kI32, kVoid>,         //
        &BodyGen::sequence<kVoid, kI32>,         //
        &BodyGen::sequence<kVoid, kI32, kVoid>,  //

        &BodyGen::op<kExprI32Eqz, kI32>,        //
        &BodyGen::op<kExprI32Eq, kI32, kI32>,   //
        &BodyGen::op<kExprI32Ne, kI32, kI32>,   //
        &BodyGen::op<kExprI32LtS, kI32, kI32>,  //
        &BodyGen::op<kExprI32LtU, kI32, kI32>,  //
        &BodyGen::op<kExprI32GeS, kI32, kI32>,  //
        &BodyGen::op<kExprI32GeU, kI32, kI32>,  //

        &BodyGen::op<kExprI64Eqz, kI64>,        //
        &BodyGen::op<kExprI64Eq, kI64, kI64>,   //
        &BodyGen::op<kExprI64Ne, kI64, kI64>,   //
        &BodyGen::op<kExprI64LtS, kI64, kI64>,  //
        &BodyGen::op<kExprI64LtU, kI64, kI64>,  //
        &BodyGen::op<kExprI64GeS, kI64, kI64>,  //
        &BodyGen::op<kExprI64GeU, kI64, kI64>,  //

        &BodyGen::op<kExprF32Eq, kF32, kF32>,
        &BodyGen::op<kExprF32Ne, kF32, kF32>,
        &BodyGen::op<kExprF32Lt, kF32, kF32>,
        &BodyGen::op<kExprF32Ge, kF32, kF32>,

        &BodyGen::op<kExprF64Eq, kF64, kF64>,
        &BodyGen::op<kExprF64Ne, kF64, kF64>,
        &BodyGen::op<kExprF64Lt, kF64, kF64>,
        &BodyGen::op<kExprF64Ge, kF64, kF64>,

        &BodyGen::op<kExprI32Add, kI32, kI32>,
        &BodyGen::op<kExprI32Sub, kI32, kI32>,
        &BodyGen::op<kExprI32Mul, kI32, kI32>,

        &BodyGen::op<kExprI32DivS, kI32, kI32>,
        &BodyGen::op<kExprI32DivU, kI32, kI32>,
        &BodyGen::op<kExprI32RemS, kI32, kI32>,
        &BodyGen::op<kExprI32RemU, kI32, kI32>,

        &BodyGen::op<kExprI32And, kI32, kI32>,
        &BodyGen::op<kExprI32Ior, kI32, kI32>,
        &BodyGen::op<kExprI32Xor, kI32, kI32>,
        &BodyGen::op<kExprI32Shl, kI32, kI32>,
        &BodyGen::op<kExprI32ShrU, kI32, kI32>,
        &BodyGen::op<kExprI32ShrS, kI32, kI32>,
        &BodyGen::op<kExprI32Ror, kI32, kI32>,
        &BodyGen::op<kExprI32Rol, kI32, kI32>,

        &BodyGen::op<kExprI32Clz, kI32>,     //
        &BodyGen::op<kExprI32Ctz, kI32>,     //
        &BodyGen::op<kExprI32Popcnt, kI32>,  //

        &BodyGen::op<kExprI32ConvertI64, kI64>,
        &BodyGen::op<kExprI32SConvertF32, kF32>,
        &BodyGen::op<kExprI32UConvertF32, kF32>,
        &BodyGen::op<kExprI32SConvertF64, kF64>,
        &BodyGen::op<kExprI32UConvertF64, kF64>,
        &BodyGen::op<kExprI32ReinterpretF32, kF32>,

        &BodyGen::op_with_prefix<kExprI32SConvertSatF32, kF32>,
        &BodyGen::op_with_prefix<kExprI32UConvertSatF32, kF32>,
        &BodyGen::op_with_prefix<kExprI32SConvertSatF64, kF64>,
        &BodyGen::op_with_prefix<kExprI32UConvertSatF64, kF64>,

        &BodyGen::block<kI32>,           //
        &BodyGen::loop<kI32>,            //
        &BodyGen::finite_loop<kI32>,     //
        &BodyGen::if_<kI32, kIfElse>,    //
        &BodyGen::br_if<kI32>,           //
        &BodyGen::br_on_null<kI32>,      //
        &BodyGen::br_on_non_null<kI32>,  //
        &BodyGen::br_table<kI32>,        //

        &BodyGen::memop<kExprI32LoadMem>,                               //
        &BodyGen::memop<kExprI32LoadMem8S>,                             //
        &BodyGen::memop<kExprI32LoadMem8U>,                             //
        &BodyGen::memop<kExprI32LoadMem16S>,                            //
        &BodyGen::memop<kExprI32LoadMem16U>,                            //
                                                                        //
        &BodyGen::memop<kExprI32AtomicLoad>,                            //
        &BodyGen::memop<kExprI32AtomicLoad8U>,                          //
        &BodyGen::memop<kExprI32AtomicLoad16U>,                         //
        &BodyGen::memop<kExprI32AtomicAdd, kI32>,                       //
        &BodyGen::memop<kExprI32AtomicSub, kI32>,                       //
        &BodyGen::memop<kExprI32AtomicAnd, kI32>,                       //
        &BodyGen::memop<kExprI32AtomicOr, kI32>,                        //
        &BodyGen::memop<kExprI32AtomicXor, kI32>,                       //
        &BodyGen::memop<kExprI32AtomicExchange, kI32>,                  //
        &BodyGen::memop<kExprI32AtomicCompareExchange, kI32, kI32>,     //
        &BodyGen::memop<kExprI32AtomicAdd8U, kI32>,                     //
        &BodyGen::memop<kExprI32AtomicSub8U, kI32>,                     //
        &BodyGen::memop<kExprI32AtomicAnd8U, kI32>,                     //
        &BodyGen::memop<kExprI32AtomicOr8U, kI32>,                      //
        &BodyGen::memop<kExprI32AtomicXor8U, kI32>,                     //
        &BodyGen::memop<kExprI32AtomicExchange8U, kI32>,                //
        &BodyGen::memop<kExprI32AtomicCompareExchange8U, kI32, kI32>,   //
        &BodyGen::memop<kExprI32AtomicAdd16U, kI32>,                    //
        &BodyGen::memop<kExprI32AtomicSub16U, kI32>,                    //
        &BodyGen::memop<kExprI32AtomicAnd16U, kI32>,                    //
        &BodyGen::memop<kExprI32AtomicOr16U, kI32>,                     //
        &BodyGen::memop<kExprI32AtomicXor16U, kI32>,                    //
        &BodyGen::memop<kExprI32AtomicExchange16U, kI32>,               //
        &BodyGen::memop<kExprI32AtomicCompareExchange16U, kI32, kI32>,  //

        &BodyGen::memory_size,  //
        &BodyGen::grow_memory,  //

        &BodyGen::get_local<kI32>,                    //
        &BodyGen::tee_local<kI32>,                    //
        &BodyGen::get_global<kI32>,                   //
        &BodyGen::op<kExprSelect, kI32, kI32, kI32>,  //
        &BodyGen::select_with_type<kI32>,             //

        &BodyGen::call<kI32>,           //
        &BodyGen::call_indirect<kI32>,  //
        &BodyGen::call_ref<kI32>,       //
        &BodyGen::try_block<kI32>,      //

        &BodyGen::table_size,   //
        &BodyGen::table_grow);  //

    auto constexpr simd_alternatives =
        CreateArray(&BodyGen::op_with_prefix<kExprV128AnyTrue, kS128>,
                    &BodyGen::op_with_prefix<kExprI8x16AllTrue, kS128>,
                    &BodyGen::op_with_prefix<kExprI8x16BitMask, kS128>,
                    &BodyGen::op_with_prefix<kExprI16x8AllTrue, kS128>,
                    &BodyGen::op_with_prefix<kExprI16x8BitMask, kS128>,
                    &BodyGen::op_with_prefix<kExprI32x4AllTrue, kS128>,
                    &BodyGen::op_with_prefix<kExprI32x4BitMask, kS128>,
                    &BodyGen::op_with_prefix<kExprI64x2AllTrue, kS128>,
                    &BodyGen::op_with_prefix<kExprI64x2BitMask, kS128>,
                    &BodyGen::simd_lane_op<kExprI8x16ExtractLaneS, 16, kS128>,
                    &BodyGen::simd_lane_op<kExprI8x16ExtractLaneU, 16, kS128>,
                    &BodyGen::simd_lane_op<kExprI16x8ExtractLaneS, 8, kS128>,
                    &BodyGen::simd_lane_op<kExprI16x8ExtractLaneU, 8, kS128>,
                    &BodyGen::simd_lane_op<kExprI32x4ExtractLane, 4, kS128>);

    auto constexpr wasmGC_alternatives =
        CreateArray(&BodyGen::i31_get,                     //
                                                           //
                    &BodyGen::struct_get<kI32>,            //
                    &BodyGen::array_get<kI32>,             //
                    &BodyGen::array_len,                   //
                                                           //
                    &BodyGen::ref_is_null,                 //
                    &BodyGen::ref_eq,                      //
                    &BodyGen::ref_test<kExprRefTest>,      //
                    &BodyGen::ref_test<kExprRefTestNull>,  //
                                                           //
                    &BodyGen::string_test,                 //
                    &BodyGen::string_charcodeat,           //
                    &BodyGen::string_codepointat,          //
                    &BodyGen::string_length,               //
                    &BodyGen::string_equals,               //
                    &BodyGen::string_compare,              //
                    &BodyGen::string_intocharcodearray,    //
                    &BodyGen::string_intoutf8array,        //
                    &BodyGen::string_measureutf8);         //

    constexpr auto alternatives = AppendArrayIf<ShouldGenerateWasmGC(options)>(
        AppendArrayIf<ShouldGenerateSIMD(options)>(mvp_alternatives,
                                                   simd_alternatives),
        wasmGC_alternatives);
    GenerateOneOf(alternatives, data);
  }

  void GenerateI64(DataRange* data) {
    GeneratorRecursionScope rec_scope(this);
    if (recursion_limit_reached() || data->size() <= 1) {
      builder_->EmitI64Const(data->getPseudoRandom<int64_t>());
      return;
    }

    constexpr auto mvp_alternatives = CreateArray(
        &BodyGen::i64_const<1>,  //
        &BodyGen::i64_const<2>,  //
        &BodyGen::i64_const<3>,  //
        &BodyGen::i64_const<4>,  //
        &BodyGen::i64_const<5>,  //
        &BodyGen::i64_const<6>,  //
        &BodyGen::i64_const<7>,  //
        &BodyGen::i64_const<8>,  //

        &BodyGen::sequence<kI64, kVoid>,         //
        &BodyGen::sequence<kVoid, kI64>,         //
        &BodyGen::sequence<kVoid, kI64, kVoid>,  //

        &BodyGen::op<kExprI64Add, kI64, kI64>,
        &BodyGen::op<kExprI64Sub, kI64, kI64>,
        &BodyGen::op<kExprI64Mul, kI64, kI64>,

        &BodyGen::op<kExprI64DivS, kI64, kI64>,
        &BodyGen::op<kExprI64DivU, kI64, kI64>,
        &BodyGen::op<kExprI64RemS, kI64, kI64>,
        &BodyGen::op<kExprI64RemU, kI64, kI64>,

        &BodyGen::op<kExprI64And, kI64, kI64>,
        &BodyGen::op<kExprI64Ior, kI64, kI64>,
        &BodyGen::op<kExprI64Xor, kI64, kI64>,
        &BodyGen::op<kExprI64Shl, kI64, kI64>,
        &BodyGen::op<kExprI64ShrU, kI64, kI64>,
        &BodyGen::op<kExprI64ShrS, kI64, kI64>,
        &BodyGen::op<kExprI64Ror, kI64, kI64>,
        &BodyGen::op<kExprI64Rol, kI64, kI64>,

        &BodyGen::op<kExprI64Clz, kI64>,     //
        &BodyGen::op<kExprI64Ctz, kI64>,     //
        &BodyGen::op<kExprI64Popcnt, kI64>,  //

        &BodyGen::op_with_prefix<kExprI64SConvertSatF32, kF32>,
        &BodyGen::op_with_prefix<kExprI64UConvertSatF32, kF32>,
        &BodyGen::op_with_prefix<kExprI64SConvertSatF64, kF64>,
        &BodyGen::op_with_prefix<kExprI64UConvertSatF64, kF64>,

        &BodyGen::block<kI64>,           //
        &BodyGen::loop<kI64>,            //
        &BodyGen::finite_loop<kI64>,     //
        &BodyGen::if_<kI64, kIfElse>,    //
        &BodyGen::br_if<kI64>,           //
        &BodyGen::br_on_null<kI64>,      //
        &BodyGen::br_on_non_null<kI64>,  //
        &BodyGen::br_table<kI64>,        //

        &BodyGen::memop<kExprI64LoadMem>,                               //
        &BodyGen::memop<kExprI64LoadMem8S>,                             //
        &BodyGen::memop<kExprI64LoadMem8U>,                             //
        &BodyGen::memop<kExprI64LoadMem16S>,                            //
        &BodyGen::memop<kExprI64LoadMem16U>,                            //
        &BodyGen::memop<kExprI64LoadMem32S>,                            //
        &BodyGen::memop<kExprI64LoadMem32U>,                            //
                                                                        //
        &BodyGen::memop<kExprI64AtomicLoad>,                            //
        &BodyGen::memop<kExprI64AtomicLoad8U>,                          //
        &BodyGen::memop<kExprI64AtomicLoad16U>,                         //
        &BodyGen::memop<kExprI64AtomicLoad32U>,                         //
        &BodyGen::memop<kExprI64AtomicAdd, kI64>,                       //
        &BodyGen::memop<kExprI64AtomicSub, kI64>,                       //
        &BodyGen::memop<kExprI64AtomicAnd, kI64>,                       //
        &BodyGen::memop<kExprI64AtomicOr, kI64>,                        //
        &BodyGen::memop<kExprI64AtomicXor, kI64>,                       //
        &BodyGen::memop<kExprI64AtomicExchange, kI64>,                  //
        &BodyGen::memop<kExprI64AtomicCompareExchange, kI64, kI64>,     //
        &BodyGen::memop<kExprI64AtomicAdd8U, kI64>,                     //
        &BodyGen::memop<kExprI64AtomicSub8U, kI64>,                     //
        &BodyGen::memop<kExprI64AtomicAnd8U, kI64>,                     //
        &BodyGen::memop<kExprI64AtomicOr8U, kI64>,                      //
        &BodyGen::memop<kExprI64AtomicXor8U, kI64>,                     //
        &BodyGen::memop<kExprI64AtomicExchange8U, kI64>,                //
        &BodyGen::memop<kExprI64AtomicCompareExchange8U, kI64, kI64>,   //
        &BodyGen::memop<kExprI64AtomicAdd16U, kI64>,                    //
        &BodyGen::memop<kExprI64AtomicSub16U, kI64>,                    //
        &BodyGen::memop<kExprI64AtomicAnd16U, kI64>,                    //
        &BodyGen::memop<kExprI64AtomicOr16U, kI64>,                     //
        &BodyGen::memop<kExprI64AtomicXor16U, kI64>,                    //
        &BodyGen::memop<kExprI64AtomicExchange16U, kI64>,               //
        &BodyGen::memop<kExprI64AtomicCompareExchange16U, kI64, kI64>,  //
        &BodyGen::memop<kExprI64AtomicAdd32U, kI64>,                    //
        &BodyGen::memop<kExprI64AtomicSub32U, kI64>,                    //
        &BodyGen::memop<kExprI64AtomicAnd32U, kI64>,                    //
        &BodyGen::memop<kExprI64AtomicOr32U, kI64>,                     //
        &BodyGen::memop<kExprI64AtomicXor32U, kI64>,                    //
        &BodyGen::memop<kExprI64AtomicExchange32U, kI64>,               //
        &BodyGen::memop<kExprI64AtomicCompareExchange32U, kI64, kI64>,  //

        &BodyGen::get_local<kI64>,                    //
        &BodyGen::tee_local<kI64>,                    //
        &BodyGen::get_global<kI64>,                   //
        &BodyGen::op<kExprSelect, kI64, kI64, kI32>,  //
        &BodyGen::select_with_type<kI64>,             //

        &BodyGen::call<kI64>,           //
        &BodyGen::call_indirect<kI64>,  //
        &BodyGen::call_ref<kI64>,       //
        &BodyGen::try_block<kI64>);     //

    auto constexpr simd_alternatives =
        CreateArray(&BodyGen::simd_lane_op<kExprI64x2ExtractLane, 2, kS128>);

    auto constexpr wasmGC_alternatives =
        CreateArray(&BodyGen::struct_get<kI64>,  //
                    &BodyGen::array_get<kI64>);  //

    constexpr auto alternatives = AppendArrayIf<ShouldGenerateWasmGC(options)>(
        AppendArrayIf<ShouldGenerateSIMD(options)>(mvp_alternatives,
                                                   simd_alternatives),
        wasmGC_alternatives);
    GenerateOneOf(alternatives, data);
  }

  void GenerateF32(DataRange* data) {
    GeneratorRecursionScope rec_scope(this);
    if (recursion_limit_reached() || data->size() <= sizeof(float)) {
      builder_->EmitF32Const(data->getPseudoRandom<float>());
      return;
    }

    constexpr auto mvp_alternatives = CreateArray(
        &BodyGen::sequence<kF32, kVoid>, &BodyGen::sequence<kVoid, kF32>,
        &BodyGen::sequence<kVoid, kF32, kVoid>,

        &BodyGen::op<kExprF32Abs, kF32>,             //
        &BodyGen::op<kExprF32Neg, kF32>,             //
        &BodyGen::op<kExprF32Ceil, kF32>,            //
        &BodyGen::op<kExprF32Floor, kF32>,           //
        &BodyGen::op<kExprF32Trunc, kF32>,           //
        &BodyGen::op<kExprF32NearestInt, kF32>,      //
        &BodyGen::op<kExprF32Sqrt, kF32>,            //
        &BodyGen::op<kExprF32Add, kF32, kF32>,       //
        &BodyGen::op<kExprF32Sub, kF32, kF32>,       //
        &BodyGen::op<kExprF32Mul, kF32, kF32>,       //
        &BodyGen::op<kExprF32Div, kF32, kF32>,       //
        &BodyGen::op<kExprF32Min, kF32, kF32>,       //
        &BodyGen::op<kExprF32Max, kF32, kF32>,       //
        &BodyGen::op<kExprF32CopySign, kF32, kF32>,  //

        &BodyGen::op<kExprF32SConvertI32, kI32>,
        &BodyGen::op<kExprF32UConvertI32, kI32>,
        &BodyGen::op<kExprF32SConvertI64, kI64>,
        &BodyGen::op<kExprF32UConvertI64, kI64>,
        &BodyGen::op<kExprF32ConvertF64, kF64>,
        &BodyGen::op<kExprF32ReinterpretI32, kI32>,

        &BodyGen::block<kF32>,           //
        &BodyGen::loop<kF32>,            //
        &BodyGen::finite_loop<kF32>,     //
        &BodyGen::if_<kF32, kIfElse>,    //
        &BodyGen::br_if<kF32>,           //
        &BodyGen::br_on_null<kF32>,      //
        &BodyGen::br_on_non_null<kF32>,  //
        &BodyGen::br_table<kF32>,        //

        &BodyGen::memop<kExprF32LoadMem>,

        &BodyGen::get_local<kF32>,                    //
        &BodyGen::tee_local<kF32>,                    //
        &BodyGen::get_global<kF32>,                   //
        &BodyGen::op<kExprSelect, kF32, kF32, kI32>,  //
        &BodyGen::select_with_type<kF32>,             //

        &BodyGen::call<kF32>,           //
        &BodyGen::call_indirect<kF32>,  //
        &BodyGen::call_ref<kF32>,       //
        &BodyGen::try_block<kF32>);     //

    auto constexpr simd_alternatives =
        CreateArray(&BodyGen::simd_lane_op<kExprF32x4ExtractLane, 4, kS128>);

    auto constexpr wasmGC_alternatives =
        CreateArray(&BodyGen::struct_get<kF32>,  //
                    &BodyGen::array_get<kF32>);  //

    constexpr auto alternatives = AppendArrayIf<ShouldGenerateWasmGC(options)>(
        AppendArrayIf<ShouldGenerateSIMD(options)>(mvp_alternatives,
                                                   simd_alternatives),
        wasmGC_alternatives);
    GenerateOneOf(alternatives, data);
  }

  void GenerateF64(DataRange* data) {
    GeneratorRecursionScope rec_scope(this);
    if (recursion_limit_reached() || data->size() <= sizeof(double)) {
      builder_->EmitF64Const(data->getPseudoRandom<double>());
      return;
    }

    constexpr auto mvp_alternatives = CreateArray(
        &BodyGen::sequence<kF64, kVoid>, &BodyGen::sequence<kVoid, kF64>,
        &BodyGen::sequence<kVoid, kF64, kVoid>,

        &BodyGen::op<kExprF64Abs, kF64>,             //
        &BodyGen::op<kExprF64Neg, kF64>,             //
        &BodyGen::op<kExprF64Ceil, kF64>,            //
        &BodyGen::op<kExprF64Floor, kF64>,           //
        &BodyGen::op<kExprF64Trunc, kF64>,           //
        &BodyGen::op<kExprF64NearestInt, kF64>,      //
        &BodyGen::op<kExprF64Sqrt, kF64>,            //
        &BodyGen::op<kExprF64Add, kF64, kF64>,       //
        &BodyGen::op<kExprF64Sub, kF64, kF64>,       //
        &BodyGen::op<kExprF64Mul, kF64, kF64>,       //
        &BodyGen::op<kExprF64Div, kF64, kF64>,       //
        &BodyGen::op<kExprF64Min, kF64, kF64>,       //
        &BodyGen::op<kExprF64Max, kF64, kF64>,       //
        &BodyGen::op<kExprF64CopySign, kF64, kF64>,  //

        &BodyGen::op<kExprF64SConvertI32, kI32>,
        &BodyGen::op<kExprF64UConvertI32, kI32>,
        &BodyGen::op<kExprF64SConvertI64, kI64>,
        &BodyGen::op<kExprF64UConvertI64, kI64>,
        &BodyGen::op<kExprF64ConvertF32, kF32>,
        &BodyGen::op<kExprF64ReinterpretI64, kI64>,

        &BodyGen::block<kF64>,           //
        &BodyGen::loop<kF64>,            //
        &BodyGen::finite_loop<kF64>,     //
        &BodyGen::if_<kF64, kIfElse>,    //
        &BodyGen::br_if<kF64>,           //
        &BodyGen::br_on_null<kF64>,      //
        &BodyGen::br_on_non_null<kF64>,  //
        &BodyGen::br_table<kF64>,        //

        &BodyGen::memop<kExprF64LoadMem>,

        &BodyGen::get_local<kF64>,                    //
        &BodyGen::tee_local<kF64>,                    //
        &BodyGen::get_global<kF64>,                   //
        &BodyGen::op<kExprSelect, kF64, kF64, kI32>,  //
        &BodyGen::select_with_type<kF64>,             //

        &BodyGen::call<kF64>,           //
        &BodyGen::call_indirect<kF64>,  //
        &BodyGen::call_ref<kF64>,       //
        &BodyGen::try_block<kF64>);     //

    auto constexpr simd_alternatives =
        CreateArray(&BodyGen::simd_lane_op<kExprF64x2ExtractLane, 2, kS128>);

    auto constexpr wasmGC_alternatives =
        CreateArray(&BodyGen::struct_get<kF64>,  //
                    &BodyGen::array_get<kF64>);  //

    constexpr auto alternatives = AppendArrayIf<ShouldGenerateWasmGC(options)>(
        AppendArrayIf<ShouldGenerateSIMD(options)>(mvp_alternatives,
                                                   simd_alternatives),
        wasmGC_alternatives);
    GenerateOneOf(alternatives, data);
  }

  void GenerateS128(DataRange* data) {
    CHECK(ShouldGenerateSIMD(options));
    GeneratorRecursionScope rec_scope(this);
    has_simd_ = true;
    if (recursion_limit_reached() || data->size() <= sizeof(int32_t)) {
      // TODO(v8:8460): v128.const is not implemented yet, and we need a way to
      // "bottom-out", so use a splat to generate this.
      builder_->EmitI32Const(0);
      builder_->EmitWithPrefix(kExprI8x16Splat);
      return;
    }

    constexpr auto alternatives = CreateArray(
        &BodyGen::simd_const,
        &BodyGen::simd_lane_op<kExprI8x16ReplaceLane, 16, kS128, kI32>,
        &BodyGen::simd_lane_op<kExprI16x8ReplaceLane, 8, kS128, kI32>,
        &BodyGen::simd_lane_op<kExprI32x4ReplaceLane, 4, kS128, kI32>,
        &BodyGen::simd_lane_op<kExprI64x2ReplaceLane, 2, kS128, kI64>,
        &BodyGen::simd_lane_op<kExprF32x4ReplaceLane, 4, kS128, kF32>,
        &BodyGen::simd_lane_op<kExprF64x2ReplaceLane, 2, kS128, kF64>,

        &BodyGen::op_with_prefix<kExprI8x16Splat, kI32>,
        &BodyGen::op_with_prefix<kExprI8x16Eq, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16Ne, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16LtS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16LtU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16GtS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16GtU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16LeS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16LeU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16GeS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16GeU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16Abs, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16Neg, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16Shl, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI8x16ShrS, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI8x16ShrU, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI8x16Add, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16AddSatS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16AddSatU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16Sub, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16SubSatS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16SubSatU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16MinS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16MinU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16MaxS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16MaxU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16RoundingAverageU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16Popcnt, kS128>,

        &BodyGen::op_with_prefix<kExprI16x8Splat, kI32>,
        &BodyGen::op_with_prefix<kExprI16x8Eq, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8Ne, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8LtS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8LtU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8GtS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8GtU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8LeS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8LeU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8GeS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8GeU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8Abs, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8Neg, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8Shl, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI16x8ShrS, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI16x8ShrU, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI16x8Add, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8AddSatS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8AddSatU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8Sub, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8SubSatS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8SubSatU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8Mul, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8MinS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8MinU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8MaxS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8MaxU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8RoundingAverageU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8ExtMulLowI8x16S, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8ExtMulLowI8x16U, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8ExtMulHighI8x16S, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8ExtMulHighI8x16U, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8Q15MulRSatS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8ExtAddPairwiseI8x16S, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8ExtAddPairwiseI8x16U, kS128>,

        &BodyGen::op_with_prefix<kExprI32x4Splat, kI32>,
        &BodyGen::op_with_prefix<kExprI32x4Eq, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4Ne, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4LtS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4LtU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4GtS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4GtU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4LeS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4LeU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4GeS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4GeU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4Abs, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4Neg, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4Shl, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI32x4ShrS, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI32x4ShrU, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI32x4Add, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4Sub, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4Mul, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4MinS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4MinU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4MaxS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4MaxU, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4DotI16x8S, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4ExtMulLowI16x8S, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4ExtMulLowI16x8U, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4ExtMulHighI16x8S, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4ExtMulHighI16x8U, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4ExtAddPairwiseI16x8S, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4ExtAddPairwiseI16x8U, kS128>,

        &BodyGen::op_with_prefix<kExprI64x2Splat, kI64>,
        &BodyGen::op_with_prefix<kExprI64x2Eq, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2Ne, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2LtS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2GtS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2LeS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2GeS, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2Abs, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2Neg, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2Shl, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI64x2ShrS, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI64x2ShrU, kS128, kI32>,
        &BodyGen::op_with_prefix<kExprI64x2Add, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2Sub, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2Mul, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2ExtMulLowI32x4S, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2ExtMulLowI32x4U, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2ExtMulHighI32x4S, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2ExtMulHighI32x4U, kS128, kS128>,

        &BodyGen::op_with_prefix<kExprF32x4Splat, kF32>,
        &BodyGen::op_with_prefix<kExprF32x4Eq, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Ne, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Lt, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Gt, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Le, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Ge, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Abs, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Neg, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Sqrt, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Add, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Sub, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Mul, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Div, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Min, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Max, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Pmin, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Pmax, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Ceil, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Floor, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Trunc, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4NearestInt, kS128>,

        &BodyGen::op_with_prefix<kExprF64x2Splat, kF64>,
        &BodyGen::op_with_prefix<kExprF64x2Eq, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Ne, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Lt, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Gt, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Le, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Ge, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Abs, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Neg, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Sqrt, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Add, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Sub, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Mul, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Div, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Min, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Max, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Pmin, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Pmax, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Ceil, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Floor, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Trunc, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2NearestInt, kS128>,

        &BodyGen::op_with_prefix<kExprF64x2PromoteLowF32x4, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2ConvertLowI32x4S, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2ConvertLowI32x4U, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4DemoteF64x2Zero, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4TruncSatF64x2SZero, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4TruncSatF64x2UZero, kS128>,

        &BodyGen::op_with_prefix<kExprI64x2SConvertI32x4Low, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2SConvertI32x4High, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2UConvertI32x4Low, kS128>,
        &BodyGen::op_with_prefix<kExprI64x2UConvertI32x4High, kS128>,

        &BodyGen::op_with_prefix<kExprI32x4SConvertF32x4, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4UConvertF32x4, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4SConvertI32x4, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4UConvertI32x4, kS128>,

        &BodyGen::op_with_prefix<kExprI8x16SConvertI16x8, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16UConvertI16x8, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8SConvertI32x4, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8UConvertI32x4, kS128, kS128>,

        &BodyGen::op_with_prefix<kExprI16x8SConvertI8x16Low, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8SConvertI8x16High, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8UConvertI8x16Low, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8UConvertI8x16High, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4SConvertI16x8Low, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4SConvertI16x8High, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4UConvertI16x8Low, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4UConvertI16x8High, kS128>,

        &BodyGen::op_with_prefix<kExprS128Not, kS128>,
        &BodyGen::op_with_prefix<kExprS128And, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprS128AndNot, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprS128Or, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprS128Xor, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprS128Select, kS128, kS128, kS128>,

        &BodyGen::simd_shuffle,
        &BodyGen::op_with_prefix<kExprI8x16Swizzle, kS128, kS128>,

        &BodyGen::memop<kExprS128LoadMem>,                         //
        &BodyGen::memop<kExprS128Load8x8S>,                        //
        &BodyGen::memop<kExprS128Load8x8U>,                        //
        &BodyGen::memop<kExprS128Load16x4S>,                       //
        &BodyGen::memop<kExprS128Load16x4U>,                       //
        &BodyGen::memop<kExprS128Load32x2S>,                       //
        &BodyGen::memop<kExprS128Load32x2U>,                       //
        &BodyGen::memop<kExprS128Load8Splat>,                      //
        &BodyGen::memop<kExprS128Load16Splat>,                     //
        &BodyGen::memop<kExprS128Load32Splat>,                     //
        &BodyGen::memop<kExprS128Load64Splat>,                     //
        &BodyGen::memop<kExprS128Load32Zero>,                      //
        &BodyGen::memop<kExprS128Load64Zero>,                      //
        &BodyGen::simd_lane_memop<kExprS128Load8Lane, 16, kS128>,  //
        &BodyGen::simd_lane_memop<kExprS128Load16Lane, 8, kS128>,  //
        &BodyGen::simd_lane_memop<kExprS128Load32Lane, 4, kS128>,  //
        &BodyGen::simd_lane_memop<kExprS128Load64Lane, 2, kS128>,  //

        &BodyGen::op_with_prefix<kExprI8x16RelaxedSwizzle, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI8x16RelaxedLaneSelect, kS128, kS128,
                                 kS128>,
        &BodyGen::op_with_prefix<kExprI16x8RelaxedLaneSelect, kS128, kS128,
                                 kS128>,
        &BodyGen::op_with_prefix<kExprI32x4RelaxedLaneSelect, kS128, kS128,
                                 kS128>,
        &BodyGen::op_with_prefix<kExprI64x2RelaxedLaneSelect, kS128, kS128,
                                 kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Qfma, kS128, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4Qfms, kS128, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Qfma, kS128, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2Qfms, kS128, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4RelaxedMin, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF32x4RelaxedMax, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2RelaxedMin, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprF64x2RelaxedMax, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4RelaxedTruncF32x4S, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4RelaxedTruncF32x4U, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4RelaxedTruncF64x2SZero, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4RelaxedTruncF64x2UZero, kS128>,
        &BodyGen::op_with_prefix<kExprI16x8DotI8x16I7x16S, kS128, kS128>,
        &BodyGen::op_with_prefix<kExprI32x4DotI8x16I7x16AddS, kS128, kS128,
                                 kS128>);

    GenerateOneOf(alternatives, data);
  }

  void Generate(ValueType type, DataRange* data) {
    switch (type.kind()) {
      case kVoid:
        return GenerateVoid(data);
      case kI32:
        return GenerateI32(data);
      case kI64:
        return GenerateI64(data);
      case kF32:
        return GenerateF32(data);
      case kF64:
        return GenerateF64(data);
      case kS128:
        return GenerateS128(data);
      case kRefNull:
        return GenerateRef(type.heap_type(), data, kNullable);
      case kRef:
        return GenerateRef(type.heap_type(), data, kNonNullable);
      default:
        UNREACHABLE();
    }
  }

  template <ValueKind kind>
  constexpr void Generate(DataRange* data) {
    switch (kind) {
      case kVoid:
        return GenerateVoid(data);
      case kI32:
        return GenerateI32(data);
      case kI64:
        return GenerateI64(data);
      case kF32:
        return GenerateF32(data);
      case kF64:
        return GenerateF64(data);
      case kS128:
        return GenerateS128(data);
      default:
        // For kRefNull and kRef we need the HeapType which we can get from the
        // ValueType.
        UNREACHABLE();
    }
  }

  template <ValueKind T1, ValueKind T2, ValueKind... Ts>
  void Generate(DataRange* data) {
    // TODO(clemensb): Implement a more even split.
    // TODO(mliedtke): Instead of splitting we should probably "reserve" amount
    // x for the first part, any reserved but potentially unused random bytes
    // should then carry over instead of throwing them away which heavily
    // reduces the amount of actually used random input bytes.
    auto first_data = data->split();
    Generate<T1>(&first_data);
    Generate<T2, Ts...>(data);
  }

  void GenerateRef(HeapType type, DataRange* data,
                   Nullability nullability = kNullable) {
    std::optional<GeneratorRecursionScope> rec_scope;
    if (nullability) {
      rec_scope.emplace(this);
    }

    if (recursion_limit_reached() || data->size() == 0) {
      if (nullability == kNullable) {
        ref_null(type, data);
        return;
      }
      // It is ok not to return here because the non-nullable types are not
      // recursive by construction, so the depth is limited already.
    }

    constexpr auto alternatives_indexed_type =
        CreateArray(&BodyGen::new_object,       //
                    &BodyGen::get_local_ref,    //
                    &BodyGen::array_get_ref,    //
                    &BodyGen::struct_get_ref,   //
                    &BodyGen::ref_cast,         //
                    &BodyGen::ref_as_non_null,  //
                    &BodyGen::br_on_cast);      //

    constexpr auto alternatives_func_any =
        CreateArray(&BodyGen::table_get,           //
                    &BodyGen::get_local_ref,       //
                    &BodyGen::array_get_ref,       //
                    &BodyGen::struct_get_ref,      //
                    &BodyGen::ref_cast,            //
                    &BodyGen::any_convert_extern,  //
                    &BodyGen::ref_as_non_null,     //
                    &BodyGen::br_on_cast);         //

    constexpr auto alternatives_other =
        CreateArray(&BodyGen::array_get_ref,    //
                    &BodyGen::get_local_ref,    //
                    &BodyGen::struct_get_ref,   //
                    &BodyGen::ref_cast,         //
                    &BodyGen::ref_as_non_null,  //
                    &BodyGen::br_on_cast);      //

    switch (type.representation()) {
      // For abstract types, sometimes generate one of their subtypes.
      case HeapType::kAny: {
        // Weighted according to the types in the module:
        // If there are D data types and F function types, the relative
        // frequencies for dataref is D, for funcref F, and for i31ref and
        // falling back to anyref 2.
        const uint8_t num_data_types =
            static_cast<uint8_t>(structs_.size() + arrays_.size());
        const uint8_t emit_i31ref = 2;
        const uint8_t fallback_to_anyref = 2;
        uint8_t random = data->get<uint8_t>() %
                         (num_data_types + emit_i31ref + fallback_to_anyref);
        // We have to compute this first so in case GenerateOneOf fails
        // we will continue to fall back on an alternative that is guaranteed
        // to generate a value of the wanted type.
        // In order to know which alternative to fall back to in case
        // GenerateOneOf failed, the random variable is recomputed.
        if (random >= num_data_types + emit_i31ref) {
          if (GenerateOneOf(alternatives_func_any, type, data, nullability)) {
            return;
          }
          random = data->get<uint8_t>() % (num_data_types + emit_i31ref);
        }
        if (random < structs_.size()) {
          GenerateRef(HeapType(HeapType::kStruct), data, nullability);
        } else if (random < num_data_types) {
          GenerateRef(HeapType(HeapType::kArray), data, nullability);
        } else {
          GenerateRef(HeapType(HeapType::kI31), data, nullability);
        }
        return;
      }
      case HeapType::kArray: {
        constexpr uint8_t fallback_to_dataref = 1;
        uint8_t random =
            data->get<uint8_t>() % (arrays_.size() + fallback_to_dataref);
        // Try generating one of the alternatives and continue to the rest of
        // the methods in case it fails.
        if (random >= arrays_.size()) {
          if (GenerateOneOf(alternatives_other, type, data, nullability))
            return;
          random = data->get<uint8_t>() % arrays_.size();
        }
        uint32_t index = arrays_[random];
        DCHECK(builder_->builder()->IsArrayType(index));
        GenerateRef(HeapType(index), data, nullability);
        return;
      }
      case HeapType::kStruct: {
        constexpr uint8_t fallback_to_dataref = 2;
        uint8_t random =
            data->get<uint8_t>() % (structs_.size() + fallback_to_dataref);
        // Try generating one of the alternatives
        // and continue to the rest of the methods in case it fails.
        if (random >= structs_.size()) {
          if (GenerateOneOf(alternatives_other, type, data, nullability)) {
            return;
          }
          random = data->get<uint8_t>() % structs_.size();
        }
        uint32_t index = structs_[random];
        DCHECK(builder_->builder()->IsStructType(index));
        GenerateRef(HeapType(index), data, nullability);
        return;
      }
      case HeapType::kEq: {
        const uint8_t num_types = arrays_.size() + structs_.size();
        const uint8_t emit_i31ref = 2;
        constexpr uint8_t fallback_to_eqref = 1;
        uint8_t random = data->get<uint8_t>() %
                         (num_types + emit_i31ref + fallback_to_eqref);
        // Try generating one of the alternatives
        // and continue to the rest of the methods in case it fails.
        if (random >= num_types + emit_i31ref) {
          if (GenerateOneOf(alternatives_other, type, data, nullability)) {
            return;
          }
          random = data->get<uint8_t>() % (num_types + emit_i31ref);
        }
        if (random < num_types) {
          // Using `HeapType(random)` here relies on the assumption that struct
          // and array types come before signatures.
          DCHECK(builder_->builder()->IsArrayType(random) ||
                 builder_->builder()->IsStructType(random));
          GenerateRef(HeapType(random), data, nullability);
        } else {
          GenerateRef(HeapType(HeapType::kI31), data, nullability);
        }
        return;
      }
      case HeapType::kFunc: {
        uint32_t random = data->get<uint8_t>() % (functions_.size() + 1);
        /// Try generating one of the alternatives
        // and continue to the rest of the methods in case it fails.
        if (random >= functions_.size()) {
          if (GenerateOneOf(alternatives_func_any, type, data, nullability)) {
            return;
          }
          random = data->get<uint8_t>() % functions_.size();
        }
        uint32_t signature_index = functions_[random];
        DCHECK(builder_->builder()->IsSignature(signature_index));
        GenerateRef(HeapType(signature_index), data, nullability);
        return;
      }
      case HeapType::kI31: {
        // Try generating one of the alternatives
        // and continue to the rest of the methods in case it fails.
        if (data->get<bool>() &&
            GenerateOneOf(alternatives_other, type, data, nullability)) {
          return;
        }
        Generate(kWasmI32, data);
        builder_->EmitWithPrefix(kExprRefI31);
        return;
      }
      case HeapType::kExn: {
        // TODO(manoskouk): Can we somehow come up with a nontrivial exnref?
        ref_null(type, data);
        if (nullability == kNonNullable) {
          builder_->Emit(kExprRefAsNonNull);
        }
        return;
      }
      case HeapType::kExtern: {
        uint8_t choice = data->get<uint8_t>();
        if (choice < 25) {
          // ~10% chance of extern.convert_any.
          GenerateRef(HeapType(HeapType::kAny), data);
          builder_->EmitWithPrefix(kExprExternConvertAny);
          if (nullability == kNonNullable) {
            builder_->Emit(kExprRefAsNonNull);
          }
          return;
        }
        // ~80% chance of string.
        if (choice < 230 && ShouldGenerateWasmGC(options)) {
          uint8_t subchoice = choice % 7;
          switch (subchoice) {
            case 0:
              return string_cast(data);
            case 1:
              return string_fromcharcode(data);
            case 2:
              return string_fromcodepoint(data);
            case 3:
              return string_concat(data);
            case 4:
              return string_substring(data);
            case 5:
              return string_fromcharcodearray(data);
            case 6:
              return string_fromutf8array(data);
          }
        }
        // ~10% chance of fallthrough.
        [[fallthrough]];
      }
      case HeapType::kNoExtern:
      case HeapType::kNoFunc:
      case HeapType::kNone:
      case HeapType::kNoExn:
        ref_null(type, data);
        if (nullability == kNonNullable) {
          builder_->Emit(kExprRefAsNonNull);
        }
        return;
      default:
        // Indexed type (i.e. user-defined type).
        DCHECK(type.is_index());
        if (ShouldGenerateWasmGC(options) &&
            type.ref_index() == string_imports_.array_i8 &&
            data->get<uint8_t>() < 32) {
          // 1/8th chance, fits the number of remaining alternatives (7) well.
          return string_toutf8array(data);
        }
        GenerateOneOf(alternatives_indexed_type, type, data, nullability);
        return;
    }
    UNREACHABLE();
  }

  void GenerateRef(DataRange* data) {
    constexpr HeapType::Representation top_types[] = {
        HeapType::kAny,
        HeapType::kFunc,
        HeapType::kExtern,
    };
    HeapType::Representation type =
        top_types[data->get<uint8_t>() % arraysize(top_types)];
    GenerateRef(HeapType(type), data);
  }

  std::vector<ValueType> GenerateTypes(DataRange* data) {
    return fuzzing::GenerateTypes<options>(
        data, static_cast<uint32_t>(functions_.size() + structs_.size() +
                                    arrays_.size()));
  }

  void Generate(base::Vector<const ValueType> types, DataRange* data) {
    // Maybe emit a multi-value block with the expected return type. Use a
    // non-default value to indicate block generation to avoid recursion when we
    // reach the end of the data.
    bool generate_block = data->get<uint8_t>() % 32 == 1;
    if (generate_block) {
      GeneratorRecursionScope rec_scope(this);
      if (!recursion_limit_reached()) {
        const auto param_types = GenerateTypes(data);
        Generate(base::VectorOf(param_types), data);
        any_block(base::VectorOf(param_types), types, data);
        return;
      }
    }

    if (types.size() == 0) {
      Generate(kWasmVoid, data);
      return;
    }
    if (types.size() == 1) {
      Generate(types[0], data);
      return;
    }

    // Split the types in two halves and recursively generate each half.
    // Each half is non empty to ensure termination.
    size_t split_index = data->get<uint8_t>() % (types.size() - 1) + 1;
    base::Vector<const ValueType> lower_half = types.SubVector(0, split_index);
    base::Vector<const ValueType> upper_half =
        types.SubVector(split_index, types.size());
    DataRange first_range = data->split();
    Generate(lower_half, &first_range);
    Generate(upper_half, data);
  }

  void Consume(ValueType type) {
    // Try to store the value in a local if there is a local with the same
    // type. TODO(14034): For reference types a local with a super type
    // would also be fine.
    size_t num_params = builder_->signature()->parameter_count();
    for (uint32_t local_offset = 0; local_offset < locals_.size();
         ++local_offset) {
      if (locals_[local_offset] == type) {
        uint32_t local_index = static_cast<uint32_t>(local_offset + num_params);
        builder_->EmitWithU32V(kExprLocalSet, local_index);
        return;
      }
    }
    for (uint32_t param_index = 0; param_index < num_params; ++param_index) {
      if (builder_->signature()->GetParam(param_index) == type) {
        builder_->EmitWithU32V(kExprLocalSet, param_index);
        return;
      }
    }
    // No opportunity found to use the value, so just drop it.
    builder_->Emit(kExprDrop);
  }

  // Emit code to match an arbitrary signature.
  // TODO(11954): Add the missing reference type conversion/upcasting.
  void ConsumeAndGenerate(base::Vector<const ValueType> param_types,
                          base::Vector<const ValueType> return_types,
                          DataRange* data) {
    // This numeric conversion logic consists of picking exactly one
    // index in the return values and dropping all the values that come
    // before that index. Then we convert the value from that index to the
    // wanted type. If we don't find any value we generate it.
    auto primitive = [](ValueType t) -> bool {
      switch (t.kind()) {
        case kI32:
        case kI64:
        case kF32:
        case kF64:
          return true;
        default:
          return false;
      }
    };

    if (return_types.size() == 0 || param_types.size() == 0 ||
        !primitive(return_types[0])) {
      for (auto iter = param_types.rbegin(); iter != param_types.rend();
           ++iter) {
        Consume(*iter);
      }
      Generate(return_types, data);
      return;
    }

    int bottom_primitives = 0;

    while (static_cast<int>(param_types.size()) > bottom_primitives &&
           primitive(param_types[bottom_primitives])) {
      bottom_primitives++;
    }
    int return_index =
        bottom_primitives > 0 ? (data->get<uint8_t>() % bottom_primitives) : -1;
    for (int i = static_cast<int>(param_types.size() - 1); i > return_index;
         --i) {
      Consume(param_types[i]);
    }
    for (int i = return_index; i > 0; --i) {
      Convert(param_types[i], param_types[i - 1]);
      builder_->EmitI32Const(0);
      builder_->Emit(kExprSelect);
    }
    DCHECK(!return_types.empty());
    if (return_index >= 0) {
      Convert(param_types[0], return_types[0]);
      Generate(return_types + 1, data);
    } else {
      Generate(return_types, data);
    }
  }

  bool HasSimd() { return has_simd_; }

  void InitializeNonDefaultableLocals(DataRange* data) {
    for (uint32_t i = 0; i < locals_.size(); i++) {
      if (!locals_[i].is_defaultable()) {
        GenerateRef(locals_[i].heap_type(), data, kNonNullable);
        builder_->EmitWithU32V(
            kExprLocalSet, i + static_cast<uint32_t>(
                                   builder_->signature()->parameter_count()));
      }
    }
    locals_initialized_ = true;
  }

 private:
  WasmFunctionBuilder* builder_;
  std::vector<std::vector<ValueType>> blocks_;
  const std::vector<uint32_t>& functions_;
  std::vector<ValueType> locals_;
  std::vector<ValueType> globals_;
  std::vector<uint8_t> mutable_globals_;  // indexes into {globals_}.
  uint32_t recursion_depth = 0;
  std::vector<int> catch_blocks_;
  bool has_simd_;
  const std::vector<uint32_t>& structs_;
  const std::vector<uint32_t>& arrays_;
  const StringImports& string_imports_;
  bool locals_initialized_;

  bool recursion_limit_reached() {
    return recursion_depth >= kMaxRecursionDepth;
  }
};

WasmInitExpr GenerateInitExpr(Zone* zone, DataRange& range,
                              WasmModuleBuilder* builder, ValueType type,
                              const std::vector<uint32_t>& structs,
                              const std::vector<uint32_t>& arrays,
                              uint32_t recursion_depth);

template <WasmModuleGenerationOptions options>
class ModuleGen {
 public:
  explicit ModuleGen(Zone* zone, WasmModuleBuilder* fn, DataRange* module_range,
                     uint8_t num_functions, uint8_t num_structs,
                     uint8_t num_arrays, uint8_t num_signatures)
      : zone_(zone),
        builder_(fn),
        module_range_(module_range),
        num_functions_(num_functions),
        num_structs_(num_structs),
        num_arrays_(num_arrays),
        num_signatures_(num_signatures),
        num_types_(num_signatures + num_structs + num_arrays) {}

  // Generates and adds random number of memories.
  void GenerateRandomMemories() {
    int random_uint8_t = module_range_->get<uint8_t>();
    static_assert(
        kMaxMemories <= 5,
        "Too many memories. Use more random bits to chose their types!");
    // Use the lower 3 bits to get the number of memories.
    int num_memories = 1 + ((random_uint8_t & 7) % kMaxMemories);
    // Use the unused upper 5 bits to decide about each memory's type.
    random_uint8_t >>= 3;
    for (int i = 0; i < num_memories; i++) {
      (random_uint8_t & 1) ? builder_->AddMemory64(0, kMaxMemorySize)
                           : builder_->AddMemory(0, kMaxMemorySize);
      random_uint8_t >>= 1;
    }
  }

  // Puts the types into random recursive groups.
  std::map<uint8_t, uint8_t> GenerateRandomRecursiveGroups(
      uint8_t kNumDefaultArrayTypes) {
    // (Type_index -> end of explicit rec group).
    std::map<uint8_t, uint8_t> explicit_rec_groups;
    uint8_t current_type_index = 0;

    // The default array types are each in their own recgroup.
    for (uint8_t i = 0; i < kNumDefaultArrayTypes; i++) {
      explicit_rec_groups.emplace(current_type_index, current_type_index);
      builder_->AddRecursiveTypeGroup(current_type_index++, 1);
    }

    while (current_type_index < num_types_) {
      // First, pick a random start for the next group. We allow it to be
      // beyond the end of types (i.e., we add no further recursive groups).
      uint8_t group_start = module_range_->get<uint8_t>() %
                                (num_types_ - current_type_index + 1) +
                            current_type_index;
      DCHECK_GE(group_start, current_type_index);
      current_type_index = group_start;
      if (group_start < num_types_) {
        // If we did not reach the end of the types, pick a random group size.
        uint8_t group_size =
            module_range_->get<uint8_t>() % (num_types_ - group_start) + 1;
        DCHECK_LE(group_start + group_size, num_types_);
        for (uint8_t i = group_start; i < group_start + group_size; i++) {
          explicit_rec_groups.emplace(i, group_start + group_size - 1);
        }
        builder_->AddRecursiveTypeGroup(group_start, group_size);
        current_type_index += group_size;
      }
    }
    return explicit_rec_groups;
  }

  // Generates and adds random struct types.
  void GenerateRandomStructs(
      const std::map<uint8_t, uint8_t>& explicit_rec_groups,
      std::vector<uint32_t>& struct_types, uint8_t& current_type_index,
      uint8_t kNumDefaultArrayTypes) {
    uint8_t last_struct_type_index = current_type_index + num_structs_;
    for (; current_type_index < last_struct_type_index; current_type_index++) {
      auto rec_group = explicit_rec_groups.find(current_type_index);
      uint8_t current_rec_group_end = rec_group != explicit_rec_groups.end()
                                          ? rec_group->second
                                          : current_type_index;

      uint32_t supertype = kNoSuperType;
      uint8_t num_fields =
          module_range_->get<uint8_t>() % (kMaxStructFields + 1);

      uint8_t existing_struct_types =
          current_type_index - kNumDefaultArrayTypes;
      if (existing_struct_types > 0 && module_range_->get<bool>()) {
        supertype = module_range_->get<uint8_t>() % existing_struct_types +
                    kNumDefaultArrayTypes;
        num_fields += builder_->GetStructType(supertype)->field_count();
      }
      StructType::Builder struct_builder(zone_, num_fields);

      // Add all fields from super type.
      uint32_t field_index = 0;
      if (supertype != kNoSuperType) {
        const StructType* parent = builder_->GetStructType(supertype);
        for (; field_index < parent->field_count(); ++field_index) {
          // TODO(14034): This could also be any sub type of the supertype's
          // element type.
          struct_builder.AddField(parent->field(field_index),
                                  parent->mutability(field_index));
        }
      }
      for (; field_index < num_fields; field_index++) {
        // Notes:
        // - We allow a type to only have non-nullable fields of types that
        //   are defined earlier. This way we avoid infinite non-nullable
        //   constructions. Also relevant for arrays and functions.
        // - On the other hand, nullable fields can be picked up to the end of
        //   the current recursive group.
        // - We exclude the non-nullable generic types arrayref, anyref,
        //   structref, eqref and externref from the fields of structs and
        //   arrays. This is so that GenerateInitExpr has a way to break a
        //   recursion between a struct/array field and those types
        //   ((ref extern) gets materialized through (ref any)).
        ValueType type = GetValueTypeHelper<options>(
            module_range_, current_rec_group_end + 1, current_type_index,
            kIncludeNumericTypes, kIncludePackedTypes, kExcludeSomeGenerics);

        bool mutability = module_range_->get<bool>();
        struct_builder.AddField(type, mutability);
      }
      StructType* struct_fuz = struct_builder.Build();
      // TODO(14034): Generate some final types too.
      uint32_t index = builder_->AddStructType(struct_fuz, false, supertype);
      struct_types.push_back(index);
    }
  }

  // Creates and adds random array types.
  void GenerateRandomArrays(
      const std::map<uint8_t, uint8_t>& explicit_rec_groups,
      std::vector<uint32_t>& array_types, uint8_t& current_type_index) {
    uint8_t last_struct_type_index = current_type_index + num_structs_;
    for (; current_type_index < num_structs_ + num_arrays_;
         current_type_index++) {
      auto rec_group = explicit_rec_groups.find(current_type_index);
      uint8_t current_rec_group_end = rec_group != explicit_rec_groups.end()
                                          ? rec_group->second
                                          : current_type_index;
      ValueType type = GetValueTypeHelper<options>(
          module_range_, current_rec_group_end + 1, current_type_index,
          kIncludeNumericTypes, kIncludePackedTypes, kExcludeSomeGenerics);
      uint32_t supertype = kNoSuperType;
      if (current_type_index > last_struct_type_index &&
          module_range_->get<bool>()) {
        // Do not include the default array types, because they are final.
        uint8_t existing_array_types =
            current_type_index - last_struct_type_index;
        supertype = last_struct_type_index +
                    (module_range_->get<uint8_t>() % existing_array_types);
        // TODO(14034): This could also be any sub type of the supertype's
        // element type.
        type = builder_->GetArrayType(supertype)->element_type();
      }
      ArrayType* array_fuz = zone_->New<ArrayType>(type, true);
      // TODO(14034): Generate some final types too.
      uint32_t index = builder_->AddArrayType(array_fuz, false, supertype);
      array_types.push_back(index);
    }
  }

  enum SigKind { kFunctionSig, kExceptionSig };

  FunctionSig* GenerateSig(SigKind sig_kind, int num_types) {
    // Generate enough parameters to spill some to the stack.
    int num_params = int{module_range_->get<uint8_t>()} % (kMaxParameters + 1);
    int num_returns =
        sig_kind == kFunctionSig
            ? int{module_range_->get<uint8_t>()} % (kMaxReturns + 1)
            : 0;

    FunctionSig::Builder builder(zone_, num_returns, num_params);
    for (int i = 0; i < num_returns; ++i) {
      builder.AddReturn(GetValueType<options>(module_range_, num_types));
    }
    for (int i = 0; i < num_params; ++i) {
      builder.AddParam(GetValueType<options>(module_range_, num_types));
    }
    return builder.Build();
  }

  // Creates and adds random function signatures.
  void GenerateRandomFunctionSigs(
      const std::map<uint8_t, uint8_t>& explicit_rec_groups,
      std::vector<uint32_t>& function_signatures, uint8_t& current_type_index,
      bool kIsFinal) {
    // Recursive groups consist of recursive types that came with the WasmGC
    // proposal.
    DCHECK_IMPLIES(!ShouldGenerateWasmGC(options), explicit_rec_groups.empty());

    for (; current_type_index < num_types_; current_type_index++) {
      auto rec_group = explicit_rec_groups.find(current_type_index);
      uint8_t current_rec_group_end = rec_group != explicit_rec_groups.end()
                                          ? rec_group->second
                                          : current_type_index;
      FunctionSig* sig = GenerateSig(kFunctionSig, current_rec_group_end + 1);
      uint32_t signature_index = builder_->ForceAddSignature(sig, kIsFinal);
      function_signatures.push_back(signature_index);
    }
  }

  void GenerateRandomExceptions(uint8_t num_exceptions) {
    for (int i = 0; i < num_exceptions; ++i) {
      FunctionSig* sig = GenerateSig(kExceptionSig, num_types_);
      builder_->AddTag(sig);
    }
  }

  // Adds the "wasm:js-string" imports to the module.
  StringImports AddImportedStringImports() {
    static constexpr uint32_t kArrayI8 = 0;
    static constexpr uint32_t kArrayI16 = 1;
    StringImports strings;
    strings.array_i8 = kArrayI8;
    strings.array_i16 = kArrayI16;
    static constexpr ValueType kRefExtern = ValueType::Ref(HeapType::kExtern);
    static constexpr ValueType kExternRef = kWasmExternRef;
    static constexpr ValueType kI32 = kWasmI32;
    static constexpr ValueType kRefA8 = ValueType::Ref(kArrayI8);
    static constexpr ValueType kRefNullA8 = ValueType::RefNull(kArrayI8);
    static constexpr ValueType kRefNullA16 = ValueType::RefNull(kArrayI16);

    // Shorthands: "r" = nullable "externref",
    // "e" = non-nullable "ref extern".
    static constexpr ValueType kReps_e_i[] = {kRefExtern, kI32};
    static constexpr ValueType kReps_e_rr[] = {kRefExtern, kExternRef,
                                               kExternRef};
    static constexpr ValueType kReps_e_rii[] = {kRefExtern, kExternRef, kI32,
                                                kI32};
    static constexpr ValueType kReps_i_ri[] = {kI32, kExternRef, kI32};
    static constexpr ValueType kReps_i_rr[] = {kI32, kExternRef, kExternRef};
    static constexpr ValueType kReps_from_a16[] = {kRefExtern, kRefNullA16,
                                                   kI32, kI32};
    static constexpr ValueType kReps_from_a8[] = {kRefExtern, kRefNullA8, kI32,
                                                  kI32};
    static constexpr ValueType kReps_into_a16[] = {kI32, kExternRef,
                                                   kRefNullA16, kI32};
    static constexpr ValueType kReps_into_a8[] = {kI32, kExternRef, kRefNullA8,
                                                  kI32};
    static constexpr ValueType kReps_to_a8[] = {kRefA8, kExternRef};

    static constexpr FunctionSig kSig_e_i(1, 1, kReps_e_i);
    static constexpr FunctionSig kSig_e_r(1, 1, kReps_e_rr);
    static constexpr FunctionSig kSig_e_rr(1, 2, kReps_e_rr);
    static constexpr FunctionSig kSig_e_rii(1, 3, kReps_e_rii);

    static constexpr FunctionSig kSig_i_r(1, 1, kReps_i_ri);
    static constexpr FunctionSig kSig_i_ri(1, 2, kReps_i_ri);
    static constexpr FunctionSig kSig_i_rr(1, 2, kReps_i_rr);
    static constexpr FunctionSig kSig_from_a16(1, 3, kReps_from_a16);
    static constexpr FunctionSig kSig_from_a8(1, 3, kReps_from_a8);
    static constexpr FunctionSig kSig_into_a16(1, 3, kReps_into_a16);
    static constexpr FunctionSig kSig_into_a8(1, 3, kReps_into_a8);
    static constexpr FunctionSig kSig_to_a8(1, 1, kReps_to_a8);

    static constexpr base::Vector<const char> kJsString =
        base::StaticCharVector("wasm:js-string");
    static constexpr base::Vector<const char> kTextDecoder =
        base::StaticCharVector("wasm:text-decoder");
    static constexpr base::Vector<const char> kTextEncoder =
        base::StaticCharVector("wasm:text-encoder");

#define STRINGFUNC(name, sig, group) \
  strings.name = builder_->AddImport(base::CStrVector(#name), &sig, group)

    STRINGFUNC(cast, kSig_e_r, kJsString);
    STRINGFUNC(test, kSig_i_r, kJsString);
    STRINGFUNC(fromCharCode, kSig_e_i, kJsString);
    STRINGFUNC(fromCodePoint, kSig_e_i, kJsString);
    STRINGFUNC(charCodeAt, kSig_i_ri, kJsString);
    STRINGFUNC(codePointAt, kSig_i_ri, kJsString);
    STRINGFUNC(length, kSig_i_r, kJsString);
    STRINGFUNC(concat, kSig_e_rr, kJsString);
    STRINGFUNC(substring, kSig_e_rii, kJsString);
    STRINGFUNC(equals, kSig_i_rr, kJsString);
    STRINGFUNC(compare, kSig_i_rr, kJsString);
    STRINGFUNC(fromCharCodeArray, kSig_from_a16, kJsString);
    STRINGFUNC(intoCharCodeArray, kSig_into_a16, kJsString);
    STRINGFUNC(measureStringAsUTF8, kSig_i_r, kTextEncoder);
    STRINGFUNC(encodeStringIntoUTF8Array, kSig_into_a8, kTextEncoder);
    STRINGFUNC(encodeStringToUTF8Array, kSig_to_a8, kTextEncoder);
    STRINGFUNC(decodeStringFromUTF8Array, kSig_from_a8, kTextDecoder);

#undef STRINGFUNC

    return strings;
  }

  // Creates and adds random tables.
  void GenerateRandomTables(const std::vector<uint32_t>& array_types,
                            const std::vector<uint32_t>& struct_types) {
    int num_tables = module_range_->get<uint8_t>() % kMaxTables + 1;
    for (int i = 0; i < num_tables; i++) {
      uint32_t min_size = i == 0
                              ? num_functions_
                              : module_range_->get<uint8_t>() % kMaxTableSize;
      uint32_t max_size =
          module_range_->get<uint8_t>() % (kMaxTableSize - min_size) + min_size;
      // Table 0 is always funcref. This guarantees that
      // - call_indirect has at least one funcref table to work with,
      // - we have a place to reference all functions in the program, so they
      //   count as "declared" for ref.func.
      bool force_funcref = i == 0;
      ValueType type =
          force_funcref
              ? kWasmFuncRef
              : GetValueTypeHelper<options>(
                    module_range_, num_types_, num_types_, kExcludeNumericTypes,
                    kExcludePackedTypes, kIncludeAllGenerics);
      bool use_initializer =
          !type.is_defaultable() || module_range_->get<bool>();
      uint32_t table_index =
          use_initializer
              ? builder_->AddTable(
                    type, min_size, max_size,
                    GenerateInitExpr(zone_, *module_range_, builder_, type,
                                     struct_types, array_types, 0))
              : builder_->AddTable(type, min_size, max_size);
      if (type.is_reference_to(HeapType::kFunc)) {
        // For function tables, initialize them with functions from the program.
        // Currently, the fuzzer assumes that every funcref/(ref func) table
        // contains the functions in the program in the order they are defined.
        // TODO(11954): Consider generalizing this.
        WasmModuleBuilder::WasmElemSegment segment(zone_, type, table_index,
                                                   WasmInitExpr(0));
        for (int entry_index = 0; entry_index < static_cast<int>(min_size);
             entry_index++) {
          segment.entries.emplace_back(
              WasmModuleBuilder::WasmElemSegment::Entry::kRefFuncEntry,
              builder_->NumImportedFunctions() +
                  (entry_index % num_functions_));
        }
        builder_->AddElementSegment(std::move(segment));
      }
    }
  }

  // Creates and adds random globals.
  std::tuple<std::vector<ValueType>, std::vector<uint8_t>>
  GenerateRandomGlobals(const std::vector<uint32_t>& array_types,
                        const std::vector<uint32_t>& struct_types) {
    int num_globals = module_range_->get<uint8_t>() % (kMaxGlobals + 1);
    std::vector<ValueType> globals;
    std::vector<uint8_t> mutable_globals;
    globals.reserve(num_globals);
    mutable_globals.reserve(num_globals);

    for (int i = 0; i < num_globals; ++i) {
      ValueType type = GetValueType<options>(module_range_, num_types_);
      // 1/8 of globals are immutable.
      const bool mutability = (module_range_->get<uint8_t>() % 8) != 0;
      builder_->AddGlobal(type, mutability,
                          GenerateInitExpr(zone_, *module_range_, builder_,
                                           type, struct_types, array_types, 0));
      globals.push_back(type);
      if (mutability) mutable_globals.push_back(static_cast<uint8_t>(i));
    }

    return {globals, mutable_globals};
  }

 private:
  Zone* zone_;
  WasmModuleBuilder* builder_;
  DataRange* module_range_;
  const uint8_t num_functions_;
  const uint8_t num_structs_;
  const uint8_t num_arrays_;
  const uint8_t num_signatures_;
  const uint16_t num_types_;
};

WasmInitExpr GenerateStructNewInitExpr(Zone* zone, DataRange& range,
                                       WasmModuleBuilder* builder,
                                       uint32_t index,
                                       const std::vector<uint32_t>& structs,
                                       const std::vector<uint32_t>& arrays,
                                       uint32_t recursion_depth) {
  const StructType* struct_type = builder->GetStructType(index);
  bool use_new_default =
      std::all_of(struct_type->fields().begin(), struct_type->fields().end(),
                  [](ValueType type) { return type.is_defaultable(); }) &&
      range.get<bool>();

  if (use_new_default) {
    return WasmInitExpr::StructNewDefault(index);
  } else {
    ZoneVector<WasmInitExpr>* elements =
        zone->New<ZoneVector<WasmInitExpr>>(zone);
    int field_count = struct_type->field_count();
    for (int field_index = 0; field_index < field_count; field_index++) {
      elements->push_back(GenerateInitExpr(
          zone, range, builder, struct_type->field(field_index), structs,
          arrays, recursion_depth + 1));
    }
    return WasmInitExpr::StructNew(index, elements);
  }
}

WasmInitExpr GenerateArrayInitExpr(Zone* zone, DataRange& range,
                                   WasmModuleBuilder* builder, uint32_t index,
                                   const std::vector<uint32_t>& structs,
                                   const std::vector<uint32_t>& arrays,
                                   uint32_t recursion_depth) {
  constexpr int kMaxArrayLength = 20;
  uint8_t choice = range.get<uint8_t>() % 3;
  ValueType element_type = builder->GetArrayType(index)->element_type();
  if (choice == 0) {
    size_t element_count = range.get<uint8_t>() % kMaxArrayLength;
    if (!element_type.is_defaultable()) {
      // If the element type is not defaultable, limit the size to 0 or 1
      // to prevent having to create too many such elements on the value
      // stack. (With multiple non-nullable references, this can explode
      // in size very quickly.)
      element_count %= 2;
    }
    ZoneVector<WasmInitExpr>* elements =
        zone->New<ZoneVector<WasmInitExpr>>(zone);
    for (size_t i = 0; i < element_count; i++) {
      elements->push_back(GenerateInitExpr(zone, range, builder, element_type,
                                           structs, arrays,
                                           recursion_depth + 1));
    }
    return WasmInitExpr::ArrayNewFixed(index, elements);
  } else if (choice == 1 || !element_type.is_defaultable()) {
    // TODO(14034): Add other int expressions to length (same below).
    WasmInitExpr length = WasmInitExpr(range.get<uint8_t>() % kMaxArrayLength);
    WasmInitExpr init = GenerateInitExpr(zone, range, builder, element_type,
                                         structs, arrays, recursion_depth + 1);
    return WasmInitExpr::ArrayNew(zone, index, init, length);
  } else {
    WasmInitExpr length = WasmInitExpr(range.get<uint8_t>() % kMaxArrayLength);
    return WasmInitExpr::ArrayNewDefault(zone, index, length);
  }
}

WasmInitExpr GenerateInitExpr(Zone* zone, DataRange& range,
                              WasmModuleBuilder* builder, ValueType type,
                              const std::vector<uint32_t>& structs,
                              const std::vector<uint32_t>& arrays,
                              uint32_t recursion_depth) {
  switch (type.kind()) {
    case kI8:
    case kI16:
    case kI32: {
      if (range.size() == 0 || recursion_depth >= kMaxRecursionDepth) {
        return WasmInitExpr(int32_t{0});
      }
      // 50% to generate a constant, 50% to generate a binary operator.
      uint8_t choice = range.get<uint8_t>() % 6;
      switch (choice) {
        case 0:
        case 1:
        case 2:
          if (choice % 2 == 0 && builder->NumGlobals()) {
            // Search for a matching global to emit a global.get.
            int num_globals = builder->NumGlobals();
            int start_index = range.get<uint8_t>() % num_globals;
            for (int i = 0; i < num_globals; ++i) {
              int index = (start_index + i) % num_globals;
              if (builder->GetGlobalType(index) == type &&
                  !builder->IsMutableGlobal(index)) {
                return WasmInitExpr::GlobalGet(index);
              }
            }
            // Fall back to constant if no matching global was found.
          }
          return WasmInitExpr(range.getPseudoRandom<int32_t>());
        default:
          WasmInitExpr::Operator op = choice == 3   ? WasmInitExpr::kI32Add
                                      : choice == 4 ? WasmInitExpr::kI32Sub
                                                    : WasmInitExpr::kI32Mul;
          return WasmInitExpr::Binop(
              zone, op,
              GenerateInitExpr(zone, range, builder, kWasmI32, structs, arrays,
                               recursion_depth + 1),
              GenerateInitExpr(zone, range, builder, kWasmI32, structs, arrays,
                               recursion_depth + 1));
      }
    }
    case kI64: {
      if (range.size() == 0 || recursion_depth >= kMaxRecursionDepth) {
        return WasmInitExpr(int64_t{0});
      }
      // 50% to generate a constant, 50% to generate a binary operator.
      uint8_t choice = range.get<uint8_t>() % 6;
      switch (choice) {
        case 0:
        case 1:
        case 2:
          return WasmInitExpr(range.get<int64_t>());
        default:
          WasmInitExpr::Operator op = choice == 3   ? WasmInitExpr::kI64Add
                                      : choice == 4 ? WasmInitExpr::kI64Sub
                                                    : WasmInitExpr::kI64Mul;
          return WasmInitExpr::Binop(
              zone, op,
              GenerateInitExpr(zone, range, builder, kWasmI64, structs, arrays,
                               recursion_depth + 1),
              GenerateInitExpr(zone, range, builder, kWasmI64, structs, arrays,
                               recursion_depth + 1));
      }
    }
    case kF16:
    case kF32:
      return WasmInitExpr(0.0f);
    case kF64:
      return WasmInitExpr(0.0);
    case kS128: {
      uint8_t s128_const[kSimd128Size] = {0};
      return WasmInitExpr(s128_const);
    }
    case kRefNull: {
      bool null_only = false;
      switch (type.heap_representation()) {
        case HeapType::kNone:
        case HeapType::kNoFunc:
        case HeapType::kNoExtern:
          null_only = true;
          break;
        default:
          break;
      }
      if (range.size() == 0 || recursion_depth >= kMaxRecursionDepth ||
          null_only || (range.get<uint8_t>() % 4 == 0)) {
        return WasmInitExpr::RefNullConst(type.heap_type().representation());
      }
      [[fallthrough]];
    }
    case kRef: {
      switch (type.heap_representation()) {
        case HeapType::kStruct: {
          uint32_t index = structs[range.get<uint8_t>() % structs.size()];
          return GenerateStructNewInitExpr(zone, range, builder, index, structs,
                                           arrays, recursion_depth);
        }
        case HeapType::kAny: {
          // Do not use 0 as the determining value here, otherwise an exhausted
          // {range} will generate an infinite recursion with the {kExtern}
          // case.
          if (recursion_depth < kMaxRecursionDepth && range.size() > 0 &&
              range.get<uint8_t>() % 4 == 3) {
            return WasmInitExpr::AnyConvertExtern(
                zone,
                GenerateInitExpr(zone, range, builder,
                                 ValueType::RefMaybeNull(HeapType::kExtern,
                                                         type.nullability()),
                                 structs, arrays, recursion_depth + 1));
          }
          [[fallthrough]];
        }
        case HeapType::kEq: {
          uint8_t choice = range.get<uint8_t>() % 3;
          HeapType::Representation subtype = choice == 0   ? HeapType::kI31
                                             : choice == 1 ? HeapType::kArray
                                                           : HeapType::kStruct;

          return GenerateInitExpr(
              zone, range, builder,
              ValueType::RefMaybeNull(subtype, type.nullability()), structs,
              arrays, recursion_depth);
        }
        case HeapType::kFunc: {
          uint32_t index =
              range.get<uint32_t>() % (builder->NumDeclaredFunctions() +
                                       builder->NumImportedFunctions());
          return WasmInitExpr::RefFuncConst(index);
        }
        case HeapType::kExtern:
          return WasmInitExpr::ExternConvertAny(
              zone, GenerateInitExpr(zone, range, builder,
                                     ValueType::RefMaybeNull(
                                         HeapType::kAny, type.nullability()),
                                     structs, arrays, recursion_depth + 1));
        case HeapType::kI31:
          return WasmInitExpr::RefI31(
              zone, GenerateInitExpr(zone, range, builder, kWasmI32, structs,
                                     arrays, recursion_depth + 1));
        case HeapType::kArray: {
          uint32_t index = arrays[range.get<uint8_t>() % arrays.size()];
          return GenerateArrayInitExpr(zone, range, builder, index, structs,
                                       arrays, recursion_depth);
        }
        case HeapType::kNone:
        case HeapType::kNoFunc:
        case HeapType::kNoExtern:
          UNREACHABLE();
        default: {
          uint32_t index = type.ref_index();
          if (builder->IsStructType(index)) {
            return GenerateStructNewInitExpr(zone, range, builder, index,
                                             structs, arrays, recursion_depth);
          } else if (builder->IsArrayType(index)) {
            return GenerateArrayInitExpr(zone, range, builder, index, structs,
                                         arrays, recursion_depth);
          } else {
            DCHECK(builder->IsSignature(index));
            for (int i = 0; i < builder->NumDeclaredFunctions(); ++i) {
              if (builder->GetFunction(i)->sig_index() == index) {
                return WasmInitExpr::RefFuncConst(
                    builder->NumImportedFunctions() + i);
              }
            }
            // There has to be at least one function per signature, otherwise
            // the init expression is unable to generate a non-nullable
            // reference with the correct type.
            UNREACHABLE();
          }
          UNREACHABLE();
        }
      }
    }
    case kVoid:
    case kRtt:
    case kBottom:
      UNREACHABLE();
  }
}

}  // namespace

template <WasmModuleGenerationOptions options>
base::Vector<uint8_t> GenerateRandomWasmModule(
    Zone* zone, base::Vector<const uint8_t> data) {
  WasmModuleBuilder builder(zone);

  // Split input data in two parts:
  // - One for the "module" (types, globals, ..)
  // - One for all the function bodies
  // This prevents using a too large portion on the module resulting in
  // uninteresting function bodies.
  DataRange module_range(data);
  DataRange functions_range = module_range.split();
  std::vector<uint32_t> function_signatures;

  static_assert(kMaxFunctions >= 1, "need min. 1 function");
  uint8_t num_functions = 1 + (module_range.get<uint8_t>() % kMaxFunctions);

  // In case of WasmGC expressions:
  // Add struct and array types first so that we get a chance to generate
  // these types in function signatures.
  // Currently, `BodyGen` assumes this order for struct/array/signature
  // definitions.
  // Otherwise, for non-WasmGC we can't use structs/arrays.
  uint8_t num_structs = 0;
  uint8_t num_arrays = 0;
  std::vector<uint32_t> array_types;
  std::vector<uint32_t> struct_types;

  // In case of WasmGC expressions:
  // We always add two default array types with mutable i8 and i16 elements,
  // respectively.
  constexpr uint8_t kNumDefaultArrayTypesForWasmGC = 2;
  if constexpr (ShouldGenerateWasmGC(options)) {
    // We need at least one struct and one array in order to support
    // WasmInitExpr for abstract types.
    num_structs = 1 + module_range.get<uint8_t>() % kMaxStructs;
    num_arrays = kNumDefaultArrayTypesForWasmGC +
                 module_range.get<uint8_t>() % kMaxArrays;
  }

  uint8_t num_signatures = num_functions;
  ModuleGen<options> gen_module(zone, &builder, &module_range, num_functions,
                                num_structs, num_arrays, num_signatures);

  // Add random number of memories.
  // TODO(v8:14674): Add a mode without declaring any memory or memory
  // instructions.
  gen_module.GenerateRandomMemories();

  uint8_t current_type_index = 0;
  // In case of WasmGC expressions, we create recursive groups for the recursive
  // types.
  std::map<uint8_t, uint8_t> explicit_rec_groups;
  if constexpr (ShouldGenerateWasmGC(options)) {
    // Put the types into random recursive groups.
    explicit_rec_groups = gen_module.GenerateRandomRecursiveGroups(
        kNumDefaultArrayTypesForWasmGC);

    // Add default array types.
    static constexpr uint32_t kArrayI8 = 0;
    static constexpr uint32_t kArrayI16 = 1;
    {
      ArrayType* a8 = zone->New<ArrayType>(kWasmI8, 1);
      CHECK_EQ(kArrayI8, builder.AddArrayType(a8, true, kNoSuperType));
      array_types.push_back(kArrayI8);
      ArrayType* a16 = zone->New<ArrayType>(kWasmI16, 1);
      CHECK_EQ(kArrayI16, builder.AddArrayType(a16, true, kNoSuperType));
      array_types.push_back(kArrayI16);
    }
    static_assert(kNumDefaultArrayTypesForWasmGC == kArrayI16 + 1);
    current_type_index = kNumDefaultArrayTypesForWasmGC;

    // Add randomly generated structs.
    gen_module.GenerateRandomStructs(explicit_rec_groups, struct_types,
                                     current_type_index,
                                     kNumDefaultArrayTypesForWasmGC);
    DCHECK_EQ(current_type_index, kNumDefaultArrayTypesForWasmGC + num_structs);

    // Add randomly generated arrays.
    gen_module.GenerateRandomArrays(explicit_rec_groups, array_types,
                                    current_type_index);
    DCHECK_EQ(current_type_index, num_structs + num_arrays);
  }

  // We keep the signature for the first (main) function constant.
  constexpr bool kIsFinal = true;
  auto kMainFnSig = FixedSizeSignature<ValueType>::Returns(kWasmI32).Params(
      kWasmI32, kWasmI32, kWasmI32);
  function_signatures.push_back(
      builder.ForceAddSignature(&kMainFnSig, kIsFinal));
  current_type_index++;

  // Add randomly generated signatures.
  gen_module.GenerateRandomFunctionSigs(
      explicit_rec_groups, function_signatures, current_type_index, kIsFinal);
  DCHECK_EQ(current_type_index, num_functions + num_structs + num_arrays);

  // Add exceptions.
  int num_exceptions = 1 + (module_range.get<uint8_t>() % kMaxExceptions);
  gen_module.GenerateRandomExceptions(num_exceptions);

  // In case of WasmGC expressions:
  // Add the "wasm:js-string" imports to the module. They may or may not be
  // used later, but they'll always be available.
  StringImports strings = ShouldGenerateWasmGC(options)
                              ? gen_module.AddImportedStringImports()
                              : StringImports();

  // Generate function declarations before tables. This will be needed once we
  // have typed-function tables.
  std::vector<WasmFunctionBuilder*> functions;
  for (uint8_t i = 0; i < num_functions; i++) {
    // If we are using wasm-gc, we cannot allow signature normalization
    // performed by adding a function by {FunctionSig}, because we emit
    // everything in one recursive group which blocks signature
    // canonicalization.
    // TODO(14034): Relax this when we implement proper recursive-group
    // support.
    functions.push_back(builder.AddFunction(function_signatures[i]));
  }

  // Generate tables before function bodies, so they are available for table
  // operations. Generate tables before the globals, so tables don't
  // accidentally use globals in their initializer expressions.
  // Always generate at least one table for call_indirect.
  gen_module.GenerateRandomTables(array_types, struct_types);

  // Add globals.
  auto [globals, mutable_globals] =
      gen_module.GenerateRandomGlobals(array_types, struct_types);

  // Add passive data segments.
  int num_data_segments = module_range.get<uint8_t>() % kMaxPassiveDataSegments;
  for (int i = 0; i < num_data_segments; i++) {
    GeneratePassiveDataSegment(&module_range, &builder);
  }

  // Generate function bodies.
  for (int i = 0; i < num_functions; ++i) {
    WasmFunctionBuilder* f = functions[i];
    // On the last function don't split the DataRange but just use the
    // existing DataRange.
    DataRange function_range = i != num_functions - 1
                                   ? functions_range.split()
                                   : std::move(functions_range);
    BodyGen<options> gen_body(f, function_signatures, globals, mutable_globals,
                              struct_types, array_types, strings,
                              &function_range);
    const FunctionSig* sig = f->signature();
    base::Vector<const ValueType> return_types(sig->returns().begin(),
                                               sig->return_count());
    gen_body.InitializeNonDefaultableLocals(&function_range);
    gen_body.Generate(return_types, &function_range);
    // TODO(v8:14639): Disable SIMD expressions if needed, so that a module is
    // always generated.
    if (ShouldGenerateSIMD(options) && !CheckHardwareSupportsSimd() &&
        gen_body.HasSimd()) {
      return {};
    }
    f->Emit(kExprEnd);
    if (i == 0) builder.AddExport(base::CStrVector("main"), f);
  }

  ZoneBuffer buffer{zone};
  builder.WriteTo(&buffer);
  return base::VectorOf(buffer);
}

// Used by the initializer expression fuzzer.
base::Vector<uint8_t> GenerateWasmModuleForInitExpressions(
    Zone* zone, base::Vector<const uint8_t> data, size_t* count) {
  // Don't limit expressions for the initializer expression fuzzer.
  constexpr WasmModuleGenerationOptions options =
      WasmModuleGenerationOptions::kGenerateAll;
  WasmModuleBuilder builder(zone);

  DataRange module_range(data);
  std::vector<uint32_t> function_signatures;
  std::vector<uint32_t> array_types;
  std::vector<uint32_t> struct_types;

  int num_globals = 1 + module_range.get<uint8_t>() % (kMaxGlobals + 1);

  uint8_t num_functions = num_globals;
  *count = num_functions;

  // We need at least one struct and one array in order to support
  // WasmInitExpr for abstract types.
  uint8_t num_structs = 1 + module_range.get<uint8_t>() % kMaxStructs;
  uint8_t num_arrays = 1 + module_range.get<uint8_t>() % kMaxArrays;
  uint16_t num_types = num_functions + num_structs + num_arrays;

  uint8_t current_type_index = 0;

  // Add random-generated types.
  uint8_t last_struct_type = current_type_index + num_structs;
  for (; current_type_index < last_struct_type; current_type_index++) {
    uint32_t supertype = kNoSuperType;
    uint8_t num_fields = module_range.get<uint8_t>() % (kMaxStructFields + 1);

    uint8_t existing_struct_types = current_type_index;
    if (existing_struct_types > 0 && module_range.get<bool>()) {
      supertype = module_range.get<uint8_t>() % existing_struct_types;
      num_fields += builder.GetStructType(supertype)->field_count();
    }
    StructType::Builder struct_builder(zone, num_fields);

    // Add all fields from super type.
    uint32_t field_index = 0;
    if (supertype != kNoSuperType) {
      const StructType* parent = builder.GetStructType(supertype);
      for (; field_index < parent->field_count(); ++field_index) {
        struct_builder.AddField(parent->field(field_index),
                                parent->mutability(field_index));
      }
    }
    for (; field_index < num_fields; field_index++) {
      ValueType type = GetValueTypeHelper<options>(
          &module_range, current_type_index, current_type_index,
          kIncludeNumericTypes, kIncludePackedTypes, kExcludeSomeGenerics);

      bool mutability = module_range.get<bool>();
      struct_builder.AddField(type, mutability);
    }
    StructType* struct_fuz = struct_builder.Build();
    uint32_t index = builder.AddStructType(struct_fuz, false, supertype);
    struct_types.push_back(index);
  }

  for (; current_type_index < num_structs + num_arrays; current_type_index++) {
    ValueType type = GetValueTypeHelper<options>(
        &module_range, current_type_index, current_type_index,
        kIncludeNumericTypes, kIncludePackedTypes, kExcludeSomeGenerics);
    uint32_t supertype = kNoSuperType;
    if (current_type_index > last_struct_type && module_range.get<bool>()) {
      uint8_t existing_array_types = current_type_index - last_struct_type;
      supertype = last_struct_type +
                  (module_range.get<uint8_t>() % existing_array_types);
      type = builder.GetArrayType(supertype)->element_type();
    }
    ArrayType* array_fuz = zone->New<ArrayType>(type, true);
    uint32_t index = builder.AddArrayType(array_fuz, false, supertype);
    array_types.push_back(index);
  }

  // Choose global types and create function signatures.
  constexpr bool kIsFinal = true;
  std::vector<ValueType> globals;
  for (; current_type_index < num_types; current_type_index++) {
    ValueType return_type = GetValueTypeHelper<options>(
        &module_range, num_types - num_globals, num_types - num_globals,
        kIncludeNumericTypes, kExcludePackedTypes, kIncludeAllGenerics,
        kExcludeS128);
    globals.push_back(return_type);
    // Create a new function signature for each global. These functions will be
    // used to compare against the initializer value of the global.
    FunctionSig::Builder sig_builder(zone, 1, 0);
    sig_builder.AddReturn(return_type);
    uint32_t signature_index =
        builder.ForceAddSignature(sig_builder.Build(), kIsFinal);
    function_signatures.push_back(signature_index);
  }

  std::vector<WasmFunctionBuilder*> functions;
  for (uint8_t i = 0; i < num_functions; i++) {
    functions.push_back(builder.AddFunction(function_signatures[i]));
  }

  // Create globals.
  std::vector<uint8_t> mutable_globals;
  std::vector<WasmInitExpr> init_exprs;
  init_exprs.reserve(num_globals);
  mutable_globals.reserve(num_globals);
  CHECK_EQ(globals.size(), num_globals);
  uint64_t mutabilities = module_range.get<uint64_t>();
  for (int i = 0; i < num_globals; ++i) {
    ValueType type = globals[i];
    // 50% of globals are immutable.
    const bool mutability = mutabilities & 1;
    mutabilities >>= 1;
    WasmInitExpr init_expr = GenerateInitExpr(
        zone, module_range, &builder, type, struct_types, array_types, 0);
    init_exprs.push_back(init_expr);
    auto buffer = zone->AllocateVector<char>(8);
    size_t len = base::SNPrintF(buffer, "g%i", i);
    builder.AddExportedGlobal(type, mutability, init_expr,
                              {buffer.begin(), len});
    if (mutability) mutable_globals.push_back(static_cast<uint8_t>(i));
  }

  // Create functions containing the initializer of each global as its function
  // body.
  for (int i = 0; i < num_functions; ++i) {
    WasmFunctionBuilder* f = functions[i];
    f->EmitFromInitializerExpression(init_exprs[i]);
    auto buffer = zone->AllocateVector<char>(8);
    size_t len = base::SNPrintF(buffer, "f%i", i);
    builder.AddExport({buffer.begin(), len}, f);
  }

  ZoneBuffer buffer{zone};
  builder.WriteTo(&buffer);
  return base::VectorOf(buffer);
}

namespace {

bool HasSameReturns(const FunctionSig* a, const FunctionSig* b) {
  if (a->return_count() != b->return_count()) return false;
  for (size_t i = 0; i < a->return_count(); ++i) {
    if (a->GetReturn(i) != b->GetReturn(i)) return false;
  }
  return true;
}

template <WasmModuleGenerationOptions options>
void EmitDeoptAndReturnValues(BodyGen<options> gen_body, WasmFunctionBuilder* f,
                              const FunctionSig* target_sig,
                              uint32_t target_sig_index, uint32_t global_index,
                              uint32_t table_index, DataRange* data) {
  base::Vector<const ValueType> return_types = f->signature()->returns();
  // Split the return types randomly and generate some values before the
  // deopting call and some afterwards. (This makes sure that we have deopts
  // where there are values on the wasm value stack which are not used by the
  // deopting call itself.)
  uint32_t returns_split = data->get<uint8_t>() % (return_types.size() + 1);
  if (returns_split) {
    gen_body.Generate(return_types.SubVector(0, returns_split), data);
  }
  gen_body.Generate(target_sig->parameters(), data);
  f->EmitWithU32V(kExprGlobalGet, global_index);
  // Tail calls can only be emitted if the return types match.
  bool same_returns = HasSameReturns(target_sig, f->signature());
  size_t option_count = (same_returns + 1) * 2;
  switch (data->get<uint8_t>() % option_count) {
    case 0:
      // Emit call_ref.
      f->Emit(kExprTableGet);
      f->EmitU32V(table_index);
      f->EmitWithPrefix(kExprRefCast);
      f->EmitI32V(target_sig_index);
      f->EmitWithU32V(kExprCallRef, target_sig_index);
      break;
    case 1:
      // Emit call_indirect.
      f->EmitWithU32V(kExprCallIndirect, target_sig_index);
      f->EmitByte(table_index);
      break;
    case 2:
      // Emit return_call_ref.
      f->Emit(kExprTableGet);
      f->EmitU32V(table_index);
      f->EmitWithPrefix(kExprRefCast);
      f->EmitI32V(target_sig_index);
      f->EmitWithU32V(kExprReturnCallRef, target_sig_index);
      break;
    case 3:
      // Emit return_call_indirect.
      f->EmitWithU32V(kExprReturnCallIndirect, target_sig_index);
      f->EmitByte(table_index);
      break;
    default:
      UNREACHABLE();
  }
  gen_body.ConsumeAndGenerate(target_sig->returns(),
                              return_types.SubVectorFrom(returns_split), data);
}

template <WasmModuleGenerationOptions options>
void EmitCallAndReturnValues(BodyGen<options> gen_body, WasmFunctionBuilder* f,
                             WasmFunctionBuilder* callee, uint32_t table_index,
                             DataRange* data) {
  const FunctionSig* callee_sig = callee->signature();
  uint32_t callee_index =
      callee->func_index() + gen_body.NumImportedFunctions();

  base::Vector<const ValueType> return_types = f->signature()->returns();
  // Split the return types randomly and generate some values before the
  // deopting call and some afterwards to create more interesting test cases.
  uint32_t returns_split = data->get<uint8_t>() % (return_types.size() + 1);
  if (returns_split) {
    gen_body.Generate(return_types.SubVector(0, returns_split), data);
  }
  gen_body.Generate(callee_sig->parameters(), data);
  // Tail calls can only be emitted if the return types match.
  bool same_returns = HasSameReturns(callee_sig, f->signature());
  size_t option_count = (same_returns + 1) * 3;
  switch (data->get<uint8_t>() % option_count) {
    case 0:
      f->EmitWithU32V(kExprCallFunction, callee_index);
      break;
    case 1:
      f->EmitWithU32V(kExprRefFunc, callee_index);
      f->EmitWithU32V(kExprCallRef, callee->sig_index());
      break;
    case 2:
      // Note that this assumes that the declared function index is the same as
      // the index of the function in the table.
      f->EmitI32Const(callee->func_index());
      f->EmitWithU32V(kExprCallIndirect, callee->sig_index());
      f->EmitByte(table_index);
      break;
    case 3:
      f->EmitWithU32V(kExprReturnCall, callee_index);
      break;
    case 4:
      f->EmitWithU32V(kExprRefFunc, callee_index);
      f->EmitWithU32V(kExprReturnCallRef, callee->sig_index());
      break;
    case 5:
      // Note that this assumes that the declared function index is the same as
      // the index of the function in the table.
      f->EmitI32Const(callee->func_index());
      f->EmitWithU32V(kExprReturnCallIndirect, callee->sig_index());
      f->EmitByte(table_index);
      break;
    default:
      UNREACHABLE();
  }
  gen_body.ConsumeAndGenerate(callee_sig->returns(),
                              return_types.SubVectorFrom(returns_split), data);
}
}  // anonymous namespace

base::Vector<uint8_t> GenerateWasmModuleForDeopt(
    Zone* zone, base::Vector<const uint8_t> data,
    std::vector<std::string>& callees, std::vector<std::string>& inlinees) {
  // Don't limit the features for the deopt fuzzer.
  constexpr WasmModuleGenerationOptions options =
      WasmModuleGenerationOptions::kGenerateAll;
  WasmModuleBuilder builder(zone);

  DataRange range(data);
  std::vector<uint32_t> function_signatures;
  std::vector<uint32_t> array_types;
  std::vector<uint32_t> struct_types;

  const int kMaxCallTargets = 5;
  const int kMaxInlinees = 3;

  // We need at least 2 call targets to be able to trigger a deopt.
  const int num_call_targets = 2 + range.get<uint8_t>() % (kMaxCallTargets - 1);
  const int num_inlinees = range.get<uint8_t>() % (kMaxInlinees + 1);

  // 1 main function + x inlinees + x callees.
  uint8_t num_functions = 1 + num_inlinees + num_call_targets;
  // 1 signature for all the callees, 1 signature for the main function +
  // 1 signature per inlinee.
  uint8_t num_signatures = 2 + num_inlinees;

  uint8_t num_structs = 1 + range.get<uint8_t>() % kMaxStructs;
  // In case of WasmGC expressions:
  // We always add two default array types with mutable i8 and i16 elements,
  // respectively.
  constexpr uint8_t kNumDefaultArrayTypesForWasmGC = 2;
  uint8_t num_arrays =
      range.get<uint8_t>() % kMaxArrays + kNumDefaultArrayTypesForWasmGC;
  // Just ignoring user-defined signature types in the signatures.
  uint16_t num_types = num_structs + num_arrays;

  uint8_t current_type_index = kNumDefaultArrayTypesForWasmGC;

  // Add random-generated types.
  ModuleGen<options> gen_module(zone, &builder, &range, num_functions,
                                num_structs, num_arrays, num_signatures);

  gen_module.GenerateRandomMemories();
  std::map<uint8_t, uint8_t> explicit_rec_groups =
      gen_module.GenerateRandomRecursiveGroups(kNumDefaultArrayTypesForWasmGC);
  // Add default array types.
  static constexpr uint32_t kArrayI8 = 0;
  static constexpr uint32_t kArrayI16 = 1;
  {
    ArrayType* a8 = zone->New<ArrayType>(kWasmI8, 1);
    CHECK_EQ(kArrayI8, builder.AddArrayType(a8, true, kNoSuperType));
    array_types.push_back(kArrayI8);
    ArrayType* a16 = zone->New<ArrayType>(kWasmI16, 1);
    CHECK_EQ(kArrayI16, builder.AddArrayType(a16, true, kNoSuperType));
    array_types.push_back(kArrayI16);
  }
  static_assert(kNumDefaultArrayTypesForWasmGC == kArrayI16 + 1);
  gen_module.GenerateRandomStructs(explicit_rec_groups, struct_types,
                                   current_type_index,
                                   kNumDefaultArrayTypesForWasmGC);
  DCHECK_EQ(current_type_index, kNumDefaultArrayTypesForWasmGC + num_structs);
  gen_module.GenerateRandomArrays(explicit_rec_groups, array_types,
                                  current_type_index);
  DCHECK_EQ(current_type_index, num_structs + num_arrays);

  // Create signature for call target.
  std::vector<ValueType> return_types =
      GenerateTypes<options>(&range, num_types);
  constexpr bool kIsFinal = true;
  const FunctionSig* target_sig = CreateSignature(
      builder.zone(), base::VectorOf(GenerateTypes<options>(&range, num_types)),
      base::VectorOf(return_types));
  uint32_t target_sig_index = builder.ForceAddSignature(target_sig, kIsFinal);

  for (int i = 0; i < num_call_targets; ++i) {
    // Simplification: All call targets of a call_ref / call_indirect have the
    // same signature.
    function_signatures.push_back(target_sig_index);
  }

  // Create signatures for inlinees.
  // Use the same return types with a certain chance. This increases the chance
  // to emit return calls.
  uint8_t use_same_return = range.get<uint8_t>();
  for (int i = 0; i < num_inlinees; ++i) {
    if ((use_same_return & (1 << i)) == 0) {
      return_types = GenerateTypes<options>(&range, num_types);
    }
    const FunctionSig* inlinee_sig = CreateSignature(
        builder.zone(),
        base::VectorOf(GenerateTypes<options>(&range, num_types)),
        base::VectorOf(return_types));
    function_signatures.push_back(
        builder.ForceAddSignature(inlinee_sig, kIsFinal));
  }

  // Create signature for main function.
  const FunctionSig* main_sig = CreateSignature(
      builder.zone(), base::VectorOf({kWasmI32}), base::VectorOf({kWasmI32}));
  function_signatures.push_back(builder.ForceAddSignature(main_sig, kIsFinal));

  DCHECK_EQ(function_signatures.back(),
            num_structs + num_arrays + num_signatures - 1);

  // This needs to be done after the signatures are added.
  int num_exceptions = 1 + range.get<uint8_t>() % kMaxExceptions;
  gen_module.GenerateRandomExceptions(num_exceptions);
  StringImports strings = gen_module.AddImportedStringImports();

  // Add functions to module.
  std::vector<WasmFunctionBuilder*> functions;
  DCHECK_EQ(num_functions, function_signatures.size());
  for (uint8_t i = 0; i < num_functions; i++) {
    functions.push_back(builder.AddFunction(function_signatures[i]));
  }

  uint32_t num_entries = num_call_targets + num_inlinees;
  uint32_t table_index =
      builder.AddTable(kWasmFuncRef, num_entries, num_entries);
  WasmModuleBuilder::WasmElemSegment segment(zone, kWasmFuncRef, table_index,
                                             WasmInitExpr(0));
  for (uint32_t i = 0; i < num_entries; i++) {
    segment.entries.emplace_back(
        WasmModuleBuilder::WasmElemSegment::Entry::kRefFuncEntry,
        builder.NumImportedFunctions() + i);
  }
  builder.AddElementSegment(std::move(segment));

  gen_module.GenerateRandomTables(array_types, struct_types);

  // Create global for call target index.
  // Simplification: This global is used to specify the call target at the deopt
  // point instead of passing the call target around dynamically.
  uint32_t global_index =
      builder.AddExportedGlobal(kWasmI32, true, WasmInitExpr(0),
                                base::StaticCharVector("call_target_index"));

  // Create inlinee bodies.
  for (int i = 0; i < num_inlinees; ++i) {
    uint32_t declared_func_index = i + num_call_targets;
    WasmFunctionBuilder* f = functions[declared_func_index];
    DataRange function_range = range.split();
    BodyGen<options> gen_body(f, function_signatures, {}, {}, struct_types,
                              array_types, strings, &function_range);
    const FunctionSig* sig = f->signature();
    base::Vector<const ValueType> return_types(sig->returns().begin(),
                                               sig->return_count());
    gen_body.InitializeNonDefaultableLocals(&function_range);
    if (i == 0) {
      // For the inner-most inlinee, emit the deopt point (e.g. a call_ref).
      EmitDeoptAndReturnValues(gen_body, f, target_sig, target_sig_index,
                               global_index, table_index, &function_range);
    } else {
      // All other inlinees call the previous inlinee.
      uint32_t callee_declared_index = declared_func_index - 1;
      EmitCallAndReturnValues(gen_body, f, functions[callee_declared_index],
                              table_index, &function_range);
    }
    // TODO(v8:14639): Disable SIMD expressions if needed, so that a module is
    // always generated.
    if (ShouldGenerateSIMD(options) && !CheckHardwareSupportsSimd() &&
        gen_body.HasSimd()) {
      return {};
    }
    f->Emit(kExprEnd);
    auto buffer = zone->AllocateVector<char>(32);
    size_t len = base::SNPrintF(buffer, "inlinee_%i", i);
    builder.AddExport({buffer.begin(), len}, f);
    inlinees.emplace_back(buffer.begin(), len);
  }

  // Create main function body.
  {
    uint32_t declared_func_index = num_functions - 1;
    WasmFunctionBuilder* f = functions[declared_func_index];
    DataRange function_range = range.split();
    BodyGen<options> gen_body(f, function_signatures, {}, {}, struct_types,
                              array_types, strings, &function_range);
    const FunctionSig* sig = f->signature();
    base::Vector<const ValueType> return_types(sig->returns().begin(),
                                               sig->return_count());
    gen_body.InitializeNonDefaultableLocals(&function_range);
    // Store the call target
    f->EmitWithU32V(kExprLocalGet, 0);
    f->EmitWithU32V(kExprGlobalSet, 0);
    // Call inlinee or emit deopt.
    if (num_inlinees == 0) {
      // If we don't have any inlinees, directly emit the deopt point.
      EmitDeoptAndReturnValues(gen_body, f, target_sig, target_sig_index,
                               global_index, table_index, &function_range);
    } else {
      // Otherwise call the "outer-most" inlinee.
      uint32_t callee_declared_index = declared_func_index - 1;
      EmitCallAndReturnValues(gen_body, f, functions[callee_declared_index],
                              table_index, &function_range);
    }

    // TODO(v8:14639): Disable SIMD expressions if needed, so that a module is
    // always generated.
    if (ShouldGenerateSIMD(options) && !CheckHardwareSupportsSimd() &&
        gen_body.HasSimd()) {
      return {};
    }
    f->Emit(kExprEnd);
    builder.AddExport(base::StaticCharVector("main"), f);
  }

  // Create call target bodies.
  // This is done last as we care much less about the content of these
  // functions, so it's less of an issue if there aren't (m)any random bytes
  // left.
  for (int i = 0; i < num_call_targets; ++i) {
    WasmFunctionBuilder* f = functions[i];
    DataRange function_range = range.split();
    BodyGen<options> gen_body(f, function_signatures, {}, {}, struct_types,
                              array_types, strings, &function_range);
    const FunctionSig* sig = f->signature();
    base::Vector<const ValueType> return_types(sig->returns().begin(),
                                               sig->return_count());
    gen_body.InitializeNonDefaultableLocals(&function_range);
    gen_body.Generate(return_types, &function_range);

    // TODO(v8:14639): Disable SIMD expressions if needed, so that a module is
    // always generated.
    if (ShouldGenerateSIMD(options) && !CheckHardwareSupportsSimd() &&
        gen_body.HasSimd()) {
      return {};
    }
    f->Emit(kExprEnd);
    auto buffer = zone->AllocateVector<char>(32);
    size_t len = base::SNPrintF(buffer, "callee_%i", i);
    builder.AddExport({buffer.begin(), len}, f);
    callees.emplace_back(buffer.begin(), len);
  }

  ZoneBuffer buffer{zone};
  builder.WriteTo(&buffer);
  return base::VectorOf(buffer);
}

// Explicit template instantiation for kMVP.
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    base::Vector<uint8_t> GenerateRandomWasmModule<
        WasmModuleGenerationOptions::kMVP>(Zone*,
                                           base::Vector<const uint8_t> data);

// Explicit template instantiation for kGenerateSIMD.
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    base::Vector<uint8_t> GenerateRandomWasmModule<
        WasmModuleGenerationOptions::kGenerateSIMD>(
        Zone*, base::Vector<const uint8_t> data);

// Explicit template instantiation for kGenerateWasmGC.
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    base::Vector<uint8_t> GenerateRandomWasmModule<
        WasmModuleGenerationOptions::kGenerateWasmGC>(
        Zone*, base::Vector<const uint8_t> data);

// Explicit template instantiation for kGenerateAll.
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    base::Vector<uint8_t> GenerateRandomWasmModule<
        WasmModuleGenerationOptions::kGenerateAll>(
        Zone*, base::Vector<const uint8_t> data);

}  // namespace v8::internal::wasm::fuzzing
