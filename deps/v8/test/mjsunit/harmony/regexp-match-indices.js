// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax
// Flags: --expose-gc --stack-size=100
// Flags: --no-force-slow-path

// Sanity test.
{
  const re = /a+(?<Z>z)?/d;
  const m = re.exec("xaaaz");

  assertEquals(m.indices, [[1, 5], [4, 5]]);
  assertEquals(m.indices.groups, {'Z': [4, 5]})
}

// Capture groups that are not matched return `undefined`.
{
  const re = /a+(?<Z>z)?/d;
  const m = re.exec("xaaay");

  assertEquals(m.indices, [[1, 4], undefined]);
  assertEquals(m.indices.groups, {'Z': undefined});
}

// Two capture groups.
{
  const re = /a+(?<A>zz)?(?<B>ii)?/d;
  const m = re.exec("xaaazzii");

  assertEquals(m.indices, [[1, 8], [4, 6], [6, 8]]);
  assertEquals(m.indices.groups, {'A': [4, 6], 'B': [6, 8]});
}

// No capture groups.
{
  const re = /a+/d;
  const m = re.exec("xaaazzii");

  assertEquals(m.indices, [[1, 4]]);
  assertEquals(m.indices.groups, undefined);
}

// No match.
{
  const re = /a+/d;
  const m = re.exec("xzzii");

  assertEquals(null, m);
}

// Unnamed capture groups.
{
  const re = /a+(z)?/d;
  const m = re.exec("xaaaz");

  assertEquals(m.indices, [[1, 5], [4, 5]]);
  assertEquals(m.indices.groups, undefined)
}

// Named and unnamed capture groups.
{
  const re = /a+(z)?(?<Y>y)?/d;
  const m = re.exec("xaaazyy")

  assertEquals(m.indices, [[1, 6], [4, 5], [5, 6]]);
  assertEquals(m.indices.groups, {'Y': [5, 6]})
}


// Verify property overwrite.
{
  const re = /a+(?<Z>z)?/d;
  const m = re.exec("xaaaz");

  m.indices = null;
  assertEquals(null, m.indices);
}

// Mess with array prototype, we should still do the right thing.
{
  Object.defineProperty(Array.prototype, "groups", {
    get: () => {
      assertUnreachable();
      return null;
    },
    set: (x) => {
      assertUnreachable();
    }
  });

  Object.defineProperty(Array.prototype, "0", {
    get: () => {
      assertUnreachable();
      return null;
    },
    set: (x) => {
      assertUnreachable();
    }
  });

  const re = /a+(?<Z>z)?/d;
  const m = re.exec("xaaaz");

  assertEquals(m.indices.groups, {'Z': [4, 5]})
}

// Test atomic regexp.
{
  const m = (/undefined/d).exec();

  assertEquals(m.indices, [[0, 9]]);
}

// Test deleting unrelated fields does not break.
{
  const m = (/undefined/d).exec();
  delete m['index'];
  gc();
  assertEquals(m.indices, [[0, 9]]);
}

// Stack overflow.
{
  const re = /a+(?<Z>z)?/d;
  const m = re.exec("xaaaz");

  function rec() {
    try {
      return rec();
    } catch (e) {
      assertEquals(m.indices, [[1, 5], [4, 5]]);
      assertEquals(m.indices.groups, {'Z': [4, 5]})
      return true;
    }
  }
  assertTrue(rec());
}

// Match between matches.
{
  const re = /a+(?<A>zz)?(?<B>ii)?/d;
  const m = re.exec("xaaazzii");
  assertTrue(/b+(?<C>cccc)?/.test("llllllbbbbbbcccc"));
  assertEquals(m.indices, [[1, 8], [4, 6], [6, 8]]);
  assertEquals(m.indices.groups, {'A': [4, 6], 'B': [6, 8]});
}

// Redefined hasIndices should reflect in flags.
{
  let re = /./;
  Object.defineProperty(re, "hasIndices", { get: function() { return true; } });
  assertEquals("d", re.flags);
}

{
  // The flags field of a regexp should be sorted.
  assertEquals("dgmsy", (/asdf/dymsg).flags);

  // The 'hasIndices' member should be set according to the hasIndices flag.
  assertTrue((/asdf/dymsg).hasIndices);
  assertFalse((/asdf/ymsg).hasIndices);

  // The new fields installed on the regexp prototype map shouldn't make
  // unmodified regexps slow.

  // TODO(v8:11248) Enabling v8_dict_property_const_tracking currently evokes
  // that the original fast mode prototype for regexes is converted to a
  // dictionary mode one, which makes %RegexpIsUnmodified fail. Once we support
  // directly creating the regex prototype in dictionary mode if
  // v8_dict_property_const_tracking is enabled, change %RegexpIsUnmodified to
  // know about the canonical dictionary mode prototype, too.
  if (!%IsDictPropertyConstTrackingEnabled()) {
    %RegexpIsUnmodified(/asdf/);
    %RegexpIsUnmodified(/asdf/d);
  }
}
