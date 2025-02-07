// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// precondition
assertTrue(%HaveSameMap(Object.freeze({}),     Object.freeze({})));
assertTrue(%HaveSameMap(Object.freeze({a: 1}), Object.freeze({a: 1})));
assertTrue(%HaveSameMap(Object.freeze([]),     Object.freeze([])));
assertTrue(%HaveSameMap(Object.freeze([1,2]),  Object.freeze([1,2])));

assertTrue(%HaveSameMap(Object.seal({}),     Object.seal({})));
assertTrue(%HaveSameMap(Object.seal({a: 1}), Object.seal({a: 1})));
assertTrue(%HaveSameMap(Object.seal([]),     Object.seal([])));
assertTrue(%HaveSameMap(Object.seal([1,2]),  Object.seal([1,2])));

// refreezing an already frozen obj does not keep adding transitions
assertTrue(%HaveSameMap(Object.freeze({}),     Object.freeze( Object.freeze({}) )));
assertTrue(%HaveSameMap(Object.freeze({a: 1}), Object.freeze( Object.freeze({a: 1}) )));
assertTrue(%HaveSameMap(Object.freeze([]),     Object.freeze( Object.freeze([]) )));
assertTrue(%HaveSameMap(Object.freeze([1,2]),  Object.freeze( Object.freeze([1,2]) )));

// resealing a sealed object is idempotent
assertTrue(%HaveSameMap(Object.seal({}),     Object.seal( Object.seal({}) )));
assertTrue(%HaveSameMap(Object.seal({a: 1}), Object.seal( Object.seal({a: 1}) )));
assertTrue(%HaveSameMap(Object.seal([]),     Object.seal( Object.seal([]) )));
assertTrue(%HaveSameMap(Object.seal([1,2]),  Object.seal( Object.seal([1,2]) )));

// sealing a frozen object is idempotent
assertTrue(%HaveSameMap(Object.freeze({}),     Object.seal( Object.freeze({}) )));
assertTrue(%HaveSameMap(Object.freeze({a: 1}), Object.seal( Object.freeze({a: 1}) )));
assertTrue(%HaveSameMap(Object.freeze([]),     Object.seal( Object.freeze([]) )));
assertTrue(%HaveSameMap(Object.freeze([1,2]),  Object.seal( Object.freeze([1,2]) )));

// freezing a sealed empty is idempotent
assertTrue(%HaveSameMap(Object.freeze(Object.seal({})), Object.seal({})));

// sealing an unextensible empty object is idempotent
assertTrue(%HaveSameMap(Object.seal(Object.preventExtensions({})), Object.preventExtensions({})));
