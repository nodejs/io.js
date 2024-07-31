// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} =
    InspectorTest.start('Checks that accessing command line API functions are considered side-effecty (except $*)');

(async () => {
  let { result } = await Protocol.Runtime.evaluate({
    expression: 'debug',
    includeCommandLineAPI: true,
    throwOnSideEffect: true,
  });
  InspectorTest.logMessage(result);

  ({ result } = await Protocol.Runtime.evaluate({
    expression: '$0',
    includeCommandLineAPI: true,
    throwOnSideEffect: true,
  }));
  InspectorTest.logMessage(result);

  InspectorTest.completeTest();
})();
