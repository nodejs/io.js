// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const { Protocol } = InspectorTest.start('Don\'t crash when pausing while entering a nested with scope');

const script = '(function foo(a, b) { with (a) with (b) return undefinedReference })({}, {})';

(async () => {
  await Protocol.Debugger.enable();
  await Protocol.Debugger.setPauseOnExceptions({ state: 'all' });

  Protocol.Runtime.evaluate({ expression: script });
  await Protocol.Debugger.oncePaused();
  await Protocol.Debugger.resume();

  InspectorTest.completeTest();
})();
