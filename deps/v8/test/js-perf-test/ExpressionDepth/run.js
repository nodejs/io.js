// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Logical Expressions
AddTest('Or-Value', '||', false);
AddTest('Or-Test', '||', true);
AddTest('And-Value', '&&', false);
AddTest('And-Test', '&&', true);
AddTest('Comma-Value', ',', false);
AddTest('Comma-Test', ',', true);
AddTest('Comma-Test', ',', true);

// Compare Expressions
AddTest('Equals-Value', '==', false);
AddTest('Equals-Test', '==', true);
AddTest('StrictEquals-Value', '===', false);
AddTest('StrictEquals-Test', '===', true);
AddTest('GreaterThan-Value', '>', false);
AddTest('GreaterThan-Test', '>', true);

// Binary Expressions
AddTest('Add', '+');
AddTest('Sub', '-');
AddTest('BitwiseOr', '|');
AddTestCustomPrologue('StringConcat', '+', '"string" +');
AddTestCustomPrologue('TemplateString', '} ${', '`${', '}`');

function TestExpressionDepth(depth, expression, prologue, epilogue) {
  var func = '(function f(a) {\n' + prologue;
  for (var i = 0; i < depth; i++) {
    func += 'a ' + expression;
  }
  func += 'a' + epilogue + '})();'
  eval(func);
}

function RunTest(name, expression, prologue, epilogue) {
  var low_depth = 0;
  var high_depth = 1;

  // Find the upper limit where depth breaks down.
  try {
    while (high_depth <= 65536) {
      TestExpressionDepth(high_depth, expression, prologue, epilogue);
      low_depth = high_depth;
      high_depth *= 4;
    }
    // Looks like we can't get the depth to break down, just report
    // the maximum depth tested.
    print(name +  '-ExpressionDepth(Score): ' + low_depth);
    return;
  } catch (e) {
    if (!(e instanceof RangeError)) {
      print(name +  '-ExpressionDepth(Score): ERROR');
      return;
    }
  }

  // Binary search the actual limit.
  while (low_depth + 1 < high_depth) {
    var mid_depth = Math.round((low_depth + high_depth) / 2);
    try {
      TestExpressionDepth(mid_depth, expression, prologue, epilogue);
      low_depth = mid_depth;
    } catch (e) {
      if (!(e instanceof RangeError)) {
        print(name +  '-ExpressionDepth(Score): ERROR');
        return;
      }
      high_depth = mid_depth;
    }
  }

  print(name + '-ExpressionDepth(Score): ' + low_depth);
}

function AddTest(name, expression, in_test) {
  prologue = '';
  epilogue = '';
  if (in_test) {
    prologue = 'if (';
    epilogue = ') { return 1; }';
  }
  RunTest(name, expression, prologue, epilogue);
}

function AddTestCustomPrologue(name, expression, prologue, epilogue='') {
  RunTest(name, expression, prologue, epilogue);
}
