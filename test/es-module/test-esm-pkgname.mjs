import '../common/index.mjs';
import assert from 'node:assert';

import { importFixture } from '../fixtures/pkgexports.mjs';

await Promise.all([
  'as%2Ff',
  'as%5Cf',
  'as\\df',
  '@as@df',
].map((specifier) => assert.rejects(importFixture(specifier), {
  code: 'ERR_INVALID_MODULE_SPECIFIER',
})));
