// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const {
  getContinuationPreservedEmbedderData,
  setContinuationPreservedEmbedderData,
} = d8.getExtrasBindingObject();


// Basic set and get
const foo = { bar: 'baz' };
setContinuationPreservedEmbedderData(foo);
assertEquals(foo, getContinuationPreservedEmbedderData());

// Captures at the point a continuation is created
{
  // Resolve path
  setContinuationPreservedEmbedderData('init');
  let resolve;
  const p = new Promise(r => {
    resolve = r;
  });

  setContinuationPreservedEmbedderData('resolve');
  resolve();

  setContinuationPreservedEmbedderData('continuation-created');
  p.then(deferredVerify('continuation-created'));
  setContinuationPreservedEmbedderData('after');
  %PerformMicrotaskCheckpoint();
}
{
  // Reject path
  setContinuationPreservedEmbedderData('init');
  let reject;
  const p = new Promise((_, r) => {
    reject = r;
  });

  setContinuationPreservedEmbedderData('resolve');
  reject();

  setContinuationPreservedEmbedderData('continuation-created');
  p.catch(deferredVerify('continuation-created'));
  setContinuationPreservedEmbedderData('after');
  %PerformMicrotaskCheckpoint();
}

// Should propagate through thenables
function thenable(expected) {
  const verify = deferredVerify(expected);
  return {
    then(fulfill) {
      verify();
      fulfill();
    }
  }
}

async function testThenables() {
  setContinuationPreservedEmbedderData('plain thenable');
  await thenable('plain thenable');

  setContinuationPreservedEmbedderData('resolved thenable');
  await Promise.resolve(thenable('resolved thenable'));

  setContinuationPreservedEmbedderData('async returned thenable');
  await (async () => thenable('async returned thenable'))();
}

testThenables();

%PerformMicrotaskCheckpoint();

//
// Test helpers
//
function deferredVerify(expected) {
  return () => {
    assertEquals(expected, getContinuationPreservedEmbedderData());
  };
}
