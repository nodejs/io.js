// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

if (this.Worker) {
  var __v_8 =
    `var __v_9 = new Worker('postMessage(42)', {type: 'string'});
     onmessage = function({data:parentMsg}) {
       __v_9.postMessage(parentMsg);
     };`;
  var __v_9 = new Worker(__v_8, {type: 'string'});
  __v_9.postMessage(9);
}
