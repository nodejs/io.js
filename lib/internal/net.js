'use strict';

const {
  RegExp,
  RegExpPrototypeTest,
  Symbol,
} = primordials;

const Buffer = require('buffer').Buffer;
const { writeBuffer } = internalBinding('fs');
const {
  UVException,
} = require('internal/errors');

// IPv4 Segment
const v4Seg = '(?:25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])';
const v4Str = `(?:${v4Seg}\\.){3}${v4Seg}`;
const IPv4Reg = new RegExp(`^${v4Str}$`);

// IPv6 Segment
const v6Seg = '(?:[0-9a-fA-F]{1,4})';
const IPv6Reg = new RegExp('^(?:' +
  `(?:${v6Seg}:){7}(?:${v6Seg}|:)|` +
  `(?:${v6Seg}:){6}(?:${v4Str}|:${v6Seg}|:)|` +
  `(?:${v6Seg}:){5}(?::${v4Str}|(?::${v6Seg}){1,2}|:)|` +
  `(?:${v6Seg}:){4}(?:(?::${v6Seg}){0,1}:${v4Str}|(?::${v6Seg}){1,3}|:)|` +
  `(?:${v6Seg}:){3}(?:(?::${v6Seg}){0,2}:${v4Str}|(?::${v6Seg}){1,4}|:)|` +
  `(?:${v6Seg}:){2}(?:(?::${v6Seg}){0,3}:${v4Str}|(?::${v6Seg}){1,5}|:)|` +
  `(?:${v6Seg}:){1}(?:(?::${v6Seg}){0,4}:${v4Str}|(?::${v6Seg}){1,6}|:)|` +
  `(?::(?:(?::${v6Seg}){0,5}:${v4Str}|(?::${v6Seg}){1,7}|:))` +
')(?:%[0-9a-zA-Z-.:]{1,})?$');

function isIPv4(s) {
  // TODO(aduh95): Replace RegExpPrototypeTest with RegExpPrototypeExec when it
  // no longer creates a perf regression in the dns benchmark.
  // eslint-disable-next-line node-core/avoid-prototype-pollution
  return RegExpPrototypeTest(IPv4Reg, s);
}

function isIPv6(s) {
  // TODO(aduh95): Replace RegExpPrototypeTest with RegExpPrototypeExec when it
  // no longer creates a perf regression in the dns benchmark.
  // eslint-disable-next-line node-core/avoid-prototype-pollution
  return RegExpPrototypeTest(IPv6Reg, s);
}

function isIP(s) {
  if (isIPv4(s)) return 4;
  if (isIPv6(s)) return 6;
  return 0;
}

function makeSyncWrite(fd) {
  return function(chunk, enc, cb) {
    if (enc !== 'buffer')
      chunk = Buffer.from(chunk, enc);

    this._handle.bytesWritten += chunk.length;

    const ctx = {};
    writeBuffer(fd, chunk, 0, chunk.length, null, undefined, ctx);
    if (ctx.errno !== undefined) {
      const ex = new UVException(ctx);
      ex.errno = ctx.errno;
      return cb(ex);
    }
    cb();
  };
}

/**
 * https://www.iana.org/assignments/iana-ipv4-special-registry/iana-ipv4-special-registry.xhtml
 * https://www.iana.org/assignments/iana-ipv6-special-registry/iana-ipv6-special-registry.xhtml
 * https://www.iana.org/assignments/special-use-domain-names/special-use-domain-names.xhtml
 */
function isLoopback(host) {
  const hostLower = host.toLowerCase();

  return (
    hostLower === 'localhost' ||
    hostLower.startsWith('127.') ||
    hostLower.startsWith('[::1]') ||
    hostLower.startsWith('[0:0:0:0:0:0:0:1]')
  );
}

module.exports = {
  kReinitializeHandle: Symbol('kReinitializeHandle'),
  isIP,
  isIPv4,
  isIPv6,
  makeSyncWrite,
  normalizedArgsSymbol: Symbol('normalizedArgs'),
  isLoopback,
};
