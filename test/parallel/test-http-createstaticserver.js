'use strict';
const { mustCall } = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('node:assert');
const { MIMEType } = require('node:util');
const { createStaticServer } = require('node:http');

[0, 1, 1n, '', '1', true, false, NaN, Symbol(), {}, []].forEach((filter) => {
  assert.throws(() => createStaticServer({ filter }), { code: 'ERR_INVALID_ARG_TYPE' });
});

[true, false, Symbol(), () => {}, {}, [], 0n, 1n, '', Number.MAX_SAFE_INTEGER].forEach((port) => {
  assert.throws(() => createStaticServer({ port }), { code: 'ERR_SOCKET_BAD_PORT' });
});

[true, false, Symbol(), () => {}, {}, [], 0n, 1n, 0, 1].forEach((directory) => {
  assert.throws(() => createStaticServer({ directory }), { code: 'ERR_INVALID_ARG_TYPE' });
});


{
  const server = createStaticServer({
    directory: fixtures.path('static-server', '.foo'),
  }).once('listening', mustCall(() => {
    const { port } = server.address();
    Promise.all([
      fetch(`http://localhost:${port}/`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
      fetch(`http://localhost:${port}/bar.js`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
    ]).then(mustCall()).finally(() => server.close());
  }));
}

{
  const server = createStaticServer({
    directory: fixtures.path('static-server'),
  }).once('listening', mustCall(() => {
    const { port } = server.address();
    Promise.all([
      fetch(`http://localhost:${port}/test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/test.html?../../../../etc/passwd`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/././test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}//test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/////test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/../../test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/..%2F../test.html`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual((await response.text()).trimEnd(), 'Forbidden');
      }),
      fetch(`http://localhost:${port}/%2E%2E%2F%2E%2E/test.html`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual((await response.text()).trimEnd(), 'Forbidden');
      }),
      fetch(`http://localhost:${port}/%2E%2E/%2E%2E/test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/..%2f../test.html`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual((await response.text()).trimEnd(), 'Forbidden');
      }),
      fetch(`http://localhost:${port}/%2e%2e%2f%2e%2e/test.html`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual((await response.text()).trimEnd(), 'Forbidden');
      }),
      fetch(`http://localhost:${port}/%2e%2e/%2e%2e/test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/test.html?key=value`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/file.unsupported_extension`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual((await response.text()).trimEnd(), 'Dummy file to test mimeOverrides option');
      }),
      fetch(`http://localhost:${port}/subfolder`).then(async (response) => {
        assert(response.ok);
        assert(response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of subfolder/index.html');
      }),
      fetch(`http://localhost:${port}/subfolder/`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of subfolder/index.html');
      }),
      fetch(`http://localhost:${port}/.bar`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
      fetch(`http://localhost:${port}///.bar`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
      fetch(`http://localhost:${port}/%2Ebar`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
      fetch(`http://localhost:${port}///%2Ebar`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
      fetch(`http://localhost:${port}/%2ebar`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
      fetch(`http://localhost:${port}///%2ebar`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
      fetch(`http://localhost:${port}/.bar/../test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/%2Ebar%2F../test.html`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual((await response.text()).trimEnd(), 'Forbidden');
      }),
      fetch(`http://localhost:${port}/%2ebar%2f../test.html`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual((await response.text()).trimEnd(), 'Forbidden');
      }),
      fetch(`http://localhost:${port}/.foo`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
      }),
      fetch(`http://localhost:${port}/.foo/`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
      }),
      fetch(`http://localhost:${port}/.foo/bar.js`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
    ]).then(mustCall()).finally(() => server.close());
  }));
}

{
  const server = createStaticServer({
    directory: fixtures.fileURL('static-server'),
    mimeOverrides: { '.html': 'custom/mime', '.unsupported_extension': 'custom/mime2' },
  }).once('listening', mustCall(() => {
    const { port } = server.address();
    Promise.all([
      fetch(`http://localhost:${port}/test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'custom/mime');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/file.unsupported_extension`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'custom/mime2');
        assert.strictEqual((await response.text()).trimEnd(), 'Dummy file to test mimeOverrides option');
      }),
      fetch(`http://localhost:${port}/subfolder`).then(async (response) => {
        assert(response.ok);
        assert(response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'custom/mime');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of subfolder/index.html');
      }),
    ]).then(mustCall()).finally(() => server.close());
  }));
}

{
  const server = createStaticServer({
    directory: fixtures.path('static-server'),
    filter: null,
  }).once('listening', mustCall(() => {
    const { port } = server.address();
    Promise.all([
      fetch(`http://localhost:${port}/test.html`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of test.html');
      }),
      fetch(`http://localhost:${port}/.bar`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual((await response.text()).trimEnd(), 'Content of .bar');
      }),
      fetch(`http://localhost:${port}/.foo`).then(async (response) => {
        assert(!response.ok);
        assert(response.redirected);
        assert.strictEqual(response.status, 404);
        assert.strictEqual(response.statusText, 'Not Found');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
      }),
      fetch(`http://localhost:${port}/.foo/`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 404);
        assert.strictEqual(response.statusText, 'Not Found');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/html');
      }),
      fetch(`http://localhost:${port}/.foo/bar.js`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'text/javascript');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of .foo/bar.js');
      }),
    ]).then(mustCall()).finally(() => server.close());
  }));
}

{
  const server = createStaticServer({
    directory: fixtures.path('static-server'),
    mimeOverrides: { '.bar': 'foo/bar;key=value' },
    filter: (url, fileURL) => {
      assert(fileURL instanceof URL);
      assert(Object.getPrototypeOf(fileURL), URL.prototype);
      return url === '/.bar';
    },
    log: mustCall(function() {
      assert.strictEqual(arguments.length, 2);
    }, 5),
    onStart: mustCall(),
  }).once('listening', mustCall(() => {
    const { port } = server.address();
    Promise.all([
      fetch(`http://localhost:${port}/test.html`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
      fetch(`http://localhost:${port}/.bar`).then(async (response) => {
        assert(response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 200);
        assert.strictEqual(response.statusText, 'OK');
        assert.strictEqual(new MIMEType(response.headers.get('Content-Type')).essence, 'foo/bar');
        assert.strictEqual((await response.text()).trimEnd(), 'Content of .bar');
      }),
      fetch(`http://localhost:${port}/.foo`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
      }),
      fetch(`http://localhost:${port}/.foo/`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
      }),
      fetch(`http://localhost:${port}/.foo/bar.js`).then(async (response) => {
        assert(!response.ok);
        assert(!response.redirected);
        assert.strictEqual(response.status, 403);
        assert.strictEqual(response.statusText, 'Forbidden');
        assert.strictEqual(response.headers.get('Content-Type'), null);
        assert.strictEqual(await response.text(), 'Forbidden\n');
      }),
    ]).then(mustCall()).finally(() => server.close());
  }));
}
