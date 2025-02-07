// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.start('Don\'t crash when pausing in an untrusted session.');

(async () => {
  const contextGroup = new InspectorTest.ContextGroup();
  const session = contextGroup.connect(/* isFullyTrusted */ false);
  const { Protocol } = session;

  await Protocol.Debugger.enable();

  Protocol.Runtime.evaluate({ expression: 'debugger' });
  await Protocol.Debugger.oncePaused();
  await Protocol.Debugger.resume();

  InspectorTest.completeTest();
})();
