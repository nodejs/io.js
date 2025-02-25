import { hasCrypto, skip, mustCall } from '../common/index.mjs';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { path } from '../common/fixtures.mjs';
if (!hasCrypto)
  skip('missing crypto');
import { constants, createServer, connect } from 'http2';
import { ok } from 'assert';
import { openSync, closeSync } from 'fs';
import process from 'process';

// Checks that the async resource is not reused by FileHandle.
// Test is based on parallel\test-http2-respond-file-fd.js.

const hooks = initHooks();
hooks.enable();

const {
  HTTP2_HEADER_CONTENT_TYPE
} = constants;

// Use large fixture to get several file operations.
const fname = path('person-large.jpg');
const fd = openSync(fname, 'r');

const server = createServer();
server.on('stream', (stream) => {
  stream.respondWithFD(fd, {
    [HTTP2_HEADER_CONTENT_TYPE]: 'text/plain'
  });
});
server.on('close', mustCall(() => closeSync(fd)));
server.listen(0, () => {
  const client = connect(`http://localhost:${server.address().port}`);
  const req = client.request();

  req.on('response', mustCall());
  req.on('data', () => {});
  req.on('end', mustCall(() => {
    client.close();
    server.close();
  }));
  req.end();
});

process.on('exit', onExit);

function onExit() {
  hooks.disable();
  hooks.sanityCheck();
  const activities = hooks.activities;

  // Verify both invocations
  const fsReqs = activities.filter((x) => x.type === 'FSREQCALLBACK');
  ok(fsReqs.length >= 2);

  checkInvocations(fsReqs[0], { init: 1, destroy: 1 }, 'when process exits');
  checkInvocations(fsReqs[1], { init: 1, destroy: 1 }, 'when process exits');

  // Verify reuse handle has been wrapped
  ok(fsReqs[0].handle !== fsReqs[1].handle, 'Resource reused');
  ok(fsReqs[0].handle === fsReqs[1].handle.handle,
            'Resource not wrapped correctly');
}
