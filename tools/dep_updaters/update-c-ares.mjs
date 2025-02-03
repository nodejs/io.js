// Synchronize the sources for our c-ares gyp file from c-ares' Makefiles.
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const srcroot = fileURLToPath(new URL('../../', import.meta.url));
const options = { encoding: 'utf8' };

// Extract list of sources from the gyp file.
const gypFile = join(srcroot, 'deps', 'cares', 'cares.gyp');
const contents = readFileSync(gypFile, options);
const sourcesRE = /^\s+'cares_sources_common':\s+\[\s*\n(?<files>[\s\S]*?)\s+\],$/gm;
const sourcesCommon = sourcesRE.exec(contents);

// Extract the list of sources from c-ares' Makefile.inc.
const makefile = join(srcroot, 'deps', 'cares', 'src', 'lib', 'Makefile.inc');
const libSources = readFileSync(makefile, options).split('\n')
  // Extract filenames (excludes comments and variable assignment).
  .map((line) => line.match(/^(?:.*= |\s*)?([^#\s]*)\s*\\?/)?.[1])
  // Filter out empty lines.
  .filter((line) => line !== '')
  // Prefix with directory and format as list entry.
  .map((line) => `      'src/lib/${line}',`);

// Extract include files.
const includeMakefile = join(srcroot, 'deps', 'cares', 'include', 'Makefile.am');
const includeSources = readFileSync(includeMakefile, options)
  .match(/include_HEADERS\s*=\s*(.*)/)[1]
  .split(/\s/)
  .map((header) => `      'include/${header}',`);

// Combine the lists. Alphabetically sort to minimize diffs.
const fileList = includeSources.concat(libSources).sort();

// Replace the list of sources.
const newContents = contents.replace(sourcesCommon.groups.files, fileList.join('\n'));
if (newContents !== contents) {
  writeFileSync(gypFile, newContents, options);
}
