'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');

const assert = require('assert');
const events = require('events');
const fs = require('fs/promises');
const { createServer } = require('http');

assert.strictEqual(typeof WebAssembly.compileStreaming, 'function');
assert.strictEqual(typeof WebAssembly.instantiateStreaming, 'function');

const simpleWasmBytes = fixtures.readSync('simple.wasm');

// Sets up an HTTP server with the given response handler and calls fetch() to
// obtain a Response from the newly created server.
async function testRequest(handler) {
  const server = createServer((_, res) => handler(res)).unref().listen(0);
  await events.once(server, 'listening');
  const { port } = server.address();
  return fetch(`http://127.0.0.1:${port}/foo.wasm`);
}

// Runs the given function both with the promise itself and as a continuation
// of the promise. We use this to test that the API accepts not just a Response
// but also a Promise that resolves to a Response.
function withPromiseAndResolved(makePromise, consume) {
  return Promise.all([
    consume(makePromise()),
    makePromise().then(consume),
  ]);
}

// The makeResponsePromise function must return a Promise that resolves to a
// Response. The checkResult function receives the Promise returned by
// WebAssembly.compileStreaming and must return a Promise itself.
function testCompileStreaming(makeResponsePromise, checkResult) {
  return withPromiseAndResolved(
    common.mustCall(makeResponsePromise, 2),
    common.mustCall((response) => {
      return checkResult(WebAssembly.compileStreaming(response));
    }, 2)
  );
}

function testCompileStreamingSuccess(makeResponsePromise) {
  return testCompileStreaming(makeResponsePromise, async (modPromise) => {
    const mod = await modPromise;
    assert.strictEqual(mod.constructor, WebAssembly.Module);
  });
}

function testCompileStreamingRejection(makeResponsePromise, rejection) {
  return testCompileStreaming(makeResponsePromise, (modPromise) => {
    assert.strictEqual(modPromise.constructor, Promise);
    return assert.rejects(modPromise, rejection);
  });
}

function testCompileStreamingSuccessUsingFetch(responseCallback) {
  return testCompileStreamingSuccess(() => testRequest(responseCallback));
}

function testCompileStreamingRejectionUsingFetch(responseCallback, rejection) {
  return testCompileStreamingRejection(() => testRequest(responseCallback),
                                       rejection);
}

(async () => {
  // A non-Response should cause a TypeError.
  for (const invalid of [undefined, null, 0, true, 'foo', {}, [], Symbol()]) {
    await withPromiseAndResolved(() => Promise.resolve(invalid), (arg) => {
      return assert.rejects(() => WebAssembly.compileStreaming(arg), {
        name: 'TypeError',
        code: 'ERR_INVALID_ARG_TYPE',
        message: /^The "source" argument .*$/
      });
    });
  }

  // When given a Promise, any rejection should be propagated as-is.
  {
    const err = new RangeError('foo');
    await assert.rejects(() => {
      return WebAssembly.compileStreaming(Promise.reject(err));
    }, (actualError) => actualError === err);
  }

  // A valid WebAssembly file with the correct MIME type.
  await testCompileStreamingSuccessUsingFetch((res) => {
    res.setHeader('Content-Type', 'application/wasm');
    res.end(simpleWasmBytes);
  });

  // The same valid WebAssembly file with the same MIME type, but using a
  // Response whose body is a Buffer instead of calling fetch().
  await testCompileStreamingSuccess(() => {
    return Promise.resolve(new Response(simpleWasmBytes, {
      status: 200,
      headers: { 'Content-Type': 'application/wasm' }
    }));
  });

  // The same valid WebAssembly file with the same MIME type, but using a
  // Response whose body is a ReadableStream instead of calling fetch().
  await testCompileStreamingSuccess(async () => {
    const handle = await fs.open(fixtures.path('simple.wasm'));
    const stream = handle.readableWebStream();
    return Promise.resolve(new Response(stream, {
      status: 200,
      headers: { 'Content-Type': 'application/wasm' }
    }));
  });

  // A larger valid WebAssembly file with the correct MIME type that causes the
  // client to pass it to the compiler in many separate chunks. For this, we use
  // the same WebAssembly file as in the previous test but insert useless custom
  // sections into the WebAssembly module to increase the file size without
  // changing the relevant contents.
  await testCompileStreamingSuccessUsingFetch((res) => {
    res.setHeader('Content-Type', 'application/wasm');

    // Send the WebAssembly magic and version first.
    res.write(simpleWasmBytes.slice(0, 8), common.mustCall());

    // Construct a 4KiB custom section.
    const customSection = Buffer.concat([
      Buffer.from([
        0,        // Custom section.
        134, 32,  // (134 & 0x7f) + 0x80 * 32 = 6 + 4096 bytes in this section.
        5,        // The length of the following section name.
      ]),
      Buffer.from('?'.repeat(5)),      // The section name
      Buffer.from('\0'.repeat(4096)),  // The actual section data
    ]);

    // Now repeatedly send useless custom sections. These have no use for the
    // WebAssembly compiler but they are syntactically valid. The client has to
    // keep reading the stream until the very end to obtain the relevant
    // sections within the module. This adds up to a few hundred kibibytes.
    (function next(i) {
      if (i < 100) {
        while (res.write(customSection));
        res.once('drain', () => next(i + 1));
      } else {
        // End the response body with the actual module contents.
        res.end(simpleWasmBytes.slice(8));
      }
    })(0);
  });

  // A valid WebAssembly file with an empty parameter in the (otherwise valid)
  // MIME type.
  await testCompileStreamingRejectionUsingFetch((res) => {
    res.setHeader('Content-Type', 'application/wasm;');
    res.end(simpleWasmBytes);
  }, {
    name: 'TypeError',
    code: 'ERR_WEBASSEMBLY_RESPONSE',
    message: 'WebAssembly response has unsupported MIME type ' +
             "'application/wasm;'"
  });

  // A valid WebAssembly file with an invalid MIME type.
  await testCompileStreamingRejectionUsingFetch((res) => {
    res.setHeader('Content-Type', 'application/octet-stream');
    res.end(simpleWasmBytes);
  }, {
    name: 'TypeError',
    code: 'ERR_WEBASSEMBLY_RESPONSE',
    message: 'WebAssembly response has unsupported MIME type ' +
             "'application/octet-stream'"
  });

  // HTTP status code indicating an error.
  await testCompileStreamingRejectionUsingFetch((res) => {
    res.statusCode = 418;
    res.setHeader('Content-Type', 'application/wasm');
    res.end(simpleWasmBytes);
  }, {
    name: 'TypeError',
    code: 'ERR_WEBASSEMBLY_RESPONSE',
    message: /^WebAssembly response has status code 418$/
  });

  // HTTP status code indicating an error, but using a Response whose body is
  // a Buffer instead of calling fetch().
  await testCompileStreamingSuccess(() => {
    return Promise.resolve(new Response(simpleWasmBytes, {
      status: 200,
      headers: { 'Content-Type': 'application/wasm' }
    }));
  });

  // Extra bytes after the WebAssembly file.
  await testCompileStreamingRejectionUsingFetch((res) => {
    res.setHeader('Content-Type', 'application/wasm');
    res.end(Buffer.concat([simpleWasmBytes, Buffer.from('foo')]));
  }, {
    name: 'CompileError',
    message: /^WebAssembly\.compileStreaming\(\): .*$/
  });

  // Missing bytes at the end of the WebAssembly file.
  await testCompileStreamingRejectionUsingFetch((res) => {
    res.setHeader('Content-Type', 'application/wasm');
    res.end(simpleWasmBytes.subarray(0, simpleWasmBytes.length - 3));
  }, {
    name: 'CompileError',
    message: /^WebAssembly\.compileStreaming\(\): .*$/
  });

  // Incomplete HTTP response body. The TypeError might come as a surprise, but
  // it originates from within fetch().
  await testCompileStreamingRejectionUsingFetch((res) => {
    res.setHeader('Content-Length', simpleWasmBytes.length);
    res.setHeader('Content-Type', 'application/wasm');
    res.write(simpleWasmBytes.slice(0, 5), common.mustSucceed(() => {
      res.destroy();
    }));
  }, {
    name: 'TypeError',
    message: /terminated/
  });

  // Test "Developer-Facing Display Conventions" described in the WebAssembly
  // Web API specification.
  await testCompileStreaming(() => testRequest((res) => {
    // Respond with a WebAssembly module that only exports a single function,
    // which only contains an 'unreachable' instruction.
    res.setHeader('Content-Type', 'application/wasm');
    res.end(fixtures.readSync('crash.wasm'));
  }), async (modPromise) => {
    // Call the WebAssembly function and check that the error stack contains the
    // correct "WebAssembly location" as per the specification.
    const mod = await modPromise;
    const instance = new WebAssembly.Instance(mod);
    assert.throws(() => instance.exports.crash(), (err) => {
      const stack = err.stack.split(/\n/g);
      assert.strictEqual(stack[0], 'RuntimeError: unreachable');
      assert.match(stack[1],
                   /^\s*at http:\/\/127\.0\.0\.1:\d+\/foo\.wasm:wasm-function\[0\]:0x22$/);
      return true;
    });
  });
})().then(common.mustCall());
