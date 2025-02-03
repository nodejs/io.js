// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --ignore-unhandled-promises

Debug = debug.Debug

let events = 0;

function listener(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.Exception) return;
  events++;
}

async function thrower() {
  throw "a";  // Exception a
}

var reject = () => Promise.reject("b");  // Exception b

async function awaitReturn() { await 1; return; }

async function scalar() { return 1; }

function nothing() { return 1; }

function rejectConstructor() {
  return new Promise((resolve, reject) => reject("c"));  // Exception c
}

async function argThrower(x = (() => { throw "d"; })()) { }  // Exception d

async function awaitThrow() {
  await undefined;
  throw "e";  // Exception e
}

function constructorReject() {
  return new Promise((resolve, reject) =>
    Promise.resolve().then(() =>
      reject("f")  // Exception f
    )
  );
}

function constructorThrow() {
  return new Promise((resolve, reject) =>
    Promise.resolve().then(() => {
      throw "g";  // Exception g
    })
  );
}

function suppressThrow() {
  return thrower();
}

async function caught(producer) {
  try {
    await producer();
  } catch (e) {
  }
}

async function uncaught(producer) {
  await producer();
}

async function indirectUncaught(producer) {
  await uncaught(producer);
}

async function indirectCaught(producer) {
  try {
    await uncaught(producer);
  } catch (e) {
  }
}

function dotCatch(producer) {
  Promise.resolve(producer()).catch(() => {});
}

function indirectReturnDotCatch(producer) {
  (async() => producer())().catch(() => {});
}

function indirectAwaitDotCatch(producer) {
  (async() => await producer())().catch(() => {});
}

function nestedDotCatch(producer) {
  Promise.resolve(producer()).then().catch(() => {});
}

async function indirectAwaitCatch(producer) {
  try {
    await (() => producer())();
  } catch (e) {
  }
}

function switchCatch(producer) {
  let resolve;
  let promise = new Promise(r => resolve = r);
  async function localCaught() {
    try {
      await promise;  // force switching to localUncaught and back
      await producer();
    } catch (e) { }
  }
  async function localUncaught() {
    await undefined;
    resolve();
  }
  localCaught();
  localUncaught();
}

function switchDotCatch(producer) {
  let resolve;
  let promise = new Promise(r => resolve = r);
  async function localCaught() {
    await promise;  // force switching to localUncaught and back
    await producer();
  }
  async function localUncaught() {
    await undefined;
    resolve();
  }
  localCaught().catch(() => {});
  localUncaught();
}

let catches = [caught,
               indirectCaught,
               indirectAwaitCatch,
               indirectAwaitDotCatch,
               indirectReturnDotCatch,
               switchCatch,
               switchDotCatch];
let noncatches = [uncaught, indirectUncaught];
let lateCatches = [dotCatch,
                   nestedDotCatch];

let throws = [rejectConstructor, thrower, reject, argThrower, suppressThrow];
let nonthrows = [awaitReturn, scalar, nothing];
let lateThrows = [awaitThrow, constructorReject];
let uncatchable = [constructorThrow];

let cases = [];

for (let producer of throws.concat(lateThrows)) {
  for (let consumer of catches) {
    cases.push({ producer, consumer, expectedEvents: 1, caught: true });
    cases.push({ producer, consumer, expectedEvents: 0, caught: false });
  }
}

for (let producer of throws.concat(lateThrows)) {
  for (let consumer of noncatches) {
    cases.push({ producer, consumer, expectedEvents: 1, caught: true });
    cases.push({ producer, consumer, expectedEvents: 1, caught: false });
  }
}

for (let producer of nonthrows) {
  for (let consumer of catches.concat(noncatches, lateCatches)) {
    cases.push({ producer, consumer, expectedEvents: 0, caught: true });
    cases.push({ producer, consumer, expectedEvents: 0, caught: false });
  }
}

for (let producer of uncatchable) {
  for (let consumer of catches.concat(noncatches, lateCatches)) {
    cases.push({ producer, consumer, expectedEvents: 1, caught: true });
    cases.push({ producer, consumer, expectedEvents: 1, caught: false });
  }
}

for (let producer of lateThrows) {
  for (let consumer of lateCatches) {
    cases.push({ producer, consumer, expectedEvents: 1, caught: true });
    cases.push({ producer, consumer, expectedEvents: 0, caught: false });
  }
}

for (let producer of throws) {
  for (let consumer of lateCatches) {
    cases.push({ producer, consumer, expectedEvents: 1, caught: true });
    cases.push({ producer, consumer, expectedEvents: 1, caught: false });
  }
}


function runPart(n) {
  let subcases = cases.slice(n * cases.length / 4,
                             ((n + 1) * cases.length) / 4);
  for (let {producer, consumer, expectedEvents, caught} of subcases) {
    Debug.setListener(listener);
    if (caught) {
      Debug.setBreakOnException();
    } else {
      Debug.setBreakOnUncaughtException();
    }

    events = 0;
    consumer(producer);
    %PerformMicrotaskCheckpoint();

    Debug.setListener(null);
    if (caught) {
      Debug.clearBreakOnException();
    } else {
      Debug.clearBreakOnUncaughtException();
    }
    if (expectedEvents != events) {
      %AbortJS(`producer ${producer} consumer ${consumer} expectedEvents ` +
               `${expectedEvents} caught ${caught} events ${events}`);
    }
  }
}
