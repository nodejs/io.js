'use strict';

const { hasIntl } = require('../common');

const assert = require('node:assert');
const url = require('node:url');
const { test } = require('node:test');

test('format slightly wonky content to a valid URL', { skip: !hasIntl }, () => {
  const formatTests = {
    'http://example.com?': {
      href: 'http://example.com/?',
      protocol: 'http:',
      slashes: true,
      host: 'example.com',
      hostname: 'example.com',
      search: '?',
      query: {},
      pathname: '/'
    },
    'http://example.com?foo=bar#frag': {
      href: 'http://example.com/?foo=bar#frag',
      protocol: 'http:',
      host: 'example.com',
      hostname: 'example.com',
      hash: '#frag',
      search: '?foo=bar',
      query: 'foo=bar',
      pathname: '/'
    },
    'http://example.com?foo=@bar#frag': {
      href: 'http://example.com/?foo=@bar#frag',
      protocol: 'http:',
      host: 'example.com',
      hostname: 'example.com',
      hash: '#frag',
      search: '?foo=@bar',
      query: 'foo=@bar',
      pathname: '/'
    },
    'http://example.com?foo=/bar/#frag': {
      href: 'http://example.com/?foo=/bar/#frag',
      protocol: 'http:',
      host: 'example.com',
      hostname: 'example.com',
      hash: '#frag',
      search: '?foo=/bar/',
      query: 'foo=/bar/',
      pathname: '/'
    },
    'http://example.com?foo=?bar/#frag': {
      href: 'http://example.com/?foo=?bar/#frag',
      protocol: 'http:',
      host: 'example.com',
      hostname: 'example.com',
      hash: '#frag',
      search: '?foo=?bar/',
      query: 'foo=?bar/',
      pathname: '/'
    },
    'http://example.com#frag=?bar/#frag': {
      href: 'http://example.com/#frag=?bar/#frag',
      protocol: 'http:',
      host: 'example.com',
      hostname: 'example.com',
      hash: '#frag=?bar/#frag',
      pathname: '/'
    },
    'http://google.com" onload="alert(42)/': {
      href: 'http://google.com/%22%20onload=%22alert(42)/',
      protocol: 'http:',
      host: 'google.com',
      pathname: '/%22%20onload=%22alert(42)/'
    },
    'http://a.com/a/b/c?s#h': {
      href: 'http://a.com/a/b/c?s#h',
      protocol: 'http',
      host: 'a.com',
      pathname: 'a/b/c',
      hash: 'h',
      search: 's'
    },
    'xmpp:isaacschlueter@jabber.org': {
      href: 'xmpp:isaacschlueter@jabber.org',
      protocol: 'xmpp:',
      host: 'jabber.org',
      auth: 'isaacschlueter',
      hostname: 'jabber.org'
    },
    'http://atpass:foo%40bar@127.0.0.1/': {
      href: 'http://atpass:foo%40bar@127.0.0.1/',
      auth: 'atpass:foo@bar',
      hostname: '127.0.0.1',
      protocol: 'http:',
      pathname: '/'
    },
    'http://atslash%2F%40:%2F%40@foo/': {
      href: 'http://atslash%2F%40:%2F%40@foo/',
      auth: 'atslash/@:/@',
      hostname: 'foo',
      protocol: 'http:',
      pathname: '/'
    },
    'svn+ssh://foo/bar': {
      href: 'svn+ssh://foo/bar',
      hostname: 'foo',
      protocol: 'svn+ssh:',
      pathname: '/bar',
      slashes: true
    },
    'dash-test://foo/bar': {
      href: 'dash-test://foo/bar',
      hostname: 'foo',
      protocol: 'dash-test:',
      pathname: '/bar',
      slashes: true
    },
    'dash-test:foo/bar': {
      href: 'dash-test:foo/bar',
      hostname: 'foo',
      protocol: 'dash-test:',
      pathname: '/bar'
    },
    'dot.test://foo/bar': {
      href: 'dot.test://foo/bar',
      hostname: 'foo',
      protocol: 'dot.test:',
      pathname: '/bar',
      slashes: true
    },
    'dot.test:foo/bar': {
      href: 'dot.test:foo/bar',
      hostname: 'foo',
      protocol: 'dot.test:',
      pathname: '/bar'
    },
    // IPv6 support
    'coap:u:p@[::1]:61616/.well-known/r?n=Temperature': {
      href: 'coap:u:p@[::1]:61616/.well-known/r?n=Temperature',
      protocol: 'coap:',
      auth: 'u:p',
      hostname: '::1',
      port: '61616',
      pathname: '/.well-known/r',
      search: 'n=Temperature'
    },
    'coap:[fedc:ba98:7654:3210:fedc:ba98:7654:3210]:61616/s/stopButton': {
      href: 'coap:[fedc:ba98:7654:3210:fedc:ba98:7654:3210]:61616/s/stopButton',
      protocol: 'coap',
      host: '[fedc:ba98:7654:3210:fedc:ba98:7654:3210]:61616',
      pathname: '/s/stopButton'
    },
    'http://[::]/': {
      href: 'http://[::]/',
      protocol: 'http:',
      hostname: '[::]',
      pathname: '/'
    },

    // Encode context-specific delimiters in path and query, but do not touch
    // other non-delimiter chars like `%`.
    // <https://github.com/nodejs/node-v0.x-archive/issues/4082>

    // `#`,`?` in path
    '/path/to/%%23%3F+=&.txt?foo=theA1#bar': {
      href: '/path/to/%%23%3F+=&.txt?foo=theA1#bar',
      pathname: '/path/to/%#?+=&.txt',
      query: {
        foo: 'theA1'
      },
      hash: '#bar'
    },

    // `#`,`?` in path + `#` in query
    '/path/to/%%23%3F+=&.txt?foo=the%231#bar': {
      href: '/path/to/%%23%3F+=&.txt?foo=the%231#bar',
      pathname: '/path/to/%#?+=&.txt',
      query: {
        foo: 'the#1'
      },
      hash: '#bar'
    },

    // `#` in path end + `#` in query
    '/path/to/%%23?foo=the%231#bar': {
      href: '/path/to/%%23?foo=the%231#bar',
      pathname: '/path/to/%#',
      query: {
        foo: 'the#1'
      },
      hash: '#bar'
    },

    // `?` and `#` in path and search
    'http://ex.com/foo%3F100%m%23r?abc=the%231?&foo=bar#frag': {
      href: 'http://ex.com/foo%3F100%m%23r?abc=the%231?&foo=bar#frag',
      protocol: 'http:',
      hostname: 'ex.com',
      hash: '#frag',
      search: '?abc=the#1?&foo=bar',
      pathname: '/foo?100%m#r',
    },

    // `?` and `#` in search only
    'http://ex.com/fooA100%mBr?abc=the%231?&foo=bar#frag': {
      href: 'http://ex.com/fooA100%mBr?abc=the%231?&foo=bar#frag',
      protocol: 'http:',
      hostname: 'ex.com',
      hash: '#frag',
      search: '?abc=the#1?&foo=bar',
      pathname: '/fooA100%mBr',
    },

    // Multiple `#` in search
    'http://example.com/?foo=bar%231%232%233&abc=%234%23%235#frag': {
      href: 'http://example.com/?foo=bar%231%232%233&abc=%234%23%235#frag',
      protocol: 'http:',
      slashes: true,
      host: 'example.com',
      hostname: 'example.com',
      hash: '#frag',
      search: '?foo=bar#1#2#3&abc=#4##5',
      query: {},
      pathname: '/'
    },

    // More than 255 characters in hostname which exceeds the limit
    [`http://${'a'.repeat(255)}.com/node`]: {
      href: 'http:///node',
      protocol: 'http:',
      slashes: true,
      host: '',
      hostname: '',
      pathname: '/node',
      path: '/node'
    },

    // Greater than or equal to 63 characters after `.` in hostname
    [`http://www.${'z'.repeat(63)}example.com/node`]: {
      href: `http://www.${'z'.repeat(63)}example.com/node`,
      protocol: 'http:',
      slashes: true,
      host: `www.${'z'.repeat(63)}example.com`,
      hostname: `www.${'z'.repeat(63)}example.com`,
      pathname: '/node',
      path: '/node'
    },

    // https://github.com/nodejs/node/issues/3361
    'file:///home/user': {
      href: 'file:///home/user',
      protocol: 'file',
      pathname: '/home/user',
      path: '/home/user'
    },

    // surrogate in auth
    'http://%F0%9F%98%80@www.example.com/': {
      href: 'http://%F0%9F%98%80@www.example.com/',
      protocol: 'http:',
      auth: '\uD83D\uDE00',
      hostname: 'www.example.com',
      pathname: '/'
    }
  };

  for (const u in formatTests) {
    const expect = formatTests[u].href;
    delete formatTests[u].href;
    const actual = url.format(u);
    const actualObj = url.format(formatTests[u]);
    assert.strictEqual(actual, expect,
                       `wonky format(${u}) == ${expect}\nactual:${actual}`);
    assert.strictEqual(actualObj, expect,
                       `wonky format(${JSON.stringify(formatTests[u])}) == ${
                         expect}\nactual: ${actualObj}`);
  }
});
