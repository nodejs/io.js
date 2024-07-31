// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

(function TestRegistryHitWithFieldsOnly() {
  let T1 = new SharedStructType(['x', 'y'], 'Fields');
  let T2 = new SharedStructType(['x', 'y'], 'Fields');
  assertNotSame(T1, T2);
  %HaveSameMap(new T1, new T2);
})();

(function TestRegistryHitWithFieldsInternalized() {
  let T1 = new SharedStructType(
      [
        'qwporugaoasdjioasdjiaods',
        'bhuiweuryiq190hvaodeh'
      ],
      'FieldsInternalized');
  let T2 = new SharedStructType(
      [
        %ConstructConsString('qwporugao', 'asdjioasdjiaods'),
        %ConstructConsString('bhuiweuryiq', '190hvaodeh')
      ],
      'FieldsInternalized');
  assertNotSame(T1, T2);
  %HaveSameMap(new T1, new T2);
})();

(function TestRegistryHitWithElementsOnly() {
  let T1 = new SharedStructType([0, 4], 'Elements');
  let T2 = new SharedStructType([0, 4], 'Elements');
  assertNotSame(T1, T2);
  %HaveSameMap(new T1, new T2);
})();

(function TestRegistryHitWithElementsOnlyOrderDoesntMatter() {
  let T1 = new SharedStructType([0, 4], 'ElementsOrderDoesntMatter');
  let T2 = new SharedStructType([4, 0], 'ElementsOrderDoesntMatter');
  assertNotSame(T1, T2);
  %HaveSameMap(new T1, new T2);
})();

(function TestRegistryHitWithFieldsAndElements() {
  let T1 = new SharedStructType([0, 'x', 4, 'y'], 'FieldsAndElements');
  let T2 = new SharedStructType([0, 'x', 4, 'y'], 'FieldsAndElements');
  assertNotSame(T1, T2);
  %HaveSameMap(new T1, new T2);
})();

(function TestRegistryHitWithFieldsAndElements2() {
  // Order doesn't matter even when interleaved with field names.
  let T1 = new SharedStructType([0, 'x', 4, 'y'], 'FieldsAndElements2');
  let T2 = new SharedStructType([0, 4, 'x', 'y'], 'FieldsAndElements2');
  assertNotSame(T1, T2);
  %HaveSameMap(new T1, new T2);
})();

(function TestRegistryMismatchLength() {
  let T1 = new SharedStructType(['x'], 'MismatchLength');
  assertThrows(() => {
    new SharedStructType(['x', 'y'], 'MismatchLength');
  }, TypeError);
})();

(function TestRegistryMismatchFieldNames() {
  let T1 = new SharedStructType(['x', 'y'], 'MismatchFieldNames');
  assertThrows(() => {
    new SharedStructType(['z', 'w'], 'MismatchFieldNames');
  }, TypeError);
})();

(function TestRegistryMismatchOrder() {
  let T1 = new SharedStructType(['y', 'x'], 'MismatchOrder');
  assertThrows(() => {
    new SharedStructType(['x', 'y'], 'MismatchOrder');
  }, TypeError);
})();

(function TestRegistryMismatchElementsLength() {
  let T1 = new SharedStructType([0, 1], 'MismatchElementsLength');
  assertThrows(() => {
    new SharedStructType([0], 'MismatchElementsLength');
  }, TypeError);
})();

(function TestRegistryMismatchElements() {
  let T1 = new SharedStructType([0, 1], 'MismatchElements');
  assertThrows(() => {
    new SharedStructType([2, 3], 'MismatchElements');
  }, TypeError);
})();

(function TestRegistryMismatchElements2() {
  let T1 = new SharedStructType(['x'], 'MismatchElements2');
  assertThrows(() => {
    new SharedStructType([0, 1], 'MismatchElements2');
  }, TypeError);
})();
