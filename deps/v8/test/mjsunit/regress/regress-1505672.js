// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const re = /(?<a>.)/;
// Force slow-path.
re.prototype = new Proxy(RegExp.prototype, {});
assertEquals('','a'.replace(re, '$<0>'));
