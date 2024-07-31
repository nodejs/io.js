// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/cpu-features.h"
#include "src/objects/objects-inl.h"
#include "src/objects/swiss-name-dictionary-inl.h"
#include "test/cctest/compiler/function-tester.h"
#include "test/cctest/test-swiss-name-dictionary-infra.h"
#include "test/cctest/test-swiss-name-dictionary-shared-tests.h"
#include "test/common/code-assembler-tester.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

// The non-SIMD SwissNameDictionary implementation requires 64 bit integer
// operations, which CSA/Torque don't offer on 32 bit platforms. Therefore, we
// cannot run the CSA version of the tests on 32 bit platforms. The only
// exception is IA32, where we can use SSE and don't need 64 bit integers.
// TODO(v8:11330) The Torque SIMD implementation is not specific to SSE (like
// the C++ one), but works on other platforms. It should be possible to create a
// workaround where on 32 bit, non-IA32 platforms we use the "portable", non-SSE
// implementation on the C++ side (which uses a group size of 8) and create a
// special version of the SIMD Torque implementation that works for group size 8
// instead of 16.
#if V8_TARGET_ARCH_64_BIT || V8_TARGET_ARCH_IA32

// Executes tests by executing CSA/Torque versions of dictionary operations.
// See RuntimeTestRunner for description of public functions.
class CSATestRunner {
 public:
  CSATestRunner(Isolate* isolate, int initial_capacity, KeyCache& keys);

  // TODO(v8:11330): Remove once CSA implementation has a fallback for
  // non-SSSE3/AVX configurations.
  static bool IsEnabled() {
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
    CpuFeatures::SupportedFeatures();
    return CpuFeatures::IsSupported(CpuFeature::AVX) ||
           CpuFeatures::IsSupported(CpuFeature::SSSE3);
#else
    // Other 64-bit architectures always support the required operations.
    return true;
#endif
  }

  void Add(Handle<Name> key, Handle<Object> value, PropertyDetails details);
  InternalIndex FindEntry(Handle<Name> key);
  void Put(InternalIndex entry, Handle<Object> new_value,
           PropertyDetails new_details);
  void Delete(InternalIndex entry);
  void RehashInplace();
  void Shrink();

  Handle<FixedArray> GetData(InternalIndex entry);
  void CheckCounts(base::Optional<int> capacity, base::Optional<int> elements,
                   base::Optional<int> deleted);
  void CheckEnumerationOrder(const std::vector<std::string>& expected_keys);
  void CheckCopy();
  void VerifyHeap();

  void PrintTable();

  Handle<SwissNameDictionary> table;

 private:
  using Label = compiler::CodeAssemblerLabel;
  template <class T>
  using TVariable = compiler::TypedCodeAssemblerVariable<T>;

  void CheckAgainstReference();

  void Allocate(Handle<Smi> capacity);

  Isolate* isolate_;

  // Used to mirror all operations using C++ versions of all operations,
  // yielding a reference to compare against.
  Handle<SwissNameDictionary> reference_;

  // CSA functions execute the corresponding dictionary operation.
  compiler::FunctionTester find_entry_ft_;
  compiler::FunctionTester get_data_ft_;
  compiler::FunctionTester put_ft_;
  compiler::FunctionTester delete_ft_;
  compiler::FunctionTester add_ft_;
  compiler::FunctionTester allocate_ft_;
  compiler::FunctionTester get_counts_ft_;
  compiler::FunctionTester copy_ft_;

  // Used to create the FunctionTesters above.
  static Handle<Code> create_get_data(Isolate* isolate);
  static Handle<Code> create_find_entry(Isolate* isolate);
  static Handle<Code> create_put(Isolate* isolate);
  static Handle<Code> create_delete(Isolate* isolate);
  static Handle<Code> create_add(Isolate* isolate);
  static Handle<Code> create_allocate(Isolate* isolate);
  static Handle<Code> create_get_counts(Isolate* isolate);
  static Handle<Code> create_copy(Isolate* isolate);

  // Number of parameters of each of the tester functions above.
  static constexpr int kFindEntryParams = 2;  // (table, key)
  static constexpr int kGetDataParams = 2;    // (table, entry)
  static constexpr int kPutParams = 4;        // (table, entry, value,  details)
  static constexpr int kDeleteParams = 2;     // (table, entry)
  static constexpr int kAddParams = 4;        // (table, key, value, details)
  static constexpr int kAllocateParams = 1;   // (capacity)
  static constexpr int kGetCountsParams = 1;  // (table)
  static constexpr int kCopyParams = 1;       // (table)
};

CSATestRunner::CSATestRunner(Isolate* isolate, int initial_capacity,
                             KeyCache& keys)
    : isolate_{isolate},
      reference_{isolate_->factory()->NewSwissNameDictionaryWithCapacity(
          initial_capacity, AllocationType::kYoung)},
      find_entry_ft_(create_find_entry(isolate), kFindEntryParams),
      get_data_ft_(create_get_data(isolate), kGetDataParams),
      put_ft_{create_put(isolate), kPutParams},
      delete_ft_{create_delete(isolate), kDeleteParams},
      add_ft_{create_add(isolate), kAddParams},
      allocate_ft_{create_allocate(isolate), kAllocateParams},
      get_counts_ft_{create_get_counts(isolate), kGetCountsParams},
      copy_ft_{create_copy(isolate), kCopyParams} {
  Allocate(handle(Smi::FromInt(initial_capacity), isolate));
}

void CSATestRunner::Add(Handle<Name> key, Handle<Object> value,
                        PropertyDetails details) {
  ReadOnlyRoots roots(isolate_);
  reference_ =
      SwissNameDictionary::Add(isolate_, reference_, key, value, details);

  Handle<Smi> details_smi = handle(details.AsSmi(), isolate_);
  Handle<Boolean> success =
      add_ft_.CallChecked<Boolean>(table, key, value, details_smi);

  if (*success == roots.false_value()) {
    // |add_ft_| does not resize and indicates the need to do so by returning
    // false.
    int capacity = table->Capacity();
    int used_capacity = table->UsedCapacity();
    CHECK_GT(used_capacity + 1,
             SwissNameDictionary::MaxUsableCapacity(capacity));

    table = SwissNameDictionary::Add(isolate_, table, key, value, details);
  }

  CheckAgainstReference();
}

void CSATestRunner::Allocate(Handle<Smi> capacity) {
  // We must handle |capacity| == 0 specially, because
  // AllocateSwissNameDictionary (just like AllocateNameDictionary) always
  // returns a non-zero sized table.
  if ((*capacity).value() == 0) {
    table = ReadOnlyRoots(isolate_).empty_swiss_property_dictionary_handle();
  } else {
    table = allocate_ft_.CallChecked<SwissNameDictionary>(capacity);
  }

  CheckAgainstReference();
}

InternalIndex CSATestRunner::FindEntry(Handle<Name> key) {
  Tagged<Smi> index = *find_entry_ft_.CallChecked<Smi>(table, key);
  if (index.value() == SwissNameDictionary::kNotFoundSentinel) {
    return InternalIndex::NotFound();
  } else {
    return InternalIndex(index.value());
  }
}

Handle<FixedArray> CSATestRunner::GetData(InternalIndex entry) {
  DCHECK(entry.is_found());

  return get_data_ft_.CallChecked<FixedArray>(
      table, handle(Smi::FromInt(entry.as_int()), isolate_));
}

void CSATestRunner::CheckCounts(base::Optional<int> capacity,
                                base::Optional<int> elements,
                                base::Optional<int> deleted) {
  Handle<FixedArray> counts = get_counts_ft_.CallChecked<FixedArray>(table);

  if (capacity.has_value()) {
    CHECK_EQ(Smi::FromInt(capacity.value()), counts->get(0));
  }

  if (elements.has_value()) {
    CHECK_EQ(Smi::FromInt(elements.value()), counts->get(1));
  }

  if (deleted.has_value()) {
    CHECK_EQ(Smi::FromInt(deleted.value()), counts->get(2));
  }

  CheckAgainstReference();
}

void CSATestRunner::CheckEnumerationOrder(
    const std::vector<std::string>& expected_keys) {
  // Not implemented in CSA. Making this a no-op (rather than forbidding
  // executing CSA tests with this operation) because CheckEnumerationOrder is
  // also used by some tests whose main goal is not to test the enumeration
  // order.
}

void CSATestRunner::Put(InternalIndex entry, Handle<Object> new_value,
                        PropertyDetails new_details) {
  DCHECK(entry.is_found());
  reference_->ValueAtPut(entry, *new_value);
  reference_->DetailsAtPut(entry, new_details);

  Handle<Smi> entry_smi = handle(Smi::FromInt(entry.as_int()), isolate_);
  Handle<Smi> details_smi = handle(new_details.AsSmi(), isolate_);

  put_ft_.Call(table, entry_smi, new_value, details_smi);

  CheckAgainstReference();
}

void CSATestRunner::Delete(InternalIndex entry) {
  DCHECK(entry.is_found());
  reference_ = SwissNameDictionary::DeleteEntry(isolate_, reference_, entry);

  Handle<Smi> entry_smi = handle(Smi::FromInt(entry.as_int()), isolate_);
  table = delete_ft_.CallChecked<SwissNameDictionary>(table, entry_smi);

  CheckAgainstReference();
}

void CSATestRunner::RehashInplace() {
  // There's no CSA version of this. Use IsRuntimeTest to ensure that we only
  // run a test using this if it's a runtime test.
  UNREACHABLE();
}

void CSATestRunner::Shrink() {
  // There's no CSA version of this. Use IsRuntimeTest to ensure that we only
  // run a test using this if it's a runtime test.
  UNREACHABLE();
}

void CSATestRunner::CheckCopy() {
  Handle<SwissNameDictionary> copy =
      copy_ft_.CallChecked<SwissNameDictionary>(table);
  CHECK(table->EqualsForTesting(*copy));
}

void CSATestRunner::VerifyHeap() {
#if VERIFY_HEAP
  table->SwissNameDictionaryVerify(isolate_, true);
#endif
}

void CSATestRunner::PrintTable() {
#ifdef OBJECT_PRINT
  table->SwissNameDictionaryPrint(std::cout);
#endif
}

Handle<Code> CSATestRunner::create_find_entry(Isolate* isolate) {
  // TODO(v8:11330): Remove once CSA implementation has a fallback for
  // non-SSSE3/AVX configurations.
  if (!IsEnabled()) {
    return isolate->builtins()->code_handle(Builtin::kIllegal);
  }
  static_assert(kFindEntryParams == 2);  // (table, key)
  compiler::CodeAssemblerTester asm_tester(isolate,
                                           JSParameterCount(kFindEntryParams));
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Name> key = m.Parameter<Name>(2);

    Label done(&m);
    TVariable<IntPtrT> entry_var(
        m.IntPtrConstant(SwissNameDictionary::kNotFoundSentinel), &m);

    // |entry_var| defaults to |kNotFoundSentinel| meaning that  one label
    // suffices.
    m.SwissNameDictionaryFindEntry(table, key, &done, &entry_var, &done);

    m.Bind(&done);
    m.Return(m.SmiFromIntPtr(entry_var.value()));
  }

  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_get_data(Isolate* isolate) {
  static_assert(kGetDataParams == 2);  // (table, entry)
  compiler::CodeAssemblerTester asm_tester(isolate,
                                           JSParameterCount(kGetDataParams));
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<IntPtrT> entry = m.SmiToIntPtr(m.Parameter<Smi>(2));

    TNode<FixedArray> data = m.AllocateZeroedFixedArray(m.IntPtrConstant(3));

    TNode<Object> key = m.LoadSwissNameDictionaryKey(table, entry);
    TNode<Object> value = m.LoadValueByKeyIndex(table, entry);
    TNode<Smi> details = m.SmiFromUint32(m.LoadDetailsByKeyIndex(table, entry));

    m.StoreFixedArrayElement(data, 0, key);
    m.StoreFixedArrayElement(data, 1, value);
    m.StoreFixedArrayElement(data, 2, details);

    m.Return(data);
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_put(Isolate* isolate) {
  static_assert(kPutParams == 4);  // (table, entry, value, details)
  compiler::CodeAssemblerTester asm_tester(isolate,
                                           JSParameterCount(kPutParams));
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Smi> entry = m.Parameter<Smi>(2);
    TNode<Object> value = m.Parameter<Object>(3);
    TNode<Smi> details = m.Parameter<Smi>(4);

    TNode<IntPtrT> entry_intptr = m.SmiToIntPtr(entry);

    m.StoreValueByKeyIndex(table, entry_intptr, value,
                           WriteBarrierMode::UPDATE_WRITE_BARRIER);
    m.StoreDetailsByKeyIndex(table, entry_intptr, details);

    m.Return(m.UndefinedConstant());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_delete(Isolate* isolate) {
  // TODO(v8:11330): Remove once CSA implementation has a fallback for
  // non-SSSE3/AVX configurations.
  if (!IsEnabled()) {
    return isolate->builtins()->code_handle(Builtin::kIllegal);
  }
  static_assert(kDeleteParams == 2);  // (table, entry)
  compiler::CodeAssemblerTester asm_tester(isolate,
                                           JSParameterCount(kDeleteParams));
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<IntPtrT> entry = m.SmiToIntPtr(m.Parameter<Smi>(2));

    TVariable<SwissNameDictionary> shrunk_table_var(table, &m);
    Label done(&m);

    m.SwissNameDictionaryDelete(table, entry, &done, &shrunk_table_var);
    m.Goto(&done);

    m.Bind(&done);
    m.Return(shrunk_table_var.value());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_add(Isolate* isolate) {
  // TODO(v8:11330): Remove once CSA implementation has a fallback for
  // non-SSSE3/AVX configurations.
  if (!IsEnabled()) {
    return isolate->builtins()->code_handle(Builtin::kIllegal);
  }
  static_assert(kAddParams == 4);  // (table, key, value, details)
  compiler::CodeAssemblerTester asm_tester(isolate,
                                           JSParameterCount(kAddParams));
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Name> key = m.Parameter<Name>(2);
    TNode<Object> value = m.Parameter<Object>(3);
    TNode<Smi> details = m.Parameter<Smi>(4);

    Label needs_resize(&m);

    TNode<Int32T> d32 = m.SmiToInt32(details);
    TNode<Uint8T> d = m.UncheckedCast<Uint8T>(d32);

    m.SwissNameDictionaryAdd(table, key, value, d, &needs_resize);
    m.Return(m.TrueConstant());

    m.Bind(&needs_resize);
    m.Return(m.FalseConstant());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_allocate(Isolate* isolate) {
  static_assert(kAllocateParams == 1);  // (capacity)
  compiler::CodeAssemblerTester asm_tester(isolate,
                                           JSParameterCount(kAllocateParams));
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<IntPtrT> capacity = m.SmiToIntPtr(m.Parameter<Smi>(1));

    TNode<SwissNameDictionary> table =
        m.AllocateSwissNameDictionaryWithCapacity(capacity);

    m.Return(table);
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_get_counts(Isolate* isolate) {
  static_assert(kGetCountsParams == 1);  // (table)
  compiler::CodeAssemblerTester asm_tester(isolate,
                                           JSParameterCount(kGetCountsParams));
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);

    TNode<IntPtrT> capacity =
        m.ChangeInt32ToIntPtr(m.LoadSwissNameDictionaryCapacity(table));
    TNode<IntPtrT> elements =
        m.LoadSwissNameDictionaryNumberOfElements(table, capacity);
    TNode<IntPtrT> deleted =
        m.LoadSwissNameDictionaryNumberOfDeletedElements(table, capacity);

    TNode<FixedArray> results = m.AllocateZeroedFixedArray(m.IntPtrConstant(3));

    auto check_and_add = [&](TNode<IntPtrT> value, int array_index) {
      CSA_DCHECK(&m, m.UintPtrGreaterThanOrEqual(value, m.IntPtrConstant(0)));
      CSA_DCHECK(&m, m.UintPtrLessThanOrEqual(
                         value, m.IntPtrConstant(Smi::kMaxValue)));
      TNode<Smi> smi = m.SmiFromIntPtr(value);
      m.StoreFixedArrayElement(results, array_index, smi);
    };

    check_and_add(capacity, 0);
    check_and_add(elements, 1);
    check_and_add(deleted, 2);

    m.Return(results);
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_copy(Isolate* isolate) {
  static_assert(kCopyParams == 1);  // (table)
  compiler::CodeAssemblerTester asm_tester(isolate,
                                           JSParameterCount(kCopyParams));
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);

    m.Return(m.CopySwissNameDictionary(table));
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

void CSATestRunner::CheckAgainstReference() {
  CHECK(table->EqualsForTesting(*reference_));
}

// Executes the tests defined in test-swiss-name-dictionary-shared-tests.h as if
// they were defined in this file, using the CSATestRunner. See comments in
// test-swiss-name-dictionary-shared-tests.h and in
// swiss-name-dictionary-infra.h for details.
const char kCSATestFileName[] = __FILE__;
SharedSwissTableTests<CSATestRunner, kCSATestFileName> execute_shared_tests_csa;

#endif

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8
