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

#ifndef V8_AST_AST_VALUE_FACTORY_H_
#define V8_AST_AST_VALUE_FACTORY_H_

#include <forward_list>

#include "src/base/hashmap.h"
#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/numbers/conversions.h"
#include "src/objects/name.h"
#include "src/zone/zone.h"

// Ast(Raw|Cons)String and AstValueFactory are for storing strings and
// values independent of the V8 heap and internalizing them later. During
// parsing, they are created and stored outside the heap, in AstValueFactory.
// After parsing, the strings and values are internalized (moved into the V8
// heap).
namespace v8 {
namespace internal {

class Isolate;

class AstRawString final : public ZoneObject {
 public:
  static bool Equal(const AstRawString* lhs, const AstRawString* rhs);

  // Returns 0 if lhs is equal to rhs.
  // Returns <0 if lhs is less than rhs in code point order.
  // Returns >0 if lhs is greater than than rhs in code point order.
  static int Compare(const AstRawString* lhs, const AstRawString* rhs);

  bool IsEmpty() const { return literal_bytes_.length() == 0; }
  int length() const {
    return is_one_byte() ? literal_bytes_.length()
                         : literal_bytes_.length() / 2;
  }
  bool AsArrayIndex(uint32_t* index) const;
  bool IsIntegerIndex() const;
  V8_EXPORT_PRIVATE bool IsOneByteEqualTo(const char* data) const;
  uint16_t FirstCharacter() const;

  template <typename IsolateT>
  void Internalize(IsolateT* isolate);

  // Access the physical representation:
  bool is_one_byte() const { return is_one_byte_; }
  int byte_length() const { return literal_bytes_.length(); }
  const unsigned char* raw_data() const { return literal_bytes_.begin(); }

  bool IsPrivateName() const { return length() > 0 && FirstCharacter() == '#'; }

  // For storing AstRawStrings in a hash map.
  uint32_t raw_hash_field() const { return raw_hash_field_; }
  uint32_t Hash() const {
    // Hash field must be computed.
    DCHECK_EQ(raw_hash_field_ & Name::kHashNotComputedMask, 0);
    return Name::HashBits::decode(raw_hash_field_);
  }

  // This function can be called after internalizing.
  V8_INLINE Handle<String> string() const {
    DCHECK(has_string_);
    return string_;
  }

#ifdef OBJECT_PRINT
  void Print() const;
#endif  // OBJECT_PRINT

 private:
  friend class AstRawStringInternalizationKey;
  friend class AstStringConstants;
  friend class AstValueFactory;
  friend Zone;

  // Members accessed only by the AstValueFactory & related classes:
  AstRawString(bool is_one_byte, base::Vector<const uint8_t> literal_bytes,
               uint32_t raw_hash_field)
      : next_(nullptr),
        literal_bytes_(literal_bytes),
        raw_hash_field_(raw_hash_field),
        is_one_byte_(is_one_byte) {}
  AstRawString* next() {
    DCHECK(!has_string_);
    return next_;
  }
  AstRawString** next_location() {
    DCHECK(!has_string_);
    return &next_;
  }

  void set_string(Handle<String> string) {
    DCHECK(!string.is_null());
    DCHECK(!has_string_);
    string_ = string;
#ifdef DEBUG
    has_string_ = true;
#endif
  }

  union {
    AstRawString* next_;
    Handle<String> string_;
  };

  base::Vector<const uint8_t> literal_bytes_;  // Memory owned by Zone.
  uint32_t raw_hash_field_;
  bool is_one_byte_;
#ifdef DEBUG
  // (Debug-only:) Verify the object life-cylce: Some functions may only be
  // called after internalization (that is, after a v8::internal::String has
  // been set); some only before.
  bool has_string_ = false;
#endif
};

extern template EXPORT_TEMPLATE_DECLARE(
    V8_EXPORT_PRIVATE) void AstRawString::Internalize(Isolate* isolate);
extern template EXPORT_TEMPLATE_DECLARE(
    V8_EXPORT_PRIVATE) void AstRawString::Internalize(LocalIsolate* isolate);

class AstConsString final : public ZoneObject {
 public:
  AstConsString* AddString(Zone* zone, const AstRawString* s) {
    if (s->IsEmpty()) return this;
    if (!IsEmpty()) {
      // We're putting the new string to the head of the list, meaning
      // the string segments will be in reverse order.
      Segment* tmp = zone->New<Segment>(segment_);
      segment_.next = tmp;
    }
    segment_.string = s;
    return this;
  }

  bool IsEmpty() const {
    DCHECK_IMPLIES(segment_.string == nullptr, segment_.next == nullptr);
    DCHECK_IMPLIES(segment_.string != nullptr, !segment_.string->IsEmpty());
    return segment_.string == nullptr;
  }

  template <typename IsolateT>
  Handle<String> GetString(IsolateT* isolate) {
    if (string_.is_null()) {
      string_ = Allocate(isolate);
    }
    return string_;
  }

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  Handle<String> AllocateFlat(IsolateT* isolate) const;

  std::forward_list<const AstRawString*> ToRawStrings() const;

 private:
  friend class AstValueFactory;
  friend Zone;

  AstConsString() : string_(), segment_({nullptr, nullptr}) {}

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  Handle<String> Allocate(IsolateT* isolate) const;

  Handle<String> string_;

  // A linked list of AstRawStrings of the contents of this AstConsString.
  // This list has several properties:
  //
  //   * For empty strings the string pointer is null,
  //   * Appended raw strings are added to the head of the list, so they are in
  //     reverse order
  struct Segment {
    const AstRawString* string;
    AstConsString::Segment* next;
  };
  Segment segment_;
};

class AstBigInt {
 public:
  // |bigint| must be a NUL-terminated string of ASCII characters
  // representing a BigInt (suitable for passing to BigIntLiteral()
  // from conversions.h).
  explicit AstBigInt(const char* bigint) : bigint_(bigint) {}

  const char* c_str() const { return bigint_; }

 private:
  const char* bigint_;
};

struct AstRawStringMapMatcher {
  bool operator()(uint32_t hash1, uint32_t hash2,
                  const AstRawString* lookup_key,
                  const AstRawString* entry_key) const {
    return hash1 == hash2 && AstRawString::Equal(lookup_key, entry_key);
  }
};

using AstRawStringMap =
    base::TemplateHashMapImpl<const AstRawString*, base::NoHashMapValue,
                              AstRawStringMapMatcher,
                              base::DefaultAllocationPolicy>;

// For generating constants.
#define AST_STRING_CONSTANTS(F)                    \
  F(anonymous, "anonymous")                        \
  F(arguments, "arguments")                        \
  F(as, "as")                                      \
  F(assert, "assert")                              \
  F(async, "async")                                \
  F(bigint, "bigint")                              \
  F(boolean, "boolean")                            \
  F(computed, "<computed>")                        \
  F(dot_brand, ".brand")                           \
  F(constructor, "constructor")                    \
  F(default, "default")                            \
  F(done, "done")                                  \
  F(dot, ".")                                      \
  F(dot_default, ".default")                       \
  F(dot_for, ".for")                               \
  F(dot_generator_object, ".generator_object")     \
  F(dot_home_object, ".home_object")               \
  F(dot_result, ".result")                         \
  F(dot_repl_result, ".repl_result")               \
  F(dot_static_home_object, ".static_home_object") \
  F(dot_switch_tag, ".switch_tag")                 \
  F(dot_catch, ".catch")                           \
  F(empty, "")                                     \
  F(eval, "eval")                                  \
  F(from, "from")                                  \
  F(function, "function")                          \
  F(get_space, "get ")                             \
  F(length, "length")                              \
  F(let, "let")                                    \
  F(meta, "meta")                                  \
  F(native, "native")                              \
  F(new_target, ".new.target")                     \
  F(next, "next")                                  \
  F(number, "number")                              \
  F(object, "object")                              \
  F(private_constructor, "#constructor")           \
  F(proto, "__proto__")                            \
  F(prototype, "prototype")                        \
  F(return, "return")                              \
  F(set_space, "set ")                             \
  F(source, "source")                              \
  F(string, "string")                              \
  F(symbol, "symbol")                              \
  F(target, "target")                              \
  F(this, "this")                                  \
  F(this_function, ".this_function")               \
  F(throw, "throw")                                \
  F(undefined, "undefined")                        \
  F(value, "value")

class AstStringConstants final {
 public:
  AstStringConstants(Isolate* isolate, uint64_t hash_seed);
  AstStringConstants(const AstStringConstants&) = delete;
  AstStringConstants& operator=(const AstStringConstants&) = delete;

#define F(name, str) \
  const AstRawString* name##_string() const { return name##_string_; }
  AST_STRING_CONSTANTS(F)
#undef F

  uint64_t hash_seed() const { return hash_seed_; }
  const AstRawStringMap* string_table() const { return &string_table_; }

 private:
  Zone zone_;
  AstRawStringMap string_table_;
  uint64_t hash_seed_;

#define F(name, str) AstRawString* name##_string_;
  AST_STRING_CONSTANTS(F)
#undef F
};

class AstValueFactory {
 public:
  AstValueFactory(Zone* zone, const AstStringConstants* string_constants,
                  uint64_t hash_seed)
      : AstValueFactory(zone, zone, string_constants, hash_seed) {}

  AstValueFactory(Zone* ast_raw_string_zone, Zone* single_parse_zone,
                  const AstStringConstants* string_constants,
                  uint64_t hash_seed)
      : string_table_(string_constants->string_table()),
        strings_(nullptr),
        strings_end_(&strings_),
        string_constants_(string_constants),
        empty_cons_string_(nullptr),
        ast_raw_string_zone_(ast_raw_string_zone),
        single_parse_zone_(single_parse_zone),
        hash_seed_(hash_seed) {
    DCHECK_NOT_NULL(ast_raw_string_zone_);
    DCHECK_NOT_NULL(single_parse_zone_);
    DCHECK_EQ(hash_seed, string_constants->hash_seed());
    std::fill(one_character_strings_,
              one_character_strings_ + arraysize(one_character_strings_),
              nullptr);

    // Allocate the empty ConsString in the AstRawString Zone instead of the
    // single parse Zone like other ConsStrings, because unlike those it can be
    // reused across parses.
    empty_cons_string_ = ast_raw_string_zone_->New<AstConsString>();
  }

  Zone* ast_raw_string_zone() const {
    DCHECK_NOT_NULL(ast_raw_string_zone_);
    return ast_raw_string_zone_;
  }

  Zone* single_parse_zone() const {
    DCHECK_NOT_NULL(single_parse_zone_);
    return single_parse_zone_;
  }

  const AstRawString* GetOneByteString(base::Vector<const uint8_t> literal) {
    return GetOneByteStringInternal(literal);
  }
  const AstRawString* GetOneByteString(const char* string) {
    return GetOneByteString(base::OneByteVector(string));
  }
  const AstRawString* GetTwoByteString(base::Vector<const uint16_t> literal) {
    return GetTwoByteStringInternal(literal);
  }
  const AstRawString* GetString(Tagged<String> literal,
                                const SharedStringAccessGuardIfNeeded&);

  V8_EXPORT_PRIVATE AstConsString* NewConsString();
  V8_EXPORT_PRIVATE AstConsString* NewConsString(const AstRawString* str);
  V8_EXPORT_PRIVATE AstConsString* NewConsString(const AstRawString* str1,
                                                 const AstRawString* str2);

  // Internalize all the strings in the factory, and prevent any more from being
  // allocated. Multiple calls to Internalize are allowed, for simplicity, where
  // subsequent calls are a no-op.
  template <typename IsolateT>
  void Internalize(IsolateT* isolate);

#define F(name, str)                           \
  const AstRawString* name##_string() const {  \
    return string_constants_->name##_string(); \
  }
  AST_STRING_CONSTANTS(F)
#undef F
  AstConsString* empty_cons_string() const { return empty_cons_string_; }

 private:
  AstRawString* AddString(AstRawString* string) {
    *strings_end_ = string;
    strings_end_ = string->next_location();
    return string;
  }
  void ResetStrings() {
    strings_ = nullptr;
    strings_end_ = &strings_;
  }
  V8_EXPORT_PRIVATE const AstRawString* GetOneByteStringInternal(
      base::Vector<const uint8_t> literal);
  const AstRawString* GetTwoByteStringInternal(
      base::Vector<const uint16_t> literal);
  const AstRawString* GetString(uint32_t raw_hash_field, bool is_one_byte,
                                base::Vector<const uint8_t> literal_bytes);

  // All strings are copied here.
  AstRawStringMap string_table_;

  AstRawString* strings_;
  AstRawString** strings_end_;

  // Holds constant string values which are shared across the isolate.
  const AstStringConstants* string_constants_;

  AstConsString* empty_cons_string_;

  // Caches one character lowercase strings (for minified code).
  static const int kMaxOneCharStringValue = 128;
  const AstRawString* one_character_strings_[kMaxOneCharStringValue];

  Zone* ast_raw_string_zone_;
  Zone* single_parse_zone_;

  uint64_t hash_seed_;
};

extern template EXPORT_TEMPLATE_DECLARE(
    V8_EXPORT_PRIVATE) void AstValueFactory::Internalize<Isolate>(Isolate*
                                                                      isolate);

extern template EXPORT_TEMPLATE_DECLARE(
    V8_EXPORT_PRIVATE) void AstValueFactory::
    Internalize<LocalIsolate>(LocalIsolate* isolate);

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_AST_VALUE_FACTORY_H_
