// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


const replacements = [42,
                      ['foo'],
                      {foo:'bar'},
                      'foo'];

function TestArrayForwardModify(replacement) {
  let alreadyReplaced = false;
  let expectedKeys = ['0','1',''];
  // lol who designed reviver semantics
  if (typeof replacement === 'object') {
    expectedKeys.splice(1, 0, ...Object.keys(replacement));
  }
  const o = JSON.parse('[1, 2]', function (k, v, { source }) {
    assertEquals(expectedKeys.shift(), k);
    if (k === '0') {
      if (!alreadyReplaced) {
        this[1] = replacement;
        alreadyReplaced = true;
      }
    } else if (k !== '') {
      assertSame(undefined, source);
    }
    return this[k];
  });
  assertEquals(0, expectedKeys.length);
  assertEquals([1, replacement], o);
}

function TestObjectForwardModify(replacement) {
  let alreadyReplaced = false;
  let expectedKeys = ['p','q',''];
  if (typeof replacement === 'object') {
    expectedKeys.splice(1, 0, ...Object.keys(replacement));
  }
  const o = JSON.parse('{"p":1, "q":2}', function (k, v, { source }) {
    assertEquals(expectedKeys.shift(), k);
    if (k === 'p') {
      if (!alreadyReplaced) {
        this.q = replacement;
        alreadyReplaced = true;
      }
    } else if (k !== '') {
      assertSame(undefined, source);
    }
    return this[k];
  });
  assertEquals(0, expectedKeys.length);
  assertEquals({p:1, q:replacement}, o);
}

for (const r of replacements) {
  TestArrayForwardModify(r);
  TestObjectForwardModify(r);
}

(function TestArrayAppend() {
  let log = [];
  const o = JSON.parse('[1,[]]', function (k, v, { source }) {
    log.push([k, v, source]);
    if (v === 1) {
      this[1].push('barf');
    }
    return this[k];
  });
  assertEquals([['0', 1, '1'],
                ['0', 'barf', undefined],
                ['1', ['barf'], undefined],
                ['', [1, ['barf']], undefined]],
               log);
})();

(function TestObjectAddProperty() {
  let log = [];
  const o = JSON.parse('{"p":1,"q":{}}', function (k, v, { source }) {
    log.push([k, v, source]);
    if (v === 1) {
      this.q.added = 'barf';
    }
    return this[k];
  });
  assertEquals([['p', 1, '1'],
                ['added', 'barf', undefined],
                ['q', {added:'barf'}, undefined],
                ['', {p:1, q:{added:'barf'}}, undefined]],
               log);
})();
