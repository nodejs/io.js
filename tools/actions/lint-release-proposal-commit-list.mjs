#!/usr/bin/env node

// Takes a stream of JSON objects as inputs, validates the CHANGELOG contains a
// line corresponding, then outputs the prURL value.
//
// Example:
// $ git log upstream/vXX.x...upstream/vX.X.X-proposal \
//     --format='{"prURL":"%(trailers:key=PR-URL,valueonly,separator=)","title":"%s","smallSha":"%h"}' \
//   | ./lint-release-proposal-commit-list.mjs "path/to/CHANGELOG.md" "$(git rev-parse upstream/vX.X.X-proposal)"

const [,, CHANGELOG_PATH, RELEASE_COMMIT_SHA] = process.argv;

import assert from 'node:assert';
import { readFile } from 'node:fs/promises';
import { createInterface } from 'node:readline';

// Creating the iterator early to avoid missing any data:
const stdinLineByLine = createInterface(process.stdin)[Symbol.asyncIterator]();

const changelog = await readFile(CHANGELOG_PATH, 'utf-8');
const commitListingStart = changelog.indexOf('\n### Commits\n');
const commitListingEnd = changelog.indexOf('\n\n<a', commitListingStart);
const commitList = changelog.slice(commitListingStart, commitListingEnd === -1 ? undefined : commitListingEnd + 1)
  // Checking for semverness is too expansive, it is left as a exercice for human reviewers.
  .replaceAll('**(SEMVER-MINOR)** ', '')
  // Correct Markdown escaping is validated by the linter, getting rid of it here helps.
  .replaceAll('\\', '');

let expectedNumberOfCommitsLeft = commitList.match(/\n\* \[/g).length;
for await (const line of stdinLineByLine) {
  const { smallSha, title, prURL } = JSON.parse(line);

  if (smallSha === RELEASE_COMMIT_SHA.slice(0, 10)) {
    assert.strictEqual(
      expectedNumberOfCommitsLeft, 0,
      'Some commits are listed without being included in the proposal, or are listed more than once',
    );
    continue;
  }

  const lineStart = commitList.indexOf(`\n* [[\`${smallSha}\`]`);
  assert.notStrictEqual(lineStart, -1, `Cannot find ${smallSha} on the list`);
  const lineEnd = commitList.indexOf('\n', lineStart + 1);

  const colonIndex = title.indexOf(':');
  const expectedCommitTitle = `${`**${title.slice(0, colonIndex)}`.replace('**Revert "', '_**Revert**_ "**')}**${title.slice(colonIndex)}`;
  try {
    assert(commitList.lastIndexOf(`/${smallSha})] - ${expectedCommitTitle} (`, lineEnd) > lineStart, `Commit title doesn't match`);
  } catch (e) {
    if (e?.code === 'ERR_ASSERTION') {
      e.operator = 'includes';
      e.expected = expectedCommitTitle;
      e.actual = commitList.slice(lineStart + 1, lineEnd);
    }
    throw e;
  }
  assert.strictEqual(commitList.slice(lineEnd - prURL.length - 2, lineEnd), `(${prURL})`, `when checking ${smallSha} ${title}`);

  expectedNumberOfCommitsLeft--;
  console.log(prURL);
}
assert.strictEqual(expectedNumberOfCommitsLeft, 0, 'Release commit is not the last commit in the proposal');
