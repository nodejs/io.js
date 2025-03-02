// Copyright 2013 the V8 project authors. All rights reserved.
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

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

deopt_trigger = 0;
side_effect = 0;

function test(a, b, c, d, e, v) {
  // This test expects some specific input values.
  assertEquals(10.0, a);
  assertEquals(20.0, b);
  assertEquals(30.0, c);
  assertEquals(40.0, d);
  assertEquals(50.0, e);
  assertEquals(1.5, v);

  // Perform a few double calculations.
  a = a * 0.1;
  b = b * 0.2;
  c = c * 0.3;
  d = d * 0.4;
  e = e * 0.5;

  // Write to a field of a global object. As for any side effect, a HSimulate
  // will be introduced after the instructions to support this. If we deopt
  // later in this function, the execution will resume in full-codegen after
  // this point.
  side_effect++;
  // The following field of the global object will be deleted to force a deopt.
  // If we use %DeoptimizeFunction, all values will be on the frame due to the
  // call and we will not exercise the translation mechanism handling fp
  // registers.
  deopt_trigger = v;

  // Do a few more calculations using the previous values after our deopt point
  // so the floating point registers which hold those values are recorded in the
  // environment and will be used during deoptimization.
  a = a * v;
  b = b * v;
  c = c * v;
  d = d * v;
  e = e * v;

  // Check that we got the expected results.
  assertEquals(1.5,  a);
  assertEquals(6,    b);
  assertEquals(13.5, c);
  assertEquals(24,   d);
  assertEquals(37.5, e);
}
%PrepareFunctionForOptimization(test);


test(10.0, 20.0, 30.0, 40.0, 50.0, 1.5);
test(10.0, 20.0, 30.0, 40.0, 50.0, 1.5);
%OptimizeFunctionOnNextCall(test);
test(10.0, 20.0, 30.0, 40.0, 50.0, 1.5);
assertOptimized(test);

// By deleting the field we are forcing the code to deopt when the field is
// read on next execution.
delete deopt_trigger;
assertUnoptimized(test);
test(10.0, 20.0, 30.0, 40.0, 50.0, 1.5);
