// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function benchName(bench, setup) {
  var name = bench.name;
  if (setup) name += "/" + setup.name;
  return name;
}

function slowBenchName(bench, setup) {
  return benchName(bench, setup) + " (Slow)";
}

function slow(setupFunction) {
  return () => {
    setupFunction();
    // Trigger RegExp slow paths.
    const regExpExec = re.exec;
    re.exec = (str) => regExpExec.call(re, str);
  };
}

function createHaystack() {
  let s = "abCdefgz";
  for (let i = 0; i < 3; i++) s += s;
  return s;
}

function createBenchmarkSuite(name) {
  return new BenchmarkSuite(
    name, [1000],
    benchmarks.map(([bench, setup]) =>
        new Benchmark(benchName(bench, setup), false, false, 0, bench,
                      setup, null, null, 2)));
}

function createSlowBenchmarkSuite(name) {
  return new BenchmarkSuite(
    "Slow" + name, [1000],
    benchmarks.map(([bench, setup]) =>
        new Benchmark(slowBenchName(bench, setup), false, false, 0, bench,
                      slow(setup), null, null, 2)));
}
