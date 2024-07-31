// Copyright 2014 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/ast/ast-value-factory.h"

#include "src/base/hashmap-entry.h"
#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/heap/factory-inl.h"
#include "src/heap/local-factory-inl.h"
#include "src/objects/string.h"
#include "src/strings/string-hasher.h"
#include "src/utils/utils-inl.h"

namespace v8 {
namespace internal {

namespace {

// For using StringToIndex.
class OneByteStringStream {
 public:
  explicit OneByteStringStream(base::Vector<const uint8_t> lb)
      : literal_bytes_(lb), pos_(0) {}

  bool HasMore() { return pos_ < literal_bytes_.length(); }
  uint16_t GetNext() { return literal_bytes_[pos_++]; }

 private:
  base::Vector<const uint8_t> literal_bytes_;
  int pos_;
};

}  // namespace

template <typename IsolateT>
void AstRawString::Internalize(IsolateT* isolate) {
  DCHECK(!has_string_);
  if (literal_bytes_.empty()) {
    set_string(isolate->factory()->empty_string());
  } else if (is_one_byte()) {
    OneByteStringKey key(raw_hash_field_, literal_bytes_);
    set_string(isolate->factory()->InternalizeStringWithKey(&key));
  } else {
    TwoByteStringKey key(raw_hash_field_,
                         base::Vector<const uint16_t>::cast(literal_bytes_));
    set_string(isolate->factory()->InternalizeStringWithKey(&key));
  }
}

template EXPORT_TEMPLATE_DEFINE(
    V8_EXPORT_PRIVATE) void AstRawString::Internalize(Isolate* isolate);
template EXPORT_TEMPLATE_DEFINE(
    V8_EXPORT_PRIVATE) void AstRawString::Internalize(LocalIsolate* isolate);

bool AstRawString::AsArrayIndex(uint32_t* index) const {
  // The StringHasher will set up the hash. Bail out early if we know it
  // can't be convertible to an array index.
  if (!IsIntegerIndex()) return false;
  if (length() <= Name::kMaxCachedArrayIndexLength) {
    *index = Name::ArrayIndexValueBits::decode(raw_hash_field_);
    return true;
  }
  // Might be an index, but too big to cache it. Do the slow conversion. This
  // might fail if the string is outside uint32_t (but within "safe integer")
  // range.
  OneByteStringStream stream(literal_bytes_);
  return StringToIndex(&stream, index);
}

bool AstRawString::IsIntegerIndex() const {
  return Name::IsIntegerIndex(raw_hash_field_);
}

bool AstRawString::IsOneByteEqualTo(const char* data) const {
  if (!is_one_byte()) return false;

  size_t length = static_cast<size_t>(literal_bytes_.length());
  if (length != strlen(data)) return false;

  return 0 == strncmp(reinterpret_cast<const char*>(literal_bytes_.begin()),
                      data, length);
}

uint16_t AstRawString::FirstCharacter() const {
  if (is_one_byte()) return literal_bytes_[0];
  const uint16_t* c = reinterpret_cast<const uint16_t*>(literal_bytes_.begin());
  return *c;
}

bool AstRawString::Equal(const AstRawString* lhs, const AstRawString* rhs) {
  DCHECK_EQ(lhs->Hash(), rhs->Hash());

  if (lhs->length() != rhs->length()) return false;
  if (lhs->length() == 0) return true;
  const unsigned char* l = lhs->raw_data();
  const unsigned char* r = rhs->raw_data();
  size_t length = rhs->length();
  if (lhs->is_one_byte()) {
    if (rhs->is_one_byte()) {
      return CompareCharsEqualUnsigned(reinterpret_cast<const uint8_t*>(l),
                                       reinterpret_cast<const uint8_t*>(r),
                                       length);
    } else {
      return CompareCharsEqualUnsigned(reinterpret_cast<const uint8_t*>(l),
                                       reinterpret_cast<const uint16_t*>(r),
                                       length);
    }
  } else {
    if (rhs->is_one_byte()) {
      return CompareCharsEqualUnsigned(reinterpret_cast<const uint16_t*>(l),
                                       reinterpret_cast<const uint8_t*>(r),
                                       length);
    } else {
      return CompareCharsEqualUnsigned(reinterpret_cast<const uint16_t*>(l),
                                       reinterpret_cast<const uint16_t*>(r),
                                       length);
    }
  }
}

int AstRawString::Compare(const AstRawString* lhs, const AstRawString* rhs) {
  // Fast path for equal pointers.
  if (lhs == rhs) return 0;

  const unsigned char* lhs_data = lhs->raw_data();
  const unsigned char* rhs_data = rhs->raw_data();
  size_t length = std::min(lhs->length(), rhs->length());

  // Code point order by contents.
  if (lhs->is_one_byte()) {
    if (rhs->is_one_byte()) {
      if (int result = CompareCharsUnsigned(
              reinterpret_cast<const uint8_t*>(lhs_data),
              reinterpret_cast<const uint8_t*>(rhs_data), length))
        return result;
    } else {
      if (int result = CompareCharsUnsigned(
              reinterpret_cast<const uint8_t*>(lhs_data),
              reinterpret_cast<const uint16_t*>(rhs_data), length))
        return result;
    }
  } else {
    if (rhs->is_one_byte()) {
      if (int result = CompareCharsUnsigned(
              reinterpret_cast<const uint16_t*>(lhs_data),
              reinterpret_cast<const uint8_t*>(rhs_data), length))
        return result;
    } else {
      if (int result = CompareCharsUnsigned(
              reinterpret_cast<const uint16_t*>(lhs_data),
              reinterpret_cast<const uint16_t*>(rhs_data), length))
        return result;
    }
  }

  return lhs->byte_length() - rhs->byte_length();
}

#ifdef OBJECT_PRINT
void AstRawString::Print() const { printf("%.*s", byte_length(), raw_data()); }
#endif  // OBJECT_PRINT

template <typename IsolateT>
Handle<String> AstConsString::Allocate(IsolateT* isolate) const {
  DCHECK(string_.is_null());

  if (IsEmpty()) {
    return isolate->factory()->empty_string();
  }
  // AstRawStrings are internalized before AstConsStrings are allocated, so
  // AstRawString::string() will just work.
  Handle<String> tmp = segment_.string->string();
  for (AstConsString::Segment* current = segment_.next; current != nullptr;
       current = current->next) {
    tmp = isolate->factory()
              ->NewConsString(current->string->string(), tmp,
                              AllocationType::kOld)
              .ToHandleChecked();
  }
  return tmp;
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<String> AstConsString::Allocate<Isolate>(Isolate* isolate) const;
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<String> AstConsString::Allocate<LocalIsolate>(
        LocalIsolate* isolate) const;

template <typename IsolateT>
Handle<String> AstConsString::AllocateFlat(IsolateT* isolate) const {
  if (IsEmpty()) {
    return isolate->factory()->empty_string();
  }
  if (!segment_.next) {
    return segment_.string->string();
  }

  int result_length = 0;
  bool is_one_byte = true;
  for (const AstConsString::Segment* current = &segment_; current != nullptr;
       current = current->next) {
    result_length += current->string->length();
    is_one_byte = is_one_byte && current->string->is_one_byte();
  }

  if (is_one_byte) {
    Handle<SeqOneByteString> result =
        isolate->factory()
            ->NewRawOneByteString(result_length, AllocationType::kOld)
            .ToHandleChecked();
    DisallowGarbageCollection no_gc;
    uint8_t* dest =
        result->GetChars(no_gc, SharedStringAccessGuardIfNeeded::NotNeeded()) +
        result_length;
    for (const AstConsString::Segment* current = &segment_; current != nullptr;
         current = current->next) {
      int length = current->string->length();
      dest -= length;
      CopyChars(dest, current->string->raw_data(), length);
    }
    DCHECK_EQ(dest, result->GetChars(
                        no_gc, SharedStringAccessGuardIfNeeded::NotNeeded()));
    return result;
  }

  Handle<SeqTwoByteString> result =
      isolate->factory()
          ->NewRawTwoByteString(result_length, AllocationType::kOld)
          .ToHandleChecked();
  DisallowGarbageCollection no_gc;
  uint16_t* dest =
      result->GetChars(no_gc, SharedStringAccessGuardIfNeeded::NotNeeded()) +
      result_length;
  for (const AstConsString::Segment* current = &segment_; current != nullptr;
       current = current->next) {
    int length = current->string->length();
    dest -= length;
    if (current->string->is_one_byte()) {
      CopyChars(dest, current->string->raw_data(), length);
    } else {
      CopyChars(dest,
                reinterpret_cast<const uint16_t*>(current->string->raw_data()),
                length);
    }
  }
  DCHECK_EQ(dest, result->GetChars(
                      no_gc, SharedStringAccessGuardIfNeeded::NotNeeded()));
  return result;
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<String> AstConsString::AllocateFlat<Isolate>(Isolate* isolate) const;
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<String> AstConsString::AllocateFlat<LocalIsolate>(
        LocalIsolate* isolate) const;

std::forward_list<const AstRawString*> AstConsString::ToRawStrings() const {
  std::forward_list<const AstRawString*> result;
  if (IsEmpty()) {
    return result;
  }

  result.emplace_front(segment_.string);
  for (AstConsString::Segment* current = segment_.next; current != nullptr;
       current = current->next) {
    result.emplace_front(current->string);
  }
  return result;
}

AstStringConstants::AstStringConstants(Isolate* isolate, uint64_t hash_seed)
    : zone_(isolate->allocator(), ZONE_NAME),
      string_table_(),
      hash_seed_(hash_seed) {
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
#define F(name, str)                                                         \
  {                                                                          \
    const char* data = str;                                                  \
    base::Vector<const uint8_t> literal(                                     \
        reinterpret_cast<const uint8_t*>(data),                              \
        static_cast<int>(strlen(data)));                                     \
    uint32_t raw_hash_field = StringHasher::HashSequentialString<uint8_t>(   \
        literal.begin(), literal.length(), hash_seed_);                      \
    name##_string_ = zone_.New<AstRawString>(true, literal, raw_hash_field); \
    /* The Handle returned by the factory is located on the roots */         \
    /* array, not on the temporary HandleScope, so this is safe.  */         \
    name##_string_->set_string(isolate->factory()->name##_string());         \
    string_table_.InsertNew(name##_string_, name##_string_->Hash());         \
  }
  AST_STRING_CONSTANTS(F)
#undef F
}

const AstRawString* AstValueFactory::GetOneByteStringInternal(
    base::Vector<const uint8_t> literal) {
  if (literal.length() == 1 && literal[0] < kMaxOneCharStringValue) {
    int key = literal[0];
    if (V8_UNLIKELY(one_character_strings_[key] == nullptr)) {
      uint32_t raw_hash_field = StringHasher::HashSequentialString<uint8_t>(
          literal.begin(), literal.length(), hash_seed_);
      one_character_strings_[key] = GetString(raw_hash_field, true, literal);
    }
    return one_character_strings_[key];
  }
  uint32_t raw_hash_field = StringHasher::HashSequentialString<uint8_t>(
      literal.begin(), literal.length(), hash_seed_);
  return GetString(raw_hash_field, true, literal);
}

const AstRawString* AstValueFactory::GetTwoByteStringInternal(
    base::Vector<const uint16_t> literal) {
  uint32_t raw_hash_field = StringHasher::HashSequentialString<uint16_t>(
      literal.begin(), literal.length(), hash_seed_);
  return GetString(raw_hash_field, false,
                   base::Vector<const uint8_t>::cast(literal));
}

const AstRawString* AstValueFactory::GetString(
    Tagged<String> literal,
    const SharedStringAccessGuardIfNeeded& access_guard) {
  const AstRawString* result = nullptr;
  DisallowGarbageCollection no_gc;
  String::FlatContent content = literal->GetFlatContent(no_gc, access_guard);
  if (content.IsOneByte()) {
    result = GetOneByteStringInternal(content.ToOneByteVector());
  } else {
    DCHECK(content.IsTwoByte());
    result = GetTwoByteStringInternal(content.ToUC16Vector());
  }
  return result;
}

AstConsString* AstValueFactory::NewConsString() {
  return single_parse_zone()->New<AstConsString>();
}

AstConsString* AstValueFactory::NewConsString(const AstRawString* str) {
  return NewConsString()->AddString(single_parse_zone(), str);
}

AstConsString* AstValueFactory::NewConsString(const AstRawString* str1,
                                              const AstRawString* str2) {
  return NewConsString()
      ->AddString(single_parse_zone(), str1)
      ->AddString(single_parse_zone(), str2);
}

template <typename IsolateT>
void AstValueFactory::Internalize(IsolateT* isolate) {
  // Strings need to be internalized before values, because values refer to
  // strings.
  for (AstRawString* current = strings_; current != nullptr;) {
    AstRawString* next = current->next();
    current->Internalize(isolate);
    current = next;
  }

  ResetStrings();
}
template EXPORT_TEMPLATE_DEFINE(
    V8_EXPORT_PRIVATE) void AstValueFactory::Internalize(Isolate* isolate);
template EXPORT_TEMPLATE_DEFINE(
    V8_EXPORT_PRIVATE) void AstValueFactory::Internalize(LocalIsolate* isolate);

const AstRawString* AstValueFactory::GetString(
    uint32_t raw_hash_field, bool is_one_byte,
    base::Vector<const uint8_t> literal_bytes) {
  // literal_bytes here points to whatever the user passed, and this is OK
  // because we use vector_compare (which checks the contents) to compare
  // against the AstRawStrings which are in the string_table_. We should not
  // return this AstRawString.
  AstRawString key(is_one_byte, literal_bytes, raw_hash_field);
  AstRawStringMap::Entry* entry = string_table_.LookupOrInsert(
      &key, key.Hash(),
      [&]() {
        // Copy literal contents for later comparison.
        int length = literal_bytes.length();
        uint8_t* new_literal_bytes =
            ast_raw_string_zone()->AllocateArray<uint8_t>(length);
        memcpy(new_literal_bytes, literal_bytes.begin(), length);
        AstRawString* new_string = ast_raw_string_zone()->New<AstRawString>(
            is_one_byte, base::Vector<const uint8_t>(new_literal_bytes, length),
            raw_hash_field);
        CHECK_NOT_NULL(new_string);
        AddString(new_string);
        return new_string;
      },
      [&]() { return base::NoHashMapValue(); });
  return entry->key;
}

}  // namespace internal
}  // namespace v8
