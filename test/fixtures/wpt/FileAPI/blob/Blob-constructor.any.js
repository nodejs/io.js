// META: title=Blob constructor
// META: script=../support/Blob.js
'use strict';

test(function() {
  assert_true("Blob" in globalThis, "globalThis should have a Blob property.");
  assert_equals(Blob.length, 0, "Blob.length should be 0.");
  assert_true(Blob instanceof Function, "Blob should be a function.");
}, "Blob interface object");

// Step 1.
test(function() {
  var blob = new Blob();
  assert_true(blob instanceof Blob);
  assert_equals(String(blob), '[object Blob]');
  assert_equals(blob.size, 0);
  assert_equals(blob.type, "");
}, "Blob constructor with no arguments");
test(function() {
  assert_throws_js(TypeError, function() { var blob = Blob(); });
}, "Blob constructor with no arguments, without 'new'");
test(function() {
  var blob = new Blob;
  assert_true(blob instanceof Blob);
  assert_equals(blob.size, 0);
  assert_equals(blob.type, "");
}, "Blob constructor without brackets");
test(function() {
  var blob = new Blob(undefined);
  assert_true(blob instanceof Blob);
  assert_equals(String(blob), '[object Blob]');
  assert_equals(blob.size, 0);
  assert_equals(blob.type, "");
}, "Blob constructor with undefined as first argument");

// blobParts argument (WebIDL).
test(function() {
  var args = [
    null,
    true,
    false,
    0,
    1,
    1.5,
    "FAIL",
    new Date(),
    new RegExp(),
    {},
    { 0: "FAIL", length: 1 },
  ];
  args.forEach(function(arg) {
    assert_throws_js(TypeError, function() {
      new Blob(arg);
    }, "Should throw for argument " + format_value(arg) + ".");
  });
}, "Passing non-objects, Dates and RegExps for blobParts should throw a TypeError.");

test_blob(function() {
  return new Blob({
    [Symbol.iterator]: Array.prototype[Symbol.iterator],
  });
}, {
  expected: "",
  type: "",
  desc: "A plain object with @@iterator should be treated as a sequence for the blobParts argument."
});
test(t => {
  const blob = new Blob({
    [Symbol.iterator]() {
      var i = 0;
      return {next: () => [
        {done:false, value:'ab'},
        {done:false, value:'cde'},
        {done:true}
      ][i++]
      };
    }
  });
  assert_equals(blob.size, 5, 'Custom @@iterator should be treated as a sequence');
}, "A plain object with custom @@iterator should be treated as a sequence for the blobParts argument.");
test_blob(function() {
  return new Blob({
    [Symbol.iterator]: Array.prototype[Symbol.iterator],
    0: "PASS",
    length: 1
  });
}, {
  expected: "PASS",
  type: "",
  desc: "A plain object with @@iterator and a length property should be treated as a sequence for the blobParts argument."
});
test_blob(function() {
  return new Blob(new String("xyz"));
}, {
  expected: "xyz",
  type: "",
  desc: "A String object should be treated as a sequence for the blobParts argument."
});
test_blob(function() {
  return new Blob(new Uint8Array([1, 2, 3]));
}, {
  expected: "123",
  type: "",
  desc: "A Uint8Array object should be treated as a sequence for the blobParts argument."
});

var test_error = {
  name: "test",
  message: "test error",
};

test(function() {
  var obj = {
    [Symbol.iterator]: Array.prototype[Symbol.iterator],
    get length() { throw test_error; }
  };
  assert_throws_exactly(test_error, function() {
    new Blob(obj);
  });
}, "The length getter should be invoked and any exceptions should be propagated.");

test(function() {
  assert_throws_exactly(test_error, function() {
    var obj = {
      [Symbol.iterator]: Array.prototype[Symbol.iterator],
      length: {
        valueOf: null,
        toString: function() { throw test_error; }
      }
    };
    new Blob(obj);
  });
  assert_throws_exactly(test_error, function() {
    var obj = {
      [Symbol.iterator]: Array.prototype[Symbol.iterator],
      length: { valueOf: function() { throw test_error; } }
    };
    new Blob(obj);
  });
}, "ToUint32 should be applied to the length and any exceptions should be propagated.");

test(function() {
  var received = [];
  var obj = {
    get [Symbol.iterator]() {
      received.push("Symbol.iterator");
      return Array.prototype[Symbol.iterator];
    },
    get length() {
      received.push("length getter");
      return {
        valueOf: function() {
          received.push("length valueOf");
          return 3;
        }
      };
    },
    get 0() {
      received.push("0 getter");
      return {
        toString: function() {
          received.push("0 toString");
          return "a";
        }
      };
    },
    get 1() {
      received.push("1 getter");
      throw test_error;
    },
    get 2() {
      received.push("2 getter");
      assert_unreached("Should not call the getter for 2 if the getter for 1 threw.");
    }
  };
  assert_throws_exactly(test_error, function() {
    new Blob(obj);
  });
  assert_array_equals(received, [
    "Symbol.iterator",
    "length getter",
    "length valueOf",
    "0 getter",
    "0 toString",
    "length getter",
    "length valueOf",
    "1 getter",
  ]);
}, "Getters and value conversions should happen in order until an exception is thrown.");

// XXX should add tests edge cases of ToLength(length)

test(function() {
  assert_throws_exactly(test_error, function() {
    new Blob([{ toString: function() { throw test_error; } }]);
  }, "Throwing toString");
  assert_throws_exactly(test_error, function() {
    new Blob([{ toString: undefined, valueOf: function() { throw test_error; } }]);
  }, "Throwing valueOf");
  assert_throws_exactly(test_error, function() {
    new Blob([{
      toString: function() { throw test_error; },
      valueOf: function() { assert_unreached("Should not call valueOf if toString is present."); }
    }]);
  }, "Throwing toString and valueOf");
  assert_throws_js(TypeError, function() {
    new Blob([{toString: null, valueOf: null}]);
  }, "Null toString and valueOf");
}, "ToString should be called on elements of the blobParts array and any exceptions should be propagated.");

test_blob(function() {
  var arr = [
    { toString: function() { arr.pop(); return "PASS"; } },
    { toString: function() { assert_unreached("Should have removed the second element of the array rather than called toString() on it."); } }
  ];
  return new Blob(arr);
}, {
  expected: "PASS",
  type: "",
  desc: "Changes to the blobParts array should be reflected in the returned Blob (pop)."
});

test_blob(function() {
  var arr = [
    {
      toString: function() {
        if (arr.length === 3) {
          return "A";
        }
        arr.unshift({
          toString: function() {
            assert_unreached("Should only access index 0 once.");
          }
        });
        return "P";
      }
    },
    {
      toString: function() {
        return "SS";
      }
    }
  ];
  return new Blob(arr);
}, {
  expected: "PASS",
  type: "",
  desc: "Changes to the blobParts array should be reflected in the returned Blob (unshift)."
});

test_blob(function() {
  // https://www.w3.org/Bugs/Public/show_bug.cgi?id=17652
  return new Blob([
    null,
    undefined,
    true,
    false,
    0,
    1,
    new String("stringobject"),
    [],
    ['x', 'y'],
    {},
    { 0: "FAIL", length: 1 },
    { toString: function() { return "stringA"; } },
    { toString: undefined, valueOf: function() { return "stringB"; } },
    { valueOf: function() { assert_unreached("Should not call valueOf if toString is present on the prototype."); } }
  ]);
}, {
  expected: "nullundefinedtruefalse01stringobjectx,y[object Object][object Object]stringAstringB[object Object]",
  type: "",
  desc: "ToString should be called on elements of the blobParts array."
});

test_blob(function() {
  return new Blob([
    new ArrayBuffer(8)
  ]);
}, {
  expected: "\0\0\0\0\0\0\0\0",
  type: "",
  desc: "ArrayBuffer elements of the blobParts array should be supported."
});

test_blob(function() {
  return new Blob([
    new Uint8Array([0x50, 0x41, 0x53, 0x53]),
    new Int8Array([0x50, 0x41, 0x53, 0x53]),
    new Uint16Array([0x4150, 0x5353]),
    new Int16Array([0x4150, 0x5353]),
    new Uint32Array([0x53534150]),
    new Int32Array([0x53534150]),
    new Float16Array([2.65625, 58.59375]),
    new Float32Array([0xD341500000])
  ]);
}, {
  expected: "PASSPASSPASSPASSPASSPASSPASSPASS",
  type: "",
  desc: "Passing typed arrays as elements of the blobParts array should work."
});
test_blob(function() {
  return new Blob([
    // 0x535 3415053534150
    // 0x535 = 0b010100110101 -> Sign = +, Exponent = 1333 - 1023 = 310
    // 0x13415053534150 * 2**(-52)
    // ==> 0x13415053534150 * 2**258 = 2510297372767036725005267563121821874921913208671273727396467555337665343087229079989707079680
    new Float64Array([2510297372767036725005267563121821874921913208671273727396467555337665343087229079989707079680])
  ]);
}, {
  expected: "PASSPASS",
  type: "",
  desc: "Passing a Float64Array as element of the blobParts array should work."
});

test_blob(function() {
  return new Blob([
    new BigInt64Array([BigInt("0x5353415053534150")]),
    new BigUint64Array([BigInt("0x5353415053534150")])
  ]);
}, {
  expected: "PASSPASSPASSPASS",
  type: "",
  desc: "Passing BigInt typed arrays as elements of the blobParts array should work."
});

var t_ports = async_test("Passing a FrozenArray as the blobParts array should work (FrozenArray<MessagePort>).");
t_ports.step(function() {
    var channel = new MessageChannel();
    channel.port2.onmessage = this.step_func(function(e) {
        var b_ports = new Blob(e.ports);
        assert_equals(b_ports.size, "[object MessagePort]".length);
        this.done();
    });
    var channel2 = new MessageChannel();
    channel.port1.postMessage('', [channel2.port1]);
});

test_blob(function() {
  var blob = new Blob(['foo']);
  return new Blob([blob, blob]);
}, {
  expected: "foofoo",
  type: "",
  desc: "Array with two blobs"
});

test_blob_binary(function() {
  var view = new Uint8Array([0, 255, 0]);
  return new Blob([view.buffer, view.buffer]);
}, {
  expected: [0, 255, 0, 0, 255, 0],
  type: "",
  desc: "Array with two buffers"
});

test_blob_binary(function() {
  var view = new Uint8Array([0, 255, 0, 4]);
  var blob = new Blob([view, view]);
  assert_equals(blob.size, 8);
  var view1 = new Uint16Array(view.buffer, 2);
  return new Blob([view1, view.buffer, view1]);
}, {
  expected: [0, 4, 0, 255, 0, 4, 0, 4],
  type: "",
  desc: "Array with two bufferviews"
});

test_blob(function() {
  var view = new Uint8Array([0]);
  var blob = new Blob(["fo"]);
  return new Blob([view.buffer, blob, "foo"]);
}, {
  expected: "\0fofoo",
  type: "",
  desc: "Array with mixed types"
});

test(function() {
  const accessed = [];
  const stringified = [];

  new Blob([], {
    get type() { accessed.push('type'); },
    get endings() { accessed.push('endings'); }
  });
  new Blob([], {
    type: { toString: () => { stringified.push('type'); return ''; } },
    endings: { toString: () => { stringified.push('endings'); return 'transparent'; } }
  });
  assert_array_equals(accessed, ['endings', 'type']);
  assert_array_equals(stringified, ['endings', 'type']);
}, "options properties should be accessed in lexicographic order.");

test(function() {
  assert_throws_exactly(test_error, function() {
    new Blob(
      [{ toString: function() { throw test_error } }],
      {
        get type() { assert_unreached("type getter should not be called."); }
      }
    );
  });
}, "Arguments should be evaluated from left to right.");

[
  null,
  undefined,
  {},
  { unrecognized: true },
  /regex/,
  function() {}
].forEach(function(arg, idx) {
  test_blob(function() {
    return new Blob([], arg);
  }, {
    expected: "",
    type: "",
    desc: "Passing " + format_value(arg) + " (index " + idx + ") for options should use the defaults."
  });
  test_blob(function() {
    return new Blob(["\na\r\nb\n\rc\r"], arg);
  }, {
    expected: "\na\r\nb\n\rc\r",
    type: "",
    desc: "Passing " + format_value(arg) + " (index " + idx + ") for options should use the defaults (with newlines)."
  });
});

[
  123,
  123.4,
  true,
  'abc'
].forEach(arg => {
  test(t => {
    assert_throws_js(TypeError, () => new Blob([], arg),
                     'Blob constructor should throw with invalid property bag');
  }, `Passing ${JSON.stringify(arg)} for options should throw`);
});

var type_tests = [
  // blobParts, type, expected type
  [[], '', ''],
  [[], 'a', 'a'],
  [[], 'A', 'a'],
  [[], 'text/html', 'text/html'],
  [[], 'TEXT/HTML', 'text/html'],
  [[], 'text/plain;charset=utf-8', 'text/plain;charset=utf-8'],
  [[], '\u00E5', ''],
  [[], '\uD801\uDC7E', ''], // U+1047E
  [[], ' image/gif ', ' image/gif '],
  [[], '\timage/gif\t', ''],
  [[], 'image/gif;\u007f', ''],
  [[], '\u0130mage/gif', ''], // uppercase i with dot
  [[], '\u0131mage/gif', ''], // lowercase dotless i
  [[], 'image/gif\u0000', ''],
  // check that type isn't changed based on sniffing
  [[0x3C, 0x48, 0x54, 0x4D, 0x4C, 0x3E], 'unknown/unknown', 'unknown/unknown'], // "<HTML>"
  [[0x00, 0xFF], 'text/plain', 'text/plain'],
  [[0x47, 0x49, 0x46, 0x38, 0x39, 0x61], 'image/png', 'image/png'], // "GIF89a"
];

type_tests.forEach(function(t) {
  test(function() {
    var arr = new Uint8Array([t[0]]).buffer;
    var b = new Blob([arr], {type:t[1]});
    assert_equals(b.type, t[2]);
  }, "Blob with type " + format_value(t[1]));
});
