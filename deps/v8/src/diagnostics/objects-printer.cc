// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>
#include <memory>
#include <optional>

#include "src/api/api-arguments.h"
#include "src/common/globals.h"
#include "src/diagnostics/disasm.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/heap/heap-inl.h"                // For InOldSpace.
#include "src/heap/heap-write-barrier-inl.h"  // For GetIsolateFromWritableObj.
#include "src/heap/marking-inl.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/init/bootstrapper.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects/all-objects-inl.h"
#include "src/objects/code-kind.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-objects.h"
#include "src/regexp/regexp.h"
#include "src/snapshot/embedded/embedded-data.h"
#include "src/strings/string-stream.h"
#include "src/utils/ostreams.h"
#include "third_party/fp16/src/include/fp16.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/debug/debug-wasm-objects-inl.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-objects-inl.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8::internal {

namespace {
constexpr char kUnavailableString[] = "unavailable";
}  // namespace

#ifdef OBJECT_PRINT

void Print(Tagged<Object> obj) {
  // Output into debugger's command window if a debugger is attached.
  DbgStdoutStream dbg_os;
  Print(obj, dbg_os);
  dbg_os << std::flush;

  StdoutStream os;
  Print(obj, os);
  os << std::flush;
}

void Print(Tagged<Object> obj, std::ostream& os) {
  if (IsSmi(obj)) {
    os << "Smi: " << std::hex << "0x" << Smi::ToInt(obj);
    os << std::dec << " (" << Smi::ToInt(obj) << ")\n";
  } else {
    Cast<HeapObject>(obj)->HeapObjectPrint(os);
  }
}

namespace {

#define AS_PTR(x) reinterpret_cast<void*>(x)
#define AS_OBJ(x) Brief(Tagged<Object>(x))

void PrintFunctionCallbackInfo(Address* implicit_args, Address* js_args,
                               Address length, std::ostream& os) {
  using FCA = FunctionCallbackArguments;

  static_assert(FCA::kArgsLength == 6);
  os << "FunctionCallbackInfo: "  //
     << "\n - isolate: " << AS_PTR(implicit_args[FCA::kIsolateIndex])
     << "\n - return_value: " << AS_OBJ(implicit_args[FCA::kReturnValueIndex])
     << "\n - target: " << AS_OBJ(implicit_args[FCA::kTargetIndex])
     << "\n - new_target: " << AS_OBJ(implicit_args[FCA::kNewTargetIndex])
     << "\n - holder: " << AS_OBJ(implicit_args[FCA::kHolderIndex])

     << "\n - argc: " << length  //
     << "\n - receiver: " << AS_OBJ(js_args[0]);

  constexpr int kMaxArgs = 4;
  for (int i = 0; i < std::min(static_cast<int>(length), kMaxArgs); i++) {
    os << "\n - arg[" << i << "]: " << AS_OBJ(js_args[i]);
  }
  os << "\n";
}

void PrintPropertyCallbackInfo(Address* args, std::ostream& os) {
  using PCA = internal::PropertyCallbackArguments;

  static_assert(PCA::kArgsLength == 8);
  os << "PropertyCallbackInfo: "  //
     << "\n - isolate: " << AS_PTR(args[PCA::kIsolateIndex])
     << "\n - return_value: " << AS_OBJ(args[PCA::kReturnValueIndex])
     << "\n - should_throw: " << AS_OBJ(args[PCA::kShouldThrowOnErrorIndex])
     << "\n - holder: " << AS_OBJ(args[PCA::kHolderIndex])
     << "\n - holderV2: " << AS_OBJ(args[PCA::kHolderV2Index])
     << "\n - data: " << AS_OBJ(args[PCA::kDataIndex])  //
     << "\n - property_key: " << AS_OBJ(args[PCA::kPropertyKeyIndex])
     << "\n - receiver: " << AS_OBJ(args[PCA::kThisIndex]);

  // In case it's a setter call there will be additional |value| parameter,
  // print it as a raw pointer to avoid crashing.
  os << "\n - value?: " << AS_PTR(args[PCA::kArgsLength]);
  os << "\n";
}

#undef AS_PTR
#undef AS_OBJ

}  // namespace

void PrintFunctionCallbackInfo(void* function_callback_info) {
  using FCI = v8::FunctionCallbackInfo<v8::Value>;
  FCI& info = *reinterpret_cast<FCI*>(function_callback_info);

  // |values| points to the first argument after the receiver.
  Address* js_args = info.values_ - 1;

  // Output into debugger's command window if a debugger is attached.
  DbgStdoutStream dbg_os;
  PrintFunctionCallbackInfo(info.implicit_args_, js_args, info.length_, dbg_os);
  dbg_os << std::flush;

  StdoutStream os;
  PrintFunctionCallbackInfo(info.implicit_args_, js_args, info.length_, os);
  os << std::flush;
}

void PrintPropertyCallbackInfo(void* property_callback_info) {
  using PCI = v8::PropertyCallbackInfo<v8::Value>;
  PCI& info = *reinterpret_cast<PCI*>(property_callback_info);

  // Output into debugger's command window if a debugger is attached.
  DbgStdoutStream dbg_os;
  PrintPropertyCallbackInfo(info.args_, dbg_os);
  dbg_os << std::flush;

  StdoutStream os;
  PrintPropertyCallbackInfo(info.args_, os);
  os << std::flush;
}

namespace {

void PrintHeapObjectHeaderWithoutMap(Tagged<HeapObject> object,
                                     std::ostream& os, const char* id) {
  PtrComprCageBase cage_base = GetPtrComprCageBase();
  os << reinterpret_cast<void*>(object.ptr()) << ": [";
  if (id != nullptr) {
    os << id;
  } else {
    os << object->map(cage_base)->instance_type();
  }
  os << "]";
  if (ReadOnlyHeap::Contains(object)) {
    os << " in ReadOnlySpace";
  } else if (GetHeapFromWritableObject(object)->InOldSpace(object)) {
    os << " in OldSpace";
  }
}

template <typename T>
void PrintDictionaryContents(std::ostream& os, Tagged<T> dict) {
  DisallowGarbageCollection no_gc;
  ReadOnlyRoots roots = dict->GetReadOnlyRoots();

  if (dict->Capacity() == 0) {
    return;
  }

#ifdef V8_ENABLE_SWISS_NAME_DICTIONARY
  Isolate* isolate = GetIsolateFromWritableObject(dict);
  // IterateEntries for SwissNameDictionary needs to create a handle.
  HandleScope scope(isolate);
#endif
  for (InternalIndex i : dict->IterateEntries()) {
    Tagged<Object> k;
    if (!dict->ToKey(roots, i, &k)) continue;
    os << "\n   ";
    if (IsString(k)) {
      Cast<String>(k)->PrintUC16(os);
    } else {
      os << Brief(k);
    }
    os << ": " << Brief(dict->ValueAt(i)) << " ";
    dict->DetailsAt(i).PrintAsSlowTo(os, !T::kIsOrderedDictionaryType);
  }
}
}  // namespace

void HeapObjectLayout::PrintHeader(std::ostream& os, const char* id) {
  Tagged<HeapObject>(this)->PrintHeader(os, id);
}

void HeapObject::PrintHeader(std::ostream& os, const char* id) {
  PrintHeapObjectHeaderWithoutMap(*this, os, id);
  PtrComprCageBase cage_base = GetPtrComprCageBase();
  if (!SafeEquals(GetReadOnlyRoots().meta_map())) {
    os << "\n - map: " << Brief(map(cage_base));
  }
}

void HeapObject::HeapObjectPrint(std::ostream& os) {
  PtrComprCageBase cage_base = GetPtrComprCageBase();

  InstanceType instance_type = map(cage_base)->instance_type();

  if (instance_type < FIRST_NONSTRING_TYPE) {
    Cast<String>(*this)->StringPrint(os);
    os << "\n";
    return;
  }

  switch (instance_type) {
    case AWAIT_CONTEXT_TYPE:
    case BLOCK_CONTEXT_TYPE:
    case CATCH_CONTEXT_TYPE:
    case DEBUG_EVALUATE_CONTEXT_TYPE:
    case EVAL_CONTEXT_TYPE:
    case FUNCTION_CONTEXT_TYPE:
    case MODULE_CONTEXT_TYPE:
    case SCRIPT_CONTEXT_TYPE:
    case WITH_CONTEXT_TYPE:
      Cast<Context>(*this)->ContextPrint(os);
      break;
    case NATIVE_CONTEXT_TYPE:
      Cast<NativeContext>(*this)->NativeContextPrint(os);
      break;
    case HASH_TABLE_TYPE:
      Cast<ObjectHashTable>(*this)->ObjectHashTablePrint(os);
      break;
    case NAME_TO_INDEX_HASH_TABLE_TYPE:
      Cast<NameToIndexHashTable>(*this)->NameToIndexHashTablePrint(os);
      break;
    case REGISTERED_SYMBOL_TABLE_TYPE:
      Cast<RegisteredSymbolTable>(*this)->RegisteredSymbolTablePrint(os);
      break;
    case ORDERED_HASH_MAP_TYPE:
      Cast<OrderedHashMap>(*this)->OrderedHashMapPrint(os);
      break;
    case ORDERED_HASH_SET_TYPE:
      Cast<OrderedHashSet>(*this)->OrderedHashSetPrint(os);
      break;
    case ORDERED_NAME_DICTIONARY_TYPE:
      Cast<OrderedNameDictionary>(*this)->OrderedNameDictionaryPrint(os);
      break;
    case NAME_DICTIONARY_TYPE:
      Cast<NameDictionary>(*this)->NameDictionaryPrint(os);
      break;
    case GLOBAL_DICTIONARY_TYPE:
      Cast<GlobalDictionary>(*this)->GlobalDictionaryPrint(os);
      break;
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
      Cast<FixedArray>(*this)->FixedArrayPrint(os);
      break;
    case NUMBER_DICTIONARY_TYPE:
      Cast<NumberDictionary>(*this)->NumberDictionaryPrint(os);
      break;
    case EPHEMERON_HASH_TABLE_TYPE:
      Cast<EphemeronHashTable>(*this)->EphemeronHashTablePrint(os);
      break;
    case TRANSITION_ARRAY_TYPE:
      Cast<TransitionArray>(*this)->TransitionArrayPrint(os);
      break;
    case FILLER_TYPE:
      os << "filler";
      break;
    case JS_API_OBJECT_TYPE:
    case JS_ARRAY_ITERATOR_PROTOTYPE_TYPE:
    case JS_CONTEXT_EXTENSION_OBJECT_TYPE:
    case JS_ERROR_TYPE:
    case JS_ITERATOR_PROTOTYPE_TYPE:
    case JS_MAP_ITERATOR_PROTOTYPE_TYPE:
    case JS_OBJECT_PROTOTYPE_TYPE:
    case JS_PROMISE_PROTOTYPE_TYPE:
    case JS_REG_EXP_PROTOTYPE_TYPE:
    case JS_SET_ITERATOR_PROTOTYPE_TYPE:
    case JS_SET_PROTOTYPE_TYPE:
    case JS_SPECIAL_API_OBJECT_TYPE:
    case JS_STRING_ITERATOR_PROTOTYPE_TYPE:
    case JS_TYPED_ARRAY_PROTOTYPE_TYPE:
      Cast<JSObject>(*this)->JSObjectPrint(os);
      break;
#if V8_ENABLE_WEBASSEMBLY
    case WASM_TRUSTED_INSTANCE_DATA_TYPE:
      Cast<WasmTrustedInstanceData>(*this)->WasmTrustedInstanceDataPrint(os);
      break;
    case WASM_DISPATCH_TABLE_TYPE:
      Cast<WasmDispatchTable>(*this)->WasmDispatchTablePrint(os);
      break;
    case WASM_VALUE_OBJECT_TYPE:
      Cast<WasmValueObject>(*this)->WasmValueObjectPrint(os);
      break;
    case WASM_EXCEPTION_PACKAGE_TYPE:
      Cast<WasmExceptionPackage>(*this)->WasmExceptionPackagePrint(os);
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case INSTRUCTION_STREAM_TYPE:
      Cast<InstructionStream>(*this)->InstructionStreamPrint(os);
      break;
    case CODE_TYPE:
      Cast<Code>(*this)->CodePrint(os);
      break;
    case CODE_WRAPPER_TYPE:
      Cast<CodeWrapper>(*this)->CodeWrapperPrint(os);
      break;
    case JS_SET_KEY_VALUE_ITERATOR_TYPE:
    case JS_SET_VALUE_ITERATOR_TYPE:
      Cast<JSSetIterator>(*this)->JSSetIteratorPrint(os);
      break;
    case JS_MAP_KEY_ITERATOR_TYPE:
    case JS_MAP_KEY_VALUE_ITERATOR_TYPE:
    case JS_MAP_VALUE_ITERATOR_TYPE:
      Cast<JSMapIterator>(*this)->JSMapIteratorPrint(os);
      break;
#define MAKE_TORQUE_CASE(Name, TYPE)    \
  case TYPE:                            \
    Cast<Name>(*this)->Name##Print(os); \
    break;
      // Every class that has its fields defined in a .tq file and corresponds
      // to exactly one InstanceType value is included in the following list.
      TORQUE_INSTANCE_CHECKERS_SINGLE_FULLY_DEFINED(MAKE_TORQUE_CASE)
      TORQUE_INSTANCE_CHECKERS_MULTIPLE_FULLY_DEFINED(MAKE_TORQUE_CASE)
#undef MAKE_TORQUE_CASE

    case ALLOCATION_SITE_TYPE:
      Cast<AllocationSite>(*this)->AllocationSitePrint(os);
      break;
    case LOAD_HANDLER_TYPE:
      Cast<LoadHandler>(*this)->LoadHandlerPrint(os);
      break;
    case STORE_HANDLER_TYPE:
      Cast<StoreHandler>(*this)->StoreHandlerPrint(os);
      break;
    case FEEDBACK_METADATA_TYPE:
      Cast<FeedbackMetadata>(*this)->FeedbackMetadataPrint(os);
      break;
    case BIG_INT_BASE_TYPE:
      Cast<BigIntBase>(*this)->BigIntBasePrint(os);
      break;
    case JS_CLASS_CONSTRUCTOR_TYPE:
    case JS_PROMISE_CONSTRUCTOR_TYPE:
    case JS_REG_EXP_CONSTRUCTOR_TYPE:
    case JS_ARRAY_CONSTRUCTOR_TYPE:
#define TYPED_ARRAY_CONSTRUCTORS_SWITCH(Type, type, TYPE, Ctype) \
  case TYPE##_TYPED_ARRAY_CONSTRUCTOR_TYPE:
      TYPED_ARRAYS(TYPED_ARRAY_CONSTRUCTORS_SWITCH)
#undef TYPED_ARRAY_CONSTRUCTORS_SWITCH
      Cast<JSFunction>(*this)->JSFunctionPrint(os);
      break;
    case INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case SEQ_TWO_BYTE_STRING_TYPE:
    case CONS_TWO_BYTE_STRING_TYPE:
    case EXTERNAL_TWO_BYTE_STRING_TYPE:
    case SLICED_TWO_BYTE_STRING_TYPE:
    case THIN_TWO_BYTE_STRING_TYPE:
    case SEQ_ONE_BYTE_STRING_TYPE:
    case CONS_ONE_BYTE_STRING_TYPE:
    case EXTERNAL_ONE_BYTE_STRING_TYPE:
    case SLICED_ONE_BYTE_STRING_TYPE:
    case THIN_ONE_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE:
    case SHARED_SEQ_TWO_BYTE_STRING_TYPE:
    case SHARED_SEQ_ONE_BYTE_STRING_TYPE:
    case SHARED_EXTERNAL_TWO_BYTE_STRING_TYPE:
    case SHARED_EXTERNAL_ONE_BYTE_STRING_TYPE:
    case SHARED_UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE:
    case SHARED_UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE:
    case JS_LAST_DUMMY_API_OBJECT_TYPE:
      // TODO(all): Handle these types too.
      os << "UNKNOWN TYPE " << map()->instance_type();
      UNREACHABLE();
  }
}

void ByteArray::ByteArrayPrint(std::ostream& os) {
  PrintHeader(os, "ByteArray");
  os << "\n - length: " << length()
     << "\n - begin: " << static_cast<void*>(begin()) << "\n";
}

void TrustedByteArray::TrustedByteArrayPrint(std::ostream& os) {
  PrintHeader(os, "TrustedByteArray");
  os << "\n - length: " << length()
     << "\n - begin: " << static_cast<void*>(begin()) << "\n";
}

void BytecodeArray::BytecodeArrayPrint(std::ostream& os) {
  PrintHeader(os, "BytecodeArray");
  os << "\n";
  Disassemble(os);
}

void BytecodeWrapper::BytecodeWrapperPrint(std::ostream& os) {
  PrintHeader(os, "BytecodeWrapper");
  Isolate* isolate = GetIsolateForSandbox(*this);
  os << "\n    bytecode: " << Brief(bytecode(isolate));
}

void FreeSpace::FreeSpacePrint(std::ostream& os) {
  os << "free space, size " << Size() << "\n";
}

bool JSObject::PrintProperties(std::ostream& os) {
  if (HasFastProperties()) {
    Tagged<DescriptorArray> descs = map()->instance_descriptors(GetIsolate());
    int nof_inobject_properties = map()->GetInObjectProperties();
    for (InternalIndex i : map()->IterateOwnDescriptors()) {
      os << "\n    ";
      descs->GetKey(i)->NamePrint(os);
      os << ": ";
      PropertyDetails details = descs->GetDetails(i);
      switch (details.location()) {
        case PropertyLocation::kField: {
          FieldIndex field_index = FieldIndex::ForDetails(map(), details);
          os << Brief(RawFastPropertyAt(field_index));
          break;
        }
        case PropertyLocation::kDescriptor:
          os << Brief(descs->GetStrongValue(i));
          break;
      }
      os << " ";
      details.PrintAsFastTo(os, PropertyDetails::kForProperties);
      if (details.location() == PropertyLocation::kField) {
        os << " @ ";
        FieldType::PrintTo(descs->GetFieldType(i), os);
        int field_index = details.field_index();
        if (field_index < nof_inobject_properties) {
          os << ", location: in-object";
        } else {
          field_index -= nof_inobject_properties;
          os << ", location: properties[" << field_index << "]";
        }
      } else {
        os << ", location: descriptor";
      }
    }
    return map()->NumberOfOwnDescriptors() > 0;
  } else if (IsJSGlobalObject(*this)) {
    PrintDictionaryContents(
        os, Cast<JSGlobalObject>(*this)->global_dictionary(kAcquireLoad));
  } else if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    PrintDictionaryContents(os, property_dictionary_swiss());
  } else {
    PrintDictionaryContents(os, property_dictionary());
  }
  return true;
}

namespace {

template <class T>
bool IsTheHoleAt(Tagged<T> array, int index) {
  return false;
}

template <>
bool IsTheHoleAt(Tagged<FixedDoubleArray> array, int index) {
  return array->is_the_hole(index);
}

template <class T>
double GetScalarElement(Tagged<T> array, int index) {
  if (IsTheHoleAt(array, index)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return array->get_scalar(index);
}

template <class T>
void DoPrintElements(std::ostream& os, Tagged<Object> object, int length) {
  const bool print_the_hole = std::is_same<T, FixedDoubleArray>::value;
  Tagged<T> array = Cast<T>(object);
  if (length == 0) return;
  int previous_index = 0;
  double previous_value = GetScalarElement(array, 0);
  double value = 0.0;
  int i;
  for (i = 1; i <= length; i++) {
    if (i < length) value = GetScalarElement(array, i);
    bool values_are_nan = std::isnan(previous_value) && std::isnan(value);
    if (i != length && (previous_value == value || values_are_nan) &&
        IsTheHoleAt(array, i - 1) == IsTheHoleAt(array, i)) {
      continue;
    }
    os << "\n";
    std::stringstream ss;
    ss << previous_index;
    if (previous_index != i - 1) {
      ss << '-' << (i - 1);
    }
    os << std::setw(12) << ss.str() << ": ";
    if (print_the_hole && IsTheHoleAt(array, i - 1)) {
      os << "<the_hole>";
    } else {
      os << previous_value;
    }
    previous_index = i;
    previous_value = value;
  }
}

struct Fp16Printer {
  uint16_t val;
  explicit Fp16Printer(float f) : val(fp16_ieee_from_fp32_value(f)) {}
  operator float() const { return fp16_ieee_to_fp32_value(val); }
};

template <typename ElementType>
void PrintTypedArrayElements(std::ostream& os, const ElementType* data_ptr,
                             size_t length, bool is_on_heap) {
  if (length == 0) return;
  size_t previous_index = 0;
  if (i::v8_flags.mock_arraybuffer_allocator && !is_on_heap) {
    // Don't try to print data that's not actually allocated.
    os << "\n    0-" << length << ": <mocked array buffer bytes>";
    return;
  }

  ElementType previous_value = data_ptr[0];
  ElementType value{0};
  for (size_t i = 1; i <= length; i++) {
    if (i < length) value = data_ptr[i];
    if (i != length && previous_value == value) {
      continue;
    }
    os << "\n";
    std::stringstream ss;
    ss << previous_index;
    if (previous_index != i - 1) {
      ss << '-' << (i - 1);
    }
    os << std::setw(12) << ss.str() << ": " << +previous_value;
    previous_index = i;
    previous_value = value;
  }
}

template <typename T>
void PrintFixedArrayElements(std::ostream& os, Tagged<T> array, int capacity,
                             Tagged<Object> (*get)(Tagged<T>, int)) {
  // Print in array notation for non-sparse arrays.
  if (capacity == 0) return;
  Tagged<Object> previous_value = get(array, 0);
  Tagged<Object> value;
  int previous_index = 0;
  int i;
  for (i = 1; i <= capacity; i++) {
    if (i < capacity) value = get(array, i);
    if (previous_value == value && i != capacity) {
      continue;
    }
    os << "\n";
    std::stringstream ss;
    ss << previous_index;
    if (previous_index != i - 1) {
      ss << '-' << (i - 1);
    }
    os << std::setw(12) << ss.str() << ": " << Brief(previous_value);
    previous_index = i;
    previous_value = value;
  }
}

template <typename T>
void PrintFixedArrayElements(std::ostream& os, Tagged<T> array) {
  PrintFixedArrayElements<T>(
      os, array, array->length(),
      [](Tagged<T> xs, int i) { return Cast<Object>(xs->get(i)); });
}

void PrintDictionaryElements(std::ostream& os,
                             Tagged<FixedArrayBase> elements) {
  // Print some internal fields
  Tagged<NumberDictionary> dict = Cast<NumberDictionary>(elements);
  if (dict->requires_slow_elements()) {
    os << "\n   - requires_slow_elements";
  } else {
    os << "\n   - max_number_key: " << dict->max_number_key();
  }
  PrintDictionaryContents(os, dict);
}

void PrintSloppyArgumentElements(std::ostream& os, ElementsKind kind,
                                 Tagged<SloppyArgumentsElements> elements) {
  Tagged<FixedArray> arguments_store = elements->arguments();
  os << "\n    0: context: " << Brief(elements->context())
     << "\n    1: arguments_store: " << Brief(arguments_store)
     << "\n    parameter to context slot map:";
  for (int i = 0; i < elements->length(); i++) {
    Tagged<Object> mapped_entry = elements->mapped_entries(i, kRelaxedLoad);
    os << "\n    " << i << ": param(" << i << "): " << Brief(mapped_entry);
    if (IsTheHole(mapped_entry)) {
      os << " in the arguments_store[" << i << "]";
    } else {
      os << " in the context";
    }
  }
  if (arguments_store->length() == 0) return;
  os << "\n }"
     << "\n - arguments_store: " << Brief(arguments_store) << " "
     << ElementsKindToString(arguments_store->map()->elements_kind()) << " {";
  if (kind == FAST_SLOPPY_ARGUMENTS_ELEMENTS) {
    PrintFixedArrayElements(os, arguments_store);
  } else {
    DCHECK_EQ(kind, SLOW_SLOPPY_ARGUMENTS_ELEMENTS);
    PrintDictionaryElements(os, arguments_store);
  }
}

void PrintEmbedderData(Isolate* isolate, std::ostream& os,
                       EmbedderDataSlot slot) {
  DisallowGarbageCollection no_gc;
  Tagged<Object> value = slot.load_tagged();
  os << Brief(value);
  void* raw_pointer;
  if (slot.ToAlignedPointer(isolate, &raw_pointer)) {
    os << ", aligned pointer: " << raw_pointer;
  }
}

}  // namespace

void JSObject::PrintElements(std::ostream& os) {
  // Don't call GetElementsKind, its validation code can cause the printer to
  // fail when debugging.
  os << " - elements: " << Brief(elements()) << " {";
  switch (map()->elements_kind()) {
    case HOLEY_SMI_ELEMENTS:
    case PACKED_SMI_ELEMENTS:
    case HOLEY_ELEMENTS:
    case HOLEY_FROZEN_ELEMENTS:
    case HOLEY_SEALED_ELEMENTS:
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
    case PACKED_ELEMENTS:
    case PACKED_FROZEN_ELEMENTS:
    case PACKED_SEALED_ELEMENTS:
    case PACKED_NONEXTENSIBLE_ELEMENTS:
    case FAST_STRING_WRAPPER_ELEMENTS:
    case SHARED_ARRAY_ELEMENTS: {
      PrintFixedArrayElements(os, Cast<FixedArray>(elements()));
      break;
    }
    case HOLEY_DOUBLE_ELEMENTS:
    case PACKED_DOUBLE_ELEMENTS: {
      DoPrintElements<FixedDoubleArray>(os, elements(), elements()->length());
      break;
    }

#define PRINT_ELEMENTS(Type, type, TYPE, elementType)                          \
  case TYPE##_ELEMENTS: {                                                      \
    size_t length = Cast<JSTypedArray>(*this)->GetLength();                    \
    bool is_on_heap = Cast<JSTypedArray>(*this)->is_on_heap();                 \
    const elementType* data_ptr =                                              \
        static_cast<const elementType*>(Cast<JSTypedArray>(*this)->DataPtr()); \
    PrintTypedArrayElements<elementType>(os, data_ptr, length, is_on_heap);    \
    break;                                                                     \
  }
      TYPED_ARRAYS(PRINT_ELEMENTS)
      RAB_GSAB_TYPED_ARRAYS(PRINT_ELEMENTS)
#undef PRINT_ELEMENTS

    case DICTIONARY_ELEMENTS:
    case SLOW_STRING_WRAPPER_ELEMENTS:
      PrintDictionaryElements(os, elements());
      break;
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
      PrintSloppyArgumentElements(os, map()->elements_kind(),
                                  Cast<SloppyArgumentsElements>(elements()));
      break;
    case WASM_ARRAY_ELEMENTS:
      // WasmArrayPrint() should be called intead.
      UNREACHABLE();
    case NO_ELEMENTS:
      break;
  }
  os << "\n }\n";
}

namespace {

void JSObjectPrintHeader(std::ostream& os, Tagged<JSObject> obj,
                         const char* id) {
  Isolate* isolate = obj->GetIsolate();
  obj->PrintHeader(os, id);
  // Don't call GetElementsKind, its validation code can cause the printer to
  // fail when debugging.
  os << " [";
  if (obj->HasFastProperties()) {
    os << "FastProperties";
  } else {
    os << "DictionaryProperties";
  }
  PrototypeIterator iter(isolate, obj);
  os << "]\n - prototype: " << Brief(iter.GetCurrent());
  os << "\n - elements: " << Brief(obj->elements()) << " ["
     << ElementsKindToString(obj->map()->elements_kind());
  if (obj->elements()->IsCowArray()) os << " (COW)";
  os << "]";
  Tagged<Object> hash = Object::GetHash(obj);
  if (IsSmi(hash)) {
    os << "\n - hash: " << Brief(hash);
  }
  if (obj->GetEmbedderFieldCount() > 0) {
    os << "\n - embedder fields: " << obj->GetEmbedderFieldCount();
  }
}

void JSAPIObjectWithEmbedderSlotsPrintHeader(std::ostream& os,
                                             Tagged<JSObject> obj,
                                             const char* id = nullptr) {
  JSObjectPrintHeader(os, obj, id);
  os << "\n - cpp_heap_wrappable: "
     << obj->ReadField<uint32_t>(
            JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset);
}

void JSObjectPrintBody(std::ostream& os, Tagged<JSObject> obj,
                       bool print_elements = true) {
  os << "\n - properties: ";
  Tagged<Object> properties_or_hash = obj->raw_properties_or_hash(kRelaxedLoad);
  if (!IsSmi(properties_or_hash)) {
    os << Brief(properties_or_hash);
  }
  os << "\n - All own properties (excluding elements): {";
  if (obj->PrintProperties(os)) os << "\n ";
  os << "}\n";

  if (print_elements) {
    size_t length = IsJSTypedArray(obj) ? Cast<JSTypedArray>(obj)->GetLength()
                                        : obj->elements()->length();
    if (length > 0) obj->PrintElements(os);
  }
  int embedder_fields = obj->GetEmbedderFieldCount();
  if (embedder_fields > 0) {
    Isolate* isolate = GetIsolateForSandbox(obj);
    os << " - embedder fields = {";
    for (int i = 0; i < embedder_fields; i++) {
      os << "\n    ";
      PrintEmbedderData(isolate, os, EmbedderDataSlot(obj, i));
    }
    os << "\n }\n";
  }
}

}  // namespace

void JSObject::JSObjectPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, nullptr);
  JSObjectPrintBody(os, *this);
}

void JSExternalObject::JSExternalObjectPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, nullptr);
  os << "\n - external value: " << value();
  JSObjectPrintBody(os, *this);
}

void JSGeneratorObject::JSGeneratorObjectPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSGeneratorObject");
  os << "\n - function: " << Brief(function());
  os << "\n - context: " << Brief(context());
  os << "\n - receiver: " << Brief(receiver());
  if (is_executing() || is_closed()) {
    os << "\n - input: " << Brief(input_or_debug_pos());
  } else {
    DCHECK(is_suspended());
    os << "\n - debug pos: " << Brief(input_or_debug_pos());
  }
  const char* mode = "(invalid)";
  switch (resume_mode()) {
    case kNext:
      mode = ".next()";
      break;
    case kReturn:
      mode = ".return()";
      break;
    case kThrow:
      mode = ".throw()";
      break;
  }
  os << "\n - resume mode: " << mode;
  os << "\n - continuation: " << continuation();
  if (is_closed()) os << " (closed)";
  if (is_executing()) os << " (executing)";
  if (is_suspended()) os << " (suspended)";
  if (is_suspended()) {
    DisallowGarbageCollection no_gc;
    Tagged<SharedFunctionInfo> fun_info = function()->shared();
    if (fun_info->HasSourceCode()) {
      Tagged<Script> script = Cast<Script>(fun_info->script());
      Tagged<String> script_name = IsString(script->name())
                                       ? Cast<String>(script->name())
                                       : GetReadOnlyRoots().empty_string();

      os << "\n - source position: ";
      // Can't collect source positions here if not available as that would
      // allocate memory.
      Isolate* isolate = GetIsolate();
      if (fun_info->HasBytecodeArray() &&
          fun_info->GetBytecodeArray(isolate)->HasSourcePositionTable()) {
        os << source_position();
        os << " (";
        script_name->PrintUC16(os);
        Script::PositionInfo info;
        script->GetPositionInfo(source_position(), &info);
        os << ", line " << info.line + 1;
        os << ", column " << info.column + 1;
      } else {
        os << kUnavailableString;
      }
      os << ")";
    }
  }
  os << "\n - register file: " << Brief(parameters_and_registers());
  JSObjectPrintBody(os, *this);
}

void JSArray::JSArrayPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSArray");
  os << "\n - length: " << Brief(this->length());
  JSObjectPrintBody(os, *this);
}

void JSPromise::JSPromisePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSPromise");
  os << "\n - status: " << JSPromise::Status(status());
  if (status() == Promise::kPending) {
    os << "\n - reactions: " << Brief(reactions());
  } else {
    os << "\n - result: " << Brief(result());
  }
  os << "\n - has_handler: " << has_handler();
  os << "\n - is_silent: " << is_silent();
  JSObjectPrintBody(os, *this);
}

void JSRegExp::JSRegExpPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSRegExp");
  Isolate* isolate = GetIsolateForSandbox(*this);
  os << "\n - data: " << Brief(data(isolate));
  os << "\n - source: " << Brief(source());
  FlagsBuffer buffer;
  os << "\n - flags: " << JSRegExp::FlagsToString(flags(), &buffer);
  JSObjectPrintBody(os, *this);
}

void RegExpData::RegExpDataPrint(std::ostream& os) {
  switch (type_tag()) {
    case RegExpData::Type::ATOM:
      PrintHeader(os, "AtomRegExpData");
      break;
    case RegExpData::Type::IRREGEXP:
      PrintHeader(os, "IrRegExpData");
      break;
    case RegExpData::Type::EXPERIMENTAL:
      PrintHeader(os, "IrRegExpData");
      break;
    default:
      UNREACHABLE();
  }
  os << "\n - source: " << source();
  JSRegExp::FlagsBuffer buffer;
  os << "\n - flags: " << JSRegExp::FlagsToString(flags(), &buffer);
}

void AtomRegExpData::AtomRegExpDataPrint(std::ostream& os) {
  RegExpDataPrint(os);
  os << "\n - pattern: " << pattern();
  os << "\n";
}

void IrRegExpData::IrRegExpDataPrint(std::ostream& os) {
  Isolate* isolate = GetIsolateForSandbox(*this);
  RegExpDataPrint(os);
  if (has_latin1_bytecode()) {
    os << "\n - latin1_bytecode: " << Brief(latin1_bytecode());
  }
  if (has_uc16_bytecode()) {
    os << "\n - uc16_bytecode: " << Brief(uc16_bytecode());
  }
  if (has_latin1_code()) {
    os << "\n - latin1_code: " << Brief(latin1_code(isolate));
  }
  if (has_uc16_code()) {
    os << "\n - uc16_code: " << Brief(uc16_code(isolate));
  }
  os << "\n - capture_name_map: " << Brief(capture_name_map());
  os << "\n - max_register_count: " << max_register_count();
  os << "\n - capture_count: " << max_register_count();
  os << "\n - ticks_until_tier_up: " << max_register_count();
  os << "\n - backtrack_limit: " << max_register_count();
  os << "\n";
}

void RegExpDataWrapper::RegExpDataWrapperPrint(std::ostream& os) {
  PrintHeader(os, "RegExpDataWrapper");
  Isolate* isolate = GetIsolateForSandbox(*this);
  os << "\n    data: " << Brief(data(isolate));
  os << "\n";
}

void JSRegExpStringIterator::JSRegExpStringIteratorPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSRegExpStringIterator");
  os << "\n - regex: " << Brief(iterating_reg_exp());
  os << "\n - string: " << Brief(iterated_string());
  os << "\n - done: " << done();
  os << "\n - global: " << global();
  os << "\n - unicode: " << unicode();
  JSObjectPrintBody(os, *this);
}

void Symbol::SymbolPrint(std::ostream& os) {
  PrintHeader(os, "Symbol");
  os << "\n - hash: " << hash();
  os << "\n - description: " << Brief(description());
  if (IsUndefined(description())) {
    os << " (" << PrivateSymbolToName() << ")";
  }
  os << "\n - private: " << is_private();
  os << "\n - private_name: " << is_private_name();
  os << "\n - private_brand: " << is_private_brand();
  os << "\n - is_interesting_symbol: " << is_interesting_symbol();
  os << "\n - is_well_known_symbol: " << is_well_known_symbol();
  os << "\n";
}

void DescriptorArray::DescriptorArrayPrint(std::ostream& os) {
  PrintHeader(os, "DescriptorArray");
  os << "\n - enum_cache: ";
  if (enum_cache()->keys()->length() == 0) {
    os << "empty";
  } else {
    os << enum_cache()->keys()->length();
    os << "\n   - keys: " << Brief(enum_cache()->keys());
    os << "\n   - indices: " << Brief(enum_cache()->indices());
  }
  os << "\n - nof slack descriptors: " << number_of_slack_descriptors();
  os << "\n - nof descriptors: " << number_of_descriptors();
  const auto raw = raw_gc_state(kRelaxedLoad);
  os << "\n - raw gc state: mc epoch "
     << DescriptorArrayMarkingState::Epoch::decode(raw) << ", marked "
     << DescriptorArrayMarkingState::Marked::decode(raw) << ", delta "
     << DescriptorArrayMarkingState::Delta::decode(raw);
  PrintDescriptors(os);
}

namespace {
template <typename T>
void PrintFixedArrayWithHeader(std::ostream& os, Tagged<T> array,
                               const char* type) {
  array->PrintHeader(os, type);
  os << "\n - length: " << array->length();
  PrintFixedArrayElements(os, array);
  os << "\n";
}

template <typename T>
void PrintWeakArrayElements(std::ostream& os, T* array) {
  // Print in array notation for non-sparse arrays.
  Tagged<MaybeObject> previous_value =
      array->length() > 0 ? array->get(0) : Tagged<MaybeObject>(kNullAddress);
  Tagged<MaybeObject> value;
  int previous_index = 0;
  int i;
  for (i = 1; i <= array->length(); i++) {
    if (i < array->length()) value = array->get(i);
    if (previous_value == value && i != array->length()) {
      continue;
    }
    os << "\n";
    std::stringstream ss;
    ss << previous_index;
    if (previous_index != i - 1) {
      ss << '-' << (i - 1);
    }
    os << std::setw(12) << ss.str() << ": " << Brief(previous_value);
    previous_index = i;
    previous_value = value;
  }
}

}  // namespace

void ObjectBoilerplateDescription::ObjectBoilerplateDescriptionPrint(
    std::ostream& os) {
  PrintHeader(os, "ObjectBoilerplateDescription");
  os << "\n - capacity: " << capacity();
  os << "\n - backing_store_size: " << backing_store_size();
  os << "\n - flags: " << flags();
  os << "\n - elements:";
  PrintFixedArrayElements<ObjectBoilerplateDescription>(
      os, *this, capacity(),
      [](Tagged<ObjectBoilerplateDescription> xs, int i) {
        return xs->get(i);
      });
  os << "\n";
}

void ClassBoilerplate::ClassBoilerplatePrint(std::ostream& os) {
  PrintHeader(os, "ClassBoilerplate");
  os << "\n - arguments_count: " << arguments_count();
  os << "\n - static_properties_template: " << static_properties_template();
  os << "\n - static_elements_template: " << static_elements_template();
  os << "\n - static_computed_properties: " << static_computed_properties();
  os << "\n - instance_properties_template: " << instance_properties_template();
  os << "\n - instance_elements_template: " << instance_elements_template();
  os << "\n - instance_computed_properties: " << instance_computed_properties();
  os << "\n";
}

void RegExpBoilerplateDescription::RegExpBoilerplateDescriptionPrint(
    std::ostream& os) {
  Isolate* isolate = GetIsolateForSandbox((*this));
  PrintHeader(os, "RegExpBoilerplate");
  os << "\n - data: " << Brief(data(isolate));
  os << "\n - source: " << source();
  os << "\n - flags: " << flags();
  os << "\n";
}

void EmbedderDataArray::EmbedderDataArrayPrint(std::ostream& os) {
  Isolate* isolate = GetIsolateForSandbox(*this);
  PrintHeader(os, "EmbedderDataArray");
  os << "\n - length: " << length();
  EmbedderDataSlot start(*this, 0);
  EmbedderDataSlot end(*this, length());
  for (EmbedderDataSlot slot = start; slot < end; ++slot) {
    os << "\n    ";
    PrintEmbedderData(isolate, os, slot);
  }
  os << "\n";
}

void FixedArray::FixedArrayPrint(std::ostream& os) {
  PrintFixedArrayWithHeader(os, Tagged{*this}, "FixedArray");
}

void TrustedFixedArray::TrustedFixedArrayPrint(std::ostream& os) {
  PrintFixedArrayWithHeader(os, Tagged{*this}, "TrustedFixedArray");
}

void ProtectedFixedArray::ProtectedFixedArrayPrint(std::ostream& os) {
  PrintFixedArrayWithHeader(os, Tagged{*this}, "ProtectedFixedArray");
}

void ArrayList::ArrayListPrint(std::ostream& os) {
  PrintHeader(os, "ArrayList");
  os << "\n - capacity: " << capacity();
  os << "\n - length: " << length();
  os << "\n - elements:";
  PrintFixedArrayElements<ArrayList>(
      os, *this, length(),
      [](Tagged<ArrayList> xs, int i) { return xs->get(i); });
  os << "\n";
}

void ScriptContextTable::ScriptContextTablePrint(std::ostream& os) {
  PrintHeader(os, "ScriptContextTable");
  os << "\n - capacity: " << capacity();
  os << "\n - length: " << length(kAcquireLoad);
  os << "\n - names_to_context_index: " << names_to_context_index();
  os << "\n - elements:";
  PrintFixedArrayElements<ScriptContextTable>(
      os, *this, length(kAcquireLoad),
      [](Tagged<ScriptContextTable> xs, int i) {
        return Cast<Object>(xs->get(i));
      });
  os << "\n";
}

void RegExpMatchInfo::RegExpMatchInfoPrint(std::ostream& os) {
  PrintHeader(os, "RegExpMatchInfo");
  os << "\n - capacity: " << capacity();
  os << "\n - number_of_capture_registers: " << number_of_capture_registers();
  os << "\n - last_subject: " << last_subject();
  os << "\n - last_input: " << last_input();
  os << "\n - captures:";
  PrintFixedArrayElements<RegExpMatchInfo>(
      os, Tagged{*this}, capacity(), [](Tagged<RegExpMatchInfo> xs, int i) {
        return Cast<Object>(xs->get(i));
      });
  os << "\n";
}

void ExternalPointerArray::ExternalPointerArrayPrint(std::ostream& os) {
  PrintHeader(os, "ExternalPointerArray");
  os << "\n - length: " << length();
  os << "\n";
}

void SloppyArgumentsElements::SloppyArgumentsElementsPrint(std::ostream& os) {
  PrintHeader(os, "SloppyArgumentsElements");
  os << "\n - length: " << length();
  os << "\n - context: " << Brief(context());
  os << "\n - arguments: " << Brief(arguments());
  os << "\n - mapped_entries:";
  PrintFixedArrayElements<SloppyArgumentsElements>(
      os, Tagged(*this), length(),
      [](Tagged<SloppyArgumentsElements> xs, int i) {
        return xs->mapped_entries(i, kRelaxedLoad);
      });
  os << '\n';
}

namespace {
const char* SideEffectType2String(SideEffectType type) {
  switch (type) {
    case SideEffectType::kHasSideEffect:
      return "kHasSideEffect";
    case SideEffectType::kHasNoSideEffect:
      return "kHasNoSideEffect";
    case SideEffectType::kHasSideEffectToReceiver:
      return "kHasSideEffectToReceiver";
  }
}
}  // namespace

void AccessorInfo::AccessorInfoPrint(std::ostream& os) {
  TorqueGeneratedAccessorInfo<AccessorInfo, HeapObject>::AccessorInfoPrint(os);
  os << " - is_sloppy: " << is_sloppy();
  os << "\n - replace_on_access: " << replace_on_access();
  os << "\n - getter_side_effect_type: "
     << SideEffectType2String(getter_side_effect_type());
  os << "\n - setter_side_effect_type: "
     << SideEffectType2String(setter_side_effect_type());
  os << "\n - initial_attributes: " << initial_property_attributes();
  Isolate* isolate;
  if (GetIsolateFromHeapObject(*this, &isolate)) {
    os << "\n - getter: " << reinterpret_cast<void*>(getter(isolate));
    if (USE_SIMULATOR_BOOL) {
      os << "\n - maybe_redirected_getter: "
         << reinterpret_cast<void*>(maybe_redirected_getter(isolate));
    }
    os << "\n - setter: " << reinterpret_cast<void*>(setter(isolate));
  } else {
    os << "\n - getter: " << kUnavailableString;
    os << "\n - maybe_redirected_getter: " << kUnavailableString;
    os << "\n - setter: " << kUnavailableString;
  }
  os << '\n';
}

void FunctionTemplateInfo::FunctionTemplateInfoPrint(std::ostream& os) {
  TorqueGeneratedFunctionTemplateInfo<
      FunctionTemplateInfo, TemplateInfo>::FunctionTemplateInfoPrint(os);

  Isolate* isolate;
  if (GetIsolateFromHeapObject(*this, &isolate)) {
    os << " - callback: " << reinterpret_cast<void*>(callback(isolate));
    if (USE_SIMULATOR_BOOL) {
      os << "\n - maybe_redirected_callback: "
         << reinterpret_cast<void*>(maybe_redirected_callback(isolate));
    }
  } else {
    os << "\n - callback: " << kUnavailableString;
    os << "\n - maybe_redirected_callback: " << kUnavailableString;
  }

  os << "\n --- flags: ";
  if (is_object_template_call_handler()) {
    os << "\n - is_object_template_call_handler";
  }
  if (has_side_effects()) os << "\n - has_side_effects";

  if (undetectable()) os << "\n - undetectable";
  if (needs_access_check()) os << "\n - needs_access_check";
  if (read_only_prototype()) os << "\n - read_only_prototype";
  if (remove_prototype()) os << "\n - remove_prototype";
  if (accept_any_receiver()) os << "\n - accept_any_receiver";
  if (published()) os << "\n - published";

  if (allowed_receiver_instance_type_range_start() ||
      allowed_receiver_instance_type_range_end()) {
    os << "\n - allowed_receiver_instance_type_range: ["
       << allowed_receiver_instance_type_range_start() << ", "
       << allowed_receiver_instance_type_range_end() << "]";
  }
  os << '\n';
}

namespace {
void PrintContextWithHeader(std::ostream& os, Tagged<Context> context,
                            const char* type) {
  context->PrintHeader(os, type);
  os << "\n - type: " << context->map()->instance_type();
  os << "\n - scope_info: " << Brief(context->scope_info());
  os << "\n - previous: " << Brief(context->unchecked_previous());
  os << "\n - native_context: " << Brief(context->native_context());
  if (context->scope_info()->HasContextExtensionSlot()) {
    os << "\n - extension: " << context->extension();
  }
  os << "\n - length: " << context->length();
  os << "\n - elements:";
  PrintFixedArrayElements(os, context);
  os << "\n";
}
}  // namespace

void Context::ContextPrint(std::ostream& os) {
  PrintContextWithHeader(os, *this, "Context");
}

void NativeContext::NativeContextPrint(std::ostream& os) {
  PrintContextWithHeader(os, *this, "NativeContext");
  os << " - microtask_queue: " << microtask_queue() << "\n";
}

namespace {
using DataPrinter = std::function<void(InternalIndex)>;

// Prints the data associated with each key (but no headers or other meta
// data) in a hash table. Works on different hash table types, like the
// subtypes of HashTable and OrderedHashTable. |print_data_at| is given an
// index into the table (where a valid key resides) and prints the data at
// that index, like just the value (in case of a hash map), or value and
// property details (in case of a property dictionary). No leading space
// required or trailing newline required. It can be null/non-callable
// std::function to indicate that there is no associcated data to be printed
// (for example in case of a hash set).
template <typename T>
void PrintTableContentsGeneric(std::ostream& os, T dict,
                               DataPrinter print_data_at) {
  DisallowGarbageCollection no_gc;
  ReadOnlyRoots roots = dict.GetReadOnlyRoots();

  for (InternalIndex i : dict.IterateEntries()) {
    Tagged<Object> k;
    if (!dict.ToKey(roots, i, &k)) continue;
    os << "\n   " << std::setw(12) << i.as_int() << ": ";
    if (IsString(k)) {
      Cast<String>(k)->PrintUC16(os);
    } else {
      os << Brief(k);
    }
    if (print_data_at) {
      os << " -> ";
      print_data_at(i);
    }
  }
}

void PrintNameDictionaryFlags(std::ostream& os, Tagged<NameDictionary> dict) {
  if (dict->may_have_interesting_properties()) {
    os << "\n - may_have_interesting_properties";
  }
}

// Used for ordered and unordered dictionaries.
template <typename T>
void PrintDictionaryContentsFull(std::ostream& os, T dict) {
  os << "\n - elements: {";
  auto print_value_and_property_details = [&](InternalIndex i) {
    os << Brief(dict.ValueAt(i)) << " ";
    dict.DetailsAt(i).PrintAsSlowTo(os, !T::kIsOrderedDictionaryType);
  };
  PrintTableContentsGeneric(os, dict, print_value_and_property_details);
  os << "\n }\n";
}

// Used for ordered and unordered hash maps.
template <typename T>
void PrintHashMapContentsFull(std::ostream& os, T dict) {
  os << "\n - elements: {";
  auto print_value = [&](InternalIndex i) { os << Brief(dict.ValueAt(i)); };
  PrintTableContentsGeneric(os, dict, print_value);
  os << "\n }\n";
}

// Used for ordered and unordered hash sets.
template <typename T>
void PrintHashSetContentsFull(std::ostream& os, T dict) {
  os << "\n - elements: {";
  // Passing non-callable std::function as there are no values to print.
  PrintTableContentsGeneric(os, dict, nullptr);
  os << "\n }\n";
}

// Used for subtypes of OrderedHashTable.
template <typename T>
void PrintOrderedHashTableHeaderAndBuckets(std::ostream& os, T table,
                                           const char* type) {
  DisallowGarbageCollection no_gc;

  PrintHeapObjectHeaderWithoutMap(table, os, type);
  os << "\n - FixedArray length: " << table.length();
  os << "\n - elements: " << table.NumberOfElements();
  os << "\n - deleted: " << table.NumberOfDeletedElements();
  os << "\n - buckets: " << table.NumberOfBuckets();
  os << "\n - capacity: " << table.Capacity();

  os << "\n - buckets: {";
  for (int bucket = 0; bucket < table.NumberOfBuckets(); bucket++) {
    Tagged<Object> entry = table.get(T::HashTableStartIndex() + bucket);
    DCHECK(IsSmi(entry));
    os << "\n   " << std::setw(12) << bucket << ": " << Brief(entry);
  }
  os << "\n }";
}

// Used for subtypes of HashTable.
template <typename T>
void PrintHashTableHeader(std::ostream& os, T table, const char* type) {
  PrintHeapObjectHeaderWithoutMap(table, os, type);
  os << "\n - FixedArray length: " << table.length();
  os << "\n - elements: " << table.NumberOfElements();
  os << "\n - deleted: " << table.NumberOfDeletedElements();
  os << "\n - capacity: " << table.Capacity();
}
}  // namespace

void ObjectHashTable::ObjectHashTablePrint(std::ostream& os) {
  PrintHashTableHeader(os, *this, "ObjectHashTable");
  PrintHashMapContentsFull(os, *this);
}

void NameToIndexHashTable::NameToIndexHashTablePrint(std::ostream& os) {
  PrintHashTableHeader(os, *this, "NameToIndexHashTable");
  PrintHashMapContentsFull(os, *this);
}

void RegisteredSymbolTable::RegisteredSymbolTablePrint(std::ostream& os) {
  PrintHashTableHeader(os, *this, "RegisteredSymbolTable");
  PrintHashMapContentsFull(os, *this);
}

void NumberDictionary::NumberDictionaryPrint(std::ostream& os) {
  PrintHashTableHeader(os, *this, "NumberDictionary");
  PrintDictionaryContentsFull(os, *this);
}

void EphemeronHashTable::EphemeronHashTablePrint(std::ostream& os) {
  PrintHashTableHeader(os, *this, "EphemeronHashTable");
  PrintHashMapContentsFull(os, *this);
}

void NameDictionary::NameDictionaryPrint(std::ostream& os) {
  PrintHashTableHeader(os, *this, "NameDictionary");
  PrintNameDictionaryFlags(os, *this);
  PrintDictionaryContentsFull(os, *this);
}

void GlobalDictionary::GlobalDictionaryPrint(std::ostream& os) {
  PrintHashTableHeader(os, *this, "GlobalDictionary");
  PrintDictionaryContentsFull(os, *this);
}

void SmallOrderedHashSet::SmallOrderedHashSetPrint(std::ostream& os) {
  PrintHeader(os, "SmallOrderedHashSet");
  // TODO(turbofan): Print all fields.
}

void SmallOrderedHashMap::SmallOrderedHashMapPrint(std::ostream& os) {
  PrintHeader(os, "SmallOrderedHashMap");
  // TODO(turbofan): Print all fields.
}

void SmallOrderedNameDictionary::SmallOrderedNameDictionaryPrint(
    std::ostream& os) {
  PrintHeader(os, "SmallOrderedNameDictionary");
  // TODO(turbofan): Print all fields.
}

void OrderedHashSet::OrderedHashSetPrint(std::ostream& os) {
  PrintOrderedHashTableHeaderAndBuckets(os, *this, "OrderedHashSet");
  PrintHashSetContentsFull(os, *this);
}

void OrderedHashMap::OrderedHashMapPrint(std::ostream& os) {
  PrintOrderedHashTableHeaderAndBuckets(os, *this, "OrderedHashMap");
  PrintHashMapContentsFull(os, *this);
}

void OrderedNameDictionary::OrderedNameDictionaryPrint(std::ostream& os) {
  PrintOrderedHashTableHeaderAndBuckets(os, *this, "OrderedNameDictionary");
  PrintDictionaryContentsFull(os, *this);
}

void print_hex_byte(std::ostream& os, int value) {
  os << "0x" << std::setfill('0') << std::setw(2) << std::right << std::hex
     << (value & 0xff) << std::setfill(' ');
}

void SwissNameDictionary::SwissNameDictionaryPrint(std::ostream& os) {
  this->PrintHeader(os, "SwissNameDictionary");
  os << "\n - meta table ByteArray: "
     << reinterpret_cast<void*>(this->meta_table().ptr());
  os << "\n - capacity: " << this->Capacity();
  os << "\n - elements: " << this->NumberOfElements();
  os << "\n - deleted: " << this->NumberOfDeletedElements();

  std::ios_base::fmtflags sav_flags = os.flags();
  os << "\n - ctrl table (omitting buckets where key is hole value): {";
  for (int i = 0; i < this->Capacity() + kGroupWidth; i++) {
    ctrl_t ctrl = CtrlTable()[i];

    if (ctrl == Ctrl::kEmpty) continue;

    os << "\n   " << std::setw(12) << std::dec << i << ": ";
    switch (ctrl) {
      case Ctrl::kEmpty:
        UNREACHABLE();
      case Ctrl::kDeleted:
        print_hex_byte(os, ctrl);
        os << " (= kDeleted)";
        break;
      case Ctrl::kSentinel:
        print_hex_byte(os, ctrl);
        os << " (= kSentinel)";
        break;
      default:
        print_hex_byte(os, ctrl);
        os << " (= H2 of a key)";
        break;
    }
  }
  os << "\n }";

  os << "\n - enumeration table: {";
  for (int enum_index = 0; enum_index < this->UsedCapacity(); enum_index++) {
    int entry = EntryForEnumerationIndex(enum_index);
    os << "\n   " << std::setw(12) << std::dec << enum_index << ": " << entry;
  }
  os << "\n }";

  os << "\n - data table (omitting slots where key is the hole): {";
  for (int bucket = 0; bucket < this->Capacity(); ++bucket) {
    Tagged<Object> k;
    if (!this->ToKey(this->GetReadOnlyRoots(), bucket, &k)) continue;

    Tagged<Object> value = this->ValueAtRaw(bucket);
    PropertyDetails details = this->DetailsAt(bucket);
    os << "\n   " << std::setw(12) << std::dec << bucket << ": ";
    if (IsString(k)) {
      Cast<String>(k)->PrintUC16(os);
    } else {
      os << Brief(k);
    }
    os << " -> " << Brief(value);
    details.PrintAsSlowTo(os, false);
  }
  os << "\n }\n";
  os.flags(sav_flags);
}

void PropertyArray::PropertyArrayPrint(std::ostream& os) {
  PrintHeader(os, "PropertyArray");
  os << "\n - length: " << length();
  os << "\n - hash: " << Hash();
  PrintFixedArrayElements(os, Tagged(*this));
  os << "\n";
}

void FixedDoubleArray::FixedDoubleArrayPrint(std::ostream& os) {
  PrintHeader(os, "FixedDoubleArray");
  os << "\n - length: " << length();
  DoPrintElements<FixedDoubleArray>(os, *this, length());
  os << "\n";
}

void WeakFixedArray::WeakFixedArrayPrint(std::ostream& os) {
  PrintHeader(os, "WeakFixedArray");
  os << "\n - length: " << length() << "\n";
  PrintWeakArrayElements(os, this);
  os << "\n";
}

void TrustedWeakFixedArray::TrustedWeakFixedArrayPrint(std::ostream& os) {
  PrintHeader(os, "TrustedWeakFixedArray");
  os << "\n - length: " << length() << "\n";
  PrintWeakArrayElements(os, this);
  os << "\n";
}

void WeakArrayList::WeakArrayListPrint(std::ostream& os) {
  PrintHeader(os, "WeakArrayList");
  os << "\n - capacity: " << capacity();
  os << "\n - length: " << length() << "\n";
  PrintWeakArrayElements(os, this);
  os << "\n";
}

void TransitionArray::TransitionArrayPrint(std::ostream& os) {
  PrintHeader(os, "TransitionArray");
  PrintInternal(os);
}

void FeedbackCell::FeedbackCellPrint(std::ostream& os) {
  PrintHeader(os, "FeedbackCell");
  ReadOnlyRoots roots = GetReadOnlyRoots();
  if (map() == roots.no_closures_cell_map()) {
    os << "\n - no closures";
  } else if (map() == roots.one_closure_cell_map()) {
    os << "\n - one closure";
  } else if (map() == roots.many_closures_cell_map()) {
    os << "\n - many closures";
  } else {
    os << "\n - Invalid FeedbackCell map";
  }
  os << "\n - value: " << Brief(value());
  os << "\n - interrupt_budget: " << interrupt_budget();
  os << "\n";
}

void FeedbackVectorSpec::Print() {
  StdoutStream os;

  FeedbackVectorSpecPrint(os);

  os << std::flush;
}

void FeedbackVectorSpec::FeedbackVectorSpecPrint(std::ostream& os) {
  os << " - slot_count: " << slot_count();
  if (slot_count() == 0) {
    os << " (empty)\n";
    return;
  }

  for (int slot = 0; slot < slot_count();) {
    FeedbackSlotKind kind = GetKind(FeedbackSlot(slot));
    int entry_size = FeedbackMetadata::GetSlotSize(kind);
    DCHECK_LT(0, entry_size);
    os << "\n Slot #" << slot << " " << kind;
    slot += entry_size;
  }
  os << "\n";
}

void FeedbackMetadata::FeedbackMetadataPrint(std::ostream& os) {
  PrintHeader(os, "FeedbackMetadata");
  os << "\n - slot_count: " << slot_count();
  os << "\n - create_closure_slot_count: " << create_closure_slot_count();

  FeedbackMetadataIterator iter(*this);
  while (iter.HasNext()) {
    FeedbackSlot slot = iter.Next();
    FeedbackSlotKind kind = iter.kind();
    os << "\n Slot " << slot << " " << kind;
  }
  os << "\n";
}

void ClosureFeedbackCellArray::ClosureFeedbackCellArrayPrint(std::ostream& os) {
  PrintHeader(os, "ClosureFeedbackCellArray");
  os << "\n - length: " << length();
  os << "\n - elements:";
  PrintFixedArrayElements<ClosureFeedbackCellArray>(os, *this);
  os << "\n";
}

void FeedbackVector::FeedbackVectorPrint(std::ostream& os) {
  PrintHeader(os, "FeedbackVector");
  os << "\n - length: " << length();
  if (length() == 0) {
    os << " (empty)\n";
    return;
  }

  os << "\n - shared function info: " << Brief(shared_function_info());
  if (has_optimized_code()) {
    os << "\n - optimized code: "
       << Brief(optimized_code(GetIsolateForSandbox(*this)));
  } else {
    os << "\n - no optimized code";
  }
  os << "\n - tiering state: " << tiering_state();
  os << "\n - maybe has maglev code: " << maybe_has_maglev_code();
  os << "\n - maybe has turbofan code: " << maybe_has_turbofan_code();
  os << "\n - invocation count: " << invocation_count();
  os << "\n - closure feedback cell array: ";
  closure_feedback_cell_array()->ClosureFeedbackCellArrayPrint(os);

  FeedbackMetadataIterator iter(metadata());
  while (iter.HasNext()) {
    FeedbackSlot slot = iter.Next();
    FeedbackSlotKind kind = iter.kind();

    os << "\n - slot " << slot << " " << kind << " ";
    FeedbackSlotPrint(os, slot);

    int entry_size = iter.entry_size();
    if (entry_size > 0) os << " {";
    for (int i = 0; i < entry_size; i++) {
      FeedbackSlot slot_with_offset = slot.WithOffset(i);
      os << "\n     [" << slot_with_offset.ToInt()
         << "]: " << Brief(Get(slot_with_offset));
    }
    if (entry_size > 0) os << "\n  }";
  }
  os << "\n";
}

void FeedbackVector::FeedbackSlotPrint(std::ostream& os, FeedbackSlot slot) {
  FeedbackNexus nexus(GetIsolate(), *this, slot);
  nexus.Print(os);
}

void FeedbackNexus::Print(std::ostream& os) {
  auto slot_kind = kind();
  switch (slot_kind) {
    case FeedbackSlotKind::kCall:
    case FeedbackSlotKind::kCloneObject:
    case FeedbackSlotKind::kHasKeyed:
    case FeedbackSlotKind::kInstanceOf:
    case FeedbackSlotKind::kTypeOf:
    case FeedbackSlotKind::kDefineKeyedOwnPropertyInLiteral:
    case FeedbackSlotKind::kStoreInArrayLiteral: {
      os << InlineCacheState2String(ic_state());
      break;
    }
    case FeedbackSlotKind::kLoadGlobalInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict: {
      os << InlineCacheState2String(ic_state());
      if (ic_state() == InlineCacheState::MONOMORPHIC) {
        os << "\n   ";
        if (GetFeedback().IsCleared()) {
          // Handler mode: feedback is the cleared value, extra is the handler.
          if (IsLoadGlobalICKind(slot_kind)) {
            LoadHandler::PrintHandler(GetFeedbackExtra().GetHeapObjectOrSmi(),
                                      os);
          } else {
            StoreHandler::PrintHandler(GetFeedbackExtra().GetHeapObjectOrSmi(),
                                       os);
          }
        } else if (IsPropertyCell(GetFeedback().GetHeapObjectOrSmi())) {
          os << Brief(GetFeedback());
        } else {
          // Lexical variable mode: the variable location is encoded in the SMI.
          int handler = GetFeedback().GetHeapObjectOrSmi().ToSmi().value();
          os << (IsLoadGlobalICKind(slot_kind) ? "Load" : "Store");
          os << "Handler(Lexical variable mode)(context ix = "
             << FeedbackNexus::ContextIndexBits::decode(handler)
             << ", slot ix = " << FeedbackNexus::SlotIndexBits::decode(handler)
             << ")";
        }
      }
      break;
    }
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kLoadProperty: {
      os << InlineCacheState2String(ic_state());
      if (ic_state() == InlineCacheState::MONOMORPHIC) {
        os << "\n   " << Brief(GetFeedback()) << ": ";
        Tagged<Object> handler = GetFeedbackExtra().GetHeapObjectOrSmi();
        if (IsWeakFixedArray(handler) &&
            !Cast<WeakFixedArray>(handler)->get(0).IsCleared()) {
          handler = Cast<WeakFixedArray>(handler)->get(0).GetHeapObjectOrSmi();
        }
        LoadHandler::PrintHandler(handler, os);
      } else if (ic_state() == InlineCacheState::POLYMORPHIC) {
        Tagged<HeapObject> feedback = GetFeedback().GetHeapObject();
        Tagged<WeakFixedArray> array;
        if (IsName(feedback)) {
          os << " with name " << Brief(feedback);
          array = Cast<WeakFixedArray>(GetFeedbackExtra().GetHeapObject());
        } else {
          array = Cast<WeakFixedArray>(feedback);
        }
        for (int i = 0; i < array->length(); i += 2) {
          os << "\n   " << Brief(array->get(i)) << ": ";
          LoadHandler::PrintHandler(array->get(i + 1).GetHeapObjectOrSmi(), os);
        }
      }
      break;
    }
    case FeedbackSlotKind::kDefineNamedOwn:
    case FeedbackSlotKind::kDefineKeyedOwn:
    case FeedbackSlotKind::kSetNamedSloppy:
    case FeedbackSlotKind::kSetNamedStrict:
    case FeedbackSlotKind::kSetKeyedSloppy:
    case FeedbackSlotKind::kSetKeyedStrict: {
      os << InlineCacheState2String(ic_state());
      if (GetFeedback().IsCleared()) {
        os << "\n   [cleared]";
        break;
      }
      if (ic_state() == InlineCacheState::MONOMORPHIC) {
        Tagged<HeapObject> feedback = GetFeedback().GetHeapObject();
        if (GetFeedbackExtra().IsCleared()) {
          os << " [cleared]\n";
          break;
        }
        if (IsName(feedback)) {
          os << " with name " << Brief(feedback);
          Tagged<WeakFixedArray> array =
              Cast<WeakFixedArray>(GetFeedbackExtra().GetHeapObject());
          os << "\n   " << Brief(array->get(0)) << ": ";
          if (array->get(1).IsCleared()) {
            os << "[cleared]\n";
          } else {
            Tagged<Object> handler = array->get(1).GetHeapObjectOrSmi();
            StoreHandler::PrintHandler(handler, os);
          }
        } else {
          os << "\n   " << Brief(feedback) << ": ";
          StoreHandler::PrintHandler(GetFeedbackExtra().GetHeapObjectOrSmi(),
                                     os);
        }
      } else if (ic_state() == InlineCacheState::POLYMORPHIC) {
        Tagged<HeapObject> feedback = GetFeedback().GetHeapObject();
        Tagged<WeakFixedArray> array;
        if (IsName(feedback)) {
          os << " with name " << Brief(feedback);
          array = Cast<WeakFixedArray>(GetFeedbackExtra().GetHeapObject());
        } else {
          array = Cast<WeakFixedArray>(feedback);
        }
        for (int i = 0; i < array->length(); i += 2) {
          os << "\n   " << Brief(array->get(i)) << ": ";
          if (!array->get(i + 1).IsCleared()) {
            StoreHandler::PrintHandler(array->get(i + 1).GetHeapObjectOrSmi(),
                                       os);
          }
        }
      }
      break;
    }
    case FeedbackSlotKind::kBinaryOp: {
      os << "BinaryOp:" << GetBinaryOperationFeedback();
      break;
    }
    case FeedbackSlotKind::kCompareOp: {
      os << "CompareOp:" << GetCompareOperationFeedback();
      break;
    }
    case FeedbackSlotKind::kForIn: {
      os << "ForIn:" << GetForInFeedback();
      break;
    }
    case FeedbackSlotKind::kLiteral:
      break;
    case FeedbackSlotKind::kJumpLoop:
      os << "JumpLoop";
      break;
    case FeedbackSlotKind::kInvalid:
      UNREACHABLE();
  }
}

void Oddball::OddballPrint(std::ostream& os) {
  PrintHeapObjectHeaderWithoutMap(Tagged<HeapObject>(this), os, "Oddball");
  os << ": ";
  Tagged<String> s = to_string();
  os << s->PrefixForDebugPrint();
  s->PrintUC16(os);
  os << s->SuffixForDebugPrint();
  os << std::endl;
}

void Hole::HolePrint(std::ostream& os) {
  PrintHeapObjectHeaderWithoutMap(*this, os, "Hole");
  os << std::endl;
}

void JSAsyncFunctionObject::JSAsyncFunctionObjectPrint(std::ostream& os) {
  JSGeneratorObjectPrint(os);
}

void JSAsyncGeneratorObject::JSAsyncGeneratorObjectPrint(std::ostream& os) {
  JSGeneratorObjectPrint(os);
}

void JSArgumentsObject::JSArgumentsObjectPrint(std::ostream& os) {
  JSObjectPrint(os);
}

void JSStringIterator::JSStringIteratorPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSStringIterator");
  os << "\n - string: " << Brief(string());
  os << "\n - index: " << index();
  JSObjectPrintBody(os, *this);
}

void JSAsyncFromSyncIterator::JSAsyncFromSyncIteratorPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSAsyncFromSyncIterator");
  os << "\n - sync_iterator: " << Brief(sync_iterator());
  os << "\n - next: " << Brief(next());
  JSObjectPrintBody(os, *this);
}

void JSValidIteratorWrapper::JSValidIteratorWrapperPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSValidIteratorWrapper");
  os << "\n - underlying.object: " << Brief(underlying_object());
  os << "\n - underlying.next: " << Brief(underlying_next());
  JSObjectPrintBody(os, *this);
}

void JSPrimitiveWrapper::JSPrimitiveWrapperPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSPrimitiveWrapper");
  os << "\n - value: " << Brief(value());
  JSObjectPrintBody(os, *this);
}

void JSMessageObject::JSMessageObjectPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSMessageObject");
  os << "\n - type: " << static_cast<int>(type());
  os << "\n - arguments: " << Brief(argument());
  os << "\n - script: " << Brief(script());
  os << "\n - stack_frames: " << Brief(stack_frames());
  os << "\n - shared_info: " << Brief(shared_info());
  if (shared_info() == Smi::zero()) {
    os << " (cleared after calculating line ends)";
  } else if (shared_info() == Smi::FromInt(-1)) {
    os << "(no line ends needed)";
  }
  os << "\n - bytecode_offset: " << bytecode_offset();
  os << "\n - start_position: " << start_position();
  os << "\n - end_position: " << end_position();
  os << "\n - error_level: " << error_level();
  JSObjectPrintBody(os, *this);
}

void String::StringPrint(std::ostream& os) {
  PrintHeapObjectHeaderWithoutMap(this, os, "String");
  os << ": ";
  os << PrefixForDebugPrint();
  PrintUC16(os, 0, length());
  os << SuffixForDebugPrint();
}

void Name::NamePrint(std::ostream& os) {
  if (IsString(this)) {
    Cast<String>(this)->StringPrint(os);
  } else {
    os << Brief(this);
  }
}

static const char* const weekdays[] = {"???", "Sun", "Mon", "Tue",
                                       "Wed", "Thu", "Fri", "Sat"};

void JSDate::JSDatePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSDate");
  os << "\n - value: " << value();
  if (!IsSmi(year())) {
    os << "\n - time = NaN\n";
  } else {
    // TODO(svenpanne) Add some basic formatting to our streams.
    base::ScopedVector<char> buf(100);
    SNPrintF(buf, "\n - time = %s %04d/%02d/%02d %02d:%02d:%02d\n",
             weekdays[IsSmi(weekday()) ? Smi::ToInt(weekday()) + 1 : 0],
             IsSmi(year()) ? Smi::ToInt(year()) : -1,
             IsSmi(month()) ? Smi::ToInt(month()) : -1,
             IsSmi(day()) ? Smi::ToInt(day()) : -1,
             IsSmi(hour()) ? Smi::ToInt(hour()) : -1,
             IsSmi(min()) ? Smi::ToInt(min()) : -1,
             IsSmi(sec()) ? Smi::ToInt(sec()) : -1);
    os << buf.begin();
  }
  JSObjectPrintBody(os, *this);
}

void JSSet::JSSetPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSSet");
  os << "\n - table: " << Brief(table());
  JSObjectPrintBody(os, *this);
}

void JSMap::JSMapPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSMap");
  os << "\n - table: " << Brief(table());
  JSObjectPrintBody(os, *this);
}

void JSCollectionIterator::JSCollectionIteratorPrint(std::ostream& os,
                                                     const char* name) {
  JSObjectPrintHeader(os, *this, name);
  os << "\n - table: " << Brief(table());
  os << "\n - index: " << Brief(index());
  JSObjectPrintBody(os, *this);
}

void JSSetIterator::JSSetIteratorPrint(std::ostream& os) {
  JSCollectionIteratorPrint(os, "JSSetIterator");
}

void JSMapIterator::JSMapIteratorPrint(std::ostream& os) {
  JSCollectionIteratorPrint(os, "JSMapIterator");
}

void JSWeakRef::JSWeakRefPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSWeakRef");
  os << "\n - target: " << Brief(target());
  JSObjectPrintBody(os, *this);
}

void JSShadowRealm::JSShadowRealmPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSShadowRealm");
  os << "\n - native_context: " << Brief(native_context());
  JSObjectPrintBody(os, *this);
}

void JSWrappedFunction::JSWrappedFunctionPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSWrappedFunction");
  os << "\n - wrapped_target_function: " << Brief(wrapped_target_function());
  JSObjectPrintBody(os, *this);
}

void JSFinalizationRegistry::JSFinalizationRegistryPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSFinalizationRegistry");
  os << "\n - native_context: " << Brief(native_context());
  os << "\n - cleanup: " << Brief(cleanup());
  os << "\n - active_cells: " << Brief(active_cells());
  Tagged<Object> active_cell = active_cells();
  while (IsWeakCell(active_cell)) {
    os << "\n   - " << Brief(active_cell);
    active_cell = Cast<WeakCell>(active_cell)->next();
  }
  os << "\n - cleared_cells: " << Brief(cleared_cells());
  Tagged<Object> cleared_cell = cleared_cells();
  while (IsWeakCell(cleared_cell)) {
    os << "\n   - " << Brief(cleared_cell);
    cleared_cell = Cast<WeakCell>(cleared_cell)->next();
  }
  os << "\n - key_map: " << Brief(key_map());
  JSObjectPrintBody(os, *this);
}

void JSSharedArray::JSSharedArrayPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSSharedArray");
  Isolate* isolate = GetIsolateFromWritableObject(*this);
  os << "\n - isolate: " << isolate;
  if (InWritableSharedSpace(*this)) os << " (shared)";
  JSObjectPrintBody(os, *this);
}

void JSSharedStruct::JSSharedStructPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSSharedStruct");
  Isolate* isolate = GetIsolateFromWritableObject(*this);
  os << "\n - isolate: " << isolate;
  if (InWritableSharedSpace(*this)) os << " (shared)";
  JSObjectPrintBody(os, *this);
}

void JSAtomicsMutex::JSAtomicsMutexPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSAtomicsMutex");
  Isolate* isolate = GetIsolateFromWritableObject(*this);
  os << "\n - isolate: " << isolate;
  if (InWritableSharedSpace(*this)) os << " (shared)";
  os << "\n - state: " << this->state();
  os << "\n - owner_thread_id: " << this->owner_thread_id();
  JSObjectPrintBody(os, *this);
}

void JSAtomicsCondition::JSAtomicsConditionPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSAtomicsCondition");
  Isolate* isolate = GetIsolateFromWritableObject(*this);
  os << "\n - isolate: " << isolate;
  if (InWritableSharedSpace(*this)) os << " (shared)";
  os << "\n - state: " << this->state();
  JSObjectPrintBody(os, *this);
}

std::ostream& operator<<(std::ostream& os, DisposableStackState state) {
  switch (state) {
    case DisposableStackState::kPending:
      return os << "Pending";
    case DisposableStackState::kDisposed:
      return os << "Disposed";
  }
  UNREACHABLE();
}

void JSDisposableStackBase::JSDisposableStackBasePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSDisposableStack");
  os << "\n - stack: " << Brief(stack());
  os << "\n - length: " << length();
  os << "\n - state: " << state();
  JSObjectPrintBody(os, *this);
}

void JSAsyncDisposableStack::JSAsyncDisposableStackPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSAsyncDisposableStack");
  os << "\n - stack: " << Brief(stack());
  os << "\n - length: " << length();
  os << "\n - state: " << state();
  JSObjectPrintBody(os, *this);
}

void JSIteratorHelper::JSIteratorHelperPrintHeader(std::ostream& os,
                                                   const char* helper_name) {
  JSObjectPrintHeader(os, *this, helper_name);
  os << "\n - underlying.object: " << Brief(underlying_object());
  os << "\n - underlying.next: " << Brief(underlying_next());
}

void JSIteratorMapHelper::JSIteratorMapHelperPrint(std::ostream& os) {
  JSIteratorHelperPrintHeader(os, "JSIteratorMapHelper");
  os << "\n - mapper: " << Brief(mapper());
  os << "\n - counter: " << counter();
  JSObjectPrintBody(os, *this);
}

void JSIteratorFilterHelper::JSIteratorFilterHelperPrint(std::ostream& os) {
  JSIteratorHelperPrintHeader(os, "JSIteratorFilterHelper");
  os << "\n - predicate: " << Brief(predicate());
  os << "\n - counter: " << counter();
  JSObjectPrintBody(os, *this);
}

void JSIteratorTakeHelper::JSIteratorTakeHelperPrint(std::ostream& os) {
  JSIteratorHelperPrintHeader(os, "JSIteratorTakeHelper");
  os << "\n - remaining: " << remaining();
  JSObjectPrintBody(os, *this);
}

void JSIteratorDropHelper::JSIteratorDropHelperPrint(std::ostream& os) {
  JSIteratorHelperPrintHeader(os, "JSIteratorDropHelper");
  os << "\n - remaining: " << remaining();
  JSObjectPrintBody(os, *this);
}

void JSIteratorFlatMapHelper::JSIteratorFlatMapHelperPrint(std::ostream& os) {
  JSIteratorHelperPrintHeader(os, "JSIteratorFlatMapHelper");
  os << "\n - mapper: " << Brief(mapper());
  os << "\n - counter: " << counter();
  os << "\n - innerIterator.object" << Brief(innerIterator_object());
  os << "\n - innerIterator.next" << Brief(innerIterator_next());
  os << "\n - innerAlive" << innerAlive();
  JSObjectPrintBody(os, *this);
}

void JSWeakMap::JSWeakMapPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSWeakMap");
  os << "\n - table: " << Brief(table());
  JSObjectPrintBody(os, *this);
}

void JSWeakSet::JSWeakSetPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSWeakSet");
  os << "\n - table: " << Brief(table());
  JSObjectPrintBody(os, *this);
}

void JSArrayBuffer::JSArrayBufferPrint(std::ostream& os) {
  JSAPIObjectWithEmbedderSlotsPrintHeader(os, *this, "JSArrayBuffer");
  os << "\n - backing_store: " << backing_store();
  os << "\n - byte_length: " << byte_length();
  os << "\n - max_byte_length: " << max_byte_length();
  os << "\n - detach key: " << detach_key();
  if (is_external()) os << "\n - external";
  if (is_detachable()) os << "\n - detachable";
  if (was_detached()) os << "\n - detached";
  if (is_shared()) os << "\n - shared";
  if (is_resizable_by_js()) os << "\n - resizable_by_js";
  JSObjectPrintBody(os, *this, !was_detached());
}

void JSTypedArray::JSTypedArrayPrint(std::ostream& os) {
  JSAPIObjectWithEmbedderSlotsPrintHeader(os, *this, "JSTypedArray");
  os << "\n - buffer: " << Brief(buffer());
  os << "\n - byte_offset: " << byte_offset();
  os << "\n - byte_length: " << byte_length();
  os << "\n - length: " << GetLength();
  os << "\n - data_ptr: " << DataPtr();
  Tagged_t base_ptr = static_cast<Tagged_t>(base_pointer().ptr());
  os << "\n   - base_pointer: "
     << reinterpret_cast<void*>(static_cast<Address>(base_ptr));
  os << "\n   - external_pointer: "
     << reinterpret_cast<void*>(external_pointer());
  if (!IsJSArrayBuffer(buffer())) {
    os << "\n <invalid buffer>\n";
    return;
  }
  if (WasDetached()) os << "\n - detached";
  if (is_length_tracking()) os << "\n - length-tracking";
  if (is_backed_by_rab()) os << "\n - backed-by-rab";
  JSObjectPrintBody(os, *this, !WasDetached());
}

void JSArrayIterator::JSArrayIteratorPrint(std::ostream& os) {  // NOLING
  JSObjectPrintHeader(os, *this, "JSArrayIterator");
  os << "\n - iterated_object: " << Brief(iterated_object());
  os << "\n - next_index: " << Brief(next_index());
  os << "\n - kind: " << kind();
  JSObjectPrintBody(os, *this);
}

void JSDataView::JSDataViewPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSDataView");
  os << "\n - buffer =" << Brief(buffer());
  os << "\n - byte_offset: " << byte_offset();
  os << "\n - byte_length: " << byte_length();
  if (!IsJSArrayBuffer(buffer())) {
    os << "\n <invalid buffer>";
    return;
  }
  if (WasDetached()) os << "\n - detached";
  JSObjectPrintBody(os, *this, !WasDetached());
}

void JSRabGsabDataView::JSRabGsabDataViewPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSRabGsabDataView");
  os << "\n - buffer =" << Brief(buffer());
  os << "\n - byte_offset: " << byte_offset();
  os << "\n - byte_length: " << byte_length();
  if (is_length_tracking()) os << "\n - length-tracking";
  if (is_backed_by_rab()) os << "\n - backed-by-rab";
  if (!IsJSArrayBuffer(buffer())) {
    os << "\n <invalid buffer>";
    return;
  }
  if (WasDetached()) os << "\n - detached";
  JSObjectPrintBody(os, *this, !WasDetached());
}

void JSBoundFunction::JSBoundFunctionPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSBoundFunction");
  os << "\n - bound_target_function: " << Brief(bound_target_function());
  os << "\n - bound_this: " << Brief(bound_this());
  os << "\n - bound_arguments: " << Brief(bound_arguments());
  JSObjectPrintBody(os, *this);
}

void JSFunction::JSFunctionPrint(std::ostream& os) {
  Isolate* isolate = GetIsolate();
  JSObjectPrintHeader(os, *this, "Function");
  os << "\n - function prototype: ";
  if (has_prototype_slot()) {
    if (has_prototype()) {
      os << Brief(prototype());
      if (map()->has_non_instance_prototype()) {
        os << " (non-instance prototype)";
      }
    }
    os << "\n - initial_map: ";
    if (has_initial_map()) os << Brief(initial_map());
  } else {
    os << "<no-prototype-slot>";
  }
  os << "\n - shared_info: " << Brief(shared());
  os << "\n - name: " << Brief(shared()->Name());

  // Print Builtin name for builtin functions
  Builtin builtin = code(isolate)->builtin_id();
  if (Builtins::IsBuiltinId(builtin)) {
    os << "\n - builtin: " << isolate->builtins()->name(builtin);
  }

  os << "\n - formal_parameter_count: "
     << shared()->internal_formal_parameter_count_without_receiver();
  os << "\n - kind: " << shared()->kind();
  os << "\n - context: " << Brief(context());
  os << "\n - code: " << Brief(code(isolate));
#ifdef V8_ENABLE_LEAPTIERING
  os << "\n - dispatch_handle: " << dispatch_handle();
#endif  // V8_ENABLE_LEAPTIERING
  if (code(isolate)->kind() == CodeKind::FOR_TESTING) {
    os << "\n - FOR_TESTING";
  } else if (ActiveTierIsIgnition(isolate)) {
    os << "\n - interpreted";
    if (shared()->HasBytecodeArray()) {
      os << "\n - bytecode: " << shared()->GetBytecodeArray(isolate);
    }
  }
#if V8_ENABLE_WEBASSEMBLY
  if (WasmExportedFunction::IsWasmExportedFunction(*this)) {
    Tagged<WasmExportedFunction> function = Cast<WasmExportedFunction>(*this);
    Tagged<WasmExportedFunctionData> data =
        function->shared()->wasm_exported_function_data();
    os << "\n - Wasm instance data: " << Brief(data->instance_data());
    os << "\n - Wasm function index: " << data->function_index();
  }
  if (WasmJSFunction::IsWasmJSFunction(*this)) {
    Tagged<WasmJSFunction> function = Cast<WasmJSFunction>(*this);
    os << "\n - Wasm wrapper around: "
       << Brief(function->shared()->wasm_js_function_data()->GetCallable());
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  shared()->PrintSourceCode(os);
  JSObjectPrintBody(os, *this);
  os << " - feedback vector: ";
  if (!shared()->HasFeedbackMetadata()) {
    os << "feedback metadata is not available in SFI\n";
  } else if (has_feedback_vector()) {
    feedback_vector()->FeedbackVectorPrint(os);
  } else if (has_closure_feedback_cell_array()) {
    os << "No feedback vector, but we have a closure feedback cell array\n";
    closure_feedback_cell_array()->ClosureFeedbackCellArrayPrint(os);
  } else {
    os << "not available\n";
  }
}

void SharedFunctionInfo::PrintSourceCode(std::ostream& os) {
  if (HasSourceCode()) {
    os << "\n - source code: ";
    Tagged<String> source = Cast<String>(Cast<Script>(script())->source());
    int start = StartPosition();
    int length = EndPosition() - start;
    std::unique_ptr<char[]> source_string = source->ToCString(
        DISALLOW_NULLS, FAST_STRING_TRAVERSAL, start, length, nullptr);
    os << source_string.get();
  }
}

void SharedFunctionInfo::SharedFunctionInfoPrint(std::ostream& os) {
  PrintHeader(os, "SharedFunctionInfo");
  os << "\n - name: ";
  if (HasSharedName()) {
    os << Brief(Name());
  } else {
    os << "<no-shared-name>";
  }
  if (HasInferredName()) {
    os << "\n - inferred name: " << Brief(inferred_name());
  }
  if (class_scope_has_private_brand()) {
    os << "\n - class_scope_has_private_brand";
  }
  if (has_static_private_methods_or_accessors()) {
    os << "\n - has_static_private_methods_or_accessors";
  }
  if (private_name_lookup_skips_outer_class()) {
    os << "\n - private_name_lookup_skips_outer_class";
  }
  os << "\n - kind: " << kind();
  os << "\n - syntax kind: " << syntax_kind();
  os << "\n - function_map_index: " << function_map_index();
  os << "\n - formal_parameter_count: "
     << internal_formal_parameter_count_without_receiver();
  os << "\n - expected_nof_properties: "
     << static_cast<int>(expected_nof_properties());
  os << "\n - language_mode: " << language_mode();
  os << "\n - trusted_function_data: "
     << Brief(GetTrustedData(GetIsolateForSandbox(*this)));
  os << "\n - untrusted_function_data: " << Brief(GetUntrustedData());
  os << "\n - code (from function_data): ";
  Isolate* isolate;
  if (GetIsolateFromHeapObject(*this, &isolate)) {
    os << Brief(GetCode(isolate));
  } else {
    os << kUnavailableString;
  }
  PrintSourceCode(os);
  // Script files are often large, thus only print their {Brief} representation.
  os << "\n - script: " << Brief(script());
  os << "\n - function token position: " << function_token_position();
  os << "\n - start position: " << StartPosition();
  os << "\n - end position: " << EndPosition();
  os << "\n - scope info: " << Brief(scope_info());
  if (HasOuterScopeInfo()) {
    os << "\n - outer scope info: " << Brief(GetOuterScopeInfo());
  }
  os << "\n - length: " << length();
  os << "\n - feedback_metadata: ";
  if (HasFeedbackMetadata()) {
    feedback_metadata()->FeedbackMetadataPrint(os);
  } else {
    os << "<none>";
  }
  os << "\n - function_literal_id: " << function_literal_id();
  os << "\n - unique_id: " << unique_id();
  os << "\n - age: " << age();
  os << "\n";
}

void SharedFunctionInfoWrapper::SharedFunctionInfoWrapperPrint(
    std::ostream& os) {
  PrintHeader(os, "SharedFunctionInfoWrapper");
  os << "\n    sfi: " << Brief(shared_info());
}

void JSGlobalProxy::JSGlobalProxyPrint(std::ostream& os) {
  JSAPIObjectWithEmbedderSlotsPrintHeader(os, *this, "JSGlobalProxy");
  JSObjectPrintBody(os, *this);
}

void JSGlobalObject::JSGlobalObjectPrint(std::ostream& os) {
  JSAPIObjectWithEmbedderSlotsPrintHeader(os, *this, "JSGlobalObject");
  os << "\n - global proxy: " << Brief(global_proxy());
  JSObjectPrintBody(os, *this);
}

void PropertyCell::PropertyCellPrint(std::ostream& os) {
  PrintHeader(os, "PropertyCell");
  os << "\n - name: ";
  name()->NamePrint(os);
  os << "\n - value: " << Brief(value(kAcquireLoad));
  os << "\n - details: ";
  PropertyDetails details = property_details(kAcquireLoad);
  details.PrintAsSlowTo(os, true);
  os << "\n - cell_type: " << details.cell_type();
  os << "\n - dependent code: " << dependent_code();
  os << "\n";
}

void ConstTrackingLetCell::ConstTrackingLetCellPrint(std::ostream& os) {
  PrintHeader(os, "ConstTrackingLetCell");
  os << "\n - dependent code: " << dependent_code();
  os << "\n";
}

void InstructionStream::InstructionStreamPrint(std::ostream& os) {
  code(kAcquireLoad)->CodePrint(os);
}

void Code::CodePrint(std::ostream& os, const char* name, Address current_pc) {
  // This prints the entire {Code,InstructionStream} composite object.
  //
  // First, Code:
  PrintHeader(os, "Code");
  os << "\n - kind: " << CodeKindToString(kind());
  if (is_builtin()) {
    os << "\n - builtin_id: " << Builtins::name(builtin_id());
  }
  os << "\n - deoptimization_data_or_interpreter_data: "
     << Brief(raw_deoptimization_data_or_interpreter_data());
  os << "\n - position_table: " << Brief(raw_position_table());
  os << "\n - instruction_stream: " << Brief(raw_instruction_stream());
  os << "\n - instruction_start: "
     << reinterpret_cast<void*>(instruction_start());
  os << "\n - is_turbofanned: " << is_turbofanned();
  os << "\n - stack_slots: " << stack_slots();
  os << "\n - marked_for_deoptimization: " << marked_for_deoptimization();
  os << "\n - embedded_objects_cleared: " << embedded_objects_cleared();
  os << "\n - can_have_weak_objects: " << can_have_weak_objects();
  os << "\n - instruction_size: " << instruction_size();
  os << "\n - metadata_size: " << metadata_size();

  if (kind() != CodeKind::WASM_TO_JS_FUNCTION) {
    os << "\n - inlined_bytecode_size: " << inlined_bytecode_size();
  } else {
    os << "\n - wasm_js_tagged_parameter_count: "
       << wasm_js_tagged_parameter_count();
    os << "\n - wasm_js_first_tagged_parameter: "
       << wasm_js_first_tagged_parameter();
  }
  os << "\n - osr_offset: " << osr_offset();
  os << "\n - handler_table_offset: " << handler_table_offset();
  os << "\n - unwinding_info_offset: " << unwinding_info_offset();
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    os << "\n - constant_pool_offset: " << constant_pool_offset();
  }
  os << "\n - code_comments_offset: " << code_comments_offset();

  // Then, InstructionStream:
  if (has_instruction_stream()) {
    Tagged<InstructionStream> istream = instruction_stream();
    os << "\n - instruction_stream.relocation_info: "
       << Brief(istream->relocation_info());
    os << "\n - instruction_stream.body_size: " << istream->body_size();
  }
  os << "\n";

  // Finally, the disassembly:
#ifdef ENABLE_DISASSEMBLER
  os << "\n--- Disassembly: ---\n";
  Disassemble(name, os, Isolate::Current(), current_pc);
#endif
}

void CodeWrapper::CodeWrapperPrint(std::ostream& os) {
  PrintHeader(os, "CodeWrapper");
  os << "\n    code: " << Brief(code(Isolate::Current()));
}

void Foreign::ForeignPrint(std::ostream& os) {
  PrintHeader(os, "Foreign");
  os << "\n - foreign address: "
     << reinterpret_cast<void*>(foreign_address_unchecked());
  os << "\n";
}

void TrustedForeign::TrustedForeignPrint(std::ostream& os) {
  PrintHeader(os, "TrustedForeign");
  os << "\n - foreign address: " << reinterpret_cast<void*>(foreign_address());
  os << "\n";
}

void AsyncGeneratorRequest::AsyncGeneratorRequestPrint(std::ostream& os) {
  PrintHeader(os, "AsyncGeneratorRequest");
  const char* mode = "Invalid!";
  switch (resume_mode()) {
    case JSGeneratorObject::kNext:
      mode = ".next()";
      break;
    case JSGeneratorObject::kReturn:
      mode = ".return()";
      break;
    case JSGeneratorObject::kThrow:
      mode = ".throw()";
      break;
  }
  os << "\n - resume mode: " << mode;
  os << "\n - value: " << Brief(value());
  os << "\n - next: " << Brief(next());
  os << "\n";
}

static void PrintModuleFields(Tagged<Module> module, std::ostream& os) {
  os << "\n - exports: " << Brief(module->exports());
  os << "\n - status: " << module->status();
  os << "\n - exception: " << Brief(module->exception());
}

void Module::ModulePrint(std::ostream& os) {
  if (IsSourceTextModule(*this)) {
    Cast<SourceTextModule>(*this)->SourceTextModulePrint(os);
  } else if (IsSyntheticModule(*this)) {
    Cast<SyntheticModule>(*this)->SyntheticModulePrint(os);
  } else {
    UNREACHABLE();
  }
}

void SourceTextModule::SourceTextModulePrint(std::ostream& os) {
  PrintHeader(os, "SourceTextModule");
  PrintModuleFields(*this, os);
  os << "\n - sfi/code/info: " << Brief(code());
  Tagged<Script> script = GetScript();
  os << "\n - script: " << Brief(script);
  os << "\n - origin: " << Brief(script->GetNameOrSourceURL());
  os << "\n - requested_modules: " << Brief(requested_modules());
  os << "\n - import_meta: " << Brief(import_meta(kAcquireLoad));
  os << "\n - cycle_root: " << Brief(cycle_root());
  os << "\n - has_toplevel_await: " << has_toplevel_await();
  os << "\n - async_evaluation_ordinal: " << async_evaluation_ordinal();
  os << "\n";
}

void JSModuleNamespace::JSModuleNamespacePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSModuleNamespace");
  os << "\n - module: " << Brief(module());
  JSObjectPrintBody(os, *this);
}

void PrototypeInfo::PrototypeInfoPrint(std::ostream& os) {
  PrintHeader(os, "PrototypeInfo");
  os << "\n - module namespace: " << Brief(module_namespace());
  os << "\n - prototype users: " << Brief(prototype_users());
  os << "\n - registry slot: " << registry_slot();
  os << "\n - derived maps: " << Brief(derived_maps());
  os << "\n - should_be_fast_map: " << should_be_fast_map();
  os << "\n";
}

void ArrayBoilerplateDescription::ArrayBoilerplateDescriptionPrint(
    std::ostream& os) {
  PrintHeader(os, "ArrayBoilerplateDescription");
  os << "\n - elements kind: " << ElementsKindToString(elements_kind());
  os << "\n - constant elements: " << Brief(constant_elements());
  os << "\n";
}

#if V8_ENABLE_WEBASSEMBLY
void AsmWasmData::AsmWasmDataPrint(std::ostream& os) {
  PrintHeader(os, "AsmWasmData");
  os << "\n - native module: " << Brief(managed_native_module());
  os << "\n - uses bitset: " << uses_bitset()->value();
  os << "\n";
}

void WasmTypeInfo::WasmTypeInfoPrint(std::ostream& os) {
  Isolate* isolate = GetIsolateForSandbox(*this);
  PrintHeader(os, "WasmTypeInfo");
  os << "\n - type address: " << reinterpret_cast<void*>(native_type());
  os << "\n - supertypes: ";
  for (int i = 0; i < supertypes_length(); i++) {
    os << "\n  - " << Brief(supertypes(i));
  }
  os << "\n - trusted_data: " << Brief(trusted_data(isolate));
  os << "\n";
}

void WasmStruct::WasmStructPrint(std::ostream& os) {
  PrintHeader(os, "WasmStruct");
  wasm::StructType* struct_type = type();
  os << "\n - fields (" << struct_type->field_count() << "):";
  for (uint32_t i = 0; i < struct_type->field_count(); i++) {
    wasm::ValueType field = struct_type->field(i);
    os << "\n   - " << field.short_name() << ": ";
    uint32_t field_offset = struct_type->field_offset(i);
    Address field_address = RawFieldAddress(field_offset);
    switch (field.kind()) {
      case wasm::kI32:
        os << base::ReadUnalignedValue<int32_t>(field_address);
        break;
      case wasm::kI64:
        os << base::ReadUnalignedValue<int64_t>(field_address);
        break;
      case wasm::kF16:
        os << fp16_ieee_to_fp32_value(
            base::ReadUnalignedValue<uint16_t>(field_address));
        break;
      case wasm::kF32:
        os << base::ReadUnalignedValue<float>(field_address);
        break;
      case wasm::kF64:
        os << base::ReadUnalignedValue<double>(field_address);
        break;
      case wasm::kI8:
        os << base::ReadUnalignedValue<int8_t>(field_address);
        break;
      case wasm::kI16:
        os << base::ReadUnalignedValue<int16_t>(field_address);
        break;
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kRtt: {
        Tagged_t raw = base::ReadUnalignedValue<Tagged_t>(field_address);
#if V8_COMPRESS_POINTERS
        Address obj = V8HeapCompressionScheme::DecompressTagged(address(), raw);
#else
        Address obj = raw;
#endif
        os << Brief(Tagged<Object>(obj));
        break;
      }
      case wasm::kS128:
        os << "0x" << std::hex << std::setfill('0');
#ifdef V8_TARGET_BIG_ENDIAN
        for (int j = 0; j < kSimd128Size; j++) {
#else
        for (int j = kSimd128Size - 1; j >= 0; j--) {
#endif
          os << std::setw(2)
             << static_cast<int>(reinterpret_cast<uint8_t*>(field_address)[j]);
        }
        os << std::dec << std::setfill(' ');
        break;
      case wasm::kBottom:
      case wasm::kVoid:
        UNREACHABLE();
    }
  }
  os << "\n";
}

void WasmArray::WasmArrayPrint(std::ostream& os) {
  PrintHeader(os, "WasmArray");
  wasm::ArrayType* array_type = type();
  uint32_t len = length();
  os << "\n - element type: " << array_type->element_type().name();
  os << "\n - length: " << len;
  Address data_ptr = ptr() + WasmArray::kHeaderSize - kHeapObjectTag;
  switch (array_type->element_type().kind()) {
    case wasm::kI32:
      PrintTypedArrayElements(os, reinterpret_cast<int32_t*>(data_ptr), len,
                              true);
      break;
    case wasm::kI64:
      PrintTypedArrayElements(os, reinterpret_cast<int64_t*>(data_ptr), len,
                              true);
      break;
    case wasm::kF16:
      PrintTypedArrayElements(os, reinterpret_cast<Fp16Printer*>(data_ptr), len,
                              true);
      break;
    case wasm::kF32:
      PrintTypedArrayElements(os, reinterpret_cast<float*>(data_ptr), len,
                              true);
      break;
    case wasm::kF64:
      PrintTypedArrayElements(os, reinterpret_cast<double*>(data_ptr), len,
                              true);
      break;
    case wasm::kI8:
      PrintTypedArrayElements(os, reinterpret_cast<int8_t*>(data_ptr), len,
                              true);
      break;
    case wasm::kI16:
      PrintTypedArrayElements(os, reinterpret_cast<int16_t*>(data_ptr), len,
                              true);
      break;
    case wasm::kRef:
    case wasm::kRefNull: {
      os << "\n - elements:";
      constexpr uint32_t kWasmArrayMaximumPrintedElements = 5;
      for (uint32_t i = 0;
           i < std::min(this->length(), kWasmArrayMaximumPrintedElements);
           i++) {
        os << "\n   " << static_cast<int>(i) << " - "
           << Brief(TaggedField<Object>::load(*this, this->element_offset(i)));
      }
      if (this->length() > kWasmArrayMaximumPrintedElements) os << "\n   ...";
      break;
    }
    case wasm::kS128: {
      os << "\n - elements:";
      constexpr uint32_t kWasmArrayMaximumPrintedElements = 5;
      for (uint32_t i = 0;
           i < std::min(this->length(), kWasmArrayMaximumPrintedElements);
           i++) {
        os << "\n   " << static_cast<int>(i) << " - 0x" << std::hex
           << std::setfill('0');
#ifdef V8_TARGET_BIG_ENDIAN
        for (int j = 0; j < kSimd128Size; j++) {
#else
        for (int j = kSimd128Size - 1; j >= 0; j--) {
#endif
          os << std::setw(2)
             << static_cast<int>(
                    reinterpret_cast<uint8_t*>(this->ElementAddress(i))[j]);
        }
        os << std::dec << std::setfill(' ');
      }
      if (this->length() > kWasmArrayMaximumPrintedElements) os << "\n   ...";
      break;
    }
    case wasm::kRtt:
    case wasm::kBottom:
    case wasm::kVoid:
      UNREACHABLE();
  }
  os << "\n";
}

void WasmContinuationObject::WasmContinuationObjectPrint(std::ostream& os) {
  PrintHeader(os, "WasmContinuationObject");
  os << "\n - parent: " << parent();
  os << "\n - jmpbuf: " << jmpbuf();
  os << "\n - stack: " << stack();
  os << "\n";
}

void WasmSuspenderObject::WasmSuspenderObjectPrint(std::ostream& os) {
  PrintHeader(os, "WasmSuspenderObject");
  os << "\n - continuation: " << continuation();
  os << "\n - parent: " << parent();
  os << "\n - promise: " << promise();
  os << "\n - resume: " << resume();
  os << "\n - reject: " << reject();
  os << "\n - state: " << state();
  os << "\n - has_js_frames: " << has_js_frames();
  os << "\n";
}

void WasmSuspendingObject::WasmSuspendingObjectPrint(std::ostream& os) {
  PrintHeader(os, "WasmSuspendingObject");
  os << "\n - callable: " << callable();
  os << "\n";
}

void WasmInstanceObject::WasmInstanceObjectPrint(std::ostream& os) {
  Isolate* isolate = GetIsolateForSandbox(*this);
  JSObjectPrintHeader(os, *this, "WasmInstanceObject");
  os << "\n - trusted_data: " << Brief(trusted_data(isolate));
  os << "\n - module_object: " << Brief(module_object());
  os << "\n - exports_object: " << Brief(exports_object());
  JSObjectPrintBody(os, *this);
  os << "\n";
}

void WasmTrustedInstanceData::WasmTrustedInstanceDataPrint(std::ostream& os) {
#define PRINT_WASM_INSTANCE_FIELD(name, convert) \
  os << "\n - " #name ": " << convert(name());
#define PRINT_OPTIONAL_WASM_INSTANCE_FIELD(name, convert) \
  if (has_##name()) os << "\n - " #name ": " << convert(name());

  auto to_void_ptr = [](auto value) {
    static_assert(sizeof(value) == kSystemPointerSize);
    return reinterpret_cast<void*>(value);
  };

  PrintHeader(os, "WasmTrustedInstanceData");
  PRINT_OPTIONAL_WASM_INSTANCE_FIELD(instance_object, Brief);
  PRINT_WASM_INSTANCE_FIELD(native_context, Brief);
  PRINT_WASM_INSTANCE_FIELD(shared_part, Brief);
  PRINT_WASM_INSTANCE_FIELD(memory_objects, Brief);
  PRINT_OPTIONAL_WASM_INSTANCE_FIELD(untagged_globals_buffer, Brief);
  PRINT_OPTIONAL_WASM_INSTANCE_FIELD(tagged_globals_buffer, Brief);
  PRINT_OPTIONAL_WASM_INSTANCE_FIELD(imported_mutable_globals_buffers, Brief);
#if V8_ENABLE_DRUMBRAKE
  PRINT_OPTIONAL_WASM_INSTANCE_FIELD(interpreter_object, Brief);
#endif  // V8_ENABLE_DRUMBRAKE
  PRINT_OPTIONAL_WASM_INSTANCE_FIELD(tables, Brief);
  PRINT_WASM_INSTANCE_FIELD(dispatch_table0, Brief);
  PRINT_WASM_INSTANCE_FIELD(dispatch_tables, Brief);
  PRINT_WASM_INSTANCE_FIELD(dispatch_table_for_imports, Brief);
  PRINT_OPTIONAL_WASM_INSTANCE_FIELD(tags_table, Brief);
  PRINT_WASM_INSTANCE_FIELD(func_refs, Brief);
  PRINT_WASM_INSTANCE_FIELD(managed_object_maps, Brief);
  PRINT_WASM_INSTANCE_FIELD(feedback_vectors, Brief);
  PRINT_WASM_INSTANCE_FIELD(well_known_imports, Brief);
  PRINT_WASM_INSTANCE_FIELD(memory0_start, to_void_ptr);
  PRINT_WASM_INSTANCE_FIELD(memory0_size, +);
  PRINT_WASM_INSTANCE_FIELD(new_allocation_limit_address, to_void_ptr);
  PRINT_WASM_INSTANCE_FIELD(new_allocation_top_address, to_void_ptr);
  PRINT_WASM_INSTANCE_FIELD(old_allocation_limit_address, to_void_ptr);
  PRINT_WASM_INSTANCE_FIELD(old_allocation_top_address, to_void_ptr);
#if V8_ENABLE_DRUMBRAKE
  PRINT_WASM_INSTANCE_FIELD(imported_function_indices, Brief);
#endif  // V8_ENABLE_DRUMBRAKE
  PRINT_WASM_INSTANCE_FIELD(globals_start, to_void_ptr);
  PRINT_WASM_INSTANCE_FIELD(imported_mutable_globals, Brief);
  PRINT_WASM_INSTANCE_FIELD(jump_table_start, to_void_ptr);
  PRINT_WASM_INSTANCE_FIELD(data_segment_starts, Brief);
  PRINT_WASM_INSTANCE_FIELD(data_segment_sizes, Brief);
  PRINT_WASM_INSTANCE_FIELD(element_segments, Brief);
  PRINT_WASM_INSTANCE_FIELD(hook_on_function_call_address, to_void_ptr);
  PRINT_WASM_INSTANCE_FIELD(tiering_budget_array, to_void_ptr);
  PRINT_WASM_INSTANCE_FIELD(memory_bases_and_sizes, Brief);
  PRINT_WASM_INSTANCE_FIELD(break_on_entry, static_cast<int>);
  os << "\n";

#undef PRINT_OPTIONAL_WASM_INSTANCE_FIELD
#undef PRINT_WASM_INSTANCE_FIELD
}

void WasmDispatchTable::WasmDispatchTablePrint(std::ostream& os) {
  PrintHeader(os, "WasmDispatchTable");
  int len = length();
  os << "\n - length: " << len;
  os << "\n - capacity: " << capacity();
  // Only print up to 55 elements; otherwise print the first 50 and "[...]".
  int printed = len > 55 ? 50 : len;
  for (int i = 0; i < printed; ++i) {
    os << "\n " << std::setw(8) << i << ": sig: " << sig(i)
       << "; target: " << AsHex::Address(target(i))
       << "; implicit_arg: " << Brief(implicit_arg(i));
  }
  if (printed != len) os << "\n  [...]";
  os << "\n";
}

// Never called directly, as WasmFunctionData is an "abstract" class.
void WasmFunctionData::WasmFunctionDataPrint(std::ostream& os) {
  Isolate* isolate = GetIsolateForSandbox(*this);
  os << "\n - func_ref: " << Brief(func_ref());
  os << "\n - internal: " << Brief(internal());
  os << "\n - wrapper_code: " << Brief(wrapper_code(isolate));
  os << "\n - js_promise_flags: " << js_promise_flags();
  // No newline here; the caller prints it after printing additional fields.
}

void WasmExportedFunctionData::WasmExportedFunctionDataPrint(std::ostream& os) {
  PrintHeader(os, "WasmExportedFunctionData");
  WasmFunctionDataPrint(os);
  os << "\n - instance_data: " << Brief(instance_data());
  os << "\n - function_index: " << function_index();
  os << "\n - signature: " << reinterpret_cast<const void*>(sig());
  os << "\n - wrapper_budget: " << wrapper_budget()->value();
  os << "\n";
}

void WasmJSFunctionData::WasmJSFunctionDataPrint(std::ostream& os) {
  PrintHeader(os, "WasmJSFunctionData");
  WasmFunctionDataPrint(os);
  os << "\n - canonical_sig_index: " << canonical_sig_index();
  os << "\n";
}

void WasmResumeData::WasmResumeDataPrint(std::ostream& os) {
  PrintHeader(os, "WasmResumeData");
  os << "\n - suspender: " << Brief(suspender());
  os << '\n';
}

void WasmImportData::WasmImportDataPrint(std::ostream& os) {
  PrintHeader(os, "WasmImportData");
  Isolate* isolate = GetIsolateForSandbox(*this);
  os << "\n - native_context: " << Brief(native_context());
  os << "\n - callable: " << Brief(callable());
  os << "\n - instance_data: ";
  if (has_instance_data()) {
    os << Brief(instance_data());
  } else {
    os << "<empty>";
  }
  os << "\n - suspend: " << suspend();
  os << "\n - wrapper_budget: " << wrapper_budget();
  os << "\n - call_origin: " << Brief(call_origin());
  os << "\n - code: " << Brief(code(isolate));
  os << "\n";
}

void WasmInternalFunction::WasmInternalFunctionPrint(std::ostream& os) {
  PrintHeader(os, "WasmInternalFunction");
  os << "\n - call target: " << reinterpret_cast<void*>(call_target());
  os << "\n - implicit arg: " << Brief(implicit_arg());
  os << "\n - external: " << Brief(external());
  os << "\n";
}

void WasmFuncRef::WasmFuncRefPrint(std::ostream& os) {
  PrintHeader(os, "WasmFuncRef");
  Isolate* isolate = GetIsolateForSandbox(*this);
  os << "\n - internal: " << Brief(internal(isolate));
  os << "\n";
}

void WasmCapiFunctionData::WasmCapiFunctionDataPrint(std::ostream& os) {
  PrintHeader(os, "WasmCapiFunctionData");
  WasmFunctionDataPrint(os);
  os << "\n - embedder_data: " << Brief(embedder_data());
  os << "\n - serialized_signature: " << Brief(serialized_signature());
  os << "\n";
}

void WasmExceptionPackage::WasmExceptionPackagePrint(std::ostream& os) {
  PrintHeader(os, "WasmExceptionPackage");
  os << "\n";
}

void WasmModuleObject::WasmModuleObjectPrint(std::ostream& os) {
  PrintHeader(os, "WasmModuleObject");
  os << "\n - module: " << module();
  os << "\n - native module: " << native_module();
  os << "\n - script: " << Brief(script());
  os << "\n";
}

void WasmGlobalObject::WasmGlobalObjectPrint(std::ostream& os) {
  PrintHeader(os, "WasmGlobalObject");
  if (type().is_reference()) {
    os << "\n - tagged_buffer: " << Brief(tagged_buffer());
  } else {
    os << "\n - untagged_buffer: " << Brief(untagged_buffer());
  }
  os << "\n - offset: " << offset();
  os << "\n - raw_type: " << raw_type();
  os << "\n - is_mutable: " << is_mutable();
  os << "\n - type: " << type();
  os << "\n - is_mutable: " << is_mutable();
  os << "\n";
}

void WasmValueObject::WasmValueObjectPrint(std::ostream& os) {
  PrintHeader(os, "WasmValueObject");
  os << "\n - value: " << Brief(value());
  os << "\n";
}
#endif  // V8_ENABLE_WEBASSEMBLY

void LoadHandler::LoadHandlerPrint(std::ostream& os) {
  PrintHeader(os, "LoadHandler");
  // TODO(ishell): implement printing based on handler kind
  os << "\n - handler: " << Brief(smi_handler());
  os << "\n - validity_cell: " << Brief(validity_cell());
  int data_count = data_field_count();
  if (data_count >= 1) {
    os << "\n - data1: " << Brief(data1());
  }
  if (data_count >= 2) {
    os << "\n - data2: " << Brief(data2());
  }
  if (data_count >= 3) {
    os << "\n - data3: " << Brief(data3());
  }
  os << "\n";
}

void StoreHandler::StoreHandlerPrint(std::ostream& os) {
  PrintHeader(os, "StoreHandler");
  // TODO(ishell): implement printing based on handler kind
  os << "\n - handler: " << Brief(smi_handler());
  os << "\n - validity_cell: " << Brief(validity_cell());
  int data_count = data_field_count();
  if (data_count >= 1) {
    os << "\n - data1: " << Brief(data1());
  }
  if (data_count >= 2) {
    os << "\n - data2: " << Brief(data2());
  }
  if (data_count >= 3) {
    os << "\n - data3: " << Brief(data3());
  }
  os << "\n";
}

void AllocationSite::AllocationSitePrint(std::ostream& os) {
  PrintHeader(os, "AllocationSite");
  if (this->HasWeakNext()) os << "\n - weak_next: " << Brief(weak_next());
  os << "\n - dependent code: " << Brief(dependent_code());
  os << "\n - nested site: " << Brief(nested_site());
  os << "\n - memento found count: "
     << Brief(Smi::FromInt(memento_found_count()));
  os << "\n - memento create count: "
     << Brief(Smi::FromInt(memento_create_count()));
  os << "\n - pretenure decision: "
     << Brief(Smi::FromInt(pretenure_decision()));
  os << "\n - transition_info: ";
  if (!PointsToLiteral()) {
    ElementsKind kind = GetElementsKind();
    os << "Array allocation with ElementsKind " << ElementsKindToString(kind);
  } else if (IsJSArray(boilerplate())) {
    os << "Array literal with boilerplate " << Brief(boilerplate());
  } else {
    os << "Object literal with boilerplate " << Brief(boilerplate());
  }
  os << "\n";
}

void AllocationMemento::AllocationMementoPrint(std::ostream& os) {
  PrintHeader(os, "AllocationMemento");
  os << "\n - allocation site: ";
  if (IsValid()) {
    GetAllocationSite()->AllocationSitePrint(os);
  } else {
    os << "<invalid>\n";
  }
}

void ScriptOrModule::ScriptOrModulePrint(std::ostream& os) {
  PrintHeader(os, "ScriptOrModule");
  os << "\n - host_defined_options: " << Brief(host_defined_options());
  os << "\n - resource_name: " << Brief(resource_name());
}

void Script::ScriptPrint(std::ostream& os) {
  PrintHeader(os, "Script");
  os << "\n - source: " << Brief(source());
  os << "\n - name: " << Brief(name());
  os << "\n - line_offset: " << line_offset();
  os << "\n - column_offset: " << column_offset();
  os << "\n - context data: " << Brief(context_data());
  os << "\n - type: " << static_cast<int>(type());
  os << "\n - line ends: " << Brief(line_ends());
  if (!has_line_ends()) os << " (not set)";
  os << "\n - id: " << id();
  os << "\n - source_url: " << Brief(source_url());
  os << "\n - source_mapping_url: " << Brief(source_mapping_url());
  os << "\n - host_defined_options: " << Brief(host_defined_options());
  os << "\n - compilation type: " << static_cast<int>(compilation_type());
  os << "\n - compiled lazy function positions: "
     << compiled_lazy_function_positions();
  bool is_wasm = false;
#if V8_ENABLE_WEBASSEMBLY
  if ((is_wasm = (type() == Type::kWasm))) {
    if (has_wasm_breakpoint_infos()) {
      os << "\n - wasm_breakpoint_infos: " << Brief(wasm_breakpoint_infos());
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  if (!is_wasm) {
    if (has_eval_from_shared()) {
      os << "\n - eval from shared: " << Brief(eval_from_shared());
    } else if (is_wrapped()) {
      os << "\n - wrapped arguments: " << Brief(wrapped_arguments());
    }
    os << "\n - eval from position: " << eval_from_position();
  }
  os << "\n - infos: " << Brief(infos());
  os << "\n";
}

void JSTemporalPlainDate::JSTemporalPlainDatePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalPlainDate");
  JSObjectPrintBody(os, *this);
}

void JSTemporalPlainTime::JSTemporalPlainTimePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalPlainTime");
  JSObjectPrintBody(os, *this);
}

void JSTemporalPlainDateTime::JSTemporalPlainDateTimePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalPlainDateTime");
  JSObjectPrintBody(os, *this);
}

void JSTemporalZonedDateTime::JSTemporalZonedDateTimePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalZonedDateTime");
  JSObjectPrintBody(os, *this);
}

void JSTemporalDuration::JSTemporalDurationPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalDuration");
  JSObjectPrintBody(os, *this);
}

void JSTemporalInstant::JSTemporalInstantPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalInstant");
  JSObjectPrintBody(os, *this);
}

void JSTemporalPlainYearMonth::JSTemporalPlainYearMonthPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalPlainYearMonth");
  JSObjectPrintBody(os, *this);
}

void JSTemporalPlainMonthDay::JSTemporalPlainMonthDayPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalPlainMonthDay");
  JSObjectPrintBody(os, *this);
}

void JSTemporalTimeZone::JSTemporalTimeZonePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalTimeZone");
  JSObjectPrintBody(os, *this);
}

void JSTemporalCalendar::JSTemporalCalendarPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSTemporalCalendar");
  JSObjectPrintBody(os, *this);
}

void JSRawJson::JSRawJsonPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSRawJson");
  JSObjectPrintBody(os, *this);
}

#ifdef V8_INTL_SUPPORT
void JSV8BreakIterator::JSV8BreakIteratorPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSV8BreakIterator");
  os << "\n - locale: " << Brief(locale());
  os << "\n - break iterator: " << Brief(break_iterator());
  os << "\n - unicode string: " << Brief(unicode_string());
  os << "\n - bound adopt text: " << Brief(bound_adopt_text());
  os << "\n - bound first: " << Brief(bound_first());
  os << "\n - bound next: " << Brief(bound_next());
  os << "\n - bound current: " << Brief(bound_current());
  os << "\n - bound break type: " << Brief(bound_break_type());
  os << "\n";
}

void JSCollator::JSCollatorPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSCollator");
  os << "\n - icu collator: " << Brief(icu_collator());
  os << "\n - bound compare: " << Brief(bound_compare());
  JSObjectPrintBody(os, *this);
}

void JSDateTimeFormat::JSDateTimeFormatPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSDateTimeFormat");
  os << "\n - locale: " << Brief(locale());
  os << "\n - icu locale: " << Brief(icu_locale());
  os << "\n - icu simple date format: " << Brief(icu_simple_date_format());
  os << "\n - icu date interval format: " << Brief(icu_date_interval_format());
  os << "\n - bound format: " << Brief(bound_format());
  os << "\n - hour cycle: " << HourCycleAsString();
  JSObjectPrintBody(os, *this);
}

void JSDisplayNames::JSDisplayNamesPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSDisplayNames");
  os << "\n - internal: " << Brief(internal());
  os << "\n - style: " << StyleAsString();
  os << "\n - fallback: " << FallbackAsString();
  JSObjectPrintBody(os, *this);
}

void JSDurationFormat::JSDurationFormatPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSDurationFormat");
  os << "\n - style_flags: " << style_flags();
  os << "\n - display_flags: " << display_flags();
  os << "\n - icu locale: " << Brief(icu_locale());
  os << "\n - icu number formatter: " << Brief(icu_number_formatter());
  JSObjectPrintBody(os, *this);
}

void JSListFormat::JSListFormatPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSListFormat");
  os << "\n - locale: " << Brief(locale());
  os << "\n - style: " << StyleAsString();
  os << "\n - type: " << TypeAsString();
  os << "\n - icu formatter: " << Brief(icu_formatter());
  JSObjectPrintBody(os, *this);
}

void JSLocale::JSLocalePrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSLocale");
  os << "\n - icu locale: " << Brief(icu_locale());
  JSObjectPrintBody(os, *this);
}

void JSNumberFormat::JSNumberFormatPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSNumberFormat");
  os << "\n - locale: " << Brief(locale());
  os << "\n - icu_number_formatter: " << Brief(icu_number_formatter());
  os << "\n - bound_format: " << Brief(bound_format());
  JSObjectPrintBody(os, *this);
}

void JSPluralRules::JSPluralRulesPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSPluralRules");
  os << "\n - locale: " << Brief(locale());
  os << "\n - type: " << TypeAsString();
  os << "\n - icu plural rules: " << Brief(icu_plural_rules());
  os << "\n - icu_number_formatter: " << Brief(icu_number_formatter());
  JSObjectPrintBody(os, *this);
}

void JSRelativeTimeFormat::JSRelativeTimeFormatPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSRelativeTimeFormat");
  os << "\n - locale: " << Brief(locale());
  os << "\n - numberingSystem: " << Brief(numberingSystem());
  os << "\n - numeric: " << NumericAsString();
  os << "\n - icu formatter: " << Brief(icu_formatter());
  os << "\n";
}

void JSSegmentIterator::JSSegmentIteratorPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSSegmentIterator");
  os << "\n - icu break iterator: " << Brief(icu_break_iterator());
  os << "\n - granularity: " << GranularityAsString(GetIsolate());
  os << "\n";
}

void JSSegmenter::JSSegmenterPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSSegmenter");
  os << "\n - locale: " << Brief(locale());
  os << "\n - granularity: " << GranularityAsString(GetIsolate());
  os << "\n - icu break iterator: " << Brief(icu_break_iterator());
  JSObjectPrintBody(os, *this);
}

void JSSegments::JSSegmentsPrint(std::ostream& os) {
  JSObjectPrintHeader(os, *this, "JSSegments");
  os << "\n - icu break iterator: " << Brief(icu_break_iterator());
  os << "\n - unicode string: " << Brief(unicode_string());
  os << "\n - granularity: " << GranularityAsString(GetIsolate());
  JSObjectPrintBody(os, *this);
}
#endif  // V8_INTL_SUPPORT

namespace {
void PrintScopeInfoList(Tagged<ScopeInfo> scope_info, std::ostream& os,
                        const char* list_name, int length) {
  DisallowGarbageCollection no_gc;
  if (length <= 0) return;
  os << "\n - " << list_name;
  os << " {\n";
  for (auto it : ScopeInfo::IterateLocalNames(scope_info, no_gc)) {
    os << "    - " << it->index() << ": " << it->name() << "\n";
  }
  os << "  }";
}
}  // namespace

void ScopeInfo::ScopeInfoPrint(std::ostream& os) {
  PrintHeader(os, "ScopeInfo");
  if (this->IsEmpty()) {
    os << "\n - empty\n";
    return;
  }
  int flags = Flags();

  os << "\n - parameters: " << ParameterCount();
  os << "\n - context locals : " << ContextLocalCount();
  if (HasInlinedLocalNames()) {
    os << "\n - inlined local names";
  } else {
    os << "\n - local names in a hashtable: "
       << Brief(context_local_names_hashtable());
  }

  os << "\n - scope type: " << scope_type();
  if (SloppyEvalCanExtendVars()) os << "\n - sloppy eval";
  os << "\n - language mode: " << language_mode();
  if (is_declaration_scope()) os << "\n - declaration scope";
  if (HasReceiver()) {
    os << "\n - receiver: " << ReceiverVariableBits::decode(flags);
  }
  if (ClassScopeHasPrivateBrand()) os << "\n - class scope has private brand";
  if (HasSavedClassVariable()) os << "\n - has saved class variable";
  if (HasNewTarget()) os << "\n - needs new target";
  if (HasFunctionName()) {
    os << "\n - function name(" << FunctionVariableBits::decode(flags) << "): ";
    ShortPrint(FunctionName(), os);
  }
  if (IsAsmModule()) os << "\n - asm module";
  if (HasSimpleParameters()) os << "\n - simple parameters";
  if (PrivateNameLookupSkipsOuterClass())
    os << "\n - private name lookup skips outer class";
  os << "\n - function kind: " << function_kind();
  if (HasOuterScopeInfo()) {
    os << "\n - outer scope info: " << Brief(OuterScopeInfo());
  }
  if (HasFunctionName()) {
    os << "\n - function name: " << Brief(FunctionName());
  }
  if (HasInferredFunctionName()) {
    os << "\n - inferred function name: " << Brief(InferredFunctionName());
  }
  if (HasContextExtensionSlot()) {
    os << "\n - has context extension slot";
  }

  if (HasPositionInfo()) {
    os << "\n - start position: " << StartPosition();
    os << "\n - end position: " << EndPosition();
  }
  os << "\n - length: " << length();
  if (length() > 0) {
    PrintScopeInfoList(*this, os, "context slots", ContextLocalCount());
    // TODO(neis): Print module stuff if present.
  }
  os << "\n";
}

void PreparseData::PreparseDataPrint(std::ostream& os) {
  PrintHeader(os, "PreparseData");
  os << "\n - data_length: " << data_length();
  os << "\n - children_length: " << children_length();
  if (data_length() > 0) {
    os << "\n - data-start: " << (address() + kDataStartOffset);
  }
  if (children_length() > 0) {
    os << "\n - children-start: " << inner_start_offset();
  }
  for (int i = 0; i < children_length(); ++i) {
    os << "\n - [" << i << "]: " << Brief(get_child(i));
  }
  os << "\n";
}

void HeapNumber::HeapNumberPrint(std::ostream& os) {
  PrintHeader(os, "HeapNumber");
  os << "\n - value: ";
  HeapNumberShortPrint(os);
  os << "\n";
}

#endif  // OBJECT_PRINT

void HeapObject::Print() { Print(*this); }

// static
void HeapObject::Print(Tagged<Object> obj) { v8::internal::Print(obj); }

// static
void HeapObject::Print(Tagged<Object> obj, std::ostream& os) {
  v8::internal::Print(obj, os);
}

void HeapObject::HeapObjectShortPrint(std::ostream& os) {
  PtrComprCageBase cage_base = GetPtrComprCageBase();
  os << AsHex::Address(this->ptr()) << " ";

  if (IsString(*this, cage_base)) {
    HeapStringAllocator allocator;
    StringStream accumulator(&allocator);
    Cast<String>(*this)->StringShortPrint(&accumulator);
    os << accumulator.ToCString().get();
    return;
  }
  if (IsJSObject(*this, cage_base)) {
    HeapStringAllocator allocator;
    StringStream accumulator(&allocator);
    Cast<JSObject>(*this)->JSObjectShortPrint(&accumulator);
    os << accumulator.ToCString().get();
    return;
  }
  switch (map(cage_base)->instance_type()) {
    case MAP_TYPE: {
      Tagged<Map> map = Cast<Map>(*this);
      if (map->instance_type() == MAP_TYPE) {
        // This is one of the meta maps, print only relevant fields.
        os << "<MetaMap (" << Brief(map->native_context_or_null()) << ")>";
      } else {
        os << "<Map";
        if (map->instance_size() != kVariableSizeSentinel) {
          os << "[" << map->instance_size() << "]";
        }
        os << "(";
        if (IsJSObjectMap(map)) {
          os << ElementsKindToString(map->elements_kind());
        } else {
          os << map->instance_type();
        }
        os << ")>";
      }
    } break;
    case AWAIT_CONTEXT_TYPE: {
      os << "<AwaitContext generator= ";
      HeapStringAllocator allocator;
      StringStream accumulator(&allocator);
      ShortPrint(Cast<Context>(*this)->extension(), &accumulator);
      os << accumulator.ToCString().get();
      os << '>';
      break;
    }
    case BLOCK_CONTEXT_TYPE:
      os << "<BlockContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case CATCH_CONTEXT_TYPE:
      os << "<CatchContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case DEBUG_EVALUATE_CONTEXT_TYPE:
      os << "<DebugEvaluateContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case EVAL_CONTEXT_TYPE:
      os << "<EvalContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case FUNCTION_CONTEXT_TYPE:
      os << "<FunctionContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case MODULE_CONTEXT_TYPE:
      os << "<ModuleContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case NATIVE_CONTEXT_TYPE:
      os << "<NativeContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case SCRIPT_CONTEXT_TYPE:
      os << "<ScriptContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case WITH_CONTEXT_TYPE:
      os << "<WithContext[" << Cast<Context>(*this)->length() << "]>";
      break;
    case SCRIPT_CONTEXT_TABLE_TYPE:
      os << "<ScriptContextTable["
         << Cast<ScriptContextTable>(*this)->capacity() << "]>";
      break;
    case HASH_TABLE_TYPE:
      os << "<HashTable[" << Cast<FixedArray>(*this)->length() << "]>";
      break;
    case ORDERED_HASH_MAP_TYPE:
      os << "<OrderedHashMap[" << Cast<FixedArray>(*this)->length() << "]>";
      break;
    case ORDERED_HASH_SET_TYPE:
      os << "<OrderedHashSet[" << Cast<FixedArray>(*this)->length() << "]>";
      break;
    case ORDERED_NAME_DICTIONARY_TYPE:
      os << "<OrderedNameDictionary[" << Cast<FixedArray>(*this)->length()
         << "]>";
      break;
    case NAME_DICTIONARY_TYPE:
      os << "<NameDictionary[" << Cast<FixedArray>(*this)->length() << "]>";
      break;
    case SWISS_NAME_DICTIONARY_TYPE:
      os << "<SwissNameDictionary["
         << Cast<SwissNameDictionary>(*this)->Capacity() << "]>";
      break;
    case GLOBAL_DICTIONARY_TYPE:
      os << "<GlobalDictionary[" << Cast<FixedArray>(*this)->length() << "]>";
      break;
    case NUMBER_DICTIONARY_TYPE:
      os << "<NumberDictionary[" << Cast<FixedArray>(*this)->length() << "]>";
      break;
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
      os << "<SimpleNumberDictionary[" << Cast<FixedArray>(*this)->length()
         << "]>";
      break;
    case FIXED_ARRAY_TYPE:
      os << "<FixedArray[" << Cast<FixedArray>(*this)->length() << "]>";
      break;
    case OBJECT_BOILERPLATE_DESCRIPTION_TYPE:
      os << "<ObjectBoilerplateDescription["
         << Cast<ObjectBoilerplateDescription>(*this)->capacity() << "]>";
      break;
    case FIXED_DOUBLE_ARRAY_TYPE:
      os << "<FixedDoubleArray[" << Cast<FixedDoubleArray>(*this)->length()
         << "]>";
      break;
    case BYTE_ARRAY_TYPE:
      os << "<ByteArray[" << Cast<ByteArray>(*this)->length() << "]>";
      break;
    case BYTECODE_ARRAY_TYPE:
      os << "<BytecodeArray[" << Cast<BytecodeArray>(*this)->length() << "]>";
      break;
    case EXTERNAL_POINTER_ARRAY_TYPE:
      os << "<ExternalPointerArray["
         << Cast<ExternalPointerArray>(*this)->length() << "]>";
      break;
    case DESCRIPTOR_ARRAY_TYPE:
      os << "<DescriptorArray["
         << Cast<DescriptorArray>(*this)->number_of_descriptors() << "]>";
      break;
    case TRANSITION_ARRAY_TYPE:
      os << "<TransitionArray[" << Cast<TransitionArray>(*this)->length()
         << "]>";
      break;
    case PROPERTY_ARRAY_TYPE:
      os << "<PropertyArray[" << Cast<PropertyArray>(*this)->length() << "]>";
      break;
    case FEEDBACK_CELL_TYPE: {
      {
        ReadOnlyRoots roots = GetReadOnlyRoots();
        os << "<FeedbackCell[";
        if (map() == roots.no_closures_cell_map()) {
          os << "no feedback";
        } else if (map() == roots.one_closure_cell_map()) {
          os << "one closure";
        } else if (map() == roots.many_closures_cell_map()) {
          os << "many closures";
        } else {
          os << "!!!INVALID MAP!!!";
        }
        os << "]>";
      }
      break;
    }
    case CLOSURE_FEEDBACK_CELL_ARRAY_TYPE:
      os << "<ClosureFeedbackCellArray["
         << Cast<ClosureFeedbackCellArray>(*this)->length() << "]>";
      break;
    case FEEDBACK_VECTOR_TYPE:
      os << "<FeedbackVector[" << Cast<FeedbackVector>(*this)->length() << "]>";
      break;
    case FREE_SPACE_TYPE:
      os << "<FreeSpace[" << Cast<FreeSpace>(*this)->size(kRelaxedLoad) << "]>";
      break;

    case PREPARSE_DATA_TYPE: {
      Tagged<PreparseData> data = Cast<PreparseData>(*this);
      os << "<PreparseData[data=" << data->data_length()
         << " children=" << data->children_length() << "]>";
      break;
    }

    case UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA_TYPE: {
      Tagged<UncompiledDataWithoutPreparseData> data =
          Cast<UncompiledDataWithoutPreparseData>(*this);
      os << "<UncompiledDataWithoutPreparseData (" << data->start_position()
         << ", " << data->end_position() << ")]>";
      break;
    }

    case UNCOMPILED_DATA_WITH_PREPARSE_DATA_TYPE: {
      Tagged<UncompiledDataWithPreparseData> data =
          Cast<UncompiledDataWithPreparseData>(*this);
      os << "<UncompiledDataWithPreparseData (" << data->start_position()
         << ", " << data->end_position()
         << ") preparsed=" << Brief(data->preparse_data()) << ">";
      break;
    }

    case SHARED_FUNCTION_INFO_TYPE: {
      Tagged<SharedFunctionInfo> shared = Cast<SharedFunctionInfo>(*this);
      std::unique_ptr<char[]> debug_name = shared->DebugNameCStr();
      if (debug_name[0] != '\0') {
        os << "<SharedFunctionInfo " << debug_name.get() << ">";
      } else {
        os << "<SharedFunctionInfo>";
      }
      break;
    }
    case JS_MESSAGE_OBJECT_TYPE:
      os << "<JSMessageObject>";
      break;
#define MAKE_STRUCT_CASE(TYPE, Name, name)    \
  case TYPE:                                  \
    os << "<" #Name;                          \
    Cast<Name>(*this)->BriefPrintDetails(os); \
    os << ">";                                \
    break;
      STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE
    case ALLOCATION_SITE_TYPE: {
      os << "<AllocationSite";
      Cast<AllocationSite>(*this)->BriefPrintDetails(os);
      os << ">";
      break;
    }
    case SCOPE_INFO_TYPE: {
      Tagged<ScopeInfo> scope = Cast<ScopeInfo>(*this);
      os << "<ScopeInfo";
      if (!scope->IsEmpty()) os << " " << scope->scope_type();
      os << ">";
      break;
    }
    case CODE_TYPE: {
      Tagged<Code> code = Cast<Code>(*this);
      os << "<Code " << CodeKindToString(code->kind());
      if (code->is_builtin()) {
        os << " " << Builtins::name(code->builtin_id());
      }
      os << ">";
      break;
    }
    case HOLE_TYPE: {
#define PRINT_HOLE(Type, Value, _) \
  if (Is##Type(*this)) {           \
    os << "<" #Value ">";          \
    break;                         \
  }
      HOLE_LIST(PRINT_HOLE)
#undef PRINT_HOLE
      UNREACHABLE();
    }
    case INSTRUCTION_STREAM_TYPE: {
      Tagged<InstructionStream> istream = Cast<InstructionStream>(*this);
      Tagged<Code> code = istream->code(kAcquireLoad);
      os << "<InstructionStream " << CodeKindToString(code->kind());
      if (code->is_builtin()) {
        os << " " << Builtins::name(code->builtin_id());
      }
      os << ">";
      break;
    }
    case ODDBALL_TYPE: {
      if (IsUndefined(*this)) {
        os << "<undefined>";
      } else if (IsNull(*this)) {
        os << "<null>";
      } else if (IsTrue(*this)) {
        os << "<true>";
      } else if (IsFalse(*this)) {
        os << "<false>";
      } else {
        os << "<Odd Oddball: ";
        os << Cast<Oddball>(*this)->to_string()->ToCString().get();
        os << ">";
      }
      break;
    }
    case SYMBOL_TYPE: {
      Tagged<Symbol> symbol = Cast<Symbol>(*this);
      symbol->SymbolShortPrint(os);
      break;
    }
    case HEAP_NUMBER_TYPE: {
      os << "<HeapNumber ";
      Cast<HeapNumber>(*this)->HeapNumberShortPrint(os);
      os << ">";
      break;
    }
    case BIGINT_TYPE: {
      os << "<BigInt ";
      Cast<BigInt>(*this)->BigIntShortPrint(os);
      os << ">";
      break;
    }
    case JS_PROXY_TYPE:
      os << "<JSProxy>";
      break;
    case FOREIGN_TYPE:
      os << "<Foreign>";
      break;
    case CELL_TYPE: {
      os << "<Cell value= ";
      HeapStringAllocator allocator;
      StringStream accumulator(&allocator);
      ShortPrint(Cast<Cell>(*this)->value(), &accumulator);
      os << accumulator.ToCString().get();
      os << '>';
      break;
    }
    case PROPERTY_CELL_TYPE: {
      Tagged<PropertyCell> cell = Cast<PropertyCell>(*this);
      os << "<PropertyCell name=";
      ShortPrint(cell->name(), os);
      os << " value=";
      HeapStringAllocator allocator;
      StringStream accumulator(&allocator);
      ShortPrint(cell->value(kAcquireLoad), &accumulator);
      os << accumulator.ToCString().get();
      os << '>';
      break;
    }
    case CONST_TRACKING_LET_CELL_TYPE: {
      os << "<ConstTrackingLetCell>";
      break;
    }
    case ACCESSOR_INFO_TYPE: {
      Tagged<AccessorInfo> info = Cast<AccessorInfo>(*this);
      os << "<AccessorInfo ";
      os << "name= " << Brief(info->name());
      os << ", data= " << Brief(info->data());
      os << ">";
      break;
    }
    case FUNCTION_TEMPLATE_INFO_TYPE: {
      Tagged<FunctionTemplateInfo> info = Cast<FunctionTemplateInfo>(*this);
      os << "<FunctionTemplateInfo ";
      Isolate* isolate;
      if (GetIsolateFromHeapObject(*this, &isolate)) {
        os << "callback= " << reinterpret_cast<void*>(info->callback(isolate));
      } else {
        os << "callback= " << kUnavailableString;
      }
      os << ", data= " << Brief(info->callback_data(kAcquireLoad));
      os << ", has_side_effects= ";
      if (info->has_side_effects()) {
        os << "true>";
      } else {
        os << "false>";
      }
      break;
    }
#if V8_ENABLE_WEBASSEMBLY
    case WASM_DISPATCH_TABLE_TYPE:
      os << "<WasmDispatchTable[" << Cast<WasmDispatchTable>(*this)->length()
         << "]>";
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    default:
      os << "<Other heap object (" << map()->instance_type() << ")>";
      break;
  }
}

void HeapNumber::HeapNumberShortPrint(std::ostream& os) {
  static constexpr uint64_t kUint64AllBitsSet =
      static_cast<uint64_t>(int64_t{-1});
  // Min/max integer values representable by 52 bits of mantissa and 1 sign bit.
  static constexpr int64_t kMinSafeInteger =
      static_cast<int64_t>(kUint64AllBitsSet << 53);
  static constexpr int64_t kMaxSafeInteger = -(kMinSafeInteger + 1);

  double val = value();
  if (i::IsMinusZero(val)) {
    os << "-0.0";
  } else if (val == DoubleToInteger(val) &&
             val >= static_cast<double>(kMinSafeInteger) &&
             val <= static_cast<double>(kMaxSafeInteger)) {
    // Print integer HeapNumbers in safe integer range with max precision: as
    // 9007199254740991.0 instead of 9.0072e+15
    int64_t i = static_cast<int64_t>(val);
    os << i << ".0";
  } else {
    os << val;
  }
}

// TODO(cbruni): remove once the new maptracer is in place.
void Name::NameShortPrint() {
  if (IsString(this)) {
    PrintF("%s", Cast<String>(this)->ToCString().get());
  } else {
    DCHECK(IsSymbol(this));
    Tagged<Symbol> s = Cast<Symbol>(this);
    if (IsUndefined(s->description())) {
      PrintF("#<%s>", s->PrivateSymbolToName());
    } else {
      PrintF("<%s>", Cast<String>(s->description())->ToCString().get());
    }
  }
}

// TODO(cbruni): remove once the new maptracer is in place.
int Name::NameShortPrint(base::Vector<char> str) {
  if (IsString(this)) {
    return SNPrintF(str, "%s", Cast<String>(this)->ToCString().get());
  } else {
    DCHECK(IsSymbol(this));
    Tagged<Symbol> s = Cast<Symbol>(this);
    if (IsUndefined(s->description())) {
      return SNPrintF(str, "#<%s>", s->PrivateSymbolToName());
    } else {
      return SNPrintF(str, "<%s>",
                      Cast<String>(s->description())->ToCString().get());
    }
  }
}

void Symbol::SymbolShortPrint(std::ostream& os) {
  os << "<Symbol:";
  if (!IsUndefined(description())) {
    os << " ";
    Tagged<String> description_as_string = Cast<String>(description());
    description_as_string->PrintUC16(os, 0, description_as_string->length());
  } else {
    os << " (" << PrivateSymbolToName() << ")";
  }
  os << ">";
}

void Map::PrintMapDetails(std::ostream& os) {
  DisallowGarbageCollection no_gc;
  this->MapPrint(os);
  instance_descriptors()->PrintDescriptors(os);
}

void Map::MapPrint(std::ostream& os) {
  bool is_meta_map = instance_type() == MAP_TYPE;
#ifdef OBJECT_PRINT
  PrintHeader(os, is_meta_map ? "MetaMap" : "Map");
#else
  os << (is_meta_map ? "MetaMap=" : "Map=") << reinterpret_cast<void*>(ptr());
#endif
  os << "\n - type: " << instance_type();
  os << "\n - instance size: ";
  if (instance_size() == kVariableSizeSentinel) {
    os << "variable";
  } else {
    os << instance_size();
  }
  if (is_meta_map) {
    // This is one of the meta maps, print only relevant fields.
    os << "\n - native_context: " << Brief(native_context_or_null());
    os << "\n";
    return;
  }

  if (IsJSObjectMap(*this)) {
    os << "\n - inobject properties: " << GetInObjectProperties();
    os << "\n - unused property fields: " << UnusedPropertyFields();
  }
  os << "\n - elements kind: " << ElementsKindToString(elements_kind());
  os << "\n - enum length: ";
  if (EnumLength() == kInvalidEnumCacheSentinel) {
    os << "invalid";
  } else {
    os << EnumLength();
  }
  if (is_deprecated()) os << "\n - deprecated_map";
  if (is_stable()) os << "\n - stable_map";
  if (is_migration_target()) os << "\n - migration_target";
  if (is_dictionary_map()) os << "\n - dictionary_map";
  if (has_named_interceptor()) os << "\n - named_interceptor";
  if (has_indexed_interceptor()) os << "\n - indexed_interceptor";
  if (may_have_interesting_properties())
    os << "\n - may_have_interesting_properties";
  if (is_undetectable()) os << "\n - undetectable";
  if (is_callable()) os << "\n - callable";
  if (is_constructor()) os << "\n - constructor";
  if (has_prototype_slot()) {
    os << "\n - has_prototype_slot";
    if (has_non_instance_prototype()) os << " (non-instance prototype)";
  }
  if (is_access_check_needed()) os << "\n - access_check_needed";
  if (!is_extensible()) os << "\n - non-extensible";
  if (IsContextMap(*this)) {
    os << "\n - native context: " << Brief(native_context());
  } else if (is_prototype_map()) {
    os << "\n - prototype_map";
    os << "\n - prototype info: " << Brief(prototype_info());
  } else {
    os << "\n - back pointer: " << Brief(GetBackPointer());
  }
  os << "\n - prototype_validity cell: "
     << Brief(prototype_validity_cell(kRelaxedLoad));
  os << "\n - instance descriptors " << (owns_descriptors() ? "(own) " : "")
     << "#" << NumberOfOwnDescriptors() << ": "
     << Brief(instance_descriptors());

  // Read-only maps can't have transitions, which is fortunate because we need
  // the isolate to iterate over the transitions.
  if (!IsReadOnlyHeapObject(*this)) {
    Isolate* isolate = GetIsolateFromWritableObject(*this);
    TransitionsAccessor transitions(isolate, *this);
    int nof_transitions = transitions.NumberOfTransitions();
    if (nof_transitions > 0) {
      os << "\n - transitions #" << nof_transitions << ": ";
      Tagged<HeapObject> heap_object;
      Tagged<Smi> smi;
      if (raw_transitions().ToSmi(&smi)) {
        os << Brief(smi);
      } else if (raw_transitions().GetHeapObject(&heap_object)) {
        os << Brief(heap_object);
      }
#ifdef OBJECT_PRINT
      transitions.PrintTransitions(os);
#endif  // OBJECT_PRINT
    }
  }
  os << "\n - prototype: " << Brief(prototype());
  if (has_non_instance_prototype()) {
    os << "\n - non-instance prototype: " << Brief(GetNonInstancePrototype());
  }
  if (!IsContextMap(*this)) {
    os << "\n - constructor: " << Brief(GetConstructor());
  }
  os << "\n - dependent code: " << Brief(dependent_code());
  os << "\n - construction counter: " << construction_counter();
  os << "\n";
}

void DescriptorArray::PrintDescriptors(std::ostream& os) {
  for (InternalIndex i : InternalIndex::Range(number_of_descriptors())) {
    Tagged<Name> key = GetKey(i);
    os << "\n  [" << i.as_int() << "]: ";
#ifdef OBJECT_PRINT
    key->NamePrint(os);
#else
    ShortPrint(key, os);
#endif
    os << " ";
    PrintDescriptorDetails(os, i, PropertyDetails::kPrintFull);
  }
  os << "\n";
}

void DescriptorArray::PrintDescriptorDetails(std::ostream& os,
                                             InternalIndex descriptor,
                                             PropertyDetails::PrintMode mode) {
  PropertyDetails details = GetDetails(descriptor);
  details.PrintAsFastTo(os, mode);
  os << " @ ";
  switch (details.location()) {
    case PropertyLocation::kField: {
      Tagged<FieldType> field_type = GetFieldType(descriptor);
      FieldType::PrintTo(field_type, os);
      break;
    }
    case PropertyLocation::kDescriptor:
      Tagged<Object> value = GetStrongValue(descriptor);
      os << Brief(value);
      if (IsAccessorPair(value)) {
        Tagged<AccessorPair> pair = Cast<AccessorPair>(value);
        os << "(get: " << Brief(pair->getter())
           << ", set: " << Brief(pair->setter()) << ")";
      }
      break;
  }
}

#if defined(DEBUG) || defined(OBJECT_PRINT)
// This method is only meant to be called from gdb for debugging purposes.
// Since the string can also be in two-byte encoding, non-Latin1 characters
// will be ignored in the output.
char* String::ToAsciiArray() {
  // Static so that subsequent calls frees previously allocated space.
  // This also means that previous results will be overwritten.
  static char* buffer = nullptr;
  if (buffer != nullptr) delete[] buffer;
  buffer = new char[length() + 1];
  WriteToFlat(this, reinterpret_cast<uint8_t*>(buffer), 0, length());
  buffer[length()] = 0;
  return buffer;
}

// static
void TransitionsAccessor::PrintOneTransition(std::ostream& os, Tagged<Name> key,
                                             Tagged<Map> target) {
  os << "\n     ";
#ifdef OBJECT_PRINT
  key->NamePrint(os);
#else
  ShortPrint(key, os);
#endif
  os << ": ";
  ReadOnlyRoots roots = key->GetReadOnlyRoots();
  if (key == roots.nonextensible_symbol()) {
    os << "(transition to non-extensible)";
  } else if (key == roots.sealed_symbol()) {
    os << "(transition to sealed)";
  } else if (key == roots.frozen_symbol()) {
    os << "(transition to frozen)";
  } else if (key == roots.elements_transition_symbol()) {
    os << "(transition to " << ElementsKindToString(target->elements_kind())
       << ")";
  } else if (key == roots.strict_function_transition_symbol()) {
    os << " (transition to strict function)";
  } else {
    DCHECK(!IsSpecialTransition(roots, key));
    os << "(transition to ";
    InternalIndex descriptor = target->LastAdded();
    Tagged<DescriptorArray> descriptors = target->instance_descriptors();
    descriptors->PrintDescriptorDetails(os, descriptor,
                                        PropertyDetails::kForTransitions);
    os << ")";
  }
  os << " -> " << Brief(target);
}

void TransitionArray::PrintInternal(std::ostream& os) {
  {
    int num_transitions = number_of_transitions();
    os << "   Transition array #" << num_transitions << ":";
    for (int i = 0; i < num_transitions; i++) {
      Tagged<Name> key = GetKey(i);
      Tagged<Map> target;
      GetTargetIfExists(i, GetIsolateFromWritableObject(*this), &target);
      TransitionsAccessor::PrintOneTransition(os, key, target);
    }
  }

  if (HasPrototypeTransitions()) {
    auto prototype_transitions = GetPrototypeTransitions();
    int num_transitions = NumberOfPrototypeTransitions(prototype_transitions);
    os << "\n   Prototype transitions #" << num_transitions << ":";
    for (int i = 0; i < num_transitions; i++) {
      auto maybe = prototype_transitions->get(
          TransitionArray::kProtoTransitionHeaderSize + i);
      Tagged<HeapObject> target;
      if (maybe.GetHeapObjectIfWeak(&target)) {
        auto map = Cast<Map>(target);
        os << "\n     " << Brief(map->prototype()) << " -> "
           << Brief(Cast<Map>(target));
      }
    }
  }
}

void TransitionsAccessor::PrintTransitions(std::ostream& os) {
  switch (encoding()) {
    case kPrototypeInfo:
    case kUninitialized:
    case kMigrationTarget:
      return;
    case kWeakRef: {
      Tagged<Map> target =
          Cast<Map>(raw_transitions_.GetHeapObjectAssumeWeak());
      Tagged<Name> key = GetSimpleTransitionKey(target);
      PrintOneTransition(os, key, target);
      break;
    }
    case kFullTransitionArray:
      std::cout << "\n";
      return transitions()->PrintInternal(os);
  }
}

void TransitionsAccessor::PrintTransitionTree() {
  StdoutStream os;
  os << (IsUndefined(map_->GetBackPointer()) ? "root_" : "")
     << "map= " << Brief(map_);
  DisallowGarbageCollection no_gc;
  PrintTransitionTree(os, 0, &no_gc);
  os << "\n" << std::flush;
}

void TransitionsAccessor::PrintTransitionTree(
    std::ostream& os, int level, DisallowGarbageCollection* no_gc) {
  ReadOnlyRoots roots = ReadOnlyRoots(isolate_);
  int pos = 0;
  int proto_pos = 0;
  ForEachTransitionWithKey(
      no_gc,
      [&](Tagged<Name> key, Tagged<Map> target) {
        os << std::endl
           << "  " << level << "/" << pos << ":" << std::setw(level * 2 + 2)
           << " ";
        pos++;
        std::stringstream ss;
        ss << Brief(target);
        os << std::left << std::setw(50) << ss.str() << ": ";

        if (key == roots.nonextensible_symbol()) {
          os << "to non-extensible";
        } else if (key == roots.sealed_symbol()) {
          os << "to sealed ";
        } else if (key == roots.frozen_symbol()) {
          os << "to frozen";
        } else if (key == roots.elements_transition_symbol()) {
          os << "to " << ElementsKindToString(target->elements_kind());
        } else if (key == roots.strict_function_transition_symbol()) {
          os << "to strict function";
        } else {
#ifdef OBJECT_PRINT
          key->NamePrint(os);
#else
          ShortPrint(key, os);
#endif
          os << " ";
          DCHECK(!IsSpecialTransition(ReadOnlyRoots(isolate_), key));
          os << "to ";
          InternalIndex descriptor = target->LastAdded();
          Tagged<DescriptorArray> descriptors =
              target->instance_descriptors(isolate_);
          descriptors->PrintDescriptorDetails(os, descriptor,
                                              PropertyDetails::kForTransitions);
        }
        TransitionsAccessor transitions(isolate_, target);
        transitions.PrintTransitionTree(os, level + 1, no_gc);
      },
      [&](Tagged<Map> target) {
        os << std::endl
           << "  " << level << "/p" << proto_pos << ":"
           << std::setw(level * 2 + 2) << " ";
        proto_pos++;
        std::stringstream ss;
        ss << Brief(target);
        os << std::left << std::setw(50) << ss.str() << ": to proto ";
        ShortPrint(target->prototype(), os);
        TransitionsAccessor transitions(isolate_, target);
        transitions.PrintTransitionTree(os, level + 1, no_gc);
      },
      [&](SideStepTransition::Kind kind, Tagged<Object> side_step) {
        os << std::endl
           << "  " << level << "/s:" << std::setw(level * 2 + 2) << " ";
        std::stringstream ss;
        ss << Brief(side_step);
        os << std::left << std::setw(50) << ss.str() << ": sidestep " << kind;
        if (!side_step.IsSmi()) {
          TransitionsAccessor transitions(isolate_, Cast<Map>(side_step));
          transitions.PrintTransitionTree(os, level + 1, no_gc);
        }
      });
}

void JSObject::PrintTransitions(std::ostream& os) {
  TransitionsAccessor ta(GetIsolate(), map());
  if (ta.NumberOfTransitions() != 0 || ta.HasPrototypeTransitions()) {
    os << "\n - transitions";
    ta.PrintTransitions(os);
  }
}

#endif  // defined(DEBUG) || defined(OBJECT_PRINT)
}  // namespace v8::internal

namespace {

inline i::Tagged<i::Object> GetObjectFromRaw(void* object) {
  i::Address object_ptr = reinterpret_cast<i::Address>(object);
#ifdef V8_COMPRESS_POINTERS
  if (RoundDown<i::kPtrComprCageBaseAlignment>(object_ptr) == i::kNullAddress) {
    // Try to decompress pointer.
    i::Isolate* isolate = i::Isolate::TryGetCurrent();
    if (isolate != nullptr) {
      object_ptr = i::V8HeapCompressionScheme::DecompressTagged(
          isolate, static_cast<i::Tagged_t>(object_ptr));
    } else {
      i::PtrComprCageBase cage_base = i::GetPtrComprCageBase();
      object_ptr = i::V8HeapCompressionScheme::DecompressTagged(
          cage_base, static_cast<i::Tagged_t>(object_ptr));
    }
  }
#endif
  return i::Tagged<i::Object>(object_ptr);
}

}  // namespace

//
// The following functions are used by our gdb macros.
//
V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern i::Tagged<i::Object> _v8_internal_Get_Object(
    void* object) {
  return GetObjectFromRaw(object);
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Print_Object(void* object) {
  i::Print(GetObjectFromRaw(object));
}

// Used by lldb_visualizers.py to create a representation of a V8 object.
V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern std::string _v8_internal_Print_Object_To_String(
    void* object) {
  std::stringstream strm;
  i::Print(GetObjectFromRaw(object), strm);
  return strm.str();
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Print_LoadHandler(void* object) {
#ifdef OBJECT_PRINT
  i::StdoutStream os;
  i::LoadHandler::PrintHandler(GetObjectFromRaw(object), os);
  os << std::endl << std::flush;
#endif
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Print_StoreHandler(void* object) {
#ifdef OBJECT_PRINT
  i::StdoutStream os;
  i::StoreHandler::PrintHandler(GetObjectFromRaw(object), os);
  os << std::flush;
#endif
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Print_Code(void* object) {
  i::Address address = reinterpret_cast<i::Address>(object);
  i::Isolate* isolate = i::Isolate::Current();

#if V8_ENABLE_WEBASSEMBLY
  {
    if (auto* wasm_code =
            i::wasm::GetWasmCodeManager()->LookupCode(isolate, address)) {
      i::StdoutStream os;
      wasm_code->Disassemble(nullptr, os, address);
      return;
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  std::optional<i::Tagged<i::Code>> lookup_result =
      isolate->heap()->TryFindCodeForInnerPointerForPrinting(address);
  if (!lookup_result.has_value()) {
    i::PrintF(
        "%p is not within the current isolate's code or embedded spaces\n",
        object);
    return;
  }

#if defined(OBJECT_PRINT)
  i::StdoutStream os;
  lookup_result.value()->CodePrint(os, nullptr, address);
#elif defined(ENABLE_DISASSEMBLER)
  i::StdoutStream os;
  lookup_result.value()->Disassemble(nullptr, os, isolate, address);
#else
  i::Print(lookup_result.value());
#endif
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Print_OnlyCode(void* object,
                                                          size_t range_limit) {
  i::Address address = reinterpret_cast<i::Address>(object);
  i::Isolate* isolate = i::Isolate::Current();

#if V8_ENABLE_WEBASSEMBLY
  {
    if (i::wasm::GetWasmCodeManager()->LookupCode(isolate, address)) {
      i::PrintF("Not supported on wasm code");
      return;
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  std::optional<i::Tagged<i::Code>> lookup_result =
      isolate->heap()->TryFindCodeForInnerPointerForPrinting(address);
  if (!lookup_result.has_value()) {
    i::PrintF(
        "%p is not within the current isolate's code or embedded spaces\n",
        object);
    return;
  }

#if defined(ENABLE_DISASSEMBLER)
  i::StdoutStream os;
  lookup_result.value()->DisassembleOnlyCode(nullptr, os, isolate, address,
                                             range_limit);
#endif
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Print_StackTrace() {
  i::Isolate* isolate = i::Isolate::Current();
  isolate->PrintStack(stdout);
}

namespace _v8_internal_debugonly {
// This class is easy to navigate in a GUI debugger and not intended for
// use elsewhere.
struct StackTraceDebugDetails {
  i::StackFrame::Type type;
  std::string summary;
  std::vector<i::Tagged<i::SharedFunctionInfo>> functions;
  std::vector<i::Tagged<i::Object>> expressions;
};
}  // namespace _v8_internal_debugonly

// Used by lldb_visualizers.py to create a representation of the V8 stack.
V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern std::vector<
    _v8_internal_debugonly::StackTraceDebugDetails>
_v8_internal_Expand_StackTrace(i::Isolate* isolate) {
  std::vector<_v8_internal_debugonly::StackTraceDebugDetails> stack;
  i::DisallowGarbageCollection no_gc;
  int i = 0;

  for (i::StackFrameIterator it(isolate); !it.done(); it.Advance()) {
    i::CommonFrame* frame = i::CommonFrame::cast(it.frame());
    _v8_internal_debugonly::StackTraceDebugDetails details;
    details.type = frame->type();

    if (frame->is_java_script()) {
      i::JavaScriptFrame::cast(frame)->GetFunctions(&details.functions);
      if (!frame->is_optimized()) {
        int exprcount = frame->ComputeExpressionsCount();
        for (int i = 0; i < exprcount; i++) {
          details.expressions.push_back(frame->GetExpression(i));
        }
      }
    }

    i::HandleScope scope(isolate);
    i::StringStream::ClearMentionedObjectCache(isolate);
    i::HeapStringAllocator allocator;
    i::StringStream accumulator(&allocator);
    frame->Print(&accumulator, i::StackFrame::OVERVIEW, i++);
    std::unique_ptr<char[]> overview = accumulator.ToCString();
    details.summary = overview.get();
    stack.push_back(std::move(details));
  }
  return stack;
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Print_TransitionTree(
    void* object, bool start_at_root = false) {
  i::Tagged<i::Object> o(GetObjectFromRaw(object));
  if (!IsMap(o)) {
    printf("Please provide a valid Map\n");
  } else {
#if defined(DEBUG) || defined(OBJECT_PRINT)
    i::Tagged<i::Map> map = i::UncheckedCast<i::Map>(o);
    i::TransitionsAccessor transitions(
        i::Isolate::Current(),
        start_at_root ? map->FindRootMap(GetPtrComprCageBase(map)) : map);
    transitions.PrintTransitionTree();
#endif
  }
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern void _v8_internal_Print_Object_MarkBit(void* object) {
#ifdef OBJECT_PRINT
  const auto mark_bit =
      v8::internal::MarkBit::From(reinterpret_cast<i::Address>(object));
  i::StdoutStream os;
  os << "Object " << object << " is "
     << (mark_bit.Get() ? "marked" : "unmarked") << std::endl;
  os << "  mark-bit cell: " << mark_bit.CellAddress()
     << ", mask: " << mark_bit.Mask() << std::endl;
#endif
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE void _v8_internal_Print_FunctionCallbackInfo(
    void* function_callback_info) {
#ifdef OBJECT_PRINT
  i::PrintFunctionCallbackInfo(function_callback_info);
#endif
}

V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE void _v8_internal_Print_PropertyCallbackInfo(
    void* property_callback_info) {
#ifdef OBJECT_PRINT
  i::PrintPropertyCallbackInfo(property_callback_info);
#endif
}
