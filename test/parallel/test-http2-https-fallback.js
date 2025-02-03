'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');

if (!common.hasCrypto)
  common.skip('missing crypto');

const { strictEqual, ok } = require('assert');
const { createSecureContext } = require('tls');
const { createSecureServer, connect } = require('http2');
const { get } = require('https');
const { parse } = require('url');
const { connect: tls } = require('tls');
const { Duplex } = require('stream');

const countdown = (count, done) => () => --count === 0 && done();

const key = fixtures.readKey('agent8-key.pem');
const cert = fixtures.readKey('agent8-cert.pem');
const ca = fixtures.readKey('fake-startcom-root-cert.pem');

const clientOptions = { secureContext: createSecureContext({ ca }) };

function onRequest(request, response) {
  const { socket: { alpnProtocol } } = request.httpVersion === '2.0' ?
    request.stream.session : request;
  response.writeHead(200, { 'content-type': 'application/json' });
  response.end(JSON.stringify({
    alpnProtocol,
    httpVersion: request.httpVersion
  }));
}

function onSession(session, next) {
  const headers = {
    ':path': '/',
    ':method': 'GET',
    ':scheme': 'https',
    ':authority': `localhost:${this.server.address().port}`
  };

  const request = session.request(headers);
  request.on('response', common.mustCall((headers) => {
    strictEqual(headers[':status'], 200);
    strictEqual(headers['content-type'], 'application/json');
  }));
  request.setEncoding('utf8');
  let raw = '';
  request.on('data', (chunk) => { raw += chunk; });
  request.on('end', common.mustCall(() => {
    const { alpnProtocol, httpVersion } = JSON.parse(raw);
    strictEqual(alpnProtocol, 'h2');
    strictEqual(httpVersion, '2.0');

    session.close();
    this.cleanup();

    if (typeof next === 'function') {
      next();
    }
  }));
  request.end();
}

// HTTP/2 & HTTP/1.1 server
{
  const server = createSecureServer(
    { cert, key, allowHTTP1: true },
    common.mustCall(onRequest, 2)
  );

  server.listen(0);

  server.on('listening', common.mustCall(() => {
    const { port } = server.address();
    const origin = `https://localhost:${port}`;

    const cleanup = countdown(2, () => server.close());

    // HTTP/2 client
    connect(
      origin,
      clientOptions,
      common.mustCall(onSession.bind({ cleanup, server }))
    );

    // HTTP/1.1 client
    get(
      Object.assign(parse(origin), clientOptions),
      common.mustCall((response) => {
        strictEqual(response.statusCode, 200);
        strictEqual(response.statusMessage, 'OK');
        strictEqual(response.headers['content-type'], 'application/json');

        response.setEncoding('utf8');
        let raw = '';
        response.on('data', (chunk) => { raw += chunk; });
        response.on('end', common.mustCall(() => {
          const { alpnProtocol, httpVersion } = JSON.parse(raw);
          strictEqual(alpnProtocol, false);
          strictEqual(httpVersion, '1.1');

          cleanup();
        }));
      })
    );
  }));
}

// HTTP/2-only server
{
  const server = createSecureServer(
    { cert, key },
    common.mustCall(onRequest)
  );

  server.once('unknownProtocol', common.mustCall((socket) => {
    strictEqual(socket instanceof Duplex, true);
    socket.destroy();
  }));

  server.listen(0);

  server.on('listening', common.mustCall(() => {
    const { port } = server.address();
    const origin = `https://localhost:${port}`;

    const cleanup = countdown(4, () => server.close());

    // HTTP/2 client
    connect(
      origin,
      clientOptions,
      common.mustCall(function(session) {
        onSession.call({ cleanup, server },
                       session,
                       common.mustCall(testNoTls));
      })
    );

    function testNoTls() {
      // HTTP/1.1 client
      get(Object.assign(parse(origin), clientOptions), common.mustNotCall())
        .on('error', common.mustCall(cleanup))
        .on('error', common.mustCall(testWrongALPN))
        .end();
    }

    function testWrongALPN() {
      // Incompatible ALPN TLS client
      tls(Object.assign({ port, ALPNProtocols: ['fake'] }, clientOptions))
        .on('error', common.mustCall((err) => {
          const allowedErrors = ['ECONNRESET', 'ERR_SSL_TLSV1_ALERT_NO_APPLICATION_PROTOCOL'];
          ok(allowedErrors.includes(err.code), `'${err.code}' was not one of ${allowedErrors}.`);
          cleanup();
          testNoALPN();
        }));
    }

    function testNoALPN() {
      // TLS client does not send an ALPN extension
      let text = '';
      tls(Object.assign({ port }, clientOptions))
        .setEncoding('utf8')
        .on('data', (chunk) => text += chunk)
        .on('end', common.mustCall(() => {
          ok(/Missing ALPN Protocol, expected `h2` to be available/.test(text));
          cleanup();
        }));
    }
  }));
}
