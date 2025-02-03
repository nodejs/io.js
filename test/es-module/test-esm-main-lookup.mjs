import { mustNotCall } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'assert';

Object.defineProperty(Error.prototype, 'url', {
  get: mustNotCall('get %Error.prototype%.url'),
  set: mustNotCall('set %Error.prototype%.url'),
});
Object.defineProperty(Object.prototype, 'url', {
  get: mustNotCall('get %Object.prototype%.url'),
  set: mustNotCall('set %Object.prototype%.url'),
});

await assert.rejects(import('../fixtures/es-modules/pjson-main'), {
  code: 'ERR_UNSUPPORTED_DIR_IMPORT',
  url: fixtures.fileURL('es-modules/pjson-main').href,
});
await assert.rejects(import(`data:text/javascript,import${encodeURIComponent(JSON.stringify(fixtures.fileURL('es-modules/pjson-main')))}`), {
  code: 'ERR_UNSUPPORTED_DIR_IMPORT',
  url: fixtures.fileURL('es-modules/pjson-main').href,
});
await assert.rejects(import(`data:text/javascript,import${encodeURIComponent(JSON.stringify(fixtures.fileURL('es-modules/does-not-exist')))}`), {
  code: 'ERR_MODULE_NOT_FOUND',
  url: fixtures.fileURL('es-modules/does-not-exist').href,
});

assert.deepStrictEqual(
  { ...await import('../fixtures/es-modules/pjson-main/main.mjs') },
  { main: 'main' },
);
