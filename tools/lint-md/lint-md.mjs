import fs from 'node:fs';
import { parseArgs } from 'node:util';
import { resolve, join } from 'node:path';

import { unified } from 'unified';
import remarkParse from 'remark-parse';
import remarkStringify from 'remark-stringify';
import presetLintNode from 'remark-preset-lint-node';
import { read } from 'to-vfile';
import { reporter } from 'vfile-reporter';

import generateManPage from './man-page.mjs';

const { values: { format }, positionals: paths } = parseArgs({
  options: {
    format: { type: 'boolean', default: false },
  },
  allowPositionals: true,
});

if (!paths.length) {
  console.error('Usage: lint-md.mjs [--format] <path> [<path> ...]');
  process.exit(1);
}

const rootDir = resolve(import.meta.dirname, '..', '..');
const cliPath = resolve(join(rootDir, 'doc', 'api', 'cli.md'));
const manPagePath = join(rootDir, 'doc', 'node.1');

function handleDifference(filePath) {
  process.exitCode = 1;
  const buildCmd = process.platform === 'win32' ? 'vcbuild' : 'make';
  console.error(`${filePath} is not formatted. Please run '${buildCmd} format-md'.`);
}

const linter = unified()
  .use(remarkParse)
  .use(presetLintNode)
  .use(remarkStringify);

paths.forEach(async (path) => {
  const file = await read(path);
  // We need to calculate `fileContents` before running `linter.process(files)`
  // because `linter.process(files)` mutates `file` and returns it as `result`.
  // So we won't be able to use `file` after that to see if its contents have
  // changed as they will have been altered to the changed version.
  const fileContents = file.toString();
  const result = await linter.process(file);
  const isDifferent = fileContents !== result.toString();

  let isManPageModified = false;
  let generatedManPage;
  if (resolve(path) === cliPath) {
    generatedManPage = await generateManPage(path);
    const oldManPage = fs.readFileSync(manPagePath, 'utf-8');
    isManPageModified = oldManPage !== generatedManPage;
  }

  if (format) {
    if (isDifferent) {
      fs.writeFileSync(path, result.toString());
    }
    if (isManPageModified) {
      fs.writeFileSync(manPagePath, generatedManPage);
    }
  } else {
    if (isDifferent) handleDifference(path);
    if (isManPageModified) handleDifference('doc/node.1');
    if (result.messages.length) {
      process.exitCode = 1;
      console.error(reporter(result));
    }
  }
});
