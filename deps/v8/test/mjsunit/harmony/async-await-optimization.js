// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// test basic interleaving
(function () {
  const actual = [];
  const expected = [ 'await', 1, 'await', 2 ];
  const iterations = 2;

  async function pushAwait() {
    actual.push('await');
  }

  async function callAsync() {
    for (let i = 0; i < iterations; i++) {
      await pushAwait();
    }
    return 0;
  }

  function checkAssertions() {
    assertArrayEquals(expected, actual,
      'Async/await and promises should be interleaved.');
  }

  assertPromiseResult((async() => {
    callAsync();

    return new Promise(function (resolve) {
      actual.push(1);
      resolve();
    }).then(function () {
      actual.push(2);
    }).then(checkAssertions);
  })());
})();

// test async generators
(function () {
  const actual = [];
  const expected = [ 'await', 1, 'await', 2 ];
  const iterations = 2;

  async function pushAwait() {
    actual.push('await');
  }

  async function* callAsync() {
    for (let i = 0; i < iterations; i++) {
      await pushAwait();
    }
    return 0;
  }

  function checkAssertions() {
    assertArrayEquals(expected, actual,
      'Async/await and promises should be interleaved when using async generators.');
  }

  assertPromiseResult((async() => {
    callAsync().next();

    return new Promise(function (resolve) {
      actual.push(1);
      resolve();
    }).then(function () {
      actual.push(2);
    }).then(checkAssertions);
  })());
})();

// test yielding from async generators
(function () {
  const actual = [];
  const expected = [
    'Promise: 6',
    'Promise: 5',
    'Await: 3',
    'Promise: 4',
    'Promise: 3',
    'Await: 2',
    'Promise: 2',
    'Promise: 1',
    'Await: 1',
    'Promise: 0'
  ];
  const iterations = 3;

  async function* naturalNumbers(start) {
    let current = start;
    while (current > 0) {
      yield Promise.resolve(current--);
    }
  }

  async function trigger() {
    for await (const num of naturalNumbers(iterations)) {
      actual.push('Await: ' + num);
    }
  }

  async function checkAssertions() {
    assertArrayEquals(expected, actual,
      'Async/await and promises should be interleaved when yielding.');
  }

  async function countdown(counter) {
    actual.push('Promise: ' + counter);
    if (counter > 0) {
      return Promise.resolve(counter - 1).then(countdown);
    } else {
      await checkAssertions();
    }
  }

  assertPromiseResult((async() => {
    trigger();

    return countdown(iterations * 2);
  })());
})();
