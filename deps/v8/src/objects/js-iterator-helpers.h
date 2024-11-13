// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ITERATOR_HELPERS_H_
#define V8_OBJECTS_JS_ITERATOR_HELPERS_H_

#include "src/objects/js-objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class Boolean;

#include "torque-generated/src/objects/js-iterator-helpers-tq.inc"

// Iterator helpers are iterators that transform an underlying iterator in some
// way. They are specified as spec generators. That is, the spec defines the
// body of iterator helpers using algorithm steps with yields (like JS
// generators) packaged in an Abstract Closure, and then makes a generator
// object internally. Generator machinery such as GeneratorResume [1] are then
// used to specify %IteratorHelperPrototype%.{next,return}. While this aids
// understandability of the specification, it is not conducive to ease of
// implementation or performance in V8.
//
// Instead, each iterator helper is implemented as an iterator directly, with
// JSIteratorHelper acting as a superclass to multiplex the various kinds of
// helpers.
//
// Each helper has its own Torque class to hold the state it needs. (In the
// spec, the state is captured in the Abstract Closures.) The classes are named
// after the name of the method that produces them. E.g., the iterator helper
// returned by Iterator.prototype.map is named JSIteratorMapHelper, and has
// fields for the underlying iterator, the mapper function, and a counter.
//
// The algorithm steps in the body Abstract Closure in the specification is
// implemented directly as next() (and return(), if necessary) builtin
// methods. E.g., the map helper's body is implemented as
// Builtin::kIteratorMapHelperNext.
//
// All iterator helper objects have %IteratorHelperPrototype% as their
// [[Prototype]]. The implementations of %IteratorHelperPrototype%.{next,return}
// multiplex, typeswitching over all known iterator helpers and manually calling
// their next() (and return(), if necessary) builtins. E.g., Calling next() on
// JSIteratorMapHelper would ultimately call Builtin::kIteratorMapHelperNext.
//
// [1] https://tc39.es/ecma262/#sec-generatorresume

// The superclass of all iterator helpers.
class JSIteratorHelper
    : public TorqueGeneratedJSIteratorHelper<JSIteratorHelper, JSObject> {
 public:
  void JSIteratorHelperPrintHeader(std::ostream& os, const char* helper_name);

  TQ_OBJECT_CONSTRUCTORS(JSIteratorHelper)
};

// The iterator helper returned by Iterator.prototype.map.
class JSIteratorMapHelper
    : public TorqueGeneratedJSIteratorMapHelper<JSIteratorMapHelper,
                                                JSIteratorHelper> {
 public:
  DECL_PRINTER(JSIteratorMapHelper)
  DECL_VERIFIER(JSIteratorMapHelper)

  TQ_OBJECT_CONSTRUCTORS(JSIteratorMapHelper)
};

// The iterator helper returned by Iterator.prototype.filter.
class JSIteratorFilterHelper
    : public TorqueGeneratedJSIteratorFilterHelper<JSIteratorFilterHelper,
                                                   JSIteratorHelper> {
 public:
  DECL_PRINTER(JSIteratorFilterHelper)
  DECL_VERIFIER(JSIteratorFilterHelper)

  TQ_OBJECT_CONSTRUCTORS(JSIteratorFilterHelper)
};

// The iterator helper returned by Iterator.prototype.take.
class JSIteratorTakeHelper
    : public TorqueGeneratedJSIteratorTakeHelper<JSIteratorTakeHelper,
                                                 JSIteratorHelper> {
 public:
  DECL_PRINTER(JSIteratorTakeHelper)
  DECL_VERIFIER(JSIteratorTakeHelper)

  TQ_OBJECT_CONSTRUCTORS(JSIteratorTakeHelper)
};

// The iterator helper returned by Iterator.prototype.drop.
class JSIteratorDropHelper
    : public TorqueGeneratedJSIteratorDropHelper<JSIteratorDropHelper,
                                                 JSIteratorHelper> {
 public:
  DECL_PRINTER(JSIteratorDropHelper)
  DECL_VERIFIER(JSIteratorDropHelper)

  TQ_OBJECT_CONSTRUCTORS(JSIteratorDropHelper)
};

// The iterator helper returned by Iterator.prototype.flatMap.
class JSIteratorFlatMapHelper
    : public TorqueGeneratedJSIteratorFlatMapHelper<JSIteratorFlatMapHelper,
                                                    JSIteratorHelper> {
 public:
  DECL_PRINTER(JSIteratorFlatMapHelper)
  DECL_VERIFIER(JSIteratorFlatMapHelper)

  TQ_OBJECT_CONSTRUCTORS(JSIteratorFlatMapHelper)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ITERATOR_HELPERS_H_
