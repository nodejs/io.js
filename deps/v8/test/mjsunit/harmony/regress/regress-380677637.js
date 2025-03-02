// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

async function* generator() {}
const resource = generator();

function MyConstructor(executor) {
  throw Error('Should not be called');
}

// When Promise.then() needs to create a new Promise object,
// MyConstructor will be used as the constructor method to create the object.
Object.defineProperty(Promise, Symbol.species, {'value': MyConstructor});

// Trigger AsyncIteratorPrototypeAsyncDispose()
resource[Symbol.asyncDispose]();
