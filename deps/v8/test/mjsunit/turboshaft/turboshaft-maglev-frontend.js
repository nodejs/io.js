// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan --no-stress-concurrent-inlining
// Flags: --invocation-count-for-turbofan=3000

// TODO(dmercadier): re-allow optimization of these functions once the
// maglev-to-turboshaft graph builder supports everything they need.
%NeverOptimizeFunction(assertEquals);
%NeverOptimizeFunction(assertOptimized);
%NeverOptimizeFunction(assertUnoptimized);

function math_smi(x, y) {
  let a = x * y;
  a = a + 152;
  a = a / x;
  a++;
  a = a - y;
  a = a % 5;
  a--;
  return a;
}
%PrepareFunctionForOptimization(math_smi);
assertEquals(3, math_smi(4, 3));
assertEquals(3, math_smi(4, 3));
%OptimizeFunctionOnNextCall(math_smi);
assertEquals(3, math_smi(4, 3));
assertOptimized(math_smi);
assertEquals(NaN, math_smi("a", "b"));
assertUnoptimized(math_smi);

function math_float(x, y) {
  let a = x * y;
  let b = a + 152.56;
  let c = b / x;
  let e = c - Math.round(y);
  let f = e % 5.56;
  let g = f ** x;
  let h = -g;
  return h;
}
%PrepareFunctionForOptimization(math_float);
assertEquals(-42.56563728706824, math_float(4.21, 3.56));
assertEquals(-42.56563728706824, math_float(4.21, 3.56));
%OptimizeFunctionOnNextCall(math_float);
assertEquals(-42.56563728706824, math_float(4.21, 3.56));
assertOptimized(math_float);

// Comparisons
{
  function cmp_int32(which, a, b) {
    if (which == 0) { return a > b; }
    if (which == 1) { return a >= b; }
    if (which == 2) { return a < b; }
    if (which == 3) { return a <= b; }
  }
  %PrepareFunctionForOptimization(cmp_int32);
  // >
  assertEquals(false, cmp_int32(0, 10, 20));
  assertEquals(true, cmp_int32(0, 20, 10));
  assertEquals(false, cmp_int32(0, 15, 15));
  // >=
  assertEquals(false, cmp_int32(1, 10, 20));
  assertEquals(true, cmp_int32(1, 20, 10));
  assertEquals(true, cmp_int32(1, 15, 15));
  // <
  assertEquals(true, cmp_int32(2, 10, 20));
  assertEquals(false, cmp_int32(2, 20, 10));
  assertEquals(false, cmp_int32(2, 15, 15));
  // <=
  assertEquals(true, cmp_int32(3, 10, 20));
  assertEquals(false, cmp_int32(3, 20, 10));
  assertEquals(true, cmp_int32(3, 15, 15));

  %OptimizeFunctionOnNextCall(cmp_int32);
  // >
  assertEquals(false, cmp_int32(0, 10, 20));
  assertEquals(true, cmp_int32(0, 20, 10));
  assertEquals(false, cmp_int32(0, 15, 15));
  // >=
  assertEquals(false, cmp_int32(1, 10, 20));
  assertEquals(true, cmp_int32(1, 20, 10));
  assertEquals(true, cmp_int32(1, 15, 15));
  // <
  assertEquals(true, cmp_int32(2, 10, 20));
  assertEquals(false, cmp_int32(2, 20, 10));
  assertEquals(false, cmp_int32(2, 15, 15));
  // <=
  assertEquals(true, cmp_int32(3, 10, 20));
  assertEquals(false, cmp_int32(3, 20, 10));
  assertEquals(true, cmp_int32(3, 15, 15));
  assertOptimized(cmp_int32);

  function cmp_float64(which, a, b) {
    if (which == 0) { return a > b; }
    if (which == 1) { return a >= b; }
    if (which == 2) { return a < b; }
    if (which == 3) { return a <= b; }
  }
  %PrepareFunctionForOptimization(cmp_float64);
  // >
  assertEquals(false, cmp_float64(0, 10.25, 20.25));
  assertEquals(true, cmp_float64(0, 20.25, 10.25));
  assertEquals(false, cmp_float64(0, 15.25, 15.25));
  // >=
  assertEquals(false, cmp_float64(1, 10.25, 20.25));
  assertEquals(true, cmp_float64(1, 20.25, 10.25));
  assertEquals(true, cmp_float64(1, 15.25, 15.25));
  // <
  assertEquals(true, cmp_float64(2, 10.25, 20.25));
  assertEquals(false, cmp_float64(2, 20.25, 10.25));
  assertEquals(false, cmp_float64(2, 15.25, 15.25));
  // <=
  assertEquals(true, cmp_float64(3, 10.25, 20.25));
  assertEquals(false, cmp_float64(3, 20.25, 10.25));
  assertEquals(true, cmp_float64(3, 15.25, 15.25));

  %OptimizeFunctionOnNextCall(cmp_float64);
  // >
  assertEquals(false, cmp_float64(0, 10.25, 20.25));
  assertEquals(true, cmp_float64(0, 20.25, 10.25));
  assertEquals(false, cmp_float64(0, 15.25, 15.25));
  // >=
  assertEquals(false, cmp_float64(1, 10.25, 20.25));
  assertEquals(true, cmp_float64(1, 20.25, 10.25));
  assertEquals(true, cmp_float64(1, 15.25, 15.25));
  // <
  assertEquals(true, cmp_float64(2, 10.25, 20.25));
  assertEquals(false, cmp_float64(2, 20.25, 10.25));
  assertEquals(false, cmp_float64(2, 15.25, 15.25));
  // <=
  assertEquals(true, cmp_float64(3, 10.25, 20.25));
  assertEquals(false, cmp_float64(3, 20.25, 10.25));
  assertEquals(true, cmp_float64(3, 15.25, 15.25));
  assertOptimized(cmp_float64);
}

function bitwise_smi(a, b) {
  let x = a | b;
  x = x & 52358961;
  x = x ^ b;
  x = x >> 2;
  x = x << 5;
  x = x >>> 1;
  return ~x;
}
%PrepareFunctionForOptimization(bitwise_smi);
assertEquals(-23041, bitwise_smi(1548, 45235));
assertEquals(-23041, bitwise_smi(1548, 45235));
%OptimizeFunctionOnNextCall(bitwise_smi);
assertEquals(-23041, bitwise_smi(1548, 45235));
assertOptimized(bitwise_smi);

function simple_loop(x) {
  let s = 0;
  for (let i = 0; i < 4; i++) {
    s += i + x;
  }
  return s;
}
%PrepareFunctionForOptimization(simple_loop);
assertEquals(74, simple_loop(17));
assertEquals(74, simple_loop(17));
%OptimizeFunctionOnNextCall(simple_loop);
assertEquals(74, simple_loop(17));
assertOptimized(simple_loop);

// Testing field loads
{
  function load_field(o) {
    let x = o.x;
    let y = o.y;
    return x + y;
  }

  let o = { x : 42, y : 15.71 };

  %PrepareFunctionForOptimization(load_field);
  assertEquals(57.71, load_field(o));
  %OptimizeFunctionOnNextCall(load_field);
  assertEquals(57.71, load_field(o));
  assertOptimized(load_field);
}

// Testing array loads
{
  function load_smi_arr(arr, idx) {
    return arr[1] + arr[idx];
  }
  let smi_arr = [1, 2, 3, 4, {}];
  %PrepareFunctionForOptimization(load_smi_arr);
  assertEquals(6, load_smi_arr(smi_arr, 3));
  assertEquals(6, load_smi_arr(smi_arr, 3));
  %OptimizeFunctionOnNextCall(load_smi_arr);
  assertEquals(6, load_smi_arr(smi_arr, 3));
  assertOptimized(load_smi_arr);

  // String indices currently work without requiring deopt.
  assertEquals(5, load_smi_arr(smi_arr, '2'));
  assertOptimized(load_smi_arr);

  function load_double_arr(arr, idx) {
    return arr[2] + arr[idx];
  }
  let double_arr = [1.552, 2.425, 3.526, 4.596, 5.986, 6.321];
  %PrepareFunctionForOptimization(load_double_arr);
  assertEquals(8.122, load_double_arr(double_arr, 3));
  assertEquals(8.122, load_double_arr(double_arr, 3));
  %OptimizeFunctionOnNextCall(load_double_arr);
  assertEquals(8.122, load_double_arr(double_arr, 3));
  assertOptimized(load_double_arr);

  // String indices currently work without requiring deopt.
  assertEquals(5.951, load_double_arr(double_arr, '1'));
  assertOptimized(load_double_arr);

  function load_holey_fixed_double(arr, idx) {
    return arr[idx];
  }
  let holey_double_arr = [2.58,3.41,,4.55];

  %PrepareFunctionForOptimization(load_holey_fixed_double);
  assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
  %OptimizeFunctionOnNextCall(load_holey_fixed_double);
  assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
  assertOptimized(load_holey_fixed_double);
  // Loading a hole should trigger a deopt
  assertEquals(undefined, load_holey_fixed_double(holey_double_arr, 2));
  assertUnoptimized(load_holey_fixed_double);

  // Reoptimizing, holes should now be handled
  %OptimizeMaglevOnNextCall(load_holey_fixed_double);
  assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
  %OptimizeFunctionOnNextCall(load_holey_fixed_double);
  assertEquals(3.41, load_holey_fixed_double(holey_double_arr, 1));
  assertEquals(undefined, load_holey_fixed_double(holey_double_arr, 2));
  assertOptimized(load_holey_fixed_double);
}

// Simple JS function call
{
  %NeverOptimizeFunction(h);
  function h(x) { return x; }
  function g(x) { return h(x); }
  function f(x) { return g(x); }

  %PrepareFunctionForOptimization(g);
  %PrepareFunctionForOptimization(f);
  assertEquals(42, f(42));
  assertEquals(42, f(42));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(42, f(42));
  assertOptimized(f);
}

// Simple JS call with receiver
{
  function f(o) { return o.x(17); }

  let o = { y : 42, x : function(a) { return a + this.y; } };
  %NeverOptimizeFunction(o.x);

  %PrepareFunctionForOptimization(f);
  assertEquals(59, f(o));
  assertEquals(59, f(o));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(59, f(o));
  assertOptimized(f);
}

// Lazy deopt during JS function call
{
  %NeverOptimizeFunction(h);
  function h(x, d) {
    if (d == 2) { return f(x, d-1); }
    if (d == 1) {
      // Calling `f` with a string as input will trigger an eager deopt of `f`,
      // which will also trigger a lazy deopt of all instances `f` on the caller
      // stack.
      return f("str", d-1);
    }
    return x;
  }

  function g(x, d) {
    let tmp = x * 12;
    let v = h(x, d);
    return tmp + v;
  }

  function f(x, d) {
    let a = x + 2;
    return g(a, d);
  }

  %PrepareFunctionForOptimization(f);
  %PrepareFunctionForOptimization(g);
  assertEquals(572, f(42, 0));
  assertEquals(572, f(42, 0));

  %OptimizeFunctionOnNextCall(f);
  assertEquals(572, f(42, 0));
  assertOptimized(f);
  assertEquals("528552NaNstr2", f(42, 2));
  assertUnoptimized(f);
}

// Testing deopt with raw floats and raw integers in the frame state.
{
  %NeverOptimizeFunction(sum);
  function sum(...args) {
    return args.reduce((a,b) => a + b, 0);
  }

  function f(a, b, c) {
    let x = a * 4.25;
    let y = b * 17;
    // This call to `sum` causes `x` and `y` to be part of the frame state.
    let s = sum(a, b);
    let z = b + c;
    // This call is just to use the values we computed before.
    return sum(s, x, y, z);
  }

  %PrepareFunctionForOptimization(f);
  assertEquals(113.39, f(2.36, 5, 6));
  assertEquals(113.39, f(2.36, 5, 6));

  %OptimizeFunctionOnNextCall(f);
  assertEquals(113.39, f(2.36, 5, 6));
  assertOptimized(f);
  assertEquals(113.93, f(2.36, 5, 6.54));
  assertUnoptimized(f);
}

// Testing exceptions.
{
  function h(x) {
    if (x) { willThrow(); }
    else { return 17; }
  }
  %NeverOptimizeFunction(h);

  function f(a, b) {
    let r = a;
    try {
      r = h(a);
      return h(b) + r;
    }
    catch {
      return r * b;
    }
  }

  %PrepareFunctionForOptimization(f);
  assertEquals(34, f(0, 0)); // Won't cause an exception
  assertEquals(187, f(0, 11)); // Will cause an exception on the 2nd call to h
  assertEquals(0, f(7, 0)); // Will cause an exception on the 1st call to h
  %OptimizeFunctionOnNextCall(f);
  assertEquals(34, f(0, 0));
  assertEquals(187, f(0, 11));
  assertEquals(0, f(7, 0));
  assertOptimized(f);
}

// Testing builtin calls
{
  // String comparison (which are currently done with builtin calls in Maglev).
  function cmp_str(a, b) {
    return a < b;
  }

  %PrepareFunctionForOptimization(cmp_str);
  assertEquals(true, cmp_str("abc", "def"));
  %OptimizeFunctionOnNextCall(cmp_str);
  assertEquals(true, cmp_str("abc", "def"));
  assertOptimized(cmp_str);

  // Megamorphic load.
  function load(o) {
    return o.x;
  }

  let o1 = { x : 42 };
  let o2 = { a : {}, x : 2.5 };
  let o3 = 42;
  let o4 = { b : 42, c: {}, x : 5.35 };
  let o5 = { u : 14, c : 2.28, d: 4.2, x : 5 };

  %PrepareFunctionForOptimization(load);
  assertEquals(42, load(o1));
  assertEquals(2.5, load(o2));
  assertEquals(undefined, load(o3));
  assertEquals(5.35, load(o4));
  assertEquals(5, load(o5));
  %OptimizeFunctionOnNextCall(load);
  assertEquals(42, load(o1));
  assertEquals(2.5, load(o2));
  assertEquals(undefined, load(o3));
  assertEquals(5.35, load(o4));
  assertEquals(5, load(o5));
  assertOptimized(load);

  // charAt is a builtin call but is done with a CallKnowJSFunctionCall
  function string_char_at(s) {
    return s.charAt(2);
  }

  %PrepareFunctionForOptimization(string_char_at);
  assertEquals("c", string_char_at("abcdef"));
  %OptimizeFunctionOnNextCall(string_char_at);
  assertEquals("c", string_char_at("abcdef"));
  assertOptimized(string_char_at);
}

// Testing stores
{
  function store_field(o, v) {
    o.x = 17; // Tagged field, no write barrier
    o.y = v; // Tagged field, with write barrier
    o.z = 12.29; // Double field
    return o;
  }

  let o = { x : 42, y : 10, z : 14.58 };

  %PrepareFunctionForOptimization(store_field);
  assertEquals({ x : 17, y : undefined, z : 12.29 }, store_field(o));
  o = { x : 42, y : 10, z : 14.58 }; // Resetting {o}
  %OptimizeFunctionOnNextCall(store_field);
  assertEquals({ x : 17, y : undefined, z : 12.29 }, store_field(o));
  assertOptimized(store_field);

  function store_arr(obj_arr, double_arr) {
    obj_arr[0] = 42; // FixedArray, no write barrier
    obj_arr[1] = double_arr; // FixedArray, with write barrier
    double_arr[1] = 42.25; // DoubleFixedArray
  }

  let obj_arr = [0, {}, 2];
  let double_arr = [1.56, 2.68, 3.51];

  %PrepareFunctionForOptimization(store_arr);
  store_arr(obj_arr, double_arr);
  assertEquals([42, double_arr, 2], obj_arr);
  assertEquals([1.56, 42.25, 3.51], double_arr);

  // Resetting {obj_arr} and {double_arr}
  obj_arr[0] = 0;
  obj_arr[1] = {};
  double_arr[1] = 2.68;

  %OptimizeFunctionOnNextCall(store_arr);
  store_arr(obj_arr, double_arr);
  assertEquals([42, double_arr, 2], obj_arr);
  assertEquals([1.56, 42.25, 3.51], double_arr);
  assertOptimized(store_arr);
}

// Testing branches
{
  function branch_cmp(w32, f64) {
    let c1 = w32 + 2;
    let v;
    if (c1) {
      v = c1 + 12;
    } else {
      v = c1 + 17;
    }
    let c2 = f64 + 1.5;
    if (c2) {
      v += 10;
    } else {
      v += 3;
    }
    if (f64 > 1.5) {
      v += 2;
    } else {
      v += 7;
    }
    return v;
  }

  %PrepareFunctionForOptimization(branch_cmp);
  assertEquals(41, branch_cmp(15, 3.25));
  assertEquals(29, branch_cmp(-2, 3.25));
  assertEquals(27, branch_cmp(-2, -1.5));
  %OptimizeFunctionOnNextCall(branch_cmp);
  assertEquals(41, branch_cmp(15, 3.25));
  assertEquals(29, branch_cmp(-2, 3.25));
  assertEquals(27, branch_cmp(-2, -1.5));
  assertOptimized(f);
}

// Test BranchIfReferenceEqual and TaggedEqual
{
  function f(x, y) {
    let is_eq = x === y;
    let r = is_eq + 2;
    if (x === y) {
      return r + 42;
    } else {
      return r + 25;
    }
  }

  let o = {};

  %PrepareFunctionForOptimization(f);
  assertEquals(27, f(o, []));
  assertEquals(45, f(o, o));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(27, f(o, []));
  assertEquals(45, f(o, o));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(27, f(o, []));
  assertEquals(45, f(o, o));
  assertOptimized(f);

  // Making sure that strings trigger a deopt (since === for strings is regular
  // string equality, not reference equality).
  assertEquals(45, f("abcdefghijklmno", "abcde" + "fghijklmno"));
  assertUnoptimized(f);
}

// Testing allocation of objects.
{
  function alloc() {
    return [1, {x : 42.27}, 3, "abc"];
  }

  %PrepareFunctionForOptimization(alloc);
  assertEquals([1, {x : 42.27}, 3, "abc"], alloc());
  %OptimizeMaglevOnNextCall(alloc);
  assertEquals([1, {x : 42.27}, 3, "abc"], alloc());
  %OptimizeFunctionOnNextCall(alloc);
  assertEquals([1, {x : 42.27}, 3, "abc"], alloc());
  assertOptimized(alloc);
}

// Testing string opcodes
{
  function f(short_str, long_str) {
    let cons_str = short_str + long_str;
    let seq_str = short_str + "cd";
    if (seq_str == "abcdcd") {
      return cons_str.length;
    } else {
      return seq_str[2];
    }
  }

  %PrepareFunctionForOptimization(f);
  assertEquals(19, f("abcd", "abcdefghijklmno"));
  assertEquals("c", f("abcde", "abcdefghijklmno"));
  %OptimizeMaglevOnNextCall(f);
  assertEquals(19, f("abcd", "abcdefghijklmno"));
  assertEquals("c", f("abcde", "abcdefghijklmno"));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(19, f("abcd", "abcdefghijklmno"));
  assertEquals("c", f("abcde", "abcdefghijklmno"));
  assertOptimized(f);

  function string_cmp(str1, str2, cmp) {
    if (cmp == 0) { return str1 < str2; }
    if (cmp == 1) { return str1 <= str2; }
    if (cmp == 2) { return str1 > str2; }
    if (cmp == 3) { return str1 >= str2; }
    if (cmp == 4) { return str1 == str2; }
    if (cmp == 5) { return str1 != str2; }
  }

  %PrepareFunctionForOptimization(string_cmp);
  assertEquals(true, string_cmp("ab", "cd", 0)); // <
  assertEquals(false, string_cmp("ab", "ab", 0));
  assertEquals(false, string_cmp("ar", "ab", 0));
  assertEquals(true, string_cmp("ab", "cd", 1)); // <=
  assertEquals(true, string_cmp("ab", "ab", 1));
  assertEquals(false, string_cmp("ar", "ab", 1));
  assertEquals(true, string_cmp("cd", "ab", 2)); // >
  assertEquals(false, string_cmp("ab", "ab", 2));
  assertEquals(false, string_cmp("ab", "cd", 2));
  assertEquals(true, string_cmp("cd", "ab", 3)); // >=
  assertEquals(true, string_cmp("ab", "ab", 3));
  assertEquals(false, string_cmp("ab", "cd", 3));
  assertEquals(true, string_cmp("ab", "ab", 4)); // ==
  assertEquals(false, string_cmp("ar", "ab", 4));
  assertEquals(true, string_cmp("ab", "cd", 5)); // !=
  assertEquals(false, string_cmp("ab", "ab", 5));

  %OptimizeFunctionOnNextCall(string_cmp);
  assertEquals(true, string_cmp("ab", "cd", 0)); // <
  assertEquals(false, string_cmp("ab", "ab", 0));
  assertEquals(false, string_cmp("ar", "ab", 0));
  assertOptimized(string_cmp);
  assertEquals(true, string_cmp("ab", "cd", 1)); // <=
  assertEquals(true, string_cmp("ab", "ab", 1));
  assertEquals(false, string_cmp("ar", "ab", 1));
  assertOptimized(string_cmp);
  assertEquals(true, string_cmp("cd", "ab", 2)); // >
  assertEquals(false, string_cmp("ab", "ab", 2));
  assertEquals(false, string_cmp("ab", "cd", 2));
  assertOptimized(string_cmp);
  assertEquals(true, string_cmp("cd", "ab", 3)); // >=
  assertEquals(true, string_cmp("ab", "ab", 3));
  assertEquals(false, string_cmp("ab", "cd", 3));
  assertOptimized(string_cmp);
  assertEquals(true, string_cmp("ab", "ab", 4)); // ==
  assertEquals(false, string_cmp("ar", "ab", 4));
  assertEquals(true, string_cmp("ab", "cd", 5)); // !=
  assertEquals(false, string_cmp("ab", "ab", 5));
  assertOptimized(string_cmp);

  // Passing a non-internal string as a parameter will trigger a deopt in "=="
  let str = "abcdefghi";
  assertEquals(false, string_cmp(str + "azeazeaze", "abc", 4));
  assertUnoptimized(string_cmp);
}

// Testing generic builtins
{
  function f(a, b, c, d) {
    let x1 = a + b;
    let x2 = a - b;
    let x3 = a * b;
    let x4 = a / b;
    let x5 = a % b;
    let x6 = a ** b;
    let x7 = a & b;
    let x8 = a | b;
    let x9 = a ^ b;
    let x10 = a << b;
    let x11 = a >> b;
    let x12 = a >>> b;
    let x13 = ~a;
    let x14 = -a;
    let x15 = a < b;
    let x16 = a <= b;
    let x17 = a > b;
    let x18 = a >= b;
    let x19 = a == b;
    let x20 = a === b;
    // It's important to do the increment/decrement last, because they lead to
    // having numeric alternatives for {a} and {b} in Maglev, which leads Maglev
    // to compiling subsequent <, <=, >, and >= to Float64 comparisons despite
    // their generic feedback.
    let x21 = a++;
    let x22 = b--;
    let x23 = ++c;
    let x24 = --d;

    return [x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15,
            x16, x17, x18, x19, x20, x21, x22, x23, x24];
  }

  %PrepareFunctionForOptimization(f);
  let expected_1 = f(1, 2, 1, 2);
  let expected_2 = f("1", 2, "1", 2);
  let expected_3 = f(2, "1", 2, "1");
  let expected_4 = f([], 1, [], 1);
  let expected_5 = f({}, 1, {}, 1);
  %OptimizeFunctionOnNextCall(f);
  assertEquals(expected_1, f(1, 2, 1, 2));
  assertEquals(expected_2, f("1", 2, "1", 2));
  assertEquals(expected_3, f(2, "1", 2, "1"));
  assertEquals(expected_4, f([], 1, [], 1));
  assertEquals(expected_5, f({}, 1, {}, 1));
  assertOptimized(f);
}

// Testing ToBoolean and LogicalNot on various datatypes.
{
  function f(x, y) {
    let a = x * 2;
    let b = a * 2.27;
    return [!a, !y, !b];
  }

  %PrepareFunctionForOptimization(f);
  assertEquals([false, false, false], f(4, 3));
  assertEquals([true, true, true], f(0, 0));
  %OptimizeFunctionOnNextCall(f);
  assertEquals([false, false, false], f(4, 3));
  assertEquals([true, true, true], f(0, 0));
  assertOptimized(f);
}

// Testing Float64 Ieee754 unary functions.
{
  function f(x) {
    let x1 = Math.abs(x);
    let x2 = Math.acos(x);
    let x3 = Math.acosh(x);
    let x4 = Math.asin(x);
    let x5 = Math.asinh(x);
    let x6 = Math.atan(x);
    let x7 = Math.atanh(x);
    let x8 = Math.cbrt(x);
    let x9 = Math.cos(x);
    let x10 = Math.cosh(x);
    let x11 = Math.exp(x);
    let x12 = Math.expm1(x);
    let x13 = Math.log(x);
    let x14 = Math.log1p(x);
    let x15 = Math.log10(x);
    let x16 = Math.log2(x);
    let x17 = Math.sin(x);
    let x18 = Math.sinh(x);
    let x19 = Math.tan(x);
    let x20 = Math.tanh(x);

    return [x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11,
            x12, x13, x14, x15, x16, x17, x18, x19, x20];
  }

  %PrepareFunctionForOptimization(f);
  let expected_1 = f(0.758);
  let expected_2 = f(2);

  %OptimizeFunctionOnNextCall(f);
  assertEquals(expected_1, f(0.758));
  assertEquals(expected_2, f(2));
}

// Testinf for-in loops.
{
  function f(o) {
    let s = 0;
    for (let i in o) {
      s += o[i];
    }
    return s;
  }

  let o = { x : 42, y : 19, z: 5 };

  %PrepareFunctionForOptimization(f);
  assertEquals(66, f(o));
  %OptimizeFunctionOnNextCall(f);
  assertEquals(66, f(o));
}
