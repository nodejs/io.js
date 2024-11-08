'use strict';
require('../common');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const { match, strictEqual } = require('node:assert');
const { spawnSync } = require('node:child_process');
const { readFileSync } = require('node:fs');
const { test } = require('node:test');

function runWithReporter(reporter) {
  const destination = tmpdir.resolve(`${reporter}.out`);
  const args = [
    '--test-force-exit',
    `--test-reporter=${reporter}`,
    `--test-reporter-destination=${destination}`,
    fixtures.path('test-runner', 'reporters.js'),
  ];
  const child = spawnSync(process.execPath, args);
  strictEqual(child.stdout.toString(), '');
  strictEqual(child.stderr.toString(), '');
  strictEqual(child.status, 1);
  return destination;
}

tmpdir.refresh();

test('junit reporter', () => {
  const output = readFileSync(runWithReporter('junit'), 'utf8');
  match(output, /<!-- tests 4 -->/);
  match(output, /<!-- pass 2 -->/);
  match(output, /<!-- fail 2 -->/);
  match(output, /<!-- duration_ms/);
  match(output, /<\/testsuites>/);
});

test('spec reporter', () => {
  const output = readFileSync(runWithReporter('spec'), 'utf8');
  match(output, /tests 4/);
  match(output, /pass 2/);
  match(output, /fail 2/);
});

test('tap reporter', () => {
  const output = readFileSync(runWithReporter('tap'), 'utf8');
  match(output, /# tests 4/);
  match(output, /# pass 2/);
  match(output, /# fail 2/);
  match(output, /# duration_ms/);
});
