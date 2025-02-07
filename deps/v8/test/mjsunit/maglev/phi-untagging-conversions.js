// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --maglev --allow-natives-syntax --no-always-turbofan


// In this example, the final block of the graph will be something like:
//
//   1: Phi(#42, y)
//   2: CheckedSmiUntag(1)
//   3: Return(1)
//
// Note how the truncation is unused (but still required, as the graph builder
// doesn't know that all of the inputs of the Phi are Smis).  After Phi
// untagging, the phi will be an Int32, and the truncation is thus not needed
// anymore. It's important that it's removed during Phi untagging, because its
// input is not tagged anymore (since the phi was untagged), and it's not
// necessary to replace it by something else.

function unused_unneeded_CheckedSmiUntag(x) {
  let y = x + 1;
  y = x ? 42 : y;
  return y | 0;
}

%PrepareFunctionForOptimization(unused_unneeded_CheckedSmiUntag);
unused_unneeded_CheckedSmiUntag(1);
unused_unneeded_CheckedSmiUntag(0);
%OptimizeMaglevOnNextCall(unused_unneeded_CheckedSmiUntag);
unused_unneeded_CheckedSmiUntag(1);

// This example is similar as the previous one, except that the graph will
// contain a CheckedTruncateNumberToInt32 instead of the CheckedSmiUntag:
//
//   1: Phi(c1, c2)
//   2: CheckedTruncateNumberToInt32(1)
//   3: Return(undefined)
//
// The conversion only fails when its input is not a number. Since the Phi was
// untagged to a Float64, the conversion can never fail anymore, and should thus
// be omitted.
//
// Note that if the result of the CheckedTruncateNumberToInt32 would have been
// used, the it would have been automatically changed into a
// TruncateFloat64ToInt32.
//
// A side-effect of have this truncation is that it ensures that its input is a
// boxed Float64, which means that subsequent untagging can use
// UncheckedNumberToFloat64 instead of CheckedNumberToFloat64. This is what is
// used here when doing `let as_double = phi + 0.6`. This
// UncheckedNumberToFloat64 will just be dropped, since the input (the phi) is
// already an unboxed Float64.

function unused_unneeded_CheckedTruncateNumberToInt32(x) {
  let c1 = x + 0.5;
  let c2 = x + 1.5;
  let phi = x ? c1 : c2;
  let as_int = phi | 0;
  let as_double = phi + 0.6;
}

%PrepareFunctionForOptimization(unused_unneeded_CheckedTruncateNumberToInt32);
unused_unneeded_CheckedTruncateNumberToInt32(1.5);
%OptimizeMaglevOnNextCall(unused_unneeded_CheckedTruncateNumberToInt32);
unused_unneeded_CheckedTruncateNumberToInt32(1.5);



// In this example, during feedback collection, we provide inputs that cause
// `phi` to always be a Smi, which means that `phi | 0` will have Smi
// feedback. The graph will thus be:
//
//    1: Phi(d, #42)
//    2: CheckedSmiUntag(1)
//    3: Return(1)
//
// Except that Phi untagging will realize that `phi` could be a Float64, and
// will thus decide that it should be a Float64 Phi. In this case, the
// conversion should not be dropped, because if the Phi is not an Int32, then it
// should not be returned directly, but instead be truncated. In practice
// though, we'll rather replace it by a CheckedTruncateFloat64ToInt32, which
// will fail if the Float64 isn't an Int32, causing a deopt, and the reoptimized
// code will have a better feedback (this is easier than to try to patch the
// `Return(1)` to return something else).
function unused_required_CheckedSmiUntag(x) {
  x = x + 0.5; // ensuring Float64 alternative
  let d = x + 2.53;
  let phi = x ? d : 42;
  return phi | 0;
}

%PrepareFunctionForOptimization(unused_required_CheckedSmiUntag);
unused_required_CheckedSmiUntag(-0.5);
%OptimizeMaglevOnNextCall(unused_required_CheckedSmiUntag);
assertEquals(42, unused_required_CheckedSmiUntag(-0.5));
assertOptimized(unused_required_CheckedSmiUntag);
// If the CheckedSmiUntag is dropped, then the truncation won't be done, and the
// non-truncated float (3.53) will be returned. Instead, if the conversion is
// changed to CheckedTruncateFloat64ToInt32, then it will deopt, and we'll get
// the correct result of 3.
assertEquals(3, unused_required_CheckedSmiUntag(0.5));
assertUnoptimized(unused_required_CheckedSmiUntag);



// Finally, int this example, during feedback collection, `phi` will always be a
// Smi, which means that `phi + 2` will be an Int32AddWithOverflow, preceeded by
// a CheckedSmiUntag(Phi):
//
//    1: Phi(d, #42)
//    2: CheckedSmiUntag(1)
//    3: Int32AddWithOverflow(2, #2)
//
// However, Phi untagging will detect that `phi` should be a Float64 phi. In
// that case, it's important that the CheckedSmiUntag conversion isn't dropped,
// and becomes a deopting Float64->Int32 conversion that deopts when its input
// cannot be converted to Int32 without loss of precision (ie, it should become
// a CheckedTruncateFloat64ToInt32 rather than a TruncateFloat64ToInt32). Then,
// when the Phi turns out to be a non-Smi Float64, the function should deopt.
function used_required_deopting_Float64ToInt32(x) {
  x = x + 0.5; // ensuring Float64 alternative
  let d = x + 2.53;
  let phi = x ? d : 42;
  return phi + 2;
}
%PrepareFunctionForOptimization(used_required_deopting_Float64ToInt32);
used_required_deopting_Float64ToInt32(-0.5);
%OptimizeMaglevOnNextCall(used_required_deopting_Float64ToInt32);
assertEquals(44, used_required_deopting_Float64ToInt32(-0.5));
// The next call should cause a deopt, since `phi` will be 4.53, which shouldn't
// be truncated to go into the Int32AddWithOverflow but should instead cause a
// deopt to allow the `phi + 2` to be computed on double values.
assertEquals(1.5+0.5+2.53+2, used_required_deopting_Float64ToInt32(1.5));
assertUnoptimized(used_required_deopting_Float64ToInt32);
