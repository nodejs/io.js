// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('../base.js');

d8.file.execute('small.js');
d8.file.execute('medium.js');
d8.file.execute('large.js');

var success = true;

function PrintResult(name, result) {
  // Multiplying results by 1000000 to have larger numbers (otherwise it's going
  // to be a lot of small numbers like 0.00586).
  print(name + '-Compile(Score): ' + result * 1000000);
}

function PrintStep(name) {}

function PrintError(name, error) {
  PrintResult(name, error);
  success = false;
}

BenchmarkSuite.config.doWarmup = undefined;
BenchmarkSuite.config.doDeterministic = undefined;

BenchmarkSuite.RunSuites({ NotifyResult: PrintResult,
                           NotifyError: PrintError,
                           NotifyStep: PrintStep });
