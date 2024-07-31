// Flags: --experimental-loader ./test/fixtures/es-module-loaders/preset-cjs-source.mjs
import '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'assert';

const { default: existingFileSource } = await import(fixtures.fileURL('es-modules', 'cjs-file.cjs'));
const { default: noSuchFileSource } = await import(new URL('./no-such-file.cjs', import.meta.url));

assert.strictEqual(existingFileSource, 'no .cjs file was read to get this source');
assert.strictEqual(noSuchFileSource, 'no .cjs file was read to get this source');
