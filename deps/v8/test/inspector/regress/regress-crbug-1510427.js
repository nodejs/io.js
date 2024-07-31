// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} =
    InspectorTest.start('Checks that we don\'t crash with a mixture of direct/indirect eval and language modes');

function createSnippet(strict_outer, direct_eval) {
  const use_strict_outer = strict_outer ? "'use strict';" : "";
  const eval_call = direct_eval ? "eval(x);" : "(0, eval)(x);";

  return `
    ${use_strict_outer}
    var x = \`
      (function y() {
        with ({}) {
          var z = () => { debugger; };
          z();
        }
      })();
    \`;
    ${eval_call}
  `;
}

const snippets = [
  createSnippet(false, false),
  createSnippet(false, true),
  createSnippet(true, false),
  createSnippet(true, true), // Must be a SyntaxError.
];

(async () => {
  await Protocol.Debugger.enable();

  for (const snippet of snippets) {
    InspectorTest.log(`Pausing via the snippet: ${snippet}`);
    const evalPromise = Protocol.Runtime.evaluate({expression: snippet});

    const event = await Promise.race([Protocol.Debugger.oncePaused(), evalPromise]);
    if (event.result?.exceptionDetails) {
      InspectorTest.logObject(event.result.result);
      continue;
    }

    const { params: { callFrames: [{ callFrameId }] } } = event;
    InspectorTest.log('Evaluating "this" on top call frame: ');
    const { result } = await Protocol.Debugger.evaluateOnCallFrame({
      expression: 'this',
      callFrameId,
    });
    InspectorTest.logObject(result);

    await Protocol.Debugger.resume();
  }

  InspectorTest.completeTest();
})();
