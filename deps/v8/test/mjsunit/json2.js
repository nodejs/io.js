// Copyright 2012 the V8 project authors. All rights reserved.
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

// Flags: --allow-natives-syntax --expose-externalize-string

// Test JSON.stringify on the global object.
var a = 12345;
assertTrue(JSON.stringify(this).indexOf('"a":12345') > 0);
assertTrue(JSON.stringify(this, null, 0).indexOf('"a":12345') > 0);

// Test JSON.stringify of array in dictionary mode.
function TestStringify(expected, input) {
  assertEquals(expected, JSON.stringify(input));
  assertEquals(expected, JSON.stringify(input, (key, value) => value));
  assertEquals(JSON.stringify(input, null, "="),
               JSON.stringify(input, (key, value) => value, "="));
}

var array_1 = [];
var array_2 = [];
array_1[1<<17] = 1;
array_2[1<<17] = function() { return 1; };
var nulls = "null,";
for (var i = 0; i < 17; i++) {
  nulls += nulls;
}

expected_1 = '[' + nulls + '1]';
expected_2 = '[' + nulls + 'null]';
TestStringify(expected_1, array_1);
TestStringify(expected_2, array_2);

// Test JSPrimitiveWrapper with custom prototype.
var num_wrapper = Object(42);
num_wrapper.__proto__ = { __proto__: null,
                          toString: function() { return true; } };
TestStringify('1', num_wrapper);

var str_wrapper = Object('2');
str_wrapper.__proto__ = { __proto__: null,
                          toString: function() { return true; } };
TestStringify('"true"', str_wrapper);

var bool_wrapper = Object(false);
bool_wrapper.__proto__ = { __proto__: null,
                           toString: function() { return true; } };
// Note that toString function is not evaluated here!
TestStringify('false', bool_wrapper);

// Test getters.
var counter = 0;
var getter_obj = { get getter() {
                         counter++;
                         return 123;
                       } };
TestStringify('{"getter":123}', getter_obj);
assertEquals(4, counter);

// Test toJSON function.
var tojson_obj = { toJSON: function() {
                             counter++;
                             return [1, 2];
                           },
                   a: 1};
TestStringify('[1,2]', tojson_obj);
assertEquals(8, counter);

// Test that we don't recursively look for the toJSON function.
var tojson_proto_obj = { a: 'fail' };
tojson_proto_obj.__proto__ = { toJSON: function() {
                                         counter++;
                                         return tojson_obj;
                                       } };
TestStringify('{"a":1}', tojson_proto_obj);

// Test toJSON produced by a getter.
var tojson_via_getter = { get toJSON() {
                                return function(x) {
                                         counter++;
                                         return 321;
                                       };
                              },
                          a: 1 };
TestStringify('321', tojson_via_getter);

assertThrows(function() {
  JSON.stringify({ get toJSON() { throw "error"; } });
});

// Test toJSON with key.
tojson_obj = { toJSON: function(key) { return key + key; } };
var tojson_with_key_1 = { a: tojson_obj, b: tojson_obj };
TestStringify('{"a":"aa","b":"bb"}', tojson_with_key_1);
var tojson_with_key_2 = [ tojson_obj, tojson_obj ];
TestStringify('["00","11"]', tojson_with_key_2);

// Test toJSON with exception.
var tojson_ex = { toJSON: function(key) { throw "123" } };
assertThrows(function() { JSON.stringify(tojson_ex); });
assertThrows(function() { JSON.stringify(tojson_ex, null, 0); });

// Test toJSON with access to this.
var obj = { toJSON: function(key) { return this.a + key; }, a: "x" };
TestStringify('{"y":"xy"}', {y: obj});

// Test invalid string length exception.
assertThrows(function() {
  JSON.stringify('a'.repeat(%StringMaxLength() - 1));
});

// Test holes in arrays.
var fast_smi = [1, 2, 3, 4];
fast_smi.__proto__ = [7, 7, 7, 7];
delete fast_smi[2];
assertTrue(%HasSmiElements(fast_smi));
TestStringify("[1,2,7,4]", fast_smi);

var fast_double = [1.1, 2, 3, 4];
fast_double.__proto__ = [7, 7, 7, 7];

delete fast_double[2];
assertTrue(%HasDoubleElements(fast_double));
TestStringify("[1.1,2,7,4]", fast_double);

var fast_obj = [1, 2, {}, {}];
fast_obj.__proto__ = [7, 7, 7, 7];

delete fast_obj[2];
assertTrue(%HasObjectElements(fast_obj));
TestStringify("[1,2,7,{}]", fast_obj);

var getter_side_effect = { a: 1,
                           get b() {
                             delete this.a;
                             delete this.c;
                             this.e = 5;
                             return 2;
                           },
                           c: 3,
                           d: 4 };
assertEquals('{"a":1,"b":2,"d":4}', JSON.stringify(getter_side_effect));
assertEquals('{"b":2,"d":4,"e":5}', JSON.stringify(getter_side_effect));

getter_side_effect = { a: 1,
    get b() {
      delete this.a;
      delete this.c;
      this.e = 5;
      return 2;
    },
    c: 3,
    d: 4 };
assertEquals('{"a":1,"b":2,"d":4}',
             JSON.stringify(getter_side_effect, null, 0));
assertEquals('{"b":2,"d":4,"e":5}',
             JSON.stringify(getter_side_effect, null, 0));

var non_enum = {};
non_enum.a = 1;
Object.defineProperty(non_enum, "b", { value: 2, enumerable: false });
non_enum.c = 3;
TestStringify('{"a":1,"c":3}', non_enum);

var str = createExternalizableString('external');
try {
  externalizeString(str);
} catch (e) { }
TestStringify("\"external\"", str, null, 0);

var o = {};
o.somespecialproperty = 10;
o["\x19"] = 10;
assertThrows("JSON.parse('{\"somespecialproperty\":100, \"\x19\":10}')");

let exception_count = 0;
function foo(v) {
  try {
    v["set-i32"];
  } catch (e) {
    exception_count++;
  }
  try {
    JSON.stringify(v);
  } catch (e) {}
}
let obj1 = Object('2');
obj1.__proto__ = { toString: function () {} };
Object.defineProperty(obj1, "toString", {value: foo});
%EnsureFeedbackVectorForFunction(foo);
foo(obj1);
assertEquals(1, exception_count);
foo({obj1, b: { toJSON: function () {} }});
assertEquals(2, exception_count);
