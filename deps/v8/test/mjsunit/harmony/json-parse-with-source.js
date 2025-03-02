// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


(function TestBigInt() {
  const tooBigForNumber = BigInt(Number.MAX_SAFE_INTEGER) + 2n;
  const intToBigInt = (key, val, { source }) =>
    typeof val === 'number' && val % 1 === 0 ? BigInt(source) : val;
  const roundTripped = JSON.parse(String(tooBigForNumber), intToBigInt);
  assertEquals(tooBigForNumber, roundTripped);

  const bigIntToRawJSON = (key, val) =>
    typeof val === 'bigint' ? JSON.rawJSON(val) : val;
  const embedded = JSON.stringify({ tooBigForNumber }, bigIntToRawJSON);
  assertEquals('{"tooBigForNumber":9007199254740993}', embedded);
})();

function GenerateParseReviverFunction(texts) {
  let i = 0;
  return function (key, value, context) {
    assertTrue(typeof context === 'object');
    assertEquals(Object.prototype, Object.getPrototypeOf(context));
    // The json value is a primitive value, it's context only has a source property.
    if (texts[i] !== undefined) {
      const descriptor = Object.getOwnPropertyDescriptor(context, 'source');
      assertTrue(descriptor.configurable);
      assertTrue(descriptor.enumerable);
      assertTrue(descriptor.writable);
      assertEquals(undefined, descriptor.get);
      assertEquals(undefined, descriptor.set);
      assertEquals(texts[i++], descriptor.value);

      assertEquals(['source'], Object.getOwnPropertyNames(context));
      assertEquals([], Object.getOwnPropertySymbols(context));
    } else {
      // The json value is JSArray or JSObject, it's context has no property.
      assertFalse(Object.hasOwn(context, 'source'));
      assertEquals([], Object.getOwnPropertyNames(context));
      assertEquals([], Object.getOwnPropertySymbols(context));
      i++;
    }
    return value;
  };
}

(function TestNumber() {
  assertEquals(1, JSON.parse('1', GenerateParseReviverFunction(['1'])));
  assertEquals(1.1, JSON.parse('1.1', GenerateParseReviverFunction(['1.1'])));
  assertEquals(-1, JSON.parse('-1', GenerateParseReviverFunction(['-1'])));
  assertEquals(
    -1.1,
    JSON.parse('-1.1', GenerateParseReviverFunction(['-1.1']))
  );
  assertEquals(
    11,
    JSON.parse('1.1e1', GenerateParseReviverFunction(['1.1e1']))
  );
  assertEquals(
    11,
    JSON.parse('1.1e+1', GenerateParseReviverFunction(['1.1e+1']))
  );
  assertEquals(
    0.11,
    JSON.parse('1.1e-1', GenerateParseReviverFunction(['1.1e-1']))
  );
  assertEquals(
    11,
    JSON.parse('1.1E1', GenerateParseReviverFunction(['1.1E1']))
  );
  assertEquals(
    11,
    JSON.parse('1.1E+1', GenerateParseReviverFunction(['1.1E+1']))
  );
  assertEquals(
    0.11,
    JSON.parse('1.1E-1', GenerateParseReviverFunction(['1.1E-1']))
  );

  assertEquals('1', JSON.stringify(JSON.rawJSON(1)));
  assertEquals('1.1', JSON.stringify(JSON.rawJSON(1.1)));
  assertEquals('-1', JSON.stringify(JSON.rawJSON(-1)));
  assertEquals('-1.1', JSON.stringify(JSON.rawJSON(-1.1)));
  assertEquals('11', JSON.stringify(JSON.rawJSON(1.1e1)));
  assertEquals('0.11', JSON.stringify(JSON.rawJSON(1.1e-1)));
})();

(function TestBasic() {
  assertEquals(
    null,
    JSON.parse('null', GenerateParseReviverFunction(['null']))
  );
  assertEquals(
    true,
    JSON.parse('true', GenerateParseReviverFunction(['true']))
  );
  assertEquals(
    false,
    JSON.parse('false', GenerateParseReviverFunction(['false']))
  );
  assertEquals(
    'foo',
    JSON.parse('"foo"', GenerateParseReviverFunction(['"foo"']))
  );

  assertEquals('null', JSON.stringify(JSON.rawJSON(null)));
  assertEquals('true', JSON.stringify(JSON.rawJSON(true)));
  assertEquals('false', JSON.stringify(JSON.rawJSON(false)));
  assertEquals('"foo"', JSON.stringify(JSON.rawJSON('"foo"')));
})();

(function TestObject() {
  assertEquals(
    {},
    JSON.parse('{}', GenerateParseReviverFunction([]))
  );
  assertEquals(
    { 42: 37 },
    JSON.parse('{"42":37}', GenerateParseReviverFunction(['37']))
  );
  assertEquals(
    { x: 1, y: 2 },
    JSON.parse('{"x": 1, "y": 2}', GenerateParseReviverFunction(['1', '2']))
  );
  // undefined means the json value is JSObject or JSArray and the passed
  // context to the reviver function has no source property.
  assertEquals(
    { x: [1, 2], y: [2, 3] },
    JSON.parse(
      '{"x": [1,2], "y": [2,3]}',
      GenerateParseReviverFunction(['1', '2', undefined, '2', '3', undefined])
    )
  );
  assertEquals(
    { x: { x: 1, y: 2 } },
    JSON.parse(
      '{"x": {"x": 1, "y": 2}}',
      GenerateParseReviverFunction(['1', '2', undefined, undefined])
    )
  );

  assertEquals('{"42":37}', JSON.stringify({ 42: JSON.rawJSON(37) }));
  assertEquals(
    '{"x":1,"y":2}',
    JSON.stringify({ x: JSON.rawJSON(1), y: JSON.rawJSON(2) })
  );
  assertEquals(
    '{"x":{"x":1,"y":2}}',
    JSON.stringify({ x: { x: JSON.rawJSON(1), y: JSON.rawJSON(2) } })
  );
  assertEquals(
    `"${'\u1234'.repeat(128)}"`,
    JSON.stringify(JSON.rawJSON(`"${'\u1234'.repeat(128)}"`))
  );
})();

(function TestArray() {
  assertEquals([1], JSON.parse('[1.0]', GenerateParseReviverFunction(['1.0'])));
  assertEquals(
    [1.1],
    JSON.parse('[1.1]', GenerateParseReviverFunction(['1.1']))
  );
  assertEquals([], JSON.parse('[]', GenerateParseReviverFunction([])));
  assertEquals(
    [1, '2', true, null, { x: 1, y: 1 }],
    JSON.parse(
      '[1, "2", true, null, {"x": 1, "y": 1}]',
      GenerateParseReviverFunction(['1', '"2"', 'true', 'null', '1', '1'])
    )
  );

  assertEquals('[1,1.1]', JSON.stringify([JSON.rawJSON(1), JSON.rawJSON(1.1)]));
  assertEquals(
    '["1",true,null,false]',
    JSON.stringify([
      JSON.rawJSON('"1"'),
      JSON.rawJSON(true),
      JSON.rawJSON(null),
      JSON.rawJSON(false),
    ])
  );
  assertEquals(
    '[{"x":1,"y":1}]',
    JSON.stringify([{ x: JSON.rawJSON(1), y: JSON.rawJSON(1) }])
  );
})();

function assertIsRawJson(rawJson, expectedRawJsonValue) {
  assertEquals(null, Object.getPrototypeOf(rawJson));
  assertTrue(Object.hasOwn(rawJson, 'rawJSON'));
  assertEquals(['rawJSON'], Object.getOwnPropertyNames(rawJson));
  assertEquals([], Object.getOwnPropertySymbols(rawJson));
  assertEquals(expectedRawJsonValue, rawJson.rawJSON);
}

(function TestRawJson() {
  assertIsRawJson(JSON.rawJSON(1), '1');
  assertIsRawJson(JSON.rawJSON(null), 'null');
  assertIsRawJson(JSON.rawJSON(true), 'true');
  assertIsRawJson(JSON.rawJSON(false), 'false');
  assertIsRawJson(JSON.rawJSON('"foo"'), '"foo"');

  assertThrows(() => {
    JSON.rawJSON(Symbol('123'));
  }, TypeError);

  assertThrows(() => {
    JSON.rawJSON(undefined);
  }, SyntaxError);

  assertThrows(() => {
    JSON.rawJSON({});
  }, SyntaxError);

  assertThrows(() => {
    JSON.rawJSON([]);
  }, SyntaxError);

  const ILLEGAL_END_CHARS = ['\n', '\t', '\r', ' '];
  for (const char of ILLEGAL_END_CHARS) {
    assertThrows(() => {
      JSON.rawJSON(`${char}123`);
    }, SyntaxError);
    assertThrows(() => {
      JSON.rawJSON(`123${char}`);
    }, SyntaxError);
  }

  assertThrows(() => {
    JSON.rawJSON('');
  }, SyntaxError);

  const values = [1, 1.1, null, false, true, '123'];
  for (const value of values) {
    assertFalse(JSON.isRawJSON(value));
    assertTrue(JSON.isRawJSON(JSON.rawJSON(value)));
  }
  assertFalse(JSON.isRawJSON(undefined));
  assertFalse(JSON.isRawJSON(Symbol('123')));
  assertFalse(JSON.isRawJSON([]));
  assertFalse(JSON.isRawJSON({ rawJSON: '123' }));
})();

(function TestReviverModifyJsonValue() {
  {
    let reviverCallIndex = 0;
    const expectedKeys = ['a', 'b', 'c', ''];
    const reviver = function(key, value, {source}) {
      assertEquals(expectedKeys[reviverCallIndex++], key);
      if (key == 'a') {
        this.b = 2;
        assertEquals('0', source);
      } else if (key == 'b') {
        this.c = 3;
        assertEquals(2, value);
        assertEquals(undefined, source);
      } else if (key == 'c') {
        assertEquals(3, value);
        assertEquals(undefined, source);
      }
      return value;
    }
    assertEquals({a: 0, b: 2, c: 3}, JSON.parse('{"a": 0, "b": 1, "c": [1, 2]}', reviver));
  }
  {
    let reviverCallIndex = 0;
    const expectedKeys = ['0', '1', '2', '3', ''];
    const reviver = function(key, value, {source}) {
      assertEquals(expectedKeys[reviverCallIndex++], key);
      if (key == '0') {
        this[1] = 3;
        assertEquals(1, value);
        assertEquals('1', source);
      } else if (key == '1') {
        this[2] = 4;
        assertEquals(3, value);
        assertEquals(undefined, source);
      } else if(key == '2') {
        this[3] = 5;
        assertEquals(4, value);
        assertEquals(undefined, source);
      } else if(key == '5'){
        assertEquals(5, value);
        assertEquals(undefined, source);
      }
      return value;
    }
    assertEquals([1, 3, 4, 5], JSON.parse('[1, 2, 3, {"a": 1}]', reviver));
  }
})();
