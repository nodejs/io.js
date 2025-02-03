// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan --no-lazy-feedback-allocation

// TODO(v8:10195): Fix these tests s.t. we assert deoptimization occurs when
// expected (e.g. in a %DeoptimizeNow call), then remove
// --no-lazy-feedback-allocation.

function clone(v) {
  // Shallow-copies arrays, returns everything else verbatim.
  if (v instanceof Array) {
    // Shallow-copy an array.
    var newArray = new Array(v.length);
    for (var i in v) {
      newArray[i] = v[i];
    }
    return newArray;
  }
  return v;
}


// Creates a callback function for reduce/reduceRight that tests the number
// of arguments and otherwise behaves as "func", but which also
// records all calls in an array on the function (as arrays of arguments
// followed by result).
function makeRecorder(func, testName) {
  var record = [];
  var f = function recorder(a, b, i, s) {
    assertEquals(4, arguments.length,
                 testName + "(number of arguments: " + arguments.length + ")");
    assertEquals("number", typeof(i), testName + "(index must be number)");
    assertEquals(s[i], b, testName + "(current argument is at index)");
    if (record.length > 0) {
      var prevRecord = record[record.length - 1];
      var prevResult = prevRecord[prevRecord.length - 1];
      assertEquals(prevResult, a,
                   testName + "(prev result -> current input)");
    }
    var args = [clone(a), clone(b), i, clone(s)];
    var result = func.apply(this, arguments);
    args.push(clone(result));
    record.push(args);
    return result;
  };
  f.record = record;
  return f;
}


function testReduce(type,
                    testName,
                    expectedResult,
                    expectedCalls,
                    array,
                    combine,
                    init) {
  var rec = makeRecorder(combine);
  var result;
  if (arguments.length > 6) {
    result = array[type](rec, init);
  } else {
    result = array[type](rec);
  }
  var calls = rec.record;
  assertEquals(expectedCalls.length, calls.length,
               testName + " (number of calls)");
  for (var i = 0; i < expectedCalls.length; i++) {
    assertEquals(expectedCalls[i], calls[i],
                 testName + " (call " + (i + 1) + ")");
  }
  assertEquals(expectedResult, result, testName + " (result)");
}


function sum(a, b) { return Number(a) + Number(b); }
function prod(a, b) { return Number(a) * Number(b); }
function dec(a, b, i, arr) { return Number(a) + Number(b) * Math.pow(10, arr.length - i - 1); }
function accumulate(acc, elem, i) { acc[i] = elem; return acc; }

// ---- Test Reduce[Left]

var simpleArray = ['2',4,6];
Object.freeze(simpleArray);

testReduce("reduce", "SimpleReduceSum", 12,
           [[0, '2', 0, simpleArray, 2],
            [2, 4, 1, simpleArray, 6],
            [6, 6, 2, simpleArray, 12]],
           simpleArray, sum, 0);

testReduce("reduce", "SimpleReduceProd", 48,
           [[1, '2', 0, simpleArray, 2],
            [2, 4, 1, simpleArray, 8],
            [8, 6, 2, simpleArray, 48]],
           simpleArray, prod, 1);

testReduce("reduce", "SimpleReduceDec", 246,
           [[0, '2', 0, simpleArray, 200],
            [200, 4, 1, simpleArray, 240],
            [240, 6, 2, simpleArray, 246]],
           simpleArray, dec, 0);

testReduce("reduce", "SimpleReduceAccumulate", simpleArray,
           [[[], '2', 0, simpleArray, ['2']],
            [['2'], 4, 1, simpleArray, ['2', 4]],
            [['2', 4], 6, 2, simpleArray, simpleArray]],
           simpleArray, accumulate, []);

var emptyArray = [];
Object.freeze(emptyArray);

testReduce("reduce", "EmptyReduceSum", 0, [], emptyArray, sum, 0);
testReduce("reduce", "EmptyReduceProd", 1, [], emptyArray, prod, 1);
testReduce("reduce", "EmptyReduceDec", 0, [], emptyArray, dec, 0);
testReduce("reduce", "EmptyReduceAccumulate", [], [], emptyArray, accumulate, []);

testReduce("reduce", "EmptyReduceSumNoInit", 0, emptyArray, [0], sum);
testReduce("reduce", "EmptyReduceProdNoInit", 1, emptyArray, [1], prod);
testReduce("reduce", "EmptyReduceDecNoInit", 0, emptyArray, [0], dec);
testReduce("reduce", "EmptyReduceAccumulateNoInit", [], emptyArray, [[]], accumulate);


var simpleSparseArray = [,,,'2',,4,,6,,];
Object.freeze(simpleSparseArray);

testReduce("reduce", "SimpleSparseReduceSum", 12,
           [[0, '2', 3, simpleSparseArray, 2],
            [2, 4, 5, simpleSparseArray, 6],
            [6, 6, 7, simpleSparseArray, 12]],
           simpleSparseArray, sum, 0);

testReduce("reduce", "SimpleSparseReduceProd", 48,
           [[1, '2', 3, simpleSparseArray, 2],
            [2, 4, 5, simpleSparseArray, 8],
            [8, 6, 7, simpleSparseArray, 48]],
           simpleSparseArray, prod, 1);

testReduce("reduce", "SimpleSparseReduceDec", 204060,
           [[0, '2', 3, simpleSparseArray, 200000],
            [200000, 4, 5, simpleSparseArray, 204000],
            [204000, 6, 7, simpleSparseArray, 204060]],
           simpleSparseArray, dec, 0);

testReduce("reduce", "SimpleSparseReduceAccumulate", [,,,'2',,4,,6],
           [[[], '2', 3, simpleSparseArray, [,,,'2']],
            [[,,,'2'], 4, 5, simpleSparseArray, [,,,'2',,4]],
            [[,,,'2',,4], 6, 7, simpleSparseArray, [,,,'2',,4,,6]]],
           simpleSparseArray, accumulate, []);


testReduce("reduce", "EmptySparseReduceSumNoInit", 0, [], [,,0,,], sum);
testReduce("reduce", "EmptySparseReduceProdNoInit", 1, [], [,,1,,], prod);
testReduce("reduce", "EmptySparseReduceDecNoInit", 0, [], [,,0,,], dec);
testReduce("reduce", "EmptySparseReduceAccumulateNoInit",
           [], [], [,,[],,], accumulate);


var verySparseArray = [];
verySparseArray.length = 10000;
verySparseArray[2000] = '2';
verySparseArray[5000] = 4;
verySparseArray[9000] = 6;
var verySparseSlice2 = verySparseArray.slice(0, 2001);
var verySparseSlice4 = verySparseArray.slice(0, 5001);
var verySparseSlice6 = verySparseArray.slice(0, 9001);
Object.freeze(verySparseArray);

testReduce("reduce", "VerySparseReduceSum", 12,
           [[0, '2', 2000, verySparseArray, 2],
            [2, 4, 5000, verySparseArray, 6],
            [6, 6, 9000, verySparseArray, 12]],
           verySparseArray, sum, 0);

testReduce("reduce", "VerySparseReduceProd", 48,
           [[1, '2', 2000, verySparseArray, 2],
            [2, 4, 5000, verySparseArray, 8],
            [8, 6, 9000, verySparseArray, 48]],
           verySparseArray, prod, 1);

testReduce("reduce", "VerySparseReduceDec", Infinity,
           [[0, '2', 2000, verySparseArray, Infinity],
            [Infinity, 4, 5000, verySparseArray, Infinity],
            [Infinity, 6, 9000, verySparseArray, Infinity]],
           verySparseArray, dec, 0);

testReduce("reduce", "VerySparseReduceAccumulate",
           verySparseSlice6,
           [[[], '2', 2000, verySparseArray, verySparseSlice2],
            [verySparseSlice2, 4, 5000, verySparseArray, verySparseSlice4],
            [verySparseSlice4, 6, 9000, verySparseArray, verySparseSlice6]],
           verySparseArray, accumulate, []);


testReduce("reduce", "VerySparseReduceSumNoInit", 12,
           [['2', 4, 5000, verySparseArray, 6],
            [6, 6, 9000, verySparseArray, 12]],
           verySparseArray, sum);

testReduce("reduce", "VerySparseReduceProdNoInit", 48,
           [['2', 4, 5000, verySparseArray, 8],
            [8, 6, 9000, verySparseArray, 48]],
           verySparseArray, prod);

testReduce("reduce", "VerySparseReduceDecNoInit", Infinity,
           [['2', 4, 5000, verySparseArray, Infinity],
            [Infinity, 6, 9000, verySparseArray, Infinity]],
           verySparseArray, dec);

testReduce("reduce", "SimpleSparseReduceAccumulateNoInit",
           '2',
           [['2', 4, 5000, verySparseArray, '2'],
            ['2', 6, 9000, verySparseArray, '2']],
           verySparseArray, accumulate);


// ---- Test ReduceRight

testReduce("reduceRight", "SimpleReduceRightSum", 12,
           [[0, 6, 2, simpleArray, 6],
            [6, 4, 1, simpleArray, 10],
            [10, '2', 0, simpleArray, 12]],
           simpleArray, sum, 0);

testReduce("reduceRight", "SimpleReduceRightProd", 48,
           [[1, 6, 2, simpleArray, 6],
            [6, 4, 1, simpleArray, 24],
            [24, '2', 0, simpleArray, 48]],
           simpleArray, prod, 1);

testReduce("reduceRight", "SimpleReduceRightDec", 246,
           [[0, 6, 2, simpleArray, 6],
            [6, 4, 1, simpleArray, 46],
            [46, '2', 0, simpleArray, 246]],
           simpleArray, dec, 0);

testReduce("reduceRight", "SimpleReduceRightAccumulate", simpleArray,
           [[[], 6, 2, simpleArray, [,,6]],
            [[,,6], 4, 1, simpleArray, [,4,6]],
            [[,4,6], '2', 0, simpleArray, simpleArray]],
           simpleArray, accumulate, []);


testReduce("reduceRight", "EmptyReduceRightSum", 0, [], [], sum, 0);
testReduce("reduceRight", "EmptyReduceRightProd", 1, [], [], prod, 1);
testReduce("reduceRight", "EmptyReduceRightDec", 0, [], [], dec, 0);
testReduce("reduceRight", "EmptyReduceRightAccumulate", [],
           [], [], accumulate, []);

testReduce("reduceRight", "EmptyReduceRightSumNoInit", 0, [], [0], sum);
testReduce("reduceRight", "EmptyReduceRightProdNoInit", 1, [], [1], prod);
testReduce("reduceRight", "EmptyReduceRightDecNoInit", 0, [], [0], dec);
testReduce("reduceRight", "EmptyReduceRightAccumulateNoInit",
           [], [], [[]], accumulate);


testReduce("reduceRight", "SimpleSparseReduceRightSum", 12,
           [[0, 6, 7, simpleSparseArray, 6],
            [6, 4, 5, simpleSparseArray, 10],
            [10, '2', 3, simpleSparseArray, 12]],
           simpleSparseArray, sum, 0);

testReduce("reduceRight", "SimpleSparseReduceRightProd", 48,
           [[1, 6, 7, simpleSparseArray, 6],
            [6, 4, 5, simpleSparseArray, 24],
            [24, '2', 3, simpleSparseArray, 48]],
           simpleSparseArray, prod, 1);

testReduce("reduceRight", "SimpleSparseReduceRightDec", 204060,
           [[0, 6, 7, simpleSparseArray, 60],
            [60, 4, 5, simpleSparseArray, 4060],
            [4060, '2', 3, simpleSparseArray, 204060]],
           simpleSparseArray, dec, 0);

testReduce("reduceRight", "SimpleSparseReduceRightAccumulate", [,,,'2',,4,,6],
           [[[], 6, 7, simpleSparseArray, [,,,,,,,6]],
            [[,,,,,,,6], 4, 5, simpleSparseArray, [,,,,,4,,6]],
            [[,,,,,4,,6], '2', 3, simpleSparseArray, [,,,'2',,4,,6]]],
           simpleSparseArray, accumulate, []);


testReduce("reduceRight", "EmptySparseReduceRightSumNoInit",
           0, [], [,,0,,], sum);
testReduce("reduceRight", "EmptySparseReduceRightProdNoInit",
           1, [], [,,1,,], prod);
testReduce("reduceRight", "EmptySparseReduceRightDecNoInit",
           0, [], [,,0,,], dec);
testReduce("reduceRight", "EmptySparseReduceRightAccumulateNoInit",
           [], [], [,,[],,], accumulate);


var verySparseSuffix6 = [];
verySparseSuffix6[9000] = 6;
var verySparseSuffix4 = [];
verySparseSuffix4[5000] = 4;
verySparseSuffix4[9000] = 6;
var verySparseSuffix2 = verySparseSlice6;


testReduce("reduceRight", "VerySparseReduceRightSum", 12,
           [[0, 6, 9000, verySparseArray, 6],
            [6, 4, 5000, verySparseArray, 10],
            [10, '2', 2000, verySparseArray, 12]],
           verySparseArray, sum, 0);

testReduce("reduceRight", "VerySparseReduceRightProd", 48,
           [[1, 6, 9000, verySparseArray, 6],
            [6, 4, 5000, verySparseArray, 24],
            [24, '2', 2000, verySparseArray, 48]],
           verySparseArray, prod, 1);

testReduce("reduceRight", "VerySparseReduceRightDec", Infinity,
           [[0, 6, 9000, verySparseArray, Infinity],
            [Infinity, 4, 5000, verySparseArray, Infinity],
            [Infinity, '2', 2000, verySparseArray, Infinity]],
           verySparseArray, dec, 0);

testReduce("reduceRight", "VerySparseReduceRightAccumulate",
           verySparseSuffix2,
           [[[], 6, 9000, verySparseArray, verySparseSuffix6],
            [verySparseSuffix6, 4, 5000, verySparseArray, verySparseSuffix4],
            [verySparseSuffix4, '2', 2000, verySparseArray, verySparseSuffix2]],
           verySparseArray, accumulate, []);


testReduce("reduceRight", "VerySparseReduceRightSumNoInit", 12,
           [[6, 4, 5000, verySparseArray, 10],
            [10, '2', 2000, verySparseArray, 12]],
           verySparseArray, sum);

testReduce("reduceRight", "VerySparseReduceRightProdNoInit", 48,
           [[6, 4, 5000, verySparseArray, 24],
            [24, '2', 2000, verySparseArray, 48]],
           verySparseArray, prod);

testReduce("reduceRight", "VerySparseReduceRightDecNoInit", Infinity,
           [[6, 4, 5000, verySparseArray, Infinity],
            [Infinity, '2', 2000, verySparseArray, Infinity]],
           verySparseArray, dec);

testReduce("reduceRight", "SimpleSparseReduceRightAccumulateNoInit",
           6,
           [[6, 4, 5000, verySparseArray, 6],
            [6, '2', 2000, verySparseArray, 6]],
           verySparseArray, accumulate);


// undefined is an element
var undefArray = [,,undefined,,undefined,,];
Object.freeze(undefArray);

testReduce("reduce", "SparseUndefinedReduceAdd", NaN,
           [[0, undefined, 2, undefArray, NaN],
            [NaN, undefined, 4, undefArray, NaN],
           ],
           undefArray, sum, 0);

testReduce("reduceRight", "SparseUndefinedReduceRightAdd", NaN,
           [[0, undefined, 4, undefArray, NaN],
            [NaN, undefined, 2, undefArray, NaN],
           ], undefArray, sum, 0);

testReduce("reduce", "SparseUndefinedReduceAddNoInit", NaN,
           [[undefined, undefined, 4, undefArray, NaN],
           ], undefArray, sum);

testReduce("reduceRight", "SparseUndefinedReduceRightAddNoInit", NaN,
           [[undefined, undefined, 2, undefArray, NaN],
           ], undefArray, sum);


// Ignore non-array properties:

var arrayPlus = [1,'2',,3];
arrayPlus[-1] = NaN;
arrayPlus[Math.pow(2,32)] = NaN;
arrayPlus[NaN] = NaN;
arrayPlus["00"] = NaN;
arrayPlus["02"] = NaN;
arrayPlus["-0"] = NaN;
Object.freeze(arrayPlus);

testReduce("reduce", "ArrayWithNonElementPropertiesReduce", 6,
           [[0, 1, 0, arrayPlus, 1],
            [1, '2', 1, arrayPlus, 3],
            [3, 3, 3, arrayPlus, 6],
           ], arrayPlus, sum, 0);

testReduce("reduceRight", "ArrayWithNonElementPropertiesReduceRight", 6,
           [[0, 3, 3, arrayPlus, 3],
            [3, '2', 1, arrayPlus, 5],
            [5, 1, 0, arrayPlus, 6],
           ], arrayPlus, sum, 0);

// Test passing undefined as initial value (to test missing parameter
// detection).
Object.freeze(['1']).reduce((a, b) => { assertEquals(a, undefined); assertEquals(b, '1') },
           undefined);
Object.freeze(['1', 2]).reduce((a, b) => { assertEquals(a, '1'); assertEquals(b, 2); });
Object.freeze(['1']).reduce((a, b) => { assertTrue(false); });

// Test error conditions:

var exception = false;
try {
  Object.freeze(['1']).reduce("not a function");
} catch (e) {
  exception = true;
  assertTrue(e instanceof TypeError,
             "reduce callback not a function not throwing TypeError");
  assertTrue(e.message.indexOf(" is not a function") >= 0,
             "reduce non function TypeError type");
}
assertTrue(exception);

exception = false;
try {
  Object.freeze(['1']).reduceRight("not a function");
} catch (e) {
  exception = true;
  assertTrue(e instanceof TypeError,
             "reduceRight callback not a function not throwing TypeError");
  assertTrue(e.message.indexOf(" is not a function") >= 0,
             "reduceRight non function TypeError type");
}
assertTrue(exception);

exception = false;
try {
  Object.freeze([]).reduce(sum);
} catch (e) {
  exception = true;
  assertTrue(e instanceof TypeError,
             "reduce no initial value not throwing TypeError");
  assertEquals("Reduce of empty array with no initial value", e.message,
               "reduce no initial TypeError type");
}
assertTrue(exception);

exception = false;
try {
  Object.freeze([]).reduceRight(sum);
} catch (e) {
  exception = true;
  assertTrue(e instanceof TypeError,
             "reduceRight no initial value not throwing TypeError");
  assertEquals("Reduce of empty array with no initial value", e.message,
               "reduceRight no initial TypeError type");
}
assertTrue(exception);

exception = false;
try {
  Object.freeze([,,,]).reduce(sum);
} catch (e) {
  exception = true;
  assertTrue(e instanceof TypeError,
             "reduce sparse no initial value not throwing TypeError");
  assertEquals("Reduce of empty array with no initial value", e.message,
               "reduce no initial TypeError type");
}
assertTrue(exception);

exception = false;
try {
  Object.freeze([,,,]).reduceRight(sum);
} catch (e) {
  exception = true;
  assertTrue(e instanceof TypeError,
             "reduceRight sparse no initial value not throwing TypeError");
  assertEquals("Reduce of empty array with no initial value", e.message,
               "reduceRight no initial TypeError type");
}
assertTrue(exception);


// Array changing length

function extender(a, b, i, s) {
  s[s.length] = s.length;
  return Number(a) + Number(b);
}

var arr = [1, '2', 3, 4];
Object.freeze(arr);
testReduce("reduce", "ArrayManipulationExtender", 10,
           [[0, 1, 0, [1, '2', 3, 4], 1],
            [1, '2', 1, [1, '2', 3, 4], 3],
            [3, 3, 2, [1, '2', 3, 4], 6],
            [6, 4, 3, [1, '2', 3, 4], 10],
           ], arr, extender, 0);

var arr = [];
Object.defineProperty(arr, "0", { get: function() { delete this[0] },
  configurable: true });
assertEquals(undefined, Object.freeze(arr).reduce(function(val) { return val }));

var arr = [];
Object.defineProperty(arr, "0", { get: function() { delete this[0] },
  configurable: true});
assertEquals(undefined, Object.freeze(arr).reduceRight(function(val) { return val }));


(function ReduceRightMaxIndex() {
  const kMaxIndex = 0xffffffff-1;
  let array = [];
  array[kMaxIndex-2] = 'value-2';
  array[kMaxIndex-1] = 'value-1';
  // Use the maximum array index possible.
  array[kMaxIndex] = 'value';
  // Add the next index which is a normal property and thus will not show up.
  array[kMaxIndex+1] = 'normal property';
  assertThrowsEquals( () => {
      Object.freeze(array).reduceRight((sum, value) => {
        assertEquals('initial', sum);
        assertEquals('value', value);
        // Throw at this point as we would very slowly loop down from kMaxIndex.
        throw 'do not continue';
      }, 'initial')
  }, 'do not continue');
})();

(function OptimizedReduce() {
  let f = (a,current) => a + Number(current);
  let g = function(a) {
    return a.reduce(f);
  };
  %PrepareFunctionForOptimization(g);
  let a = [1,'2',3,4,5,6,7,8,9,10];
  Object.freeze(a);
  g(a); g(a);
  let total = g(a);
  %OptimizeFunctionOnNextCall(g);
  assertEquals(total, g(a));
  assertOptimized(g);
})();

(function OptimizedReduceEmpty() {
  let f = (a,current) => a + Number(current);
  let g = function(a) {
    return a.reduce(f);
  };
  %PrepareFunctionForOptimization(g);
  let a = [1,'2',3,4,5,6,7,8,9,10];
  Object.freeze(a);
  g(a); g(a); g(a);
  %OptimizeFunctionOnNextCall(g);
  g(a);
  assertOptimized(g);
  assertThrows(() => g([]));
  assertUnoptimized(g);
})();

(function OptimizedReduceLazyDeopt() {
  let deopt = false;
  let f = (a,current) => { if (deopt) %DeoptimizeNow(); return a + Number(current); };
  let g = function(a) {
    return a.reduce(f);
  };
  %PrepareFunctionForOptimization(g);
  let a = [1,'2',3,4,5,6,7,8,9,10];
  Object.freeze(a);
  g(a); g(a);
  let total = g(a);
  %OptimizeFunctionOnNextCall(g);
  g(a);
  assertOptimized(g);
  deopt = true;
  assertEquals(total, g(a));
  assertOptimized(g);
})();

(function OptimizedReduceLazyDeoptMiddleOfIteration() {
  let deopt = false;
  let f = (a,current) => {
    if (current == 6 && deopt) %DeoptimizeNow();
    return a + Number(current);
  };
  let g = function(a) {
    return a.reduce(f);
  };
  %PrepareFunctionForOptimization(g);
  let a = [11,'22',33,45,56,6,77,84,93,101];
  Object.freeze(a);
  g(a); g(a);
  let total = g(a);
  %OptimizeFunctionOnNextCall(g);
  g(a);
  assertOptimized(g);
  deopt = true;
  assertEquals(total, g(a));
  assertOptimized(g);
})();

(function OptimizedReduceEagerDeoptMiddleOfIteration() {
  let deopt = false;
  let array = [11,'22',33,45,56,6,77,84,93,101];
  Object.freeze(array);
  let f = (a,current) => {
    if (current == 6 && deopt) {array[0] = 1.5; }
    return a + Number(current);
  };
  let g = function() {
    return array.reduce(f);
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertOptimized(g);
  deopt = true;
  g();
  %PrepareFunctionForOptimization(g);
  assertOptimized(g);
  deopt = false;
  array = [11,'22',33,45,56,6,77,84,93,101];
  Object.freeze(array);
  %OptimizeFunctionOnNextCall(g);
  g();
  assertOptimized(g);
  deopt = true;
  assertEquals(total, g());
  assertOptimized(g);
})();

(function OptimizedReduceEagerDeoptMiddleOfIterationHoley() {
  let deopt = false;
  let array = [, ,11,'22',,33,45,56,,6,77,84,93,101,];
  Object.freeze(array);
  let f = (a,current) => {
    if (current == 6 && deopt) {array[0] = 1.5; }
    return a + Number(current);
  };
  let g = function() {
    return array.reduce(f);
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertOptimized(g);
  deopt = true;
  g();
  %PrepareFunctionForOptimization(g);
  assertOptimized(g);
  deopt = false;
  array = [11,'22',33,45,56,6,77,84,93,101];
  Object.freeze(array);
  %OptimizeFunctionOnNextCall(g);
  g();
  assertUnoptimized(g);
  deopt = true;
  assertEquals(total, g());
  assertUnoptimized(g);
})();

(function TriggerReduceRightPreLoopDeopt() {
  function f(a) {
    a.reduceRight((x) => { return Number(x) + 1 });
  };
  %PrepareFunctionForOptimization(f);
  var arr = Object.freeze([1, '2', ]);
  f(arr);
  f(arr);
  %OptimizeFunctionOnNextCall(f);
  assertThrows(() => f([]), TypeError);
  assertUnoptimized(f);
})();

(function OptimizedReduceRightEagerDeoptMiddleOfIterationHoley() {
  let deopt = false;
  let array = [, ,11,'22',,33,45,56,,6,77,84,93,101,];
  Object.freeze(array);
  let f = (a,current) => {
    if (current == 6 && deopt) {array[array.length-1] = 1.5; }
    return a + Number(current);
  };
  let g = function() {
    return array.reduceRight(f);
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertOptimized(g);
  deopt = true;
  g();
  %PrepareFunctionForOptimization(g);
  assertOptimized(g);
  deopt = false;
  array = [11,'22',33,45,56,6,77,84,93,101];
  Object.freeze(array);
  %OptimizeFunctionOnNextCall(g);
  g();
  assertUnoptimized(g);
  deopt = true;
  assertEquals(total, g());
  assertUnoptimized(g);
})();

(function ReduceCatch() {
  let f = (a,current) => {
    return a + current;
  };
  let g = function() {
    try {
      return Object.freeze(array).reduce(f);
    } catch (e) {
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  var turbofan = willBeTurbofanned(g);
  g();
  g();
  assertEquals(total, g());
  if (turbofan) assertOptimized(g);
})();

(function ReduceThrow() {
  let done = false;
  let f = (a, current) => {
    if (done) throw "x";
    return a + Number(current);
  };
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduce(f);
    } catch (e) {
      return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceThrow() {
  let done = false;
  let f = (a, current) => {
    if (done) throw "x";
    return a + Number(current);
  };
  %NeverOptimizeFunction(f);
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduce(f);
    } catch (e) {
      return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceFinally() {
  let done = false;
  let f = (a, current) => {
    if (done) throw "x";
    return a + Number(current);
  };
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduce(f);
    } catch (e) {
    } finally {
      if (done) return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceFinallyNoInline() {
  let done = false;
  let f = (a, current) => {
    if (done) throw "x";
    return a + Number(current);
  };
  %NeverOptimizeFunction(f);
  let array = [1, '2', 3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduce(f);
    } catch (e) {
    } finally {
      if (done) return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceNonCallableOpt() {
  let done = false;
  let f = (a, current) => {
    return a + Number(current);
  };
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    return array.reduce(f);
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g(); g();
  assertEquals(6, g());
  assertOptimized(g);
  f = null;
  assertThrows(() => g());
  assertOptimized(g);
})();

(function ReduceCatchInlineDeopt() {
  let done = false;
  let f = (a, current) => {
    if (done) {
      %DeoptimizeNow();
      throw "x";
    }
    return a + Number(current);
  };
  let array = [1,2,3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduce(f);
    } catch (e) {
      if (done) return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceFinallyInlineDeopt() {
  let done = false;
  let f = (a, current) => {
    if (done) {
      %DeoptimizeNow();
      throw "x";
    }
    return a + Number(current);
  };
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduce(f);
    } catch (e) {
    } finally {
      if (done) return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function OptimizedReduceRight() {
  let count = 0;
  let f = (a,current,i) => a + Number(current) * ++count;
  let g = function(a) {
    count = 0;
    return a.reduceRight(f);
  };
  %PrepareFunctionForOptimization(g);
  let a = [1,'2',3,4,5,6,7,8,9,10];
  Object.freeze(a);
  g(a); g(a);
  let total = g(a);
  %OptimizeFunctionOnNextCall(g);
  assertEquals(total, g(a));
  assertOptimized(g);
})();

(function OptimizedReduceEmpty() {
  let count = 0;
  let f = (a,current,i) => a + Number(current) * ++count;
  let g = function(a) {
    count = 0;
    return a.reduceRight(f);
  };
  %PrepareFunctionForOptimization(g);
  let a = [1,'2',3,4,5,6,7,8,9,10];
  Object.freeze(a);
  g(a); g(a); g(a);
  %OptimizeFunctionOnNextCall(g);
  g(a);
  assertOptimized(g);
  assertThrows(() => g([]));
  assertUnoptimized(g);
})();

(function OptimizedReduceLazyDeopt() {
  let deopt = false;
  let f = (a,current) => { if (deopt) %DeoptimizeNow(); return a + Number(current); };
  let g = function(a) {
    return a.reduceRight(f);
  };
  %PrepareFunctionForOptimization(g);
  let a = [1,'2',3,4,5,6,7,8,9,10];
  Object.freeze(a);
  g(a); g(a);
  let total = g(a);
  %OptimizeFunctionOnNextCall(g);
  g(a);
  deopt = true;
  assertEquals(total, g(a));
  assertOptimized(g);
})();

(function OptimizedReduceLazyDeoptMiddleOfIteration() {
  let deopt = false;
  let f = (a,current) => {
    if (current == 6 && deopt) %DeoptimizeNow();
    return a + Number(current);
  };
  let g = function(a) {
    return a.reduceRight(f);
  };
  %PrepareFunctionForOptimization(g);
  let a = [11,'22',33,45,56,6,77,84,93,101];
  Object.freeze(a);
  g(a); g(a);
  let total = g(a);
  %OptimizeFunctionOnNextCall(g);
  g(a);
  deopt = true;
  assertEquals(total, g(a));
  assertOptimized(g);
})();

(function OptimizedReduceEagerDeoptMiddleOfIteration() {
  let deopt = false;
  let array = [11,'22',33,45,56,6,77,84,93,101];
  Object.freeze(array);
  let f = (a,current) => {
    if (current == 6 && deopt) {array[9] = 1.5; }
    return a + Number(current);
  };
  let g = function() {
    return array.reduceRight(f);
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertOptimized(g);
  deopt = true;
  g();
  %PrepareFunctionForOptimization(g);
  deopt = false;
  array = [11,'22',33,45,56,6,77,84,93,101];
  Object.freeze(array);
  %OptimizeFunctionOnNextCall(g);
  g();
  deopt = true;
  assertEquals(total, g());
  assertOptimized(g);
})();

(function ReduceCatch() {
  let f = (a,current) => {
    return a + Number(current);
  };
  let g = function() {
    try {
      return Object.freeze(array).reduceRight(f);
    } catch (e) {
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  var turbofan = willBeTurbofanned(g);
  g();
  g();
  assertEquals(total, g());
  if (turbofan) assertOptimized(g);
})();

(function ReduceThrow() {
  let done = false;
  let f = (a, current) => {
    if (done) throw "x";
    return a + Number(current);
  };
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduceRight(f);
    } catch (e) {
      return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  assertOptimized(g);
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceThrow() {
  let done = false;
  let f = (a, current) => {
    if (done) throw "x";
    return a + Number(current);
  };
  %NeverOptimizeFunction(f);
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduceRight(f);
    } catch (e) {
      return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceFinally() {
  let done = false;
  let f = (a, current) => {
    if (done) throw "x";
    return a + Number(current);
  };
  let array = [1, '2', 3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduceRight(f);
    } catch (e) {
    } finally {
      if (done) return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceFinallyNoInline() {
  let done = false;
  let f = (a, current) => {
    if (done) throw "x";
    return a + Number(current);
  };
  %NeverOptimizeFunction(f);
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduceRight(f);
    } catch (e) {
    } finally {
      if (done) return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  assertOptimized(g);
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceNonCallableOpt() {
  let done = false;
  let f = (a, current) => {
    return a + Number(current);
  };
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    return array.reduceRight(f);
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g(); g();
  assertEquals(6, g());
  f = null;
  assertThrows(() => g());
  assertOptimized(g);
})();

(function ReduceCatchInlineDeopt() {
  let done = false;
  let f = (a, current) => {
    if (done) {
      %DeoptimizeNow();
      throw "x";
    }
    return a + Number(current);
  };
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduceRight(f);
    } catch (e) {
      if (done) return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceFinallyInlineDeopt() {
  let done = false;
  let f = (a, current) => {
    if (done) {
      %DeoptimizeNow();
      throw "x";
    }
    return a + Number(current);
  };
  let array = [1,'2',3];
  Object.freeze(array);
  let g = function() {
    try {
      return array.reduceRight(f);
    } catch (e) {
    } finally {
      if (done) return null;
    }
  };
  %PrepareFunctionForOptimization(g);
  g(); g();
  let total = g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
  done = false;
  %PrepareFunctionForOptimization(g);
  g(); g();
  %OptimizeFunctionOnNextCall(g);
  g();
  assertEquals(6, g());
  done = true;
  assertEquals(null, g());
})();

(function ReduceHoleyArrayWithDefaultAccumulator() {
  var holey = new Array(10);
  Object.freeze(holey);
  function reduce(a) {
    let callback = function(accumulator, currentValue) {
      return currentValue;
    };
    return a.reduce(callback, 13);
  };
  %PrepareFunctionForOptimization(reduce);
  assertEquals(13, reduce(holey));
  assertEquals(13, reduce(holey));
  assertEquals(13, reduce(holey));
  %OptimizeFunctionOnNextCall(reduce);
  assertEquals(13, reduce(holey));
  assertOptimized(reduce);
})();

(function ReduceRightHoleyArrayWithDefaultAccumulator() {
  var holey = new Array(10);
  Object.freeze(holey);
  function reduce(a) {
    let callback = function(accumulator, currentValue) {
      return currentValue;
    };
    return a.reduceRight(callback, 13);
  };
  %PrepareFunctionForOptimization(reduce);
  assertEquals(13, reduce(holey));
  assertEquals(13, reduce(holey));
  assertEquals(13, reduce(holey));
  %OptimizeFunctionOnNextCall(reduce);
  assertEquals(13, reduce(holey));
  assertOptimized(reduce);
})();

(function ReduceHoleyArrayOneElementWithDefaultAccumulator() {
  var holey = new Array(10);
  holey[1] = '5';
  Object.freeze(holey);
  function reduce(a) {
    let callback = function(accumulator, currentValue) {
      return Number(currentValue) + accumulator;
    };
    return a.reduce(callback, 13);
  };
  %PrepareFunctionForOptimization(reduce);
  assertEquals(18, reduce(holey));
  assertEquals(18, reduce(holey));
  assertEquals(18, reduce(holey));
  %OptimizeFunctionOnNextCall(reduce);
  assertEquals(18, reduce(holey));
  assertOptimized(reduce);
})();

(function ReduceRightHoleyArrayOneElementWithDefaultAccumulator() {
  var holey = new Array(10);
  holey[1] = '5';
  Object.freeze(holey);
  function reduce(a) {
    let callback = function(accumulator, currentValue) {
      return Number(currentValue) + accumulator;
    };
    return a.reduceRight(callback, 13);
  };
  %PrepareFunctionForOptimization(reduce);
  assertEquals(18, reduce(holey));
  assertEquals(18, reduce(holey));
  assertEquals(18, reduce(holey));
  %OptimizeFunctionOnNextCall(reduce);
  assertEquals(18, reduce(holey));
  assertOptimized(reduce);
})();

(function ReduceMixedHoleyArrays() {
  function r(a) {
    return a.reduce((acc, i) => {acc[0]});
  };
  %PrepareFunctionForOptimization(r);
  assertEquals(r(Object.freeze([[0]])), [0]);
  assertEquals(r(Object.freeze([[0]])), [0]);
  assertEquals(r(Object.freeze([0,,])), 0);
  %OptimizeFunctionOnNextCall(r);
  assertEquals(r(Object.freeze([,0,0])), undefined);
  assertOptimized(r);
})();
