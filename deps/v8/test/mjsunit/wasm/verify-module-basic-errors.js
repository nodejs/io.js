// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function Foo() { }

assertThrows(function() { new WebAssembly.Module(); })
assertThrows(function() { new WebAssembly.Module(0); })
assertThrows(function() { new WebAssembly.Module("s"); })
assertThrows(function() { new WebAssembly.Module(undefined); })
assertThrows(function() { new WebAssembly.Module(1.1); })
assertThrows(function() { new WebAssembly.Module(1/0); })
assertThrows(function() { new WebAssembly.Module(null); })
assertThrows(function() { new WebAssembly.Module(new Foo()); })
assertThrows(function() { new WebAssembly.Module(new ArrayBuffer(0)); })
assertThrows(function() { new WebAssembly.Module(new ArrayBuffer(7)); })
