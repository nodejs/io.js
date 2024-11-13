// Copyright 2008 the V8 project authors. All rights reserved.
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

// Flags: --allow-natives-syntax

// Test date construction from other dates.
var date0 = new Date(1111);
var date1 = new Date(date0);
assertEquals(1111, date0.getTime());
assertEquals(date0.getTime(), date1.getTime());
var date2 = new Date(date0.toString());
assertEquals(1000, date2.getTime());

// Test that dates may contain commas.
var date0 = Date.parse("Dec 25 1995 1:30");
var date1 = Date.parse("Dec 25, 1995 1:30");
var date2 = Date.parse("Dec 25 1995, 1:30");
var date3 = Date.parse("Dec 25, 1995, 1:30");
assertEquals(date0, date1);
assertEquals(date1, date2);
assertEquals(date2, date3);

// Test limits (+/-1e8 days from epoch)

var dMax = new Date(8.64e15);
assertEquals(8.64e15, dMax.getTime());
assertEquals(275760, dMax.getFullYear());
assertEquals(8, dMax.getMonth());
assertEquals(13, dMax.getUTCDate());

var dOverflow = new Date(8.64e15+1);
assertTrue(isNaN(dOverflow.getTime()));

var dMin = new Date(-8.64e15);
assertEquals(-8.64e15, dMin.getTime());
assertEquals(-271821, dMin.getFullYear());
assertEquals(3, dMin.getMonth());
assertEquals(20, dMin.getUTCDate());

var dUnderflow = new Date(-8.64e15-1);
assertTrue(isNaN(dUnderflow.getTime()));


// Tests inspired by js1_5/Date/regress-346363.js

// Year
var a = new Date();
a.setFullYear();
a.setFullYear(2006);
assertEquals(2006, a.getFullYear());

var b = new Date();
b.setUTCFullYear();
b.setUTCFullYear(2006);
assertEquals(2006, b.getUTCFullYear());

// Month
var c = new Date();
c.setMonth();
c.setMonth(2);
assertTrue(isNaN(c.getMonth()));

var d = new Date();
d.setUTCMonth();
d.setUTCMonth(2);
assertTrue(isNaN(d.getUTCMonth()));

// Date
var e = new Date();
e.setDate();
e.setDate(2);
assertTrue(isNaN(e.getDate()));

var f = new Date();
f.setUTCDate();
f.setUTCDate(2);
assertTrue(isNaN(f.getUTCDate()));

// Hours
var g = new Date();
g.setHours();
g.setHours(2);
assertTrue(isNaN(g.getHours()));

var h = new Date();
h.setUTCHours();
h.setUTCHours(2);
assertTrue(isNaN(h.getUTCHours()));

// Minutes
var g = new Date();
g.setMinutes();
g.setMinutes(2);
assertTrue(isNaN(g.getMinutes()));

var h = new Date();
h.setUTCHours();
h.setUTCHours(2);
assertTrue(isNaN(h.getUTCHours()));


// Seconds
var i = new Date();
i.setSeconds();
i.setSeconds(2);
assertTrue(isNaN(i.getSeconds()));

var j = new Date();
j.setUTCSeconds();
j.setUTCSeconds(2);
assertTrue(isNaN(j.getUTCSeconds()));


// Milliseconds
var k = new Date();
k.setMilliseconds();
k.setMilliseconds(2);
assertTrue(isNaN(k.getMilliseconds()));

var l = new Date();
l.setUTCMilliseconds();
l.setUTCMilliseconds(2);
assertTrue(isNaN(l.getUTCMilliseconds()));

// Test that -0 is treated correctly in MakeDay.
var d = new Date();
assertDoesNotThrow("d.setDate(-0)");
assertDoesNotThrow("new Date(-0, -0, -0, -0, -0, -0, -0)");
assertDoesNotThrow("new Date(0x40000000, 0x40000000, 0x40000000," +
                   "0x40000000, 0x40000000, 0x40000000, 0x40000000)")
assertDoesNotThrow("new Date(-0x40000001, -0x40000001, -0x40000001," +
                   "-0x40000001, -0x40000001, -0x40000001, -0x40000001)")

// Test that date as double type is treated as integer type in MakeDay
// so that the hour should't be changed.
d = new Date(2018, 0);
assertEquals(Date.parse(new Date(2018, 0, 11)), d.setDate(11.2));
assertEquals(0, d.getHours());

// Modified test from WebKit
// LayoutTests/fast/js/script-tests/date-utc-timeclip.js:

assertEquals(8639999999999999, Date.UTC(275760, 8, 12, 23, 59, 59, 999));
assertEquals(8640000000000000, Date.UTC(275760, 8, 13));
assertTrue(isNaN(Date.UTC(275760, 8, 13, 0, 0, 0, 1)));
assertTrue(isNaN(Date.UTC(275760, 8, 14)));

assertEquals(Date.UTC(-271821, 3, 20, 0, 0, 0, 1), -8639999999999999);
assertEquals(Date.UTC(-271821, 3, 20), -8640000000000000);
assertTrue(isNaN(Date.UTC(-271821, 3, 19, 23, 59, 59, 999)));
assertTrue(isNaN(Date.UTC(-271821, 3, 19)));


// Test creation with large date values.
d = new Date(1969, 12, 1, 99999999999);
assertTrue(isNaN(d.getTime()));
d = new Date(1969, 12, 1, -99999999999);
assertTrue(isNaN(d.getTime()));
d = new Date(1969, 12, 1, Infinity);
assertTrue(isNaN(d.getTime()));
d = new Date(1969, 12, 1, -Infinity);
assertTrue(isNaN(d.getTime()));
d = new Date(1969, 12, 1, 0);
d.setTime(Math.pow(2, 64));
assertTrue(isNaN(d.getTime()));
d = new Date(1969, 12, 1, 0);
d.setTime(Math.pow(-2, 64));
assertTrue(isNaN(d.getTime()));


// Test setting with non-integer values, including the return value of setTime.
d = new Date();
assertEquals(42, d.setTime(42.5));
assertEquals(42, d.getTime());
assertEquals(-42, d.setTime(-42.5));
assertEquals(-42, d.getTime());
assertEquals(0, d.setTime(-0));
assertEquals(0, d.getTime());


// Test creation with obscure date values.
assertEquals(8640000000000000, Date.UTC(1970, 0, 1 + 100000001, -24));
assertEquals(-8640000000000000, Date.UTC(1970, 0, 1 - 100000001, 24));


// Parsing ES5 ISO-8601 dates.
// When TZ is omitted, it defaults to the local timezone if there is
// no time, and to UTC if a time is provided. This file tests the
// "timezone present" case; timezone absent is tested by test/mjsunit/date-parse.js

// Check epoch.
assertEquals(0, Date.parse("1970-01-01T00:00:00.000+00:00"));
assertEquals(0, Date.parse("1970-01-01T00:00:00.000-00:00"));
assertEquals(0, Date.parse("1970-01-01T00:00:00.000Z"));
assertEquals(0, Date.parse("1970-01-01T00:00:00.000Z"));
assertEquals(0, Date.parse("1970-01-01T00:00:00Z"));
assertEquals(0, Date.parse("1970-01-01T00:00Z"));
assertEquals(0, Date.parse("1970-01-01Z"));

assertEquals(0, Date.parse("1970-01T00:00:00.000+00:00"));
assertEquals(0, Date.parse("1970-01T00:00:00.000-00:00"));
assertEquals(0, Date.parse("1970-01T00:00:00.000Z"));
assertEquals(0, Date.parse("1970-01T00:00:00.000Z"));
assertEquals(0, Date.parse("1970-01T00:00:00Z"));
assertEquals(0, Date.parse("1970-01T00:00Z"));
assertEquals(0, Date.parse("1970-01Z"));

assertEquals(0, Date.parse("1970T00:00:00.000+00:00"));
assertEquals(0, Date.parse("1970T00:00:00.000-00:00"));
assertEquals(0, Date.parse("1970T00:00:00.000Z"));
assertEquals(0, Date.parse("1970T00:00:00.000Z"));
assertEquals(0, Date.parse("1970T00:00:00Z"));
assertEquals(0, Date.parse("1970T00:00Z"));
assertEquals(0, Date.parse("1970Z"));

assertEquals(0, Date.parse("+001970-01-01T00:00:00.000+00:00"));
assertEquals(0, Date.parse("+001970-01-01T00:00:00.000-00:00"));
assertEquals(0, Date.parse("+001970-01-01T00:00:00.000Z"));
assertEquals(0, Date.parse("+001970-01-01T00:00:00.000Z"));
assertEquals(0, Date.parse("+001970-01-01T00:00:00Z"));
assertEquals(0, Date.parse("+001970-01-01T00:00Z"));
assertEquals(0, Date.parse("+001970-01-01Z"));

assertEquals(0, Date.parse("+001970-01T00:00:00.000+00:00"));
assertEquals(0, Date.parse("+001970-01T00:00:00.000-00:00"));
assertEquals(0, Date.parse("+001970-01T00:00:00.000Z"));
assertEquals(0, Date.parse("+001970-01T00:00:00.000Z"));
assertEquals(0, Date.parse("+001970-01T00:00:00Z"));
assertEquals(0, Date.parse("+001970-01T00:00Z"));
assertEquals(0, Date.parse("+001970-01Z"));

assertEquals(0, Date.parse("+001970T00:00:00.000+00:00"));
assertEquals(0, Date.parse("+001970T00:00:00.000-00:00"));
assertEquals(0, Date.parse("+001970T00:00:00.000Z"));
assertEquals(0, Date.parse("+001970T00:00:00.000Z"));
assertEquals(0, Date.parse("+001970T00:00:00Z"));
assertEquals(0, Date.parse("+001970T00:00Z"));
assertEquals(0, Date.parse("+001970Z"));

// Check random date.
assertEquals(70671003500, Date.parse("1972-03-28T23:50:03.500+01:00"));
assertEquals(70674603500, Date.parse("1972-03-28T23:50:03.500Z"));
assertEquals(70674603500, Date.parse("1972-03-28T23:50:03.500Z"));
assertEquals(70674603000, Date.parse("1972-03-28T23:50:03Z"));
assertEquals(70674600000, Date.parse("1972-03-28T23:50Z"));
assertEquals(70588800000, Date.parse("1972-03-28Z"));

assertEquals(68338203500, Date.parse("1972-03T23:50:03.500+01:00"));
assertEquals(68341803500, Date.parse("1972-03T23:50:03.500Z"));
assertEquals(68341803500, Date.parse("1972-03T23:50:03.500Z"));
assertEquals(68341803000, Date.parse("1972-03T23:50:03Z"));
assertEquals(68341800000, Date.parse("1972-03T23:50Z"));
assertEquals(68256000000, Date.parse("1972-03Z"));

assertEquals(63154203500, Date.parse("1972T23:50:03.500+01:00"));
assertEquals(63157803500, Date.parse("1972T23:50:03.500Z"));
assertEquals(63157803500, Date.parse("1972T23:50:03.500Z"));
assertEquals(63157803000, Date.parse("1972T23:50:03Z"));
assertEquals(63072000000, Date.parse("1972Z"));

assertEquals(70671003500, Date.parse("+001972-03-28T23:50:03.500+01:00"));
assertEquals(70674603500, Date.parse("+001972-03-28T23:50:03.500Z"));
assertEquals(70674603500, Date.parse("+001972-03-28T23:50:03.500Z"));
assertEquals(70674603000, Date.parse("+001972-03-28T23:50:03Z"));
assertEquals(70674600000, Date.parse("+001972-03-28T23:50Z"));
assertEquals(70588800000, Date.parse("+001972-03-28Z"));

assertEquals(68338203500, Date.parse("+001972-03T23:50:03.500+01:00"));
assertEquals(68341803500, Date.parse("+001972-03T23:50:03.500Z"));
assertEquals(68341803500, Date.parse("+001972-03T23:50:03.500Z"));
assertEquals(68341803000, Date.parse("+001972-03T23:50:03Z"));
assertEquals(68341800000, Date.parse("+001972-03T23:50Z"));
assertEquals(68256000000, Date.parse("+001972-03Z"));

assertEquals(63154203500, Date.parse("+001972T23:50:03.500+01:00"));
assertEquals(63157803500, Date.parse("+001972T23:50:03.500Z"));
assertEquals(63157803500, Date.parse("+001972T23:50:03.500Z"));
assertEquals(63157803000, Date.parse("+001972T23:50:03Z"));
assertEquals(63072000000, Date.parse("+001972Z"));


// Ensure that ISO-years in the range 00-99 aren't translated to the range
// 1950..2049.
assertEquals(-60904915200000, Date.parse("0040-01-01T00:00Z"));
assertEquals(-60273763200000, Date.parse("0060-01-01T00:00Z"));
assertEquals(-62167219200000, Date.parse("0000-01-01T00:00Z"));
assertEquals(-62167219200000, Date.parse("+000000-01-01T00:00Z"));

// Test negative years.
assertEquals(-63429523200000, Date.parse("-000040-01-01Z"));
assertEquals(-64060675200000, Date.parse("-000060-01-01Z"));
assertEquals(-124397510400000, Date.parse("-001972-01-01Z"));

// Check time-zones.
assertEquals(70674603500, Date.parse("1972-03-28T23:50:03.500Z"));
for (var i = 0; i < 24; i++) {
  var hh = (i < 10) ? "0" + i : "" + i;
  for (var j = 0; j < 60; j += 15) {
    var mm = (j < 10) ? "0" + j : "" + j;
    var ms = (i * 60 + j) * 60000;
    var string = "1972-03-28T23:50:03.500-" + hh + ":" + mm;
    assertEquals(70674603500 + ms, Date.parse(string), string);
    string = "1972-03-28T23:50:03.500+" + hh + ":" + mm;
    assertEquals(70674603500 - ms, Date.parse(string), string);
  }
}

// Test padding with 0 rather than spaces
assertEquals('Wed, 01 Jan 0020 00:00:00 GMT', new Date('0020-01-01T00:00:00Z').toUTCString());
let dateRegExp = /^(Sun|Mon|Tue|Wed|Thu|Fri|Sat) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [0-9]{2} [0-9]{4}$/
match = dateRegExp.exec(new Date('0020-01-01T00:00:00Z').toDateString());
assertNotNull(match);
let stringRegExp = /^(Sun|Mon|Tue|Wed|Thu|Fri|Sat) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [0-9]{2} [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2} GMT[+-][0-9]{4}( \(.+\))?$/
match = stringRegExp.exec(new Date('0020-01-01T00:00:00Z').toString());
assertNotNull(match);

assertThrows('Date.prototype.setTime.call("", 1);', TypeError);
assertThrows('Date.prototype.setYear.call("", 1);', TypeError);
assertThrows('Date.prototype.setHours.call("", 1, 2, 3, 4);', TypeError);
assertThrows('Date.prototype.getDate.call("");', TypeError);
assertThrows('Date.prototype.getUTCDate.call("");', TypeError);

assertThrows(function() { Date.prototype.getTime.call(0) }, TypeError);
assertThrows(function() { Date.prototype.getTime.call("") }, TypeError);

assertThrows(function() { Date.prototype.getYear.call(0) }, TypeError);
assertThrows(function() { Date.prototype.getYear.call("") }, TypeError);

(function TestDatePrototypeOrdinaryObject() {
  assertEquals(Object.prototype, Date.prototype.__proto__);
  assertThrows(function () { Date.prototype.toString() }, TypeError);
})();

delete Date.prototype.getUTCFullYear;
delete Date.prototype.getUTCMonth;
delete Date.prototype.getUTCDate;
delete Date.prototype.getUTCHours;
delete Date.prototype.getUTCMinutes;
delete Date.prototype.getUTCSeconds;
delete Date.prototype.getUTCMilliseconds;
(new Date()).toISOString();

(function TestDeleteToString() {
  assertTrue(delete Date.prototype.toString);
  assertTrue('[object Date]' !== Date());
})();

// Test minimum and maximum date range according to ES6 section 20.3.1.1:
// "The actual range of times supported by ECMAScript Date objects is slightly
// smaller: exactly -100,000,000 days to 100,000,000 days measured relative to
// midnight at the beginning of 01 January, 1970 UTC. This gives a range of
// 8,640,000,000,000,000 milliseconds to either side of 01 January, 1970 UTC."
assertEquals(-8640000000000000, Date.parse("-271821-04-20T00:00:00.000Z"));
assertEquals(8640000000000000, Date.parse("+275760-09-13T00:00:00.000Z"));
assertTrue(isNaN(Date.parse("-271821-04-19T00:00:00.000Z")));
assertTrue(isNaN(Date.parse("+275760-09-14T00:00:00.000Z")));
