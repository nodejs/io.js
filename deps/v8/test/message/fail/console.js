// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

console.time();
console.timeEnd();

console.time("abcd");
console.timeEnd({ toString: () => "ab" + "cd" });

console.timeLog('a');
console.timeEnd('a');
console.time("a");
console.timeEnd('a');

console.time("a", "b");
console.timeEnd("a", "b");

console.time('b');
console.time('b');
console.timeEnd('b');

console.time('ab');
console.timeLog('ab', {x: 1, y: 2});
console.timeEnd('ab');

console.timeStamp('stamp it baby!')

console.log("log", "more");
console.warn("warn", { toString: () => 2 });
console.debug("debug");
console.info("info");

console.info({ toString: () => {throw new Error("exception");} })
