// Copyright 2018 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --expose-async-hooks

// Check for async/await asyncIds relation
(function() {
  let asyncIds = [], triggerIds = [];
  let ah = async_hooks.createHook({
    init(asyncId, type, triggerAsyncId, resource) {
      if (type !== 'PROMISE') {
        return;
      }
      asyncIds.push(asyncId);
      triggerIds.push(triggerAsyncId);
    },
  });
  ah.enable();

  // Simplified version of Node.js util.promisify(setTimeout),
  // but d8 ignores the timeout of setTimeout.
  function sleep0() {
    const promise = new Promise(function(resolve, reject) {
      try {
        setTimeout((err, ...values) => {
          if (err) {
            reject(err);
          } else {
            resolve(values[0]);
          }
        }, 0);
      } catch (err) {
        reject(err);
      }
    });
    return promise;
  }

  async function foo() {
    await sleep0();
  }

  assertPromiseResult(
    foo().then(function() {
      assertEquals(triggerIds[2], asyncIds[1]);
      assertEquals(triggerIds[3], asyncIds[0]);
      assertEquals(triggerIds[4], asyncIds[3]);
      assertEquals(triggerIds[6], asyncIds[5]);
    }));
})();
