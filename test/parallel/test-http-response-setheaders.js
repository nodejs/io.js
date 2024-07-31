'use strict';
const common = require('../common');
const http = require('http');
const assert = require('assert');

{
  const server = http.createServer({ requireHostHeader: false }, common.mustCall((req, res) => {
    res.writeHead(200); // Headers already sent
    const headers = new globalThis.Headers({ foo: '1' });
    assert.throws(() => {
      res.setHeaders(headers);
    }, {
      code: 'ERR_HTTP_HEADERS_SENT'
    });
    res.end();
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert.strictEqual(res.headers.foo, undefined);
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

{
  const server = http.createServer({ requireHostHeader: false }, common.mustCall((req, res) => {
    assert.throws(() => {
      res.setHeaders(['foo', '1']);
    }, {
      code: 'ERR_INVALID_ARG_TYPE'
    });
    assert.throws(() => {
      res.setHeaders({ foo: '1' });
    }, {
      code: 'ERR_INVALID_ARG_TYPE'
    });
    assert.throws(() => {
      res.setHeaders(null);
    }, {
      code: 'ERR_INVALID_ARG_TYPE'
    });
    assert.throws(() => {
      res.setHeaders(undefined);
    }, {
      code: 'ERR_INVALID_ARG_TYPE'
    });
    assert.throws(() => {
      res.setHeaders('test');
    }, {
      code: 'ERR_INVALID_ARG_TYPE'
    });
    assert.throws(() => {
      res.setHeaders(1);
    }, {
      code: 'ERR_INVALID_ARG_TYPE'
    });
    res.end();
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert.strictEqual(res.headers.foo, undefined);
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

{
  const server = http.createServer({ requireHostHeader: false }, common.mustCall((req, res) => {
    const headers = new globalThis.Headers({ foo: '1', bar: '2' });
    res.setHeaders(headers);
    res.writeHead(200);
    res.end();
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert.strictEqual(res.statusCode, 200);
      assert.strictEqual(res.headers.foo, '1');
      assert.strictEqual(res.headers.bar, '2');
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

{
  const server = http.createServer({ requireHostHeader: false }, common.mustCall((req, res) => {
    const headers = new globalThis.Headers({ foo: '1', bar: '2' });
    res.setHeaders(headers);
    res.writeHead(200, ['foo', '3']);
    res.end();
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert.strictEqual(res.statusCode, 200);
      assert.strictEqual(res.headers.foo, '3'); // Override by writeHead
      assert.strictEqual(res.headers.bar, '2');
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

{
  const server = http.createServer({ requireHostHeader: false }, common.mustCall((req, res) => {
    const headers = new Map([['foo', '1'], ['bar', '2']]);
    res.setHeaders(headers);
    res.writeHead(200);
    res.end();
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert.strictEqual(res.statusCode, 200);
      assert.strictEqual(res.headers.foo, '1');
      assert.strictEqual(res.headers.bar, '2');
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

{
  const server = http.createServer({ requireHostHeader: false }, common.mustCall((req, res) => {
    const headers = new Headers();
    headers.append('Set-Cookie', 'a=b');
    headers.append('Set-Cookie', 'c=d');
    res.setHeaders(headers);
    res.end();
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert(Array.isArray(res.headers['set-cookie']));
      assert.strictEqual(res.headers['set-cookie'].length, 2);
      assert.strictEqual(res.headers['set-cookie'][0], 'a=b');
      assert.strictEqual(res.headers['set-cookie'][1], 'c=d');
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

{
  const server = http.createServer({ requireHostHeader: false }, common.mustCall((req, res) => {
    const headers = new Map();
    headers.set('Set-Cookie', ['a=b', 'c=d']);
    res.setHeaders(headers);
    res.end();
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert(Array.isArray(res.headers['set-cookie']));
      assert.strictEqual(res.headers['set-cookie'].length, 2);
      assert.strictEqual(res.headers['set-cookie'][0], 'a=b');
      assert.strictEqual(res.headers['set-cookie'][1], 'c=d');
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}
