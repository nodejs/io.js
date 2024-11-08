#!/usr/bin/env node

import { execSync, spawn } from 'node:child_process';
import { promises as fs, readdirSync, statSync } from 'node:fs';
import { extname, join, relative, resolve } from 'node:path';
import process from 'node:process';

const FIX_MODE_ENABLED = process.argv.includes('--fix');
const USE_NPX = process.argv.includes('--from-npx');

const SHELLCHECK_EXE_NAME = 'shellcheck';
const SHELLCHECK_OPTIONS = ['--shell=sh', '--severity=info', '--enable=all'];
if (FIX_MODE_ENABLED) SHELLCHECK_OPTIONS.push('--format=diff');
else if (process.env.GITHUB_ACTIONS) SHELLCHECK_OPTIONS.push('--format=json');

const SPAWN_OPTIONS = {
  cwd: null,
  shell: false,
  stdio: ['pipe', 'pipe', 'inherit'],
};

function* findScriptFilesRecursively(dirPath) {
  const entries = readdirSync(dirPath, { withFileTypes: true });

  for (const entry of entries) {
    const path = join(dirPath, entry.name);

    if (
      entry.isDirectory() &&
      entry.name !== 'build' &&
      entry.name !== 'changelogs' &&
      entry.name !== 'deps' &&
      entry.name !== 'fixtures' &&
      entry.name !== 'gyp' &&
      entry.name !== 'inspector_protocol' &&
      entry.name !== 'node_modules' &&
      entry.name !== 'out' &&
      entry.name !== 'tmp'
    ) {
      yield* findScriptFilesRecursively(path);
    } else if (entry.isFile() && extname(entry.name) === '.sh') {
      yield path;
    }
  }
}

const expectedHashBang = Buffer.from('#!/bin/sh\n');
async function hasInvalidHashBang(fd) {
  const { length } = expectedHashBang;

  const actual = Buffer.allocUnsafe(length);
  await fd.read(actual, 0, length, 0);

  return Buffer.compare(actual, expectedHashBang);
}

async function checkFiles(...files) {
  const flags = FIX_MODE_ENABLED ? 'r+' : 'r';
  await Promise.all(
    files.map(async (file) => {
      const fd = await fs.open(file, flags);
      if (await hasInvalidHashBang(fd)) {
        if (FIX_MODE_ENABLED) {
          const file = await fd.readFile();

          const fileContent =
            file[0] === '#'.charCodeAt() ?
              file.subarray(file.indexOf('\n') + 1) :
              file;

          const toWrite = Buffer.concat([expectedHashBang, fileContent]);
          await fd.truncate(toWrite.length);
          await fd.write(toWrite, 0, toWrite.length, 0);
        } else {
          process.exitCode ||= 1;
          console.error(
            (process.env.GITHUB_ACTIONS ?
              `::error file=${file},line=1,col=1::` :
              'Fixable with --fix: ') +
              `Invalid hashbang for ${file} (expected /bin/sh).`,
          );
        }
      }
      await fd.close();
    }),
  );

  const stdout = await new Promise((resolve, reject) => {
    const SHELLCHECK_EXE =
      process.env.SHELLCHECK ||
      execSync('command -v ' + (USE_NPX ? 'npx' : SHELLCHECK_EXE_NAME))
        .toString()
        .trim();
    const NPX_OPTIONS = USE_NPX ? [SHELLCHECK_EXE_NAME] : [];

    const shellcheck = spawn(
      SHELLCHECK_EXE,
      [
        ...NPX_OPTIONS,
        ...SHELLCHECK_OPTIONS,
        ...(FIX_MODE_ENABLED ?
          files.map((filePath) => relative(SPAWN_OPTIONS.cwd, filePath)) :
          files),
      ],
      SPAWN_OPTIONS,
    );
    shellcheck.once('error', reject);

    let json = '';
    let childProcess = shellcheck;
    if (FIX_MODE_ENABLED) {
      const GIT_EXE =
        process.env.GIT || execSync('command -v git').toString().trim();

      const gitApply = spawn(GIT_EXE, ['apply'], SPAWN_OPTIONS);
      shellcheck.stdout.pipe(gitApply.stdin);
      shellcheck.once('exit', (code) => {
        if (!process.exitCode && code) process.exitCode = code;
      });
      gitApply.stdout.pipe(process.stdout);

      gitApply.once('error', reject);
      childProcess = gitApply;
    } else if (process.env.GITHUB_ACTIONS) {
      shellcheck.stdout.on('data', (chunk) => {
        json += chunk;
      });
    } else {
      shellcheck.stdout.pipe(process.stdout);
    }
    childProcess.once('exit', (code) => {
      if (!process.exitCode && code) process.exitCode = code;
      resolve(json);
    });
  });

  if (!FIX_MODE_ENABLED && process.env.GITHUB_ACTIONS) {
    const data = JSON.parse(stdout);
    for (const { file, line, column, message } of data) {
      console.error(
        `::error file=${file},line=${line},col=${column}::${file}:${line}:${column}: ${message}`,
      );
    }
  }
}

const USAGE_STR =
  `Usage: ${process.argv[1]} <path> [--fix] [--from-npx]\n` +
  '\n' +
  'Environment variables:\n' +
  ' - SHELLCHECK: absolute path to `shellcheck`. If not provided, the\n' +
  '   script will use the result of `command -v shellcheck`, or\n' +
  '   `$(command -v npx) shellcheck` if the flag `--from-npx` is provided\n' +
  '   (may require an internet connection).\n' +
  ' - GIT: absolute path to `git`. If not provided, the \n' +
  '   script will use the result of `command -v git`.\n';

if (
  process.argv.length < 3 ||
  process.argv.includes('-h') ||
  process.argv.includes('--help')
) {
  console.log(USAGE_STR);
} else {
  console.log('Running Shell scripts checker...');
  const entryPoint = resolve(process.argv[2]);
  const stats = statSync(entryPoint, { throwIfNoEntry: false });

  const onError = (e) => {
    console.log(USAGE_STR);
    console.error(e);
    process.exitCode = 1;
  };
  if (stats?.isDirectory()) {
    SPAWN_OPTIONS.cwd = entryPoint;
    checkFiles(...findScriptFilesRecursively(entryPoint)).catch(onError);
  } else if (stats?.isFile()) {
    SPAWN_OPTIONS.cwd = process.cwd();
    checkFiles(entryPoint).catch(onError);
  } else {
    onError(new Error('You must provide a valid directory or file path. ' +
                      `Received '${process.argv[2]}'.`));
  }
}
