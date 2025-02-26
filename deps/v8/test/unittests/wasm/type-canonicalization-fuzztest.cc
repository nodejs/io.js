// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-json.h"
#include "src/utils/ostreams.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-module-builder.h"
#include "test/unittests/fuzztest.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::wasm {

// Introduce a separate representation for recursion groups to be used by this
// fuzz test.
namespace test {

// Provide operator<<(std::ostream&, T> for types with T.Print(std::ostream*).
template <typename T>
  requires requires(const T& t, std::ostream& os) { t.Print(os); }
std::ostream& operator<<(std::ostream& os, const T& t) {
  t.Print(os);
  return os;
}

struct FieldType {
  ValueType value_type;
  bool mutability;

  void Print(std::ostream& os) const {
    os << (mutability ? "mut " : "") << value_type;
  }
};

struct StructType {
  std::vector<FieldType> field_types;

  void BuildType(Zone* zone, WasmModuleBuilder* builder) const {
    // TODO(381687256): Populate final and supertype.
    constexpr bool kNotFinal = false;
    constexpr ModuleTypeIndex kNoSupertype = ModuleTypeIndex::Invalid();
    // If this fails, we need to constrain vector sizes in the fuzzer domains.
    DCHECK_GE(kMaxUInt32, field_types.size());
    uint32_t field_count = static_cast<uint32_t>(field_types.size());
    // Offsets are not used and never accessed, hence we can pass nullptr.
    constexpr uint32_t* kNoOffsets = nullptr;
    ValueType* reps = zone->AllocateArray<ValueType>(field_count);
    bool* mutabilities = zone->AllocateArray<bool>(field_count);
    for (uint32_t i = 0; i < field_count; ++i) {
      reps[i] = field_types[i].value_type;
      mutabilities[i] = field_types[i].mutability;
    }
    builder->AddStructType(zone->New<wasm::StructType>(field_count, kNoOffsets,
                                                       reps, mutabilities),
                           kNotFinal, kNoSupertype);
  }

  void Print(std::ostream& os) const {
    os << "struct(" << PrintCollection(field_types).WithoutBrackets() << ")";
  }
};

struct ArrayType {
  FieldType field_type;

  void BuildType(Zone* zone, WasmModuleBuilder* builder) const {
    // TODO(381687256): Populate final and supertype.
    constexpr bool kNotFinal = false;
    constexpr ModuleTypeIndex kNoSupertype = ModuleTypeIndex::Invalid();
    builder->AddArrayType(zone->New<wasm::ArrayType>(field_type.value_type,
                                                     field_type.mutability),
                          kNotFinal, kNoSupertype);
  }

  void Print(std::ostream& os) const { os << "array(" << field_type << ")"; }
};

struct FunctionType {
  std::vector<ValueType> params;
  std::vector<ValueType> returns;

  void BuildType(Zone* zone, WasmModuleBuilder* builder) const {
    // TODO(381687256): Populate final and supertype.
    constexpr bool kNotFinal = false;
    constexpr ModuleTypeIndex kNoSupertype = ModuleTypeIndex::Invalid();
    FunctionSig::Builder sig_builder(zone, returns.size(), params.size());
    for (ValueType param : params) sig_builder.AddParam(param);
    for (ValueType ret : returns) sig_builder.AddReturn(ret);
    FunctionSig* sig = sig_builder.Get();
    builder->ForceAddSignature(sig, kNotFinal, kNoSupertype);
  }

  void Print(std::ostream& os) const {
    os << "func params (" << PrintCollection(params).WithoutBrackets()
       << ") returns (" << PrintCollection(returns).WithoutBrackets() << ")";
  }
};

using Type = std::variant<StructType, ArrayType, FunctionType>;

std::ostream& operator<<(std::ostream& os, const Type& type) {
  // Call operator<< on the contained type.
  std::visit([&os](auto& t) { os << t; }, type);
  return os;
}

struct RecursionGroup {
  std::vector<Type> types;
  // If {single_type} is false, this type will be outside any recursion group.
  // This is only allowed if {types.size() == 1}.
  bool single_type = false;

  void BuildTypes(Zone* zone, WasmModuleBuilder* builder) const {
    auto build_type = [zone, builder](const auto& t) {
      t.BuildType(zone, builder);
    };
    if (single_type) {
      DCHECK_EQ(1, types.size());
      std::visit(build_type, types[0]);
    } else {
      builder->StartRecursiveTypeGroup();
      for (const Type& type : types) {
        std::visit(build_type, type);
      }
      builder->EndRecursiveTypeGroup();
    }
  }

  void Print(std::ostream& os) const {
    // Note: {single_type} is not included here because it makes no difference
    // for canonicalization.
    os << "recgroup(" << PrintCollection(types).WithoutBrackets() << ")";
  }
};

// A module with a number of types.
struct Module {
  std::vector<RecursionGroup> rec_groups;

  void BuildTypes(Zone* zone, WasmModuleBuilder* builder) const {
    for (const RecursionGroup& rec_group : rec_groups) {
      rec_group.BuildTypes(zone, builder);
    }
  }
};

}  // namespace test

class TypeCanonicalizerTest
    : public fuzztest::PerFuzzTestFixtureAdapter<TestWithPlatform> {
 public:
  TypeCanonicalizerTest() : zone_(&allocator_, "TypeCanonicalizerTest") {}
  ~TypeCanonicalizerTest() override = default;

  void TestCanonicalization(const std::vector<test::Module>&);

 private:
  void Reset() {
    wasm::GetTypeCanonicalizer()->EmptyStorageForTesting();
    zone_.Reset();
  }

  v8::internal::AccountingAllocator allocator_;
  Zone zone_;
  const WasmEnabledFeatures enabled_features_ =
      WasmEnabledFeatures::FromFlags();
};

// FuzzTest domain construction.

static fuzztest::Domain<test::Module> ArbitraryModule() {
  auto storage_type_domain = fuzztest::ElementOf(
      {kWasmI8, kWasmI16, kWasmI32, kWasmI64, kWasmF32, kWasmF64
       /* TODO(381687256: Add kS128 on SIMD-enabled hosts */});
  auto value_type_domain = fuzztest::ElementOf(
      {kWasmI32, kWasmI64, kWasmF32, kWasmF64
       /* TODO(381687256: Add kS128 on SIMD-enabled hosts */});

  auto field_type_domain = fuzztest::StructOf<test::FieldType>(
      storage_type_domain, fuzztest::Arbitrary<bool>());
  auto struct_type_domain = fuzztest::StructOf<test::StructType>(
      fuzztest::VectorOf(field_type_domain));

  auto array_type_domain =
      fuzztest::StructOf<test::ArrayType>(field_type_domain);

  auto function_type_domain = fuzztest::StructOf<test::FunctionType>(
      fuzztest::VectorOf(value_type_domain),
      fuzztest::VectorOf(value_type_domain));

  auto type_domain = fuzztest::VariantOf<test::Type>(
      struct_type_domain, array_type_domain, function_type_domain);

  auto recgroup_domain = fuzztest::OneOf(
      // A single type declared outside any recursion group.
      fuzztest::StructOf<test::RecursionGroup>(
          fuzztest::VectorOf(type_domain).WithSize(1), fuzztest::Just(true)),
      // An actual recursion group of arbitrary size.
      fuzztest::StructOf<test::RecursionGroup>(fuzztest::VectorOf(type_domain),
                                               fuzztest::Just(false)));

  auto module_domain =
      fuzztest::StructOf<test::Module>(fuzztest::VectorOf(recgroup_domain));
  return module_domain;
}

// Fuzz tests.

void TypeCanonicalizerTest::TestCanonicalization(
    const std::vector<test::Module>& test_modules) {
  // For each test, reset the type canonicalizer such that individual inputs are
  // independent of each other.
  Reset();

  // Keep a map of all recgroups in all modules to check that canonicalization
  // works as expected. The key is a text representation of the respective type
  // or recursion group; we expect same text to mean identical group.
  std::map<std::string, CanonicalTypeIndex> canonical_types;

  for (const test::Module& test_module : test_modules) {
    WasmModuleBuilder builder(&zone_);
    test_module.BuildTypes(&zone_, &builder);
    ZoneBuffer buffer{&zone_};
    builder.WriteTo(&buffer);

    WasmDetectedFeatures detected_features;
    bool kValidateModule = true;
    ModuleResult result =
        DecodeWasmModule(enabled_features_, base::VectorOf(buffer),
                         kValidateModule, kWasmOrigin, &detected_features);

    // If this fails due to too many types, we need to constrain vector sizes in
    // the fuzzer domains.
    ASSERT_TRUE(result.ok());
    std::shared_ptr<WasmModule> module = std::move(result).value();
    size_t total_types = 0;
    for (const test::RecursionGroup& rec_group : test_module.rec_groups) {
      total_types += rec_group.types.size();
    }
    ASSERT_EQ(module->types.size(), total_types);

    size_t num_previous_types = 0;
    for (const test::RecursionGroup& rec_group : test_module.rec_groups) {
      // The total number of types must be within kV8MaxWasmTypes.
      ASSERT_GE(kMaxUInt32, num_previous_types);
      uint32_t first_type_id = static_cast<uint32_t>(num_previous_types);
      num_previous_types += rec_group.types.size();
      // Skip empty recursion groups; they do not get a canonical ID assigned,
      // so we cannot check anything for them (except that they do not confuse
      // canonicalization of surrounding types or groups).
      if (rec_group.types.empty()) continue;
      DCHECK(!rec_group.types.empty());
      CanonicalTypeIndex first_canonical_id =
          module->canonical_type_id(ModuleTypeIndex{first_type_id});
      for (uint32_t i = 1; i < rec_group.types.size(); ++i) {
        // Canonical IDs are consecutive within the recursion group.
        ASSERT_EQ(
            CanonicalTypeIndex{first_canonical_id.index + i},
            module->canonical_type_id(ModuleTypeIndex{first_type_id + i}));
      }
      std::string recgroup_str = (std::ostringstream{} << rec_group).str();
      auto [it, added] = canonical_types.insert(
          std::make_pair(recgroup_str, first_canonical_id));
      // Check that the entry holds first_canonical_id; either it was added
      // here, or it existed and we check against the existing entry.
      ASSERT_EQ(it->second, first_canonical_id)
          << "New recgroup:\n"
          << recgroup_str << "\nOld recgroup:\n"
          << it->first;
    }
  }
}

V8_FUZZ_TEST_F(TypeCanonicalizerTest, TestCanonicalization)
    .WithDomains(fuzztest::VectorOf(ArbitraryModule()));

}  // namespace v8::internal::wasm
