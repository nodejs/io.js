'use strict';

const common = require('../common');
const assert = require('assert');
const { createServer, request } = require('http');

const server = createServer(
  { uniqueHeaders: ['x-res-b', 'x-res-d', 'x-res-y'] },
  common.mustCall((req, res) => {
    const host = `127.0.0.1:${server.address().port}`;

    assert.deepStrictEqual(req.rawHeaders, [
      'connection', 'close',
      'X-Req-a', 'eee',
      'X-Req-a', 'fff',
      'X-Req-a', 'ggg',
      'X-Req-a', 'hhh',
      'X-Req-b', 'iii; jjj; kkk; lll',
      'Host', host,
      'Transfer-Encoding', 'chunked',
    ]);
    assert.deepStrictEqual(req.headers, {
      'connection': 'close',
      'x-req-a': 'eee, fff, ggg, hhh',
      'x-req-b': 'iii; jjj; kkk; lll',
      host,
      'transfer-encoding': 'chunked'
    });
    assert.deepStrictEqual(req.headersDistinct, {
      'connection': ['close'],
      'x-req-a': ['eee', 'fff', 'ggg', 'hhh'],
      'x-req-b': ['iii; jjj; kkk; lll'],
      'host': [host],
      'transfer-encoding': ['chunked']
    });

    req.on('end', function() {
      assert.deepStrictEqual(req.rawTrailers, [
        'x-req-x', 'xxx',
        'x-req-x', 'yyy',
        'X-req-Y', 'zzz; www',
      ]);
      assert.deepStrictEqual(
        req.trailers, { 'x-req-x': 'xxx, yyy', 'x-req-y': 'zzz; www' }
      );
      assert.deepStrictEqual(
        req.trailersDistinct,
        { 'x-req-x': ['xxx', 'yyy'], 'x-req-y': ['zzz; www'] }
      );

      res.setHeader('X-Res-a', 'AAA');
      res.appendHeader('x-res-a', ['BBB', 'CCC']);
      res.setHeader('X-Res-b', ['DDD', 'EEE']);
      res.appendHeader('x-res-b', ['FFF', 'GGG']);
      res.removeHeader('date');
      res.writeHead(200, {
        'Connection': 'close', 'x-res-c': ['HHH', 'III'],
        'x-res-d': ['JJJ', 'KKK', 'LLL']
      });
      res.addTrailers({
        'x-res-x': ['XXX', 'YYY'],
        'X-Res-Y': ['ZZZ', 'WWW']
      });
      res.write('BODY');
      res.end();

      assert.throws(() => res.appendHeader(), {
        code: 'ERR_HTTP_HEADERS_SENT',
      });

      assert.deepStrictEqual(res.getHeader('X-Res-a'), ['AAA', 'BBB', 'CCC']);
      assert.deepStrictEqual(res.getHeader('x-res-a'), ['AAA', 'BBB', 'CCC']);
      assert.deepStrictEqual(
        res.getHeader('x-res-b'), ['DDD', 'EEE', 'FFF', 'GGG']
      );
      assert.deepStrictEqual(res.getHeader('x-res-c'), ['HHH', 'III']);
      assert.strictEqual(res.getHeader('connection'), 'close');
      assert.deepStrictEqual(
        res.getHeaderNames(),
        ['x-res-a', 'x-res-b', 'connection', 'x-res-c', 'x-res-d']
      );
      assert.deepStrictEqual(
        res.getRawHeaderNames(),
        ['X-Res-a', 'X-Res-b', 'Connection', 'x-res-c', 'x-res-d']
      );

      const headers = { __proto__: null };
      Object.assign(headers, {
        'x-res-a': [ 'AAA', 'BBB', 'CCC' ],
        'x-res-b': [ 'DDD', 'EEE', 'FFF', 'GGG' ],
        'connection': 'close',
        'x-res-c': [ 'HHH', 'III' ],
        'x-res-d': [ 'JJJ', 'KKK', 'LLL' ]
      });
      assert.deepStrictEqual(res.getHeaders(), headers);
    });

    req.resume();
  }
  ));

server.listen(0, common.mustCall(() => {
  const req = request({
    host: '127.0.0.1',
    port: server.address().port,
    path: '/',
    method: 'POST',
    headers: {
      'connection': 'close',
      'x-req-a': 'aaa',
      'X-Req-a': 'bbb',
      'X-Req-b': ['ccc', 'ddd']
    },
    uniqueHeaders: ['x-req-b', 'x-req-y']
  }, common.mustCall((res) => {
    assert.deepStrictEqual(res.rawHeaders, [
      'X-Res-a', 'AAA',
      'X-Res-a', 'BBB',
      'X-Res-a', 'CCC',
      'X-Res-b', 'DDD; EEE; FFF; GGG',
      'Connection', 'close',
      'x-res-c', 'HHH',
      'x-res-c', 'III',
      'x-res-d', 'JJJ; KKK; LLL',
      'Transfer-Encoding', 'chunked',
    ]);
    assert.deepStrictEqual(res.headers, {
      'x-res-a': 'AAA, BBB, CCC',
      'x-res-b': 'DDD; EEE; FFF; GGG',
      'connection': 'close',
      'x-res-c': 'HHH, III',
      'x-res-d': 'JJJ; KKK; LLL',
      'transfer-encoding': 'chunked'
    });
    assert.deepStrictEqual(res.headersDistinct, {
      'x-res-a': [ 'AAA', 'BBB', 'CCC' ],
      'x-res-b': [ 'DDD; EEE; FFF; GGG' ],
      'connection': [ 'close' ],
      'x-res-c': [ 'HHH', 'III' ],
      'x-res-d': [ 'JJJ; KKK; LLL' ],
      'transfer-encoding': [ 'chunked' ]
    });

    res.on('end', function() {
      assert.deepStrictEqual(res.rawTrailers, [
        'x-res-x', 'XXX',
        'x-res-x', 'YYY',
        'X-Res-Y', 'ZZZ; WWW',
      ]);
      assert.deepStrictEqual(
        res.trailers,
        { 'x-res-x': 'XXX, YYY', 'x-res-y': 'ZZZ; WWW' }
      );
      assert.deepStrictEqual(
        res.trailersDistinct,
        { 'x-res-x': ['XXX', 'YYY'], 'x-res-y': ['ZZZ; WWW'] }
      );
      server.close();
    });
    res.resume();
  }));

  req.setHeader('X-Req-a', ['eee', 'fff']);
  req.appendHeader('X-req-a', ['ggg', 'hhh']);
  req.setHeader('X-Req-b', ['iii', 'jjj']);
  req.appendHeader('x-req-b', ['kkk', 'lll']);

  req.addTrailers({
    'x-req-x': ['xxx', 'yyy'],
    'X-req-Y': ['zzz', 'www']
  });

  req.write('BODY');

  req.end();
}));
