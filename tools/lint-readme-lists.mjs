#!/usr/bin/env node

// Validates the list in the README are in the correct order, and consistent with the actual GitHub teams.

import assert from 'node:assert';
import { open } from 'node:fs/promises';
import { argv } from 'node:process';

const ghHandleLine = /^\* \[(.+)\]\(https:\/\/github\.com\/\1\) -$/;
const memberInfoLine = /^ {2}\*\*[^*]+\*\* <<[^@]+@.+\.[a-z]+>>( \(\w+(\/[^)/]+)+\))?( - \[Support me\]\(.+\))?$/;

const lists = {
  '__proto__': null,

  'TSC voting members': 'tsc',
  'TSC regular members': null,
  'TSC emeriti members': null,
  'Collaborators': 'collaborators',
  'Collaborator emeriti': null,
  'Triagers': 'issue-triage',
};
const actualMembers = {
  __proto__: null,
  // The bot is part of `@nodejs/collaborators`, but is not listed in the README.
  collaborators: new Set().add('nodejs-github-bot'),
};
const tscMembers = new Set();

const readme = await open(new URL('../README.md', import.meta.url), 'r');

let currentList = null;
let previousGithubHandleInfoRequired;
let previousGithubHandle;
let lineNumber = 0;

for await (const line of readme.readLines()) {
  lineNumber++;
  if (previousGithubHandleInfoRequired) {
    if (!memberInfoLine.test(line)) {
      throw new Error(`${previousGithubHandleInfoRequired} info are not formatted correctly (README.md:${lineNumber})`);
    }
    previousGithubHandle = previousGithubHandleInfoRequired;
    previousGithubHandleInfoRequired = null;
  } else if (line.startsWith('### ')) {
    currentList = line.slice(4);
    previousGithubHandle = null;
  } else if (line.startsWith('#### ')) {
    currentList = line.slice(5);
    previousGithubHandle = null;
  } else if (currentList in lists && line.startsWith('* [')) {
    const currentGithubHandle = line.slice(3, line.indexOf(']'));
    const currentGithubHandleLowerCase = currentGithubHandle.toLowerCase();
    if (
      previousGithubHandle &&
      previousGithubHandle >= currentGithubHandleLowerCase
    ) {
      throw new Error(
        `${currentGithubHandle} should be listed before ${previousGithubHandle} in the ${currentList} list (README.md:${lineNumber})`,
      );
    }

    if (!ghHandleLine.test(line)) {
      throw new Error(`${currentGithubHandle} is not formatted correctly (README.md:${lineNumber})`);
    }

    if (
      currentList === 'TSC voting members' ||
      currentList === 'TSC regular members'
    ) {
      tscMembers.add(currentGithubHandle);
    } else if (currentList === 'Collaborators') {
      tscMembers.delete(currentGithubHandle);
    }
    if (lists[currentList]) {
      (actualMembers[lists[currentList]] ??= new Set()).add(currentGithubHandle);
    }
    previousGithubHandleInfoRequired = currentGithubHandleLowerCase;
  }
}
console.info('Lists are in the alphabetical order.');

assert.deepStrictEqual(tscMembers, new Set(), 'Some TSC members are not listed as Collaborators');

if (argv[2] && argv[2] !== '{}') {
  const reviver = (_, value) =>
    (typeof value === 'string' && value[0] === '[' && value.at(-1) === ']' ?
      new Set(JSON.parse(value)) :
      value);
  assert.deepStrictEqual(JSON.parse(argv[2], reviver), { ...actualMembers });
} else {
  console.warn('Skipping the check of GitHub teams membership.');
}
