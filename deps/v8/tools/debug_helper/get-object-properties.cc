// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <sstream>

#include "debug-helper-internal.h"
#include "heap-constants.h"
#include "include/v8-internal.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames.h"
#include "src/execution/isolate-utils.h"
#include "src/objects/string-inl.h"
#include "src/sandbox/external-pointer.h"
#include "src/strings/unicode-inl.h"
#include "torque-generated/class-debug-readers.h"
#include "torque-generated/debug-macros.h"

namespace i = v8::internal;

namespace v8::internal::debug_helper_internal {

constexpr char kTaggedValue[] = "v8::internal::TaggedValue";
constexpr char kSmi[] = "v8::internal::Smi";
constexpr char kHeapObject[] = "v8::internal::HeapObject";
constexpr char kObjectAsStoredInHeap[] =
    "v8::internal::TaggedMember<v8::internal::Object>";

std::string AppendAddressAndType(const std::string& brief, uintptr_t address,
                                 const char* type) {
  std::stringstream brief_stream;
  brief_stream << "0x" << std::hex << address << " <" << type << ">";
  return brief.empty() ? brief_stream.str()
                       : brief + " (" + brief_stream.str() + ")";
}

std::string JoinWithSpace(const std::string& a, const std::string& b) {
  return a.empty() || b.empty() ? a + b : a + " " + b;
}

struct TypedObject {
  TypedObject(d::TypeCheckResult type_check_result,
              std::unique_ptr<TqObject> object)
      : type_check_result(type_check_result), object(std::move(object)) {}

  // How we discovered the object's type, or why we failed to do so.
  d::TypeCheckResult type_check_result;

  // Pointer to some TqObject subclass, representing the most specific known
  // type for the object.
  std::unique_ptr<TqObject> object;

  // Collection of other guesses at more specific types than the one represented
  // by |object|.
  std::vector<TypedObject> possible_types;
};

TypedObject GetTypedObjectByHint(uintptr_t address,
                                 std::string type_hint_string) {
#define TYPE_NAME_CASE(ClassName, ...)                   \
  if (type_hint_string == "v8::internal::" #ClassName) { \
    return {d::TypeCheckResult::kUsedTypeHint,           \
            std::make_unique<Tq##ClassName>(address)};   \
  }

  TORQUE_INSTANCE_CHECKERS_SINGLE_FULLY_DEFINED(TYPE_NAME_CASE)
  TORQUE_INSTANCE_CHECKERS_RANGE_FULLY_DEFINED(TYPE_NAME_CASE)
  STRING_CLASS_TYPES(TYPE_NAME_CASE)

#undef TYPE_NAME_CASE

  return {d::TypeCheckResult::kUnknownTypeHint,
          std::make_unique<TqHeapObject>(address)};
}

TypedObject GetTypedObjectForString(uintptr_t address, i::InstanceType type,
                                    d::TypeCheckResult type_source) {
  class StringGetDispatcher : public i::AllStatic {
   public:
#define DEFINE_METHOD(ClassName)                                    \
  static inline TypedObject Handle##ClassName(                      \
      uintptr_t address, d::TypeCheckResult type_source) {          \
    return {type_source, std::make_unique<Tq##ClassName>(address)}; \
  }
    STRING_CLASS_TYPES(DEFINE_METHOD)
#undef DEFINE_METHOD
    static inline TypedObject HandleInvalidString(
        uintptr_t address, d::TypeCheckResult type_source) {
      return {d::TypeCheckResult::kUnknownInstanceType,
              std::make_unique<TqString>(address)};
    }
  };

  return i::StringShape(type)
      .DispatchToSpecificTypeWithoutCast<StringGetDispatcher, TypedObject>(
          address, type_source);
}

TypedObject GetTypedObjectByInstanceType(uintptr_t address,
                                         i::InstanceType type,
                                         d::TypeCheckResult type_source) {
  switch (type) {
#define INSTANCE_TYPE_CASE(ClassName, INSTANCE_TYPE) \
  case i::INSTANCE_TYPE:                             \
    return {type_source, std::make_unique<Tq##ClassName>(address)};
    TORQUE_INSTANCE_CHECKERS_SINGLE_FULLY_DEFINED(INSTANCE_TYPE_CASE)
    TORQUE_INSTANCE_CHECKERS_MULTIPLE_FULLY_DEFINED(INSTANCE_TYPE_CASE)
#undef INSTANCE_TYPE_CASE

    default:

      // Special case: concrete subtypes of String are not included in the
      // main instance type list because they use the low bits of the instance
      // type enum as flags.
      if (type <= i::LAST_STRING_TYPE) {
        return GetTypedObjectForString(address, type, type_source);
      }

#define INSTANCE_RANGE_CASE(ClassName, FIRST_TYPE, LAST_TYPE)       \
  if (type >= i::FIRST_TYPE && type <= i::LAST_TYPE) {              \
    return {type_source, std::make_unique<Tq##ClassName>(address)}; \
  }
      TORQUE_INSTANCE_CHECKERS_RANGE_FULLY_DEFINED(INSTANCE_RANGE_CASE)
#undef INSTANCE_RANGE_CASE

      return {d::TypeCheckResult::kUnknownInstanceType,
              std::make_unique<TqHeapObject>(address)};
  }
}

bool IsTypedHeapObjectInstanceTypeOf(uintptr_t address,
                                     d::MemoryAccessor accessor,
                                     i::InstanceType instance_type) {
  auto heap_object = std::make_unique<TqHeapObject>(address);
  Value<uintptr_t> map_ptr = heap_object->GetMapValue(accessor);

  if (map_ptr.validity == d::MemoryAccessResult::kOk) {
    Value<i::InstanceType> type =
        TqMap(map_ptr.value).GetInstanceTypeValue(accessor);
    if (type.validity == d::MemoryAccessResult::kOk) {
      return instance_type == type.value;
    }
  }

  return false;
}

TypedObject GetTypedHeapObject(uintptr_t address, d::MemoryAccessor accessor,
                               const char* type_hint,
                               const d::HeapAddresses& heap_addresses) {
  auto heap_object = std::make_unique<TqHeapObject>(address);
  Value<uintptr_t> map_ptr = heap_object->GetMapValue(accessor);

  if (map_ptr.validity != d::MemoryAccessResult::kOk) {
    // If we can't read the Map pointer from the object, then we likely can't
    // read anything else, so there's not any point in attempting to use the
    // type hint. Just return a failure.
    return {map_ptr.validity == d::MemoryAccessResult::kAddressNotValid
                ? d::TypeCheckResult::kObjectPointerInvalid
                : d::TypeCheckResult::kObjectPointerValidButInaccessible,
            std::move(heap_object)};
  }

  Value<i::InstanceType> type =
      TqMap(map_ptr.value).GetInstanceTypeValue(accessor);
  if (type.validity == d::MemoryAccessResult::kOk) {
    return GetTypedObjectByInstanceType(address, type.value,
                                        d::TypeCheckResult::kUsedMap);
  }

  // We can't read the Map, so check whether it is in the list of known Maps,
  // as another way to get its instance type.
  KnownInstanceType known_map_type =
      FindKnownMapInstanceTypes(map_ptr.value, heap_addresses);
  if (known_map_type.confidence == KnownInstanceType::Confidence::kHigh) {
    DCHECK_EQ(known_map_type.types.size(), 1);
    return GetTypedObjectByInstanceType(address, known_map_type.types[0],
                                        d::TypeCheckResult::kKnownMapPointer);
  }

  // Create a basic result that says that the object is a HeapObject and we
  // couldn't read its Map.
  TypedObject result = {
      type.validity == d::MemoryAccessResult::kAddressNotValid
          ? d::TypeCheckResult::kMapPointerInvalid
          : d::TypeCheckResult::kMapPointerValidButInaccessible,
      std::move(heap_object)};

  // If a type hint is available, it may give us something more specific than
  // HeapObject. However, a type hint of Object would be even less specific, so
  // we'll only use the type hint if it's a subclass of HeapObject.
  if (type_hint != nullptr) {
    TypedObject hint_result = GetTypedObjectByHint(address, type_hint);
    if (result.object->IsSuperclassOf(hint_result.object.get())) {
      result = std::move(hint_result);
    }
  }

  // If low-confidence results are available from known Maps, include them only
  // if they don't contradict the primary type and would provide some additional
  // specificity.
  for (const i::InstanceType type_guess : known_map_type.types) {
    TypedObject guess_result = GetTypedObjectByInstanceType(
        address, type_guess, d::TypeCheckResult::kKnownMapPointer);
    if (result.object->IsSuperclassOf(guess_result.object.get())) {
      result.possible_types.push_back(std::move(guess_result));
    }
  }

  return result;
}

// An object visitor that accumulates the first few characters of a string.
class ReadStringVisitor : public TqObjectVisitor {
 public:
  struct Result {
    std::optional<std::string> maybe_truncated_string;
    std::unique_ptr<ObjectProperty> maybe_raw_characters_property;
  };
  static Result Visit(d::MemoryAccessor accessor,
                      const d::HeapAddresses& heap_addresses,
                      const TqString* object) {
    ReadStringVisitor visitor(accessor, heap_addresses);
    object->Visit(&visitor);
    return {visitor.GetString(), visitor.GetRawCharactersProperty()};
  }

  // Returns the result as UTF-8 once visiting is complete.
  std::optional<std::string> GetString() {
    if (failed_) return {};
    std::vector<char> result(
        string_.size() * unibrow::Utf16::kMaxExtraUtf8BytesForOneUtf16CodeUnit);
    unsigned write_index = 0;
    int prev_char = unibrow::Utf16::kNoPreviousCharacter;
    for (size_t read_index = 0; read_index < string_.size(); ++read_index) {
      uint16_t character = string_[read_index];
      write_index +=
          unibrow::Utf8::Encode(result.data() + write_index, character,
                                prev_char, /*replace_invalid=*/true);
      prev_char = character;
    }
    return std::string(result.data(), write_index);
  }

  // Returns a property referring to the address of the flattened character
  // array, if possible, once visiting is complete.
  std::unique_ptr<ObjectProperty> GetRawCharactersProperty() {
    if (failed_ || raw_characters_address_ == 0) return {};
    DCHECK(size_per_character_ == 1 || size_per_character_ == 2);
    const char* type = size_per_character_ == 1 ? "char" : "char16_t";
    return std::make_unique<ObjectProperty>(
        "raw_characters", type, raw_characters_address_, num_characters_,
        size_per_character_, std::vector<std::unique_ptr<StructProperty>>(),
        d::PropertyKind::kArrayOfKnownSize);
  }

  template <typename T>
  Value<T> ReadValue(uintptr_t data_address, int32_t index = 0) {
    T value{};
    d::MemoryAccessResult validity =
        accessor_(data_address + index * sizeof(T),
                  reinterpret_cast<uint8_t*>(&value), sizeof(value));
    return {validity, value};
  }

  template <typename TChar>
  void ReadStringCharacters(const TqString* object, uintptr_t data_address) {
    int32_t length = GetOrFinish(object->GetLengthValue(accessor_));
    if (string_.size() == 0) {
      raw_characters_address_ = data_address + index_ * sizeof(TChar);
      size_per_character_ = sizeof(TChar);
      num_characters_ = std::min(length, limit_) - index_;
    }
    for (; index_ < length && index_ < limit_ && !done_; ++index_) {
      static_assert(sizeof(TChar) <= sizeof(char16_t));
      char16_t c = static_cast<char16_t>(
          GetOrFinish(ReadValue<TChar>(data_address, index_)));
      if (!done_) AddCharacter(c);
    }
  }

  template <typename TChar, typename TString>
  void ReadSeqString(const TString* object) {
    ReadStringCharacters<TChar>(object, object->GetCharsAddress());
  }

  void VisitSeqOneByteString(const TqSeqOneByteString* object) override {
    ReadSeqString<char>(object);
  }

  void VisitSeqTwoByteString(const TqSeqTwoByteString* object) override {
    ReadSeqString<char16_t>(object);
  }

  void VisitConsString(const TqConsString* object) override {
    uintptr_t first_address = GetOrFinish(object->GetFirstValue(accessor_));
    if (done_) return;
    auto first =
        GetTypedHeapObject(first_address, accessor_, nullptr, heap_addresses_)
            .object;
    first->Visit(this);
    // Cons strings don't have all of their characters in a contiguous memory
    // region, so it would be confusing to show the user a raw pointer to the
    // character storage for only part of the cons string.
    raw_characters_address_ = 0;
    if (done_) return;
    int32_t first_length = GetOrFinish(
        static_cast<TqString*>(first.get())->GetLengthValue(accessor_));
    uintptr_t second = GetOrFinish(object->GetSecondValue(accessor_));
    if (done_) return;
    IndexModifier modifier(this, -first_length, -first_length);
    GetTypedHeapObject(second, accessor_, nullptr, heap_addresses_)
        .object->Visit(this);
  }

  void VisitSlicedString(const TqSlicedString* object) override {
    uintptr_t parent = GetOrFinish(object->GetParentValue(accessor_));
    int32_t length = GetOrFinish(object->GetLengthValue(accessor_));
    int32_t offset = i::PlatformSmiTagging::SmiToInt(
        GetOrFinish(object->GetOffsetValue(accessor_)));
    if (done_) return;
    int32_t limit_adjust = offset + length - limit_;
    IndexModifier modifier(this, offset, limit_adjust < 0 ? limit_adjust : 0);
    GetTypedHeapObject(parent, accessor_, nullptr, heap_addresses_)
        .object->Visit(this);
  }

  void VisitThinString(const TqThinString* object) override {
    uintptr_t actual = GetOrFinish(object->GetActualValue(accessor_));
    if (done_) return;
    GetTypedHeapObject(actual, accessor_, nullptr, heap_addresses_)
        .object->Visit(this);
  }

  bool IsExternalStringCached(const TqExternalString* object) {
    // The safest way to get the instance type is to use known map pointers, in
    // case the map data is not available.
    Value<uintptr_t> map_ptr = object->GetMapValue(accessor_);
    DCHECK_IMPLIES(map_ptr.validity == d::MemoryAccessResult::kOk,
                   !v8::internal::MapWord::IsPacked(map_ptr.value));
    uintptr_t map = GetOrFinish(map_ptr);
    if (done_) return false;
    auto instance_types = FindKnownMapInstanceTypes(map, heap_addresses_);
    // Exactly one of the matched instance types should be a string type,
    // because all maps for string types are in the same space (read-only
    // space). The "uncached" flag on that instance type tells us whether it's
    // safe to read the cached data.
    for (const auto& type : instance_types.types) {
      if ((type & i::kIsNotStringMask) == i::kStringTag &&
          (type & i::kStringRepresentationMask) == i::kExternalStringTag) {
        return (type & i::kUncachedExternalStringMask) !=
               i::kUncachedExternalStringTag;
      }
    }

    // If for some reason we can't find an external string type here (maybe the
    // caller provided an external string type as the type hint, but it doesn't
    // actually match the in-memory map pointer), then we can't safely use the
    // cached data.
    return false;
  }

  template <typename TChar>
  void ReadExternalString(const TqExternalString* object) {
    // Uncached external strings require knowledge of the embedder. For now, we
    // only read cached external strings.
    if (IsExternalStringCached(object)) {
      ExternalPointer_t resource_data =
          GetOrFinish(object->GetResourceDataValue(accessor_));
#ifdef V8_ENABLE_SANDBOX
      Address memory_chunk =
          MemoryChunk::FromAddress(object->GetMapAddress())->address();
      uint32_t metadata_index = GetOrFinish(ReadValue<uint32_t>(
          memory_chunk + MemoryChunkLayout::kMetadataIndexOffset));
      Address metadata_address = GetOrFinish(ReadValue<Address>(
          heap_addresses_.metadata_pointer_table, metadata_index));
      Address heap = GetOrFinish(ReadValue<Address>(
          metadata_address + MemoryChunkLayout::kHeapOffset));
      Isolate* isolate = Isolate::FromHeap(reinterpret_cast<Heap*>(heap));
      Address external_pointer_table_address_address =
          isolate->shared_external_pointer_table_address_address();
      Address external_pointer_table_address = GetOrFinish(
          ReadValue<Address>(external_pointer_table_address_address));
      Address external_pointer_table =
          GetOrFinish(ReadValue<Address>(external_pointer_table_address));
      int32_t index =
          static_cast<int32_t>(resource_data >> kExternalPointerIndexShift);
      Address tagged_data =
          GetOrFinish(ReadValue<Address>(external_pointer_table, index));
      Address data_address = tagged_data & ~kExternalStringResourceDataTag;
#else
      uintptr_t data_address = static_cast<uintptr_t>(resource_data);
#endif  // V8_ENABLE_SANDBOX
      if (done_) return;
      ReadStringCharacters<TChar>(object, data_address);
    } else {
      // TODO(v8:9376): Come up with some way that a caller with full knowledge
      // of a particular embedder could provide a callback function for getting
      // uncached string data.
      AddEllipsisAndFinish();
    }
  }

  void VisitExternalOneByteString(
      const TqExternalOneByteString* object) override {
    ReadExternalString<char>(object);
  }

  void VisitExternalTwoByteString(
      const TqExternalTwoByteString* object) override {
    ReadExternalString<char16_t>(object);
  }

  void VisitObject(const TqObject* object) override {
    // If we fail to find a specific type for a sub-object within a cons string,
    // sliced string, or thin string, we will end up here.
    AddEllipsisAndFinish();
  }

 private:
  ReadStringVisitor(d::MemoryAccessor accessor,
                    const d::HeapAddresses& heap_addresses)
      : accessor_(accessor),
        heap_addresses_(heap_addresses),
        index_(0),
        limit_(INT32_MAX),
        done_(false),
        failed_(false) {}

  // Unpacks a value that was fetched from the debuggee. If the value indicates
  // that it couldn't successfully fetch memory, then prevents further work.
  template <typename T>
  T GetOrFinish(Value<T> value) {
    if (value.validity != d::MemoryAccessResult::kOk) {
      AddEllipsisAndFinish();
    }
    return value.value;
  }

  void AddEllipsisAndFinish() {
    if (!done_) {
      done_ = true;
      if (string_.empty()) {
        failed_ = true;
      } else {
        string_ += u"...";
      }
    }
  }

  void AddCharacter(char16_t c) {
    if (string_.size() >= kMaxCharacters) {
      AddEllipsisAndFinish();
    } else {
      string_.push_back(c);
    }
  }

  // Temporarily adds offsets to both index_ and limit_, to handle ConsString
  // and SlicedString.
  class IndexModifier {
   public:
    IndexModifier(ReadStringVisitor* that, int32_t index_adjust,
                  int32_t limit_adjust)
        : that_(that),
          index_adjust_(index_adjust),
          limit_adjust_(limit_adjust) {
      that_->index_ += index_adjust_;
      that_->limit_ += limit_adjust_;
    }
    IndexModifier(const IndexModifier&) = delete;
    IndexModifier& operator=(const IndexModifier&) = delete;
    ~IndexModifier() {
      that_->index_ -= index_adjust_;
      that_->limit_ -= limit_adjust_;
    }

   private:
    ReadStringVisitor* that_;
    int32_t index_adjust_;
    int32_t limit_adjust_;
  };

  static constexpr int kMaxCharacters = 80;  // How many characters to print.

  std::u16string string_;  // Result string.
  d::MemoryAccessor accessor_;
  const d::HeapAddresses& heap_addresses_;
  int32_t index_;  // Index of next char to read.
  int32_t limit_;  // Don't read past this index (set by SlicedString).
  bool done_;      // Whether to stop further work.
  bool failed_;    // Whether an error was encountered before any valid data.

  // If the string's characters are in a contiguous block of memory (including
  // sequential strings, external strings where we could determine the raw data
  // location, and thin or sliced strings pointing to either of those), then
  // after this visitor has run, the character data's address, size per
  // character, and number of characters will be present in the following
  // fields.
  Address raw_characters_address_ = 0;
  int32_t size_per_character_ = 0;
  int32_t num_characters_ = 0;
};

// An object visitor that supplies extra information for some types.
class AddInfoVisitor : public TqObjectVisitor {
 public:
  // Returns a descriptive string and a list of properties for the given object.
  // Both may be empty, and are meant as an addition or a replacement for,
  // the Torque-generated data about the object.
  static std::pair<std::string, std::vector<std::unique_ptr<ObjectProperty>>>
  Visit(const TqObject* object, d::MemoryAccessor accessor,
        const d::HeapAddresses& heap_addresses) {
    AddInfoVisitor visitor(accessor, heap_addresses);
    object->Visit(&visitor);
    return {std::move(visitor.brief_), std::move(visitor.properties_)};
  }

  void VisitStringImpl(const TqString* object, bool is_sequential) {
    auto visit_result =
        ReadStringVisitor::Visit(accessor_, heap_addresses_, object);
    auto str = visit_result.maybe_truncated_string;
    if (str.has_value()) {
      brief_ = "\"" + *str + "\"";
    }
    // Sequential strings already have a "chars" property based on the Torque
    // type definition, so there's no need to duplicate it. Otherwise, it is
    // useful to display a pointer to the flattened character data if possible.
    if (!is_sequential && visit_result.maybe_raw_characters_property) {
      properties_.push_back(
          std::move(visit_result.maybe_raw_characters_property));
    }
  }

  void VisitString(const TqString* object) override {
    VisitStringImpl(object, /*is_sequential=*/false);
  }

  void VisitSeqString(const TqSeqString* object) override {
    VisitStringImpl(object, /*is_sequential=*/true);
  }

  void VisitJSObject(const TqJSObject* object) override {
    // JSObject and its subclasses can be followed directly by an array of
    // property values. The start and end offsets of those values are described
    // by a pair of values in its Map.
    auto map_ptr = object->GetMapValue(accessor_);
    if (map_ptr.validity != d::MemoryAccessResult::kOk) {
      return;  // Can't read the JSObject. Nothing useful to do.
    }
    DCHECK(!v8::internal::MapWord::IsPacked(map_ptr.value));
    TqMap map(map_ptr.value);

    // On JSObject instances, this value is the start of in-object properties.
    // The constructor function index option is only for primitives.
    auto start_offset =
        map.GetInobjectPropertiesStartOrConstructorFunctionIndexValue(
            accessor_);

    // The total size of the object in memory. This may include over-allocated
    // expansion space that doesn't correspond to any user-accessible property.
    auto instance_size = map.GetInstanceSizeInWordsValue(accessor_);

    if (start_offset.validity != d::MemoryAccessResult::kOk ||
        instance_size.validity != d::MemoryAccessResult::kOk) {
      return;  // Can't read the Map. Nothing useful to do.
    }
    int num_properties = instance_size.value - start_offset.value;
    if (num_properties > 0) {
      properties_.push_back(std::make_unique<ObjectProperty>(
          "in-object properties", kObjectAsStoredInHeap,
          object->GetMapAddress() + start_offset.value * i::kTaggedSize,
          num_properties, i::kTaggedSize,
          std::vector<std::unique_ptr<StructProperty>>(),
          d::PropertyKind::kArrayOfKnownSize));
    }
  }

 private:
  AddInfoVisitor(d::MemoryAccessor accessor,
                 const d::HeapAddresses& heap_addresses)
      : accessor_(accessor), heap_addresses_(heap_addresses) {}

  // Inputs used by this visitor:

  d::MemoryAccessor accessor_;
  const d::HeapAddresses& heap_addresses_;

  // Outputs generated by this visitor:

  // A brief description of the object.
  std::string brief_;
  // A list of extra properties to append after the automatic ones that are
  // created for all Torque-defined class fields.
  std::vector<std::unique_ptr<ObjectProperty>> properties_;
};

std::unique_ptr<ObjectPropertiesResult> GetHeapObjectPropertiesNotCompressed(
    uintptr_t address, d::MemoryAccessor accessor, const char* type_hint,
    const d::HeapAddresses& heap_addresses) {
  // Regardless of whether we can read the object itself, maybe we can find its
  // pointer in the list of known objects.
  std::string brief = FindKnownObject(address, heap_addresses);

  TypedObject typed =
      GetTypedHeapObject(address, accessor, type_hint, heap_addresses);
  auto props = typed.object->GetProperties(accessor);

  // Use the AddInfoVisitor to get any extra properties or descriptive text that
  // can't be directly derived from Torque class definitions.
  auto extra_info =
      AddInfoVisitor::Visit(typed.object.get(), accessor, heap_addresses);
  brief = JoinWithSpace(brief, extra_info.first);

  // Overwrite existing properties if they have the same name.
  for (size_t i = 0; i < extra_info.second.size(); i++) {
    bool overwrite = false;
    for (size_t j = 0; j < props.size(); j++) {
      if (strcmp(props[j]->GetPublicView()->name,
                 extra_info.second[i]->GetPublicView()->name) == 0) {
        props[j] = std::move(extra_info.second[i]);
        overwrite = true;
        break;
      }
    }
    if (overwrite) continue;
    props.push_back(std::move(extra_info.second[i]));
  }

  brief = AppendAddressAndType(brief, address, typed.object->GetName());

  // Convert the low-confidence guessed types to a list of strings as expected
  // for the response.
  std::vector<std::string> guessed_types;
  for (const auto& guess : typed.possible_types) {
    guessed_types.push_back(guess.object->GetName());
  }

  return std::make_unique<ObjectPropertiesResult>(
      typed.type_check_result, brief, typed.object->GetName(), std::move(props),
      std::move(guessed_types));
}

std::unique_ptr<ObjectPropertiesResult> GetHeapObjectPropertiesMaybeCompressed(
    uintptr_t address, d::MemoryAccessor memory_accessor,
    d::HeapAddresses heap_addresses, const char* type_hint) {
  // Try to figure out the heap range, for pointer compression (this is unused
  // if pointer compression is disabled).
  uintptr_t any_uncompressed_ptr = 0;
  if (!IsPointerCompressed(address)) any_uncompressed_ptr = address;
  if (any_uncompressed_ptr == 0)
    any_uncompressed_ptr = heap_addresses.any_heap_pointer;
  if (any_uncompressed_ptr == 0)
    any_uncompressed_ptr = heap_addresses.map_space_first_page;
  if (any_uncompressed_ptr == 0)
    any_uncompressed_ptr = heap_addresses.old_space_first_page;
  if (any_uncompressed_ptr == 0)
    any_uncompressed_ptr = heap_addresses.read_only_space_first_page;
#ifdef V8_COMPRESS_POINTERS
  Address base =
      V8HeapCompressionScheme::GetPtrComprCageBaseAddress(any_uncompressed_ptr);
  if (base != V8HeapCompressionScheme::base()) {
    V8HeapCompressionScheme::InitBase(base);
  }
#endif  // V8_COMPRESS_POINTERS
  FillInUnknownHeapAddresses(&heap_addresses, any_uncompressed_ptr);
  if (any_uncompressed_ptr == 0) {
    // We can't figure out the heap range. Just check for known objects.
    std::string brief = FindKnownObject(address, heap_addresses);
    brief = AppendAddressAndType(brief, address, kTaggedValue);
    return std::make_unique<ObjectPropertiesResult>(
        d::TypeCheckResult::kUnableToDecompress, brief, kTaggedValue);
  }

  address = EnsureDecompressed(address, any_uncompressed_ptr);

  return GetHeapObjectPropertiesNotCompressed(address, memory_accessor,
                                              type_hint, heap_addresses);
}

std::unique_ptr<ObjectPropertiesResult> GetObjectProperties(
    uintptr_t address, d::MemoryAccessor memory_accessor,
    const d::HeapAddresses& heap_addresses, const char* type_hint) {
  if (static_cast<uint32_t>(address) == i::kClearedWeakHeapObjectLower32) {
    return std::make_unique<ObjectPropertiesResult>(
        d::TypeCheckResult::kWeakRef, "cleared weak ref", kHeapObject);
  }
  bool is_weak = (address & i::kHeapObjectTagMask) == i::kWeakHeapObjectTag;
  if (is_weak) {
    address &= ~i::kWeakHeapObjectMask;
  }
  if (i::Internals::HasHeapObjectTag(address)) {
    std::unique_ptr<ObjectPropertiesResult> result =
        GetHeapObjectPropertiesMaybeCompressed(address, memory_accessor,
                                               heap_addresses, type_hint);
    if (is_weak) {
      result->Prepend("weak ref to ");
    }
    return result;
  }

  // For smi values, construct a response with a description representing the
  // untagged value.
  int32_t value = i::PlatformSmiTagging::SmiToInt(address);
  std::stringstream stream;
  stream << value << " (0x" << std::hex << value << ")";
  return std::make_unique<ObjectPropertiesResult>(d::TypeCheckResult::kSmi,
                                                  stream.str(), kSmi);
}

std::unique_ptr<StackFrameResult> GetStackFrame(
    uintptr_t frame_pointer, d::MemoryAccessor memory_accessor) {
  // Read the data at frame_pointer + kContextOrFrameTypeOffset.
  intptr_t context_or_frame_type = 0;
  d::MemoryAccessResult validity = memory_accessor(
      frame_pointer + CommonFrameConstants::kContextOrFrameTypeOffset,
      reinterpret_cast<void*>(&context_or_frame_type), sizeof(intptr_t));
  auto props = std::vector<std::unique_ptr<ObjectProperty>>();
  if (validity == d::MemoryAccessResult::kOk) {
    // If it is context, not frame marker then add new property
    // "currently_executing_function".
    if (!StackFrame::IsTypeMarker(context_or_frame_type)) {
      props.push_back(std::make_unique<ObjectProperty>(
          "currently_executing_jsfunction",
          CheckTypeName<v8::internal::Tagged<v8::internal::JSFunction>>(
              "v8::internal::Tagged<v8::internal::JSFunction>"),
          frame_pointer + StandardFrameConstants::kFunctionOffset, 1,
          sizeof(v8::internal::JSFunction),
          std::vector<std::unique_ptr<StructProperty>>(),
          d::PropertyKind::kSingle));
      // Add more items in the Locals pane representing the JS function name,
      // source file name, and line & column numbers within the source file, so
      // that the user doesn’t need to dig through the shared_function_info to
      // find them.
      intptr_t js_function_ptr = 0;
      validity = memory_accessor(
          frame_pointer + StandardFrameConstants::kFunctionOffset,
          reinterpret_cast<void*>(&js_function_ptr), sizeof(intptr_t));
      if (validity == d::MemoryAccessResult::kOk) {
        TqJSFunction js_function(js_function_ptr);
        auto shared_function_info_ptr =
            js_function.GetSharedFunctionInfoValue(memory_accessor);
        if (shared_function_info_ptr.validity == d::MemoryAccessResult::kOk) {
          TqSharedFunctionInfo shared_function_info(
              shared_function_info_ptr.value);
          auto script_ptr =
              shared_function_info.GetScriptValue(memory_accessor);
          if (script_ptr.validity == d::MemoryAccessResult::kOk) {
            // Make sure script_ptr is script.
            auto address = script_ptr.value;
            if (IsTypedHeapObjectInstanceTypeOf(address, memory_accessor,
                                                i::InstanceType::SCRIPT_TYPE)) {
              TqScript script(script_ptr.value);
              props.push_back(std::make_unique<ObjectProperty>(
                  "script_name", kObjectAsStoredInHeap, script.GetNameAddress(),
                  1, i::kTaggedSize,
                  std::vector<std::unique_ptr<StructProperty>>(),
                  d::PropertyKind::kSingle));
              props.push_back(std::make_unique<ObjectProperty>(
                  "script_source", kObjectAsStoredInHeap,
                  script.GetSourceAddress(), 1, i::kTaggedSize,
                  std::vector<std::unique_ptr<StructProperty>>(),
                  d::PropertyKind::kSingle));
            }
          }
          auto name_or_scope_info_ptr =
              shared_function_info.GetNameOrScopeInfoValue(memory_accessor);
          if (name_or_scope_info_ptr.validity == d::MemoryAccessResult::kOk) {
            auto scope_info_address = name_or_scope_info_ptr.value;
            // Make sure name_or_scope_info_ptr is scope info.
            if (IsTypedHeapObjectInstanceTypeOf(
                    scope_info_address, memory_accessor,
                    i::InstanceType::SCOPE_INFO_TYPE)) {
              auto indexed_field_slice_function_variable_info =
                  TqDebugFieldSliceScopeInfoFunctionVariableInfo(
                      memory_accessor, scope_info_address);
              if (indexed_field_slice_function_variable_info.validity ==
                  d::MemoryAccessResult::kOk) {
                props.push_back(std::make_unique<ObjectProperty>(
                    "function_name", kObjectAsStoredInHeap,
                    scope_info_address - i::kHeapObjectTag +
                        std::get<1>(
                            indexed_field_slice_function_variable_info.value),
                    std::get<2>(
                        indexed_field_slice_function_variable_info.value),
                    i::kTaggedSize,
                    std::vector<std::unique_ptr<StructProperty>>(),
                    d::PropertyKind::kSingle));
              }
              std::vector<std::unique_ptr<StructProperty>>
                  position_info_struct_field_list;
              position_info_struct_field_list.push_back(
                  std::make_unique<StructProperty>(
                      "start", kObjectAsStoredInHeap, 0, 0, 0));
              position_info_struct_field_list.push_back(
                  std::make_unique<StructProperty>("end", kObjectAsStoredInHeap,
                                                   4, 0, 0));
              TqScopeInfo scope_info(scope_info_address);
              props.push_back(std::make_unique<ObjectProperty>(
                  "function_character_offset", "",
                  scope_info.GetPositionInfoAddress(), 1, 2 * i::kTaggedSize,
                  std::move(position_info_struct_field_list),
                  d::PropertyKind::kSingle));
            }
          }
        }
      }
    }
  }

  return std::make_unique<StackFrameResult>(std::move(props));
}

}  // namespace v8::internal::debug_helper_internal

namespace di = v8::internal::debug_helper_internal;

extern "C" {
V8_DEBUG_HELPER_EXPORT d::ObjectPropertiesResult*
_v8_debug_helper_GetObjectProperties(uintptr_t object,
                                     d::MemoryAccessor memory_accessor,
                                     const d::HeapAddresses& heap_addresses,
                                     const char* type_hint) {
  return di::GetObjectProperties(object, memory_accessor, heap_addresses,
                                 type_hint)
      .release()
      ->GetPublicView();
}
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_Free_ObjectPropertiesResult(
    d::ObjectPropertiesResult* result) {
  std::unique_ptr<di::ObjectPropertiesResult> ptr(
      static_cast<di::ObjectPropertiesResultExtended*>(result)->base);
}

V8_DEBUG_HELPER_EXPORT d::StackFrameResult* _v8_debug_helper_GetStackFrame(
    uintptr_t frame_pointer, d::MemoryAccessor memory_accessor) {
  return di::GetStackFrame(frame_pointer, memory_accessor)
      .release()
      ->GetPublicView();
}
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_Free_StackFrameResult(
    d::StackFrameResult* result) {
  std::unique_ptr<di::StackFrameResult> ptr(
      static_cast<di::StackFrameResultExtended*>(result)->base);
}
}
