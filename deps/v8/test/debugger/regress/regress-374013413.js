// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-inspector

var Debug = debug.Debug;
Debug.sendMessageForMethodChecked('Runtime.enable', {});
const {msgid, msg} = Debug.createMessage('Runtime.evaluate', {
  expression: 'while(true) {}',
  throwOnSideEffect: true,
  timeout: 1000,
})
Debug.sendMessage(msg);
Debug.takeReplyChecked(msgid).toString();
