// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --expose-gc --expose-externalize-string

const str = String.fromCharCode(849206214, 00, 00);
gc();
const Bar = this.SharedStructType(new String('a'));
const bar = Bar();
bar.a = str;
externalizeString(str);
bar[str] = 'foo';
const str2 = String.fromCharCode(849206214, 00, 00);
gc();
const bar2 = Bar();
bar2.a = str2;
externalizeString(str2);
bar2[str2] = 'foo';
