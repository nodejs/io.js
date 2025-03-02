// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestTable64Constructor() {
  print(arguments.callee.name);
  let is_bigint = n => typeof n == "bigint";
  let is_number = n => typeof n == "number";
  let is_undefined = n => typeof n == "undefined";
  let is_string = n => typeof n == "string";
  // Printing support.
  let Print = n => is_bigint(n) ? `${n}n` : is_string(n) ? `"${n}"` : `${n}`;

  for (let initial of [undefined, 1, 1n, "1", true]) {
    for (let maximum of [undefined, 1, 1n, "1", true]) {
      for (let address of [undefined, 'i32', 'i64', "1", true]) {
        let is_i32 = is_undefined(address) || address === 'i32';
        let valid_address = is_i32 || address === 'i64';
        let valid_initial = !is_undefined(initial) &&
            (is_i32 ? !is_bigint(initial) : !is_number(initial));
        let valid_maximum = is_i32 ? !is_bigint(maximum) : !is_number(maximum);
        let valid = valid_address && valid_initial && valid_maximum;
        let desc = `${Print(initial)} / ${Print(maximum)} / ${
            Print(address)} -> ${valid}`;
        let code = () => new WebAssembly.Table({
          element: 'anyfunc',
          initial: initial,
          maximum: maximum,
          address: address
        });
        try {
          code();
          if (!valid) {
            assertUnreachable(`Should have failed with TypeError: ${desc}`);
          }
        } catch (e) {
          if (e instanceof TypeError && !valid) continue;
          print(desc);
          throw e;
        }
      }
    }
  }
})();

(function TestTable64GetAndSet() {
  print(arguments.callee.name);
  let table32 = new WebAssembly.Table({initial: 5, element: 'externref'});
  let table64 =
      new WebAssembly.Table({initial: 5n, element: 'externref', address: 'i64'});

  assertThrows(
      () => table32.get(1n), TypeError,
      'Cannot convert a BigInt value to a number');
  assertThrows(() => table64.get(1), TypeError, 'Cannot convert 1 to a BigInt');
  assertThrows(
      () => table32.set(1n, ''), TypeError,
      'Cannot convert a BigInt value to a number');
  assertThrows(
      () => table64.set(1, ''), TypeError, 'Cannot convert 1 to a BigInt');

  assertSame(undefined, table32.get(1));
  assertSame(undefined, table64.get(1n));

  table32.set(1, '32');
  table64.set(1n, '64');

  assertSame('32', table32.get(1));
  assertSame('64', table64.get(1n));
})();

(function TestTable64Grow() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table(
      {initial: 1n, maximum: 5n, element: 'externref', address: 'i64'});
  assertEquals(1n, table.grow(2n));
  assertThrows(
      () => table.grow(3n), RangeError,
      'WebAssembly.Table.grow(): failed to grow table by 3');
  assertEquals(3n, table.grow(2n));
  assertThrows(
      () => table.grow(1n), RangeError,
      'WebAssembly.Table.grow(): failed to grow table by 1');
  assertEquals(5n, table.grow(0n));
  assertThrows(() => table.grow(0), TypeError, 'Cannot convert 0 to a BigInt');
})();
