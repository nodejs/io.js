'use strict';

// Flags: --expose-internals --permission --allow-fs-read=* --allow-child-process

require('../common');

const { describe, it } = require('node:test');
const assert = require('node:assert');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const fixtures = require('../common/fixtures.js');

function escapeWhenSepIsBackSlash(filePath) {
  return path.sep === '\\' ? filePath.replace(/\\/g, '\\\\') : filePath;
}

describe('legacyMainResolve', () => {
  it('should ensure permission model when resolving by packageConfig.main', () => {
    const fixtextureFolder = fixtures.path('/es-modules/legacy-main-resolver');

    const paths = [
      ['./index-js/index.js', []],
      ['./index-js/index', ['./index-js/index.js']],
      ['./index-json/index', ['./index-json/index.js']],
      ['./index-node/index', ['./index-node/index.js', './index-node/index.json']],
      ['./index-js', []],
      ['./index-json', ['./index-json/index.js']],
      ['./index-node', ['./index-node/index.js', './index-node/index.json']],
    ];

    for (const [mainOrFolder, allowReads] of paths) {
      const allowReadFilePaths = allowReads.map((filepath) => path.resolve(fixtextureFolder, filepath));
      const allowReadFiles = allowReads?.length > 0 ?
        allowReadFilePaths.flatMap((path) => ['--allow-fs-read', path]) :
        [];
      const fixtextureFolderEscaped = escapeWhenSepIsBackSlash(fixtextureFolder);

      const { status, stderr } = spawnSync(
        process.execPath,
        [
          '--expose-internals',
          '--permission',
          ...allowReadFiles,
          '-e',
          `
            const { legacyMainResolve } = require('node:internal/modules/esm/resolve');
            const { pathToFileURL } = require('node:url');
            const path = require('node:path');
            const assert = require('node:assert');

            const packageJsonUrl = pathToFileURL(
              path.resolve(
                ${JSON.stringify(fixtextureFolderEscaped)},
                'package.json'
              )
            );

            const packageConfig = { main: '${mainOrFolder}' };
            const base = path.resolve(
              ${JSON.stringify(fixtextureFolderEscaped)},
            );

            assert.throws(() => legacyMainResolve(packageJsonUrl, packageConfig, base), {
              code: 'ERR_ACCESS_DENIED',
              resource: path.toNamespacedPath(
                path.resolve(
                  ${JSON.stringify(fixtextureFolderEscaped)},
                  ${JSON.stringify(mainOrFolder)},
                )
              )
            });
          `,
        ]
      );

      assert.strictEqual(status, 0, stderr.toString());
    }
  });

  it('should ensure permission model when resolving by packageJsonUrl', () => {
    const fixtextureFolder = fixtures.path('/es-modules/legacy-main-resolver');

    const paths = [
      ['./index-js', './index.js', []],
      ['./index-json', './index.json', ['./index.js']],
      ['./index-node', './index.node', ['./index.js', './index.json']],
    ];

    for (const [folder, expectedFile, allowReads] of paths) {
      const allowReadFilePaths = allowReads.map((filepath) => path.resolve(fixtextureFolder, folder, filepath));
      const allowReadFiles = allowReads?.length > 0 ?
        allowReadFilePaths.flatMap((path) => ['--allow-fs-read', path]) :
        [];
      const fixtextureFolderEscaped = escapeWhenSepIsBackSlash(fixtextureFolder);

      const { status, stderr } = spawnSync(
        process.execPath,
        [
          '--expose-internals',
          '--permission',
          ...allowReadFiles,
          '-e',
          `
            const { legacyMainResolve } = require('node:internal/modules/esm/resolve');
            const { pathToFileURL } = require('node:url');
            const path = require('node:path');
            const assert = require('node:assert');

            const packageJsonUrl = pathToFileURL(
              path.resolve(
                ${JSON.stringify(fixtextureFolderEscaped)},
                ${JSON.stringify(folder)},
                'package.json'
              )
            );

            const packageConfig = { main: undefined };
            const base = path.resolve(
              ${JSON.stringify(fixtextureFolderEscaped)},
            );

            assert.throws(() => legacyMainResolve(packageJsonUrl, packageConfig, base), {
              code: 'ERR_ACCESS_DENIED',
              resource: path.toNamespacedPath(
                path.resolve(
                  ${JSON.stringify(fixtextureFolderEscaped)},
                  ${JSON.stringify(folder)},
                  ${JSON.stringify(expectedFile)},
                )
              )
            });
          `,
        ]
      );

      assert.strictEqual(status, 0, stderr.toString());
    }
  });
});
