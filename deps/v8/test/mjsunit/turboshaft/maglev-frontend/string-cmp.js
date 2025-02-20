// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

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
