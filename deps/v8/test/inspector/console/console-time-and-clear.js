// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
  InspectorTest.start('Tests console.clear() does not clear timers');

const expression = `
  console.time("a");
  console.clear();
  console.timeEnd("a");
  console.log("End of test");
`;

Protocol.Runtime.enable();
Protocol.Runtime.evaluate({ expression: expression });

Protocol.Runtime.onConsoleAPICalled(function(result) {
  let message = result.params.args[0];
  if (message.value == "End of test") {
    InspectorTest.completeTest();
  }
  message.value = message.value.replaceAll(/[0-9]*\.[0-9]+|[0-9]+/g, '<number>');
  InspectorTest.logObject(message);
});
