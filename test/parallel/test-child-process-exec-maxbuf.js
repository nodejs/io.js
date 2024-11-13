'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');

function runChecks(err, stdio, streamName, expected) {
  assert.strictEqual(err.message, `${streamName} maxBuffer length exceeded`);
  assert(err instanceof RangeError);
  assert.strictEqual(err.code, 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER');
  assert.deepStrictEqual(stdio[streamName], expected);
}

// The execPath might contain chars that should be escaped in a shell context.
// On non-Windows, we can pass the path via the env; `"` is not a valid char on
// Windows, so we can simply pass the path.
const execNode = (args, optionsOrCallback, callback) => {
  const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" `;
  let options = optionsOrCallback;
  if (typeof optionsOrCallback === 'function') {
    options = undefined;
    callback = optionsOrCallback;
  }
  return cp.exec(
    cmd + args,
    { ...opts, ...options },
    callback,
  );
};

// default value
{
  execNode(`-e "console.log('a'.repeat(1024 * 1024))"`, common.mustCall((err) => {
    assert(err instanceof RangeError);
    assert.strictEqual(err.message, 'stdout maxBuffer length exceeded');
    assert.strictEqual(err.code, 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER');
  }));
}

// default value
{
  execNode(`-e "console.log('a'.repeat(1024 * 1024 - 1))"`, common.mustSucceed((stdout, stderr) => {
    assert.strictEqual(stdout.trim(), 'a'.repeat(1024 * 1024 - 1));
    assert.strictEqual(stderr, '');
  }));
}

{
  const options = { maxBuffer: Infinity };

  execNode(`-e "console.log('hello world');"`, options, common.mustSucceed((stdout, stderr) => {
    assert.strictEqual(stdout.trim(), 'hello world');
    assert.strictEqual(stderr, '');
  }));
}

{
  const cmd = 'echo hello world';

  cp.exec(
    cmd,
    { maxBuffer: 5 },
    common.mustCall((err, stdout, stderr) => {
      runChecks(err, { stdout, stderr }, 'stdout', 'hello');
    })
  );
}

// default value
{
  execNode(
    `-e "console.log('a'.repeat(1024 * 1024))"`,
    common.mustCall((err, stdout, stderr) => {
      runChecks(
        err,
        { stdout, stderr },
        'stdout',
        'a'.repeat(1024 * 1024)
      );
    })
  );
}

// default value
{
  execNode(`-e "console.log('a'.repeat(1024 * 1024 - 1))"`, common.mustSucceed((stdout, stderr) => {
    assert.strictEqual(stdout.trim(), 'a'.repeat(1024 * 1024 - 1));
    assert.strictEqual(stderr, '');
  }));
}

const unicode = '中文测试'; // length = 4, byte length = 12

{
  execNode(
    `-e "console.log('${unicode}');"`,
    { maxBuffer: 10 },
    common.mustCall((err, stdout, stderr) => {
      runChecks(err, { stdout, stderr }, 'stdout', '中文测试\n');
    })
  );
}

{
  execNode(
    `-e "console.error('${unicode}');"`,
    { maxBuffer: 3 },
    common.mustCall((err, stdout, stderr) => {
      runChecks(err, { stdout, stderr }, 'stderr', '中文测');
    })
  );
}

{
  const child = execNode(
    `-e "console.log('${unicode}');"`,
    { encoding: null, maxBuffer: 10 },
    common.mustCall((err, stdout, stderr) => {
      runChecks(err, { stdout, stderr }, 'stdout', '中文测试\n');
    })
  );

  child.stdout.setEncoding('utf-8');
}

{
  const child = execNode(
    `-e "console.error('${unicode}');"`,
    { encoding: null, maxBuffer: 3 },
    common.mustCall((err, stdout, stderr) => {
      runChecks(err, { stdout, stderr }, 'stderr', '中文测');
    })
  );

  child.stderr.setEncoding('utf-8');
}

{
  execNode(
    `-e "console.error('${unicode}');"`,
    { encoding: null, maxBuffer: 5 },
    common.mustCall((err, stdout, stderr) => {
      const buf = Buffer.from(unicode).slice(0, 5);
      runChecks(err, { stdout, stderr }, 'stderr', buf);
    })
  );
}
