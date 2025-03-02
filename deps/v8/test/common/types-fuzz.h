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

#ifndef V8_TEST_CCTEST_TYPES_H_
#define V8_TEST_CCTEST_TYPES_H_

#include "src/base/utils/random-number-generator.h"
#include "src/compiler/js-heap-broker.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"
#include "src/init/v8.h"

namespace v8 {
namespace internal {
namespace compiler {

class Types {
 public:
  Types(Zone* zone, Isolate* isolate, v8::base::RandomNumberGenerator* rng)
      : integers(isolate),
        zone_(zone),
        js_heap_broker_(isolate, zone),
        js_heap_broker_scope_(&js_heap_broker_, isolate, zone),
        current_broker_(&js_heap_broker_),
        rng_(rng) {
#define DECLARE_TYPE(name, value) \
  name = Type::name();            \
  types.push_back(name);
    PROPER_BITSET_TYPE_LIST(DECLARE_TYPE)
#undef DECLARE_TYPE

    if (!PersistentHandlesScope::IsActive(isolate)) {
      persistent_scope_.emplace(isolate);
    }

    SignedSmall = Type::SignedSmall();
    UnsignedSmall = Type::UnsignedSmall();

    DirectHandle<i::Map> object_map =
        CanonicalHandle(isolate->factory()->NewContextfulMapForCurrentContext(
            JS_OBJECT_TYPE, JSObject::kHeaderSize));
    Handle<i::Smi> smi = CanonicalHandle(Smi::FromInt(666));
    Handle<i::HeapNumber> boxed_smi =
        CanonicalHandle(isolate->factory()->NewHeapNumber(666));
    Handle<i::HeapNumber> signed32 =
        CanonicalHandle(isolate->factory()->NewHeapNumber(0x40000000));
    Handle<i::HeapNumber> float1 =
        CanonicalHandle(isolate->factory()->NewHeapNumber(1.53));
    Handle<i::HeapNumber> float2 =
        CanonicalHandle(isolate->factory()->NewHeapNumber(0.53));
    // float3 is identical to float1 in order to test that OtherNumberConstant
    // types are equal by double value and not by handle pointer value.
    Handle<i::HeapNumber> float3 =
        CanonicalHandle(isolate->factory()->NewHeapNumber(1.53));
    Handle<i::JSObject> object1 =
        CanonicalHandle(isolate->factory()->NewJSObjectFromMap(object_map));
    Handle<i::JSObject> object2 =
        CanonicalHandle(isolate->factory()->NewJSObjectFromMap(object_map));
    Handle<i::JSArray> array =
        CanonicalHandle(isolate->factory()->NewJSArray(20));
    Handle<i::Hole> uninitialized = isolate->factory()->uninitialized_value();
    Handle<i::Oddball> undefined = isolate->factory()->undefined_value();
    Handle<i::HeapNumber> nan = isolate->factory()->nan_value();
    Handle<i::Hole> the_hole_value = isolate->factory()->the_hole_value();

    SmiConstant = Type::Constant(js_heap_broker(), smi, zone);
    Signed32Constant = Type::Constant(signed32->value(), zone);
    ObjectConstant1 = Type::Constant(js_heap_broker(), object1, zone);
    ObjectConstant2 = Type::Constant(js_heap_broker(), object2, zone);
    ArrayConstant = Type::Constant(js_heap_broker(), array, zone);
    UninitializedConstant =
        Type::Constant(js_heap_broker(), uninitialized, zone);

    values.push_back(smi);
    values.push_back(boxed_smi);
    values.push_back(signed32);
    values.push_back(object1);
    values.push_back(object2);
    values.push_back(array);
    values.push_back(uninitialized);
    values.push_back(undefined);
    values.push_back(nan);
    values.push_back(the_hole_value);
    values.push_back(float1);
    values.push_back(float2);
    values.push_back(float3);
    values.push_back(isolate->factory()->empty_string());
    values.push_back(
        CanonicalHandle(isolate->factory()->NewStringFromStaticChars(
            "I'm a little string value, short and stout...")));
    values.push_back(
        CanonicalHandle(isolate->factory()->NewStringFromStaticChars(
            "Ask not for whom the typer types; it types for thee.")));
    for (IndirectHandle<i::Object> obj : values) {
      types.push_back(Type::Constant(js_heap_broker(), obj, zone));
    }

    integers.push_back(isolate->factory()->NewNumber(-V8_INFINITY));
    integers.push_back(isolate->factory()->NewNumber(+V8_INFINITY));
    integers.push_back(isolate->factory()->NewNumber(-rng_->NextInt(10)));
    integers.push_back(isolate->factory()->NewNumber(+rng_->NextInt(10)));
    for (int i = 0; i < 10; ++i) {
      double x = rng_->NextInt();
      integers.push_back(isolate->factory()->NewNumber(x));
      x *= rng_->NextInt();
      if (!IsMinusZero(x)) integers.push_back(isolate->factory()->NewNumber(x));
    }

    Integer = Type::Range(-V8_INFINITY, +V8_INFINITY, zone);

    for (int i = 0; i < 30; ++i) {
      types.push_back(Fuzz());
    }
  }

  ~Types() {
    if (persistent_scope_) {
      persistent_scope_->Detach();
    }
  }

#define DECLARE_TYPE(name, value) Type name;
  PROPER_BITSET_TYPE_LIST(DECLARE_TYPE)
#undef DECLARE_TYPE

  Type SignedSmall;
  Type UnsignedSmall;

  Type SmiConstant;
  Type Signed32Constant;
  Type ObjectConstant1;
  Type ObjectConstant2;
  Type ArrayConstant;
  Type UninitializedConstant;

  Type Integer;

  std::vector<Type> types;
  std::vector<IndirectHandle<i::Object>> values;
  DirectHandleVector<i::Object>
      integers;  // "Integer" values used for range limits.

  Type Constant(double value) { return Type::Constant(value, zone_); }

  Type Constant(Handle<i::Object> value) {
    return Type::Constant(js_heap_broker(), value, zone_);
  }

  Type HeapConstant(Handle<i::HeapObject> value) {
    return Type::Constant(js_heap_broker(), value, zone_);
  }

  Type Range(double min, double max) { return Type::Range(min, max, zone_); }

  Type Union(Type t1, Type t2) { return Type::Union(t1, t2, zone_); }

  Type Intersect(Type t1, Type t2) { return Type::Intersect(t1, t2, zone_); }

  Type Random() { return types[rng_->NextInt(static_cast<int>(types.size()))]; }

  Type Fuzz(int depth = 4) {
    switch (rng_->NextInt(depth == 0 ? 3 : 20)) {
      case 0: {  // bitset
#define COUNT_BITSET_TYPES(type, value) +1
        int n = 0 PROPER_BITSET_TYPE_LIST(COUNT_BITSET_TYPES);
#undef COUNT_BITSET_TYPES
        // Pick a bunch of named bitsets and return their intersection.
        Type result = Type::Any();
        for (int i = 0, m = 1 + rng_->NextInt(3); i < m; ++i) {
          int j = rng_->NextInt(n);
#define PICK_BITSET_TYPE(type, value)                        \
  if (j-- == 0) {                                            \
    Type tmp = Type::Intersect(result, Type::type(), zone_); \
    if (tmp.Is(Type::None()) && i != 0) {                    \
      break;                                                 \
    } else {                                                 \
      result = tmp;                                          \
      continue;                                              \
    }                                                        \
  }
          PROPER_BITSET_TYPE_LIST(PICK_BITSET_TYPE)
#undef PICK_BITSET_TYPE
        }
        return result;
      }
      case 1: {  // constant
        int i = rng_->NextInt(static_cast<int>(values.size()));
        if (IsHeapNumber(*values[i])) {
          return Type::Constant(Cast<HeapNumber>(*values[i])->value(), zone_);
        }
        return Type::Constant(js_heap_broker(), values[i], zone_);
      }
      case 2: {  // range
        int i = rng_->NextInt(static_cast<int>(integers.size()));
        int j = rng_->NextInt(static_cast<int>(integers.size()));
        double min = Object::NumberValue(*integers[i]);
        double max = Object::NumberValue(*integers[j]);
        if (min > max) std::swap(min, max);
        return Type::Range(min, max, zone_);
      }
      default: {  // union
        int n = rng_->NextInt(10);
        Type type = None;
        for (int i = 0; i < n; ++i) {
          Type operand = Fuzz(depth - 1);
          type = Type::Union(type, operand, zone_);
        }
        return type;
      }
    }
    UNREACHABLE();
  }

  Zone* zone() { return zone_; }
  JSHeapBroker* js_heap_broker() { return &js_heap_broker_; }

  template <typename T>
  IndirectHandle<T> CanonicalHandle(Tagged<T> object) {
    return js_heap_broker_.CanonicalPersistentHandle(object);
  }
  template <typename T>
  IndirectHandle<T> CanonicalHandle(T object) {
    static_assert(kTaggedCanConvertToRawObjects);
    return CanonicalHandle(Tagged<T>(object));
  }
  template <typename T>
  IndirectHandle<T> CanonicalHandle(IndirectHandle<T> handle) {
    return CanonicalHandle(*handle);
  }
  template <typename T>
  IndirectHandle<T> CanonicalHandle(DirectHandle<T> handle) {
    return CanonicalHandle(*handle);
  }

 private:
  Zone* zone_;
  JSHeapBroker js_heap_broker_;
  JSHeapBrokerScopeForTesting js_heap_broker_scope_;
  std::optional<PersistentHandlesScope> persistent_scope_;
  CurrentHeapBrokerScope current_broker_;
  v8::base::RandomNumberGenerator* rng_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif
