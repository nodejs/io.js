// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} =
    InspectorTest.start('Checks that Object.prototype.then is not called for REPL mode results');

const source = `
Object.prototype.then = function() {
  console.log('Hello from then');
  delete Object.prototype.then;
}`;

(async () => {
  await Protocol.Runtime.enable();

  Protocol.Runtime.onConsoleAPICalled(({ params }) => {
    InspectorTest.log('Unexpected call to console API!');
    InspectorTest.logObject(params);
  });

  contextGroup.addScript(source);

  await Protocol.Runtime.evaluate({ expression: '7 + 7', replMode: true });

  InspectorTest.completeTest();
})();
