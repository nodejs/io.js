// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

import { readFileSync, promises as fs } from 'fs';
import path from 'path';

import raw from 'rehype-raw';
import htmlStringify from 'rehype-stringify';
import gfm from 'remark-gfm';
import markdown from 'remark-parse';
import remark2rehype from 'remark-rehype';
import frontmatter from 'remark-frontmatter';
import { unified } from 'unified';

import * as html from './html.mjs';
import * as json from './json.mjs';
import { replaceLinks } from './markdown.mjs';

const linksMapperFile = new URL('links-mapper.json', import.meta.url);
const linksMapper = JSON.parse(readFileSync(linksMapperFile, 'utf8'));

// Parse the args.
// Don't use nopt or whatever for this. It's simple enough.

const args = process.argv.slice(2);
let filename = null;
let nodeVersion = null;
let outputDir = null;
let apilinks = {};
let versions = [];

async function main() {
  for (const arg of args) {
    if (!arg.startsWith('--')) {
      filename = arg;
    } else if (arg.startsWith('--node-version=')) {
      nodeVersion = arg.replace(/^--node-version=/, '');
    } else if (arg.startsWith('--output-directory=')) {
      outputDir = arg.replace(/^--output-directory=/, '');
    } else if (arg.startsWith('--apilinks=')) {
      const linkFile = arg.replace(/^--apilinks=/, '');
      const data = await fs.readFile(linkFile, 'utf8');
      if (!data.trim()) {
        throw new Error(`${linkFile} is empty`);
      }
      apilinks = JSON.parse(data);
    } else if (arg.startsWith('--versions-file=')) {
      const versionsFile = arg.replace(/^--versions-file=/, '');
      const data = await fs.readFile(versionsFile, 'utf8');
      if (!data.trim()) {
        throw new Error(`${versionsFile} is empty`);
      }
      versions = JSON.parse(data);
    }
  }

  nodeVersion ||= process.version;

  if (!filename) {
    throw new Error('No input file specified');
  } else if (!outputDir) {
    throw new Error('No output directory specified');
  }

  const input = await fs.readFile(filename, 'utf8');

  const content = await unified()
    .use(frontmatter)
    .use(replaceLinks, { filename, linksMapper })
    .use(markdown)
    .use(gfm)
    .use(html.preprocessText, { nodeVersion })
    .use(json.jsonAPI, { filename })
    .use(html.firstHeader)
    .use(html.preprocessElements, { filename })
    .use(html.buildToc, { filename, apilinks })
    .use(remark2rehype, { allowDangerousHtml: true })
    .use(raw)
    .use(htmlStringify)
    .process(input);

  const myHtml = await html.toHTML({ input, content, filename, nodeVersion,
                                     versions });
  const basename = path.basename(filename, '.md');
  const htmlTarget = path.join(outputDir, `${basename}.html`);
  const jsonTarget = path.join(outputDir, `${basename}.json`);

  return Promise.allSettled([
    fs.writeFile(htmlTarget, myHtml),
    fs.writeFile(jsonTarget, JSON.stringify(content.json, null, 2)),
  ]);
}

main()
  .then((tasks) => {
    // Filter rejected tasks
    const errors = tasks.filter(({ status }) => status === 'rejected')
      .map(({ reason }) => reason);

    // Log errors
    for (const error of errors) {
      console.error(error);
    }

    // Exit process with code 1 if some errors
    if (errors.length > 0) {
      return process.exit(1);
    }

    // Else with code 0
    process.exit(0);
  })
  .catch((error) => {
    console.error(error);

    process.exit(1);
  });
