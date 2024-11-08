// doc/api/addons.md has a bunch of code.  Extract it for verification
// that the C++ code compiles and the js code runs.
// Add .gyp files which will be used to compile the C++ code.
// Modify the require paths in the js code to pull from the build tree.
// Triggered from the build-addons target in the Makefile and vcbuild.bat.

import { mkdir, writeFile } from 'fs/promises';

import gfm from 'remark-gfm';
import remarkParse from 'remark-parse';
import { readSync } from 'to-vfile';
import { unified } from 'unified';

const rootDir = new URL('../../', import.meta.url);
const doc = new URL('./doc/api/addons.md', rootDir);
const verifyDir = new URL('./test/addons/', rootDir);

const file = readSync(doc, 'utf8');
const tree = unified().use(remarkParse).use(gfm).parse(file);
const addons = {};
let id = 0;
let currentHeader;

const validNames = /^\/\/\s+(.*\.(?:cc|h|js))[\r\n]/;
tree.children.forEach((node) => {
  if (node.type === 'heading') {
    currentHeader = file.value.slice(
      node.children[0].position.start.offset,
      node.position.end.offset);
    addons[currentHeader] = { files: {} };
  } else if (node.type === 'code') {
    const match = node.value.match(validNames);
    if (match !== null) {
      addons[currentHeader].files[match[1]] = node.value;
    }
  }
});

await Promise.all(
  Object.keys(addons).flatMap(
    (header) => verifyFiles(addons[header].files, header),
  ));

function verifyFiles(files, blockName) {
  const fileNames = Object.keys(files);

  // Must have a .cc and a .js to be a valid test.
  if (!fileNames.some((name) => name.endsWith('.cc')) ||
      !fileNames.some((name) => name.endsWith('.js'))) {
    return [];
  }

  blockName = blockName.toLowerCase().replace(/\s/g, '_').replace(/\W/g, '');
  const dir = new URL(
    `./${String(++id).padStart(2, '0')}_${blockName}/`,
    verifyDir,
  );

  files = fileNames.map((name) => {
    if (name === 'test.js') {
      files[name] = `'use strict';
const common = require('../../common');
${files[name].replace(
  "'./build/Release/addon'",
  // eslint-disable-next-line no-template-curly-in-string
  '`./build/${common.buildType}/addon`')}
`;
    }
    return {
      content: files[name],
      name,
      url: new URL(`./${name}`, dir),
    };
  });

  files.push({
    url: new URL('./binding.gyp', dir),
    content: JSON.stringify({
      targets: [
        {
          target_name: 'addon',
          sources: files.map(({ name }) => name),
          includes: ['../common.gypi'],
        },
      ],
    }),
  });

  const dirCreation = mkdir(dir);

  return files.map(({ url, content }) =>
    dirCreation.then(() => writeFile(url, content)));
}
