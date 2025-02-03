# Node.js Core Benchmarks

This folder contains code and data used to measure performance
of different Node.js implementations and different ways of
writing JavaScript run by the built-in JavaScript engine.

For a detailed guide on how to write and run benchmarks in this
directory, see [the guide on benchmarks](../doc/contributing/writing-and-running-benchmarks.md).

## Table of Contents

* [File tree structure](#file-tree-structure)
* [Common API](#common-api)

## File tree structure

### Directories

Benchmarks testing the performance of a single node submodule are placed into a
directory with the corresponding name, so that they can be executed by submodule
or individually.
Benchmarks that span multiple submodules may either be placed into the `misc`
directory or into a directory named after the feature they benchmark.
E.g. benchmarks for various new ECMAScript features and their pre-ES2015
counterparts are placed in a directory named `es`.
Fixtures that are not specific to a certain benchmark but can be reused
throughout the benchmark suite should be placed in the `fixtures` directory.

### Other Top-level files

The top-level files include common dependencies of the benchmarks
and the tools for launching benchmarks and visualizing their output.
The actual benchmark scripts should be placed in their corresponding
directories.

* `_benchmark_progress.js`: implements the progress bar displayed
  when running `compare.js`
* `_cli.js`: parses the command line arguments passed to `compare.js`,
  `run.js` and `scatter.js`
* `_cli.R`: parses the command line arguments passed to `compare.R`
* `_http-benchmarkers.js`: selects and runs external tools for benchmarking
  the `http` subsystem.
* `bar.R`: R script for visualizing the output of benchmarks with bar plots.
* `common.js`: see [Common API](#common-api).
* `compare.js`: command line tool for comparing performance between different
  Node.js binaries.
* `compare.R`: R script for statistically analyzing the output of
  `compare.js`
* `run.js`: command line tool for running individual benchmark suite(s).
* `scatter.js`: command line tool for comparing the performance
  between different parameters in benchmark configurations,
  for example to analyze the time complexity.
* `scatter.R`: R script for visualizing the output of `scatter.js` with
  scatter plots.

## Common API

The common.js module is used by benchmarks for consistency across repeated
tasks. It has a number of helpful functions and properties to help with
writing benchmarks.

### `createBenchmark(fn, configs[, options])`

See [the guide on writing benchmarks](../doc/contributing/writing-and-running-benchmarks.md#basics-of-a-benchmark).

### `default_http_benchmarker`

The default benchmarker used to run HTTP benchmarks.
See [the guide on writing HTTP benchmarks](../doc/contributing/writing-and-running-benchmarks.md#creating-an-http-benchmark).

### `PORT`

The default port used to run HTTP benchmarks.
See [the guide on writing HTTP benchmarks](../doc/contributing/writing-and-running-benchmarks.md#creating-an-http-benchmark).

### `sendResult(data)`

Used in special benchmarks that can't use `createBenchmark` and the object
it returns to accomplish what they need. This function reports timing
data to the parent process (usually created by running `compare.js`, `run.js` or
`scatter.js`).
