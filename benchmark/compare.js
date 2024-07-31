'use strict';

const { spawn, fork } = require('node:child_process');
const { inspect } = require('util');
const path = require('path');
const CLI = require('./_cli.js');
const BenchmarkProgress = require('./_benchmark_progress.js');

//
// Parse arguments
//
const cli = new CLI(`usage: ./node compare.js [options] [--] <category> ...
  Run each benchmark in the <category> directory many times using two different
  node versions. More than one <category> directory can be specified.
  The output is formatted as csv, which can be processed using for
  example 'compare.R'.

  --new      ./new-node-binary  new node binary (required)
  --old      ./old-node-binary  old node binary (required)
  --runs     30                 number of samples
  --filter   pattern            includes only benchmark scripts matching
                                <pattern> (can be repeated)
  --exclude  pattern            excludes scripts matching <pattern> (can be
                                repeated)
  --set      variable=value     set benchmark variable (can be repeated)
  --no-progress                 don't show benchmark progress indicator

  Examples:
    --set CPUSET=0            Runs benchmarks on CPU core 0.
    --set CPUSET=0-2          Specifies that benchmarks should run on CPU cores 0 to 2.

  Note: The CPUSET format should match the specifications of the 'taskset' command
`, { arrayArgs: ['set', 'filter', 'exclude'], boolArgs: ['no-progress'] });

if (!cli.optional.new || !cli.optional.old) {
  cli.abort(cli.usage);
}

const binaries = ['old', 'new'];
const runs = cli.optional.runs ? parseInt(cli.optional.runs, 10) : 30;
const benchmarks = cli.benchmarks();

if (benchmarks.length === 0) {
  console.error('No benchmarks found');
  process.exitCode = 1;
  return;
}

// Create queue from the benchmarks list such both node versions are tested
// `runs` amount of times each.
// Note: BenchmarkProgress relies on this order to estimate
// how much runs remaining for a file. All benchmarks generated from
// the same file must be run consecutively.
const queue = [];
for (const filename of benchmarks) {
  for (let iter = 0; iter < runs; iter++) {
    for (const binary of binaries) {
      queue.push({ binary, filename, iter });
    }
  }
}
// queue.length = binary.length * runs * benchmarks.length

// Print csv header
console.log('"binary","filename","configuration","rate","time"');

const kStartOfQueue = 0;

const showProgress = !cli.optional['no-progress'];
let progress;
if (showProgress) {
  progress = new BenchmarkProgress(queue, benchmarks);
  progress.startQueue(kStartOfQueue);
}

(function recursive(i) {
  const job = queue[i];
  const resolvedPath = path.resolve(__dirname, job.filename);

  const cpuCore = cli.getCpuCoreSetting();
  let child;
  if (cpuCore !== null) {
    const spawnArgs = ['-c', cpuCore, cli.optional[job.binary], resolvedPath, ...cli.optional.set];
    child = spawn('taskset', spawnArgs, {
      env: process.env,
      stdio: ['inherit', 'inherit', 'inherit', 'ipc'],
    });
  } else {
    child = fork(resolvedPath, cli.optional.set, {
      execPath: cli.optional[job.binary],
    });
  }

  child.on('message', (data) => {
    if (data.type === 'report') {
      // Construct configuration string, " A=a, B=b, ..."
      let conf = '';
      for (const key of Object.keys(data.conf)) {
        conf += ` ${key}=${inspect(data.conf[key])}`;
      }
      conf = conf.slice(1);
      // Escape quotes (") for correct csv formatting
      conf = conf.replace(/"/g, '""');

      console.log(`"${job.binary}","${job.filename}","${conf}",` +
                  `${data.rate},${data.time}`);
      if (showProgress) {
        // One item in the subqueue has been completed.
        progress.completeConfig(data);
      }
    } else if (showProgress && data.type === 'config') {
      // The child has computed the configurations, ready to run subqueue.
      progress.startSubqueue(data, i);
    }
  });

  child.once('close', (code) => {
    if (code) {
      process.exit(code);
    }
    if (showProgress) {
      progress.completeRun(job);
    }

    // If there are more benchmarks execute the next
    if (i + 1 < queue.length) {
      recursive(i + 1);
    }
  });
})(kStartOfQueue);
