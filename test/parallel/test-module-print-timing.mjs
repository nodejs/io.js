import { isWindows } from '../common/index.mjs';
import assert from 'node:assert';
import { writeFileSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { it } from 'node:test';
import tmpdir from '../common/tmpdir.js';
import { spawnSyncAndAssert } from '../common/child_process.js';
import fixtures from '../common/fixtures.js';

tmpdir.refresh();

it('should print the timing information for cjs', () => {
  const result = spawnSyncAndAssert(process.execPath, ['--eval', 'require("url");'], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
      NODE_DEBUG: 'module_timer',
    },
  }, {
    stdout: '',
    stderr: /MODULE_TIMER/g,
  });

  const firstLine = result.stderr.split('\n').find((line) => line.includes('[url]'));

  assert.notStrictEqual(firstLine, undefined);
  assert.ok(firstLine.includes('MODULE_TIMER'), `Not found MODULE_TIMER on ${firstLine}`);
  assert.ok(firstLine.includes('[url]:'), `Not found [url]: on ${firstLine}`);
  assert.ok(firstLine.endsWith('ms'), `Not found ms on ${firstLine}`);
});

it('should write tracing information for cjs', async () => {
  const outputFile = tmpdir.resolve('output-trace.log');

  spawnSyncAndAssert(process.execPath, [
    '--trace-event-categories',
    'node.module_timer',
    '--trace-event-file-pattern',
    outputFile,
    '--eval',
    'require("url");',
  ], {
    cwd: tmpdir.path,
  }, {
    stdout: '',
    stderr: '',
  });

  const expectedMimeTypes = ['b', 'e'];
  const outputFileContent = await readFile(outputFile, 'utf-8');
  const outputFileJson = JSON.parse(outputFileContent).traceEvents;
  const urlTraces = outputFileJson.filter((trace) => trace.name === "require('url')");

  assert.strictEqual(urlTraces.length, 2);

  for (const trace of urlTraces) {
    assert.strictEqual(trace.ph, expectedMimeTypes.shift());
  }
});

it('should write tracing & print logs for cjs', async () => {
  const outputFile = tmpdir.resolve('output-trace-and-log.log');

  const result = spawnSyncAndAssert(process.execPath, [
    '--trace-event-categories',
    'node.module_timer',
    '--trace-event-file-pattern',
    outputFile,
    '--eval',
    'require("url");',
  ], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
      NODE_DEBUG: 'module_timer',
    },
  }, {
    stdout: '',
    stderr: /MODULE_TIMER/g,
  });

  const firstLine = result.stderr.split('\n').find((line) => line.includes('[url]'));

  assert.notStrictEqual(firstLine, undefined);
  assert.ok(firstLine.includes('MODULE_TIMER'), `Not found MODULE_TIMER on ${firstLine}`);
  assert.ok(firstLine.includes('[url]:'), `Not found [url]: on ${firstLine}`);
  assert.ok(firstLine.endsWith('ms'), `Not found ms on ${firstLine}`);

  const expectedMimeTypes = ['b', 'e'];
  const outputFileContent = await readFile(outputFile, 'utf-8');
  const outputFileJson = JSON.parse(outputFileContent).traceEvents;
  const urlTraces = outputFileJson.filter((trace) => trace.name === "require('url')");

  assert.ok(urlTraces.length > 0, 'Not found url traces');

  for (const trace of urlTraces) {
    assert.strictEqual(trace.ph, expectedMimeTypes.shift());
  }
});

it('should support enable tracing dynamically', async () => {
  try {
    spawnSyncAndAssert(process.execPath, [
      '--eval',
      'require("trace_events")',
    ], {
      stdout: '',
      stderr: '',
    });
  } catch {
    // Skip this test if the trace_events module is not available
    return;
  }


  const outputFile = tmpdir.resolve('output-dynamic-trace.log');
  let requireFileWithDoubleQuote = '';
  if (!isWindows) {
    // Double quotes are not valid char for a path on Windows.
    const fileWithDoubleQuote = tmpdir.resolve('filename-with-"double"-quotes.cjs');
    writeFileSync(fileWithDoubleQuote, ';\n');
    requireFileWithDoubleQuote = `require(${JSON.stringify(fileWithDoubleQuote)});`;
  }
  const jsScript = `
  const traceEvents = require("trace_events");
  const tracing = traceEvents.createTracing({ categories: ["node.module_timer"] });

  tracing.enable();
  ${requireFileWithDoubleQuote}
  require("http");
  tracing.disable();

  require("vm");
  `;

  spawnSyncAndAssert(process.execPath, [
    '--trace-event-file-pattern',
    outputFile,
    '--eval',
    jsScript,
  ], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
    },
  }, {
    stdout: '',
    stderr: '',
  });

  const expectedMimeTypes = ['b', 'e'];
  const outputFileContent = await readFile(outputFile, 'utf-8');

  const outputFileJson = JSON.parse(outputFileContent).traceEvents;
  const httpTraces = outputFileJson.filter((trace) => trace.name === "require('http')");

  assert.ok(httpTraces.length > 0, 'Not found http traces');

  for (const trace of httpTraces) {
    assert.strictEqual(trace.ph, expectedMimeTypes.shift());
  }

  const vmTraces = outputFileJson.filter((trace) => trace.name === "require('vm')");
  assert.strictEqual(vmTraces.length, 0);
});

it('should not print when is disabled and found duplicated labels (GH-54265)', () => {
  const testFile = fixtures.path('GH-54265/index.js');

  spawnSyncAndAssert(process.execPath, [
    testFile,
  ], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
    },
  }, {
    stdout: '',
    stderr: '',
  });
});
