'use strict';

const child_process = require('child_process');
const path = require('path');
const fs = require('fs');

const requirementsURL =
  'https://github.com/nodejs/node/blob/HEAD/doc/contributing/writing-and-running-benchmarks.md#http-benchmark-requirements';

// The port used by servers and wrk
exports.PORT = Number(process.env.PORT) || 12346;

class AutocannonBenchmarker {
  constructor() {
    const shell = (process.platform === 'win32');
    this.name = 'autocannon';
    this.opts = { shell };
    this.executable = shell ? 'autocannon.cmd' : 'autocannon';
    const result = child_process.spawnSync(this.executable, ['-h'], this.opts);
    if (shell) {
      this.present = (result.status === 0);
    } else {
      this.present = !(result.error && result.error.code === 'ENOENT');
    }
  }

  create(options) {
    const args = [
      '-d', options.duration,
      '-c', options.connections,
      '-j',
      '-n',
    ];
    for (const field in options.headers) {
      if (this.opts.shell) {
        args.push('-H', `'${field}=${options.headers[field]}'`);
      } else {
        args.push('-H', `${field}=${options.headers[field]}`);
      }
    }
    const scheme = options.scheme || 'http';
    args.push(`${scheme}://127.0.0.1:${options.port}${options.path}`);
    const child = child_process.spawn(this.executable, args, this.opts);
    return child;
  }

  processResults(output) {
    let result;
    try {
      result = JSON.parse(output);
    } catch {
      return undefined;
    }
    if (!result || !result.requests || !result.requests.average) {
      return undefined;
    }
    return result.requests.average;
  }
}

class WrkBenchmarker {
  constructor() {
    this.name = 'wrk';
    this.executable = 'wrk';
    const result = child_process.spawnSync(this.executable, ['-h']);
    this.present = !(result.error && result.error.code === 'ENOENT');
  }

  create(options) {
    const duration = typeof options.duration === 'number' ?
      Math.max(options.duration, 1) :
      options.duration;
    const scheme = options.scheme || 'http';
    const args = [
      '-d', duration,
      '-c', options.connections,
      '-t', Math.min(options.connections, require('os').availableParallelism() || 8),
      `${scheme}://127.0.0.1:${options.port}${options.path}`,
    ];
    for (const field in options.headers) {
      args.push('-H', `${field}: ${options.headers[field]}`);
    }
    const child = child_process.spawn(this.executable, args);
    return child;
  }

  processResults(output) {
    const throughputRe = /Requests\/sec:[ \t]+([0-9.]+)/;
    const match = output.match(throughputRe);
    const throughput = match && +match[1];
    if (!isFinite(throughput)) {
      return undefined;
    }
    return throughput;
  }
}

/**
 * Simple, single-threaded benchmarker for testing if the benchmark
 * works
 */
class TestDoubleBenchmarker {
  constructor(type) {
    // `type` is the type of benchmarker. Possible values are 'http', 'https',
    // and 'http2'.
    this.name = `test-double-${type}`;
    this.executable = path.resolve(__dirname, '_test-double-benchmarker.js');
    this.present = fs.existsSync(this.executable);
    this.type = type;
  }

  create(options) {
    process.env.duration ||= options.duration || 5;

    const scheme = options.scheme || 'http';
    const env = {
      test_url: `${scheme}://127.0.0.1:${options.port}${options.path}`,
      ...process.env,
    };

    const child = child_process.fork(this.executable,
                                     [this.type],
                                     { silent: true, env });
    return child;
  }

  processResults(output) {
    let result;
    try {
      result = JSON.parse(output);
    } catch {
      return undefined;
    }
    return result.throughput;
  }
}

/**
 * HTTP/2 Benchmarker
 */
class H2LoadBenchmarker {
  constructor() {
    this.name = 'h2load';
    this.executable = 'h2load';
    const result = child_process.spawnSync(this.executable, ['-h']);
    this.present = !(result.error && result.error.code === 'ENOENT');
  }

  create(options) {
    const args = [];
    if (typeof options.requests === 'number')
      args.push('-n', options.requests);
    if (typeof options.clients === 'number')
      args.push('-c', options.clients);
    if (typeof options.threads === 'number')
      args.push('-t', options.threads);
    if (typeof options.maxConcurrentStreams === 'number')
      args.push('-m', options.maxConcurrentStreams);
    if (typeof options.initialWindowSize === 'number')
      args.push('-w', options.initialWindowSize);
    if (typeof options.sessionInitialWindowSize === 'number')
      args.push('-W', options.sessionInitialWindowSize);
    if (typeof options.rate === 'number')
      args.push('-r', options.rate);
    if (typeof options.ratePeriod === 'number')
      args.push(`--rate-period=${options.ratePeriod}`);
    if (typeof options.duration === 'number')
      args.push('-T', options.duration);
    if (typeof options.timeout === 'number')
      args.push('-N', options.timeout);
    if (typeof options.headerTableSize === 'number')
      args.push(`--header-table-size=${options.headerTableSize}`);
    if (typeof options.encoderHeaderTableSize === 'number') {
      args.push(
        `--encoder-header-table-size=${options.encoderHeaderTableSize}`);
    }
    const scheme = options.scheme || 'http';
    const host = options.host || '127.0.0.1';
    args.push(`${scheme}://${host}:${options.port}${options.path}`);
    const child = child_process.spawn(this.executable, args);
    return child;
  }

  processResults(output) {
    const rex = /(\d+\.\d+) req\/s/;
    return rex.exec(output)[1];
  }
}

const http_benchmarkers = [
  new WrkBenchmarker(),
  new AutocannonBenchmarker(),
  new TestDoubleBenchmarker('http'),
  new TestDoubleBenchmarker('https'),
  new TestDoubleBenchmarker('http2'),
  new H2LoadBenchmarker(),
];

const benchmarkers = {};

http_benchmarkers.forEach((benchmarker) => {
  benchmarkers[benchmarker.name] = benchmarker;
  if (!exports.default_http_benchmarker && benchmarker.present) {
    exports.default_http_benchmarker = benchmarker.name;
  }
});

exports.run = function(options, callback) {
  options = {
    port: exports.PORT,
    path: '/',
    connections: 100,
    duration: 5,
    benchmarker: exports.default_http_benchmarker,
    ...options,
  };
  if (!options.benchmarker) {
    callback(new Error('Could not locate required http benchmarker. See ' +
                       `${requirementsURL} for further instructions.`));
    return;
  }
  const benchmarker = benchmarkers[options.benchmarker];
  if (!benchmarker) {
    callback(new Error(`Requested benchmarker '${options.benchmarker}' ` +
                       'is  not supported'));
    return;
  }
  if (!benchmarker.present) {
    callback(new Error(`Requested benchmarker '${options.benchmarker}' ` +
                       'is  not installed'));
    return;
  }

  const benchmarker_start = process.hrtime.bigint();

  const child = benchmarker.create(options);

  child.stderr.pipe(process.stderr);

  let stdout = '';
  child.stdout.setEncoding('utf8');
  child.stdout.on('data', (chunk) => stdout += chunk);

  child.once('close', (code) => {
    const benchmark_end = process.hrtime.bigint();
    if (code) {
      let error_message = `${options.benchmarker} failed with ${code}.`;
      if (stdout !== '') {
        error_message += ` Output: ${stdout}`;
      }
      callback(new Error(error_message), code);
      return;
    }

    const result = benchmarker.processResults(stdout);
    if (result === undefined) {
      callback(new Error(
        `${options.benchmarker} produced strange output: ${stdout}`), code);
      return;
    }

    const elapsed = benchmark_end - benchmarker_start;
    callback(null, code, options.benchmarker, result, elapsed);
  });

};
