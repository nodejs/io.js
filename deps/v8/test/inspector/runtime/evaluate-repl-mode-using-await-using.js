// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

const {Protocol} = InspectorTest.start(
    "Tests if `using` and `await using` works in REPL mode.");

(async function() {
    InspectorTest.logMessage(await Protocol.Runtime.evaluate({ expression: 'using x = { value: 1, [Symbol.dispose]() {throw new Error("fail");}};', replMode: true }));
    InspectorTest.logMessage(await Protocol.Runtime.evaluate({ expression: 'x', replMode: true }));


    InspectorTest.logMessage(await Protocol.Runtime.evaluate({ expression: 'await using y = { value: 2, [Symbol.asyncDispose]() {throw new Error("fail");}};', replMode: true }));
    InspectorTest.logMessage(await Protocol.Runtime.evaluate({ expression: 'y', replMode: true }));

    InspectorTest.completeTest();
  })();
