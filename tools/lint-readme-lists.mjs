#!/usr/bin/env node

// Validates the list in the README are in the correct order.

import { open } from 'node:fs/promises';

const lists = [
  'TSC voting members',
  'TSC regular members',
  'TSC emeriti members',
  'Collaborators',
  'Collaborator emeriti',
  'Triagers',
];
const tscMembers = new Set();

const readme = await open(new URL('../README.md', import.meta.url), 'r');

let currentList = null;
let previousGithubHandle;
let lineNumber = 0;

for await (const line of readme.readLines()) {
  lineNumber++;
  if (line.startsWith('### ')) {
    currentList = lists[lists.indexOf(line.slice(4))];
    previousGithubHandle = null;
  } else if (line.startsWith('#### ')) {
    currentList = lists[lists.indexOf(line.slice(5))];
    previousGithubHandle = null;
  } else if (currentList && line.startsWith('* [')) {
    const currentGithubHandle = line.slice(3, line.indexOf(']')).toLowerCase();
    if (previousGithubHandle && previousGithubHandle >= currentGithubHandle) {
      throw new Error(`${currentGithubHandle} should be listed before ${previousGithubHandle} in the ${currentList} list (README.md:${lineNumber})`);
    }

    if (currentList === 'TSC voting members' || currentList === 'TSC regular members') {
      tscMembers.add(currentGithubHandle);
    } else if (currentList === 'Collaborators') {
      tscMembers.delete(currentGithubHandle);
    }
    previousGithubHandle = currentGithubHandle;
  }
}

if (tscMembers.size !== 0) {
  throw new Error(`Some TSC members are not listed as Collaborators: ${Array.from(tscMembers)}`);
}
