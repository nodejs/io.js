'use strict';

const assert = require('assert');
const os = require('os');
const { isIP } = require('net');

const types = {
  A: 1,
  AAAA: 28,
  NS: 2,
  CNAME: 5,
  SOA: 6,
  PTR: 12,
  MX: 15,
  TXT: 16,
  ANY: 255,
  CAA: 257,
};

const classes = {
  IN: 1,
};

// Naïve DNS parser/serializer.

function readDomainFromPacket(buffer, offset) {
  assert.ok(offset < buffer.length);
  const length = buffer[offset];
  if (length === 0) {
    return { nread: 1, domain: '' };
  } else if ((length & 0xC0) === 0) {
    offset += 1;
    const chunk = buffer.toString('ascii', offset, offset + length);
    // Read the rest of the domain.
    const { nread, domain } = readDomainFromPacket(buffer, offset + length);
    return {
      nread: 1 + length + nread,
      domain: domain ? `${chunk}.${domain}` : chunk,
    };
  }
  // Pointer to another part of the packet.
  assert.strictEqual(length & 0xC0, 0xC0);
  // eslint-disable-next-line @stylistic/js/space-infix-ops, @stylistic/js/space-unary-ops
  const pointeeOffset = buffer.readUInt16BE(offset) &~ 0xC000;
  return {
    nread: 2,
    domain: readDomainFromPacket(buffer, pointeeOffset),
  };
}

function parseDNSPacket(buffer) {
  assert.ok(buffer.length > 12);

  const parsed = {
    id: buffer.readUInt16BE(0),
    flags: buffer.readUInt16BE(2),
  };

  const counts = [
    ['questions', buffer.readUInt16BE(4)],
    ['answers', buffer.readUInt16BE(6)],
    ['authorityAnswers', buffer.readUInt16BE(8)],
    ['additionalRecords', buffer.readUInt16BE(10)],
  ];

  let offset = 12;
  for (const [ sectionName, count ] of counts) {
    parsed[sectionName] = [];
    for (let i = 0; i < count; ++i) {
      const { nread, domain } = readDomainFromPacket(buffer, offset);
      offset += nread;

      const type = buffer.readUInt16BE(offset);

      const rr = {
        domain,
        cls: buffer.readUInt16BE(offset + 2),
      };
      offset += 4;

      for (const name in types) {
        if (types[name] === type)
          rr.type = name;
      }

      if (sectionName !== 'questions') {
        rr.ttl = buffer.readInt32BE(offset);
        const dataLength = buffer.readUInt16BE(offset);
        offset += 6;

        switch (type) {
          case types.A:
            assert.strictEqual(dataLength, 4);
            rr.address = `${buffer[offset + 0]}.${buffer[offset + 1]}.` +
                         `${buffer[offset + 2]}.${buffer[offset + 3]}`;
            break;
          case types.AAAA:
            assert.strictEqual(dataLength, 16);
            rr.address = buffer.toString('hex', offset, offset + 16)
                               .replace(/(.{4}(?!$))/g, '$1:');
            break;
          case types.TXT:
          {
            let position = offset;
            rr.entries = [];
            while (position < offset + dataLength) {
              const txtLength = buffer[offset];
              rr.entries.push(buffer.toString('utf8',
                                              position + 1,
                                              position + 1 + txtLength));
              position += 1 + txtLength;
            }
            assert.strictEqual(position, offset + dataLength);
            break;
          }
          case types.MX:
          {
            rr.priority = buffer.readInt16BE(buffer, offset);
            offset += 2;
            const { nread, domain } = readDomainFromPacket(buffer, offset);
            rr.exchange = domain;
            assert.strictEqual(nread, dataLength);
            break;
          }
          case types.NS:
          case types.CNAME:
          case types.PTR:
          {
            const { nread, domain } = readDomainFromPacket(buffer, offset);
            rr.value = domain;
            assert.strictEqual(nread, dataLength);
            break;
          }
          case types.SOA:
          {
            const mname = readDomainFromPacket(buffer, offset);
            const rname = readDomainFromPacket(buffer, offset + mname.nread);
            rr.nsname = mname.domain;
            rr.hostmaster = rname.domain;
            const trailerOffset = offset + mname.nread + rname.nread;
            rr.serial = buffer.readUInt32BE(trailerOffset);
            rr.refresh = buffer.readUInt32BE(trailerOffset + 4);
            rr.retry = buffer.readUInt32BE(trailerOffset + 8);
            rr.expire = buffer.readUInt32BE(trailerOffset + 12);
            rr.minttl = buffer.readUInt32BE(trailerOffset + 16);

            assert.strictEqual(trailerOffset + 20, dataLength);
            break;
          }
          default:
            throw new Error(`Unknown RR type ${rr.type}`);
        }
        offset += dataLength;
      }

      parsed[sectionName].push(rr);

      assert.ok(offset <= buffer.length);
    }
  }

  assert.strictEqual(offset, buffer.length);
  return parsed;
}

function writeIPv6(ip) {
  const parts = ip.replace(/^:|:$/g, '').split(':');
  const buf = Buffer.alloc(16);

  let offset = 0;
  for (const part of parts) {
    if (part === '') {
      offset += 16 - 2 * (parts.length - 1);
    } else {
      buf.writeUInt16BE(parseInt(part, 16), offset);
      offset += 2;
    }
  }

  return buf;
}

function writeDomainName(domain) {
  return Buffer.concat(domain.split('.').map((label) => {
    assert(label.length < 64);
    return Buffer.concat([
      Buffer.from([label.length]),
      Buffer.from(label, 'ascii'),
    ]);
  }).concat([Buffer.alloc(1)]));
}

function writeDNSPacket(parsed) {
  const buffers = [];
  const kStandardResponseFlags = 0x8180;

  buffers.push(new Uint16Array([
    parsed.id,
    parsed.flags ?? kStandardResponseFlags,
    parsed.questions?.length,
    parsed.answers?.length,
    parsed.authorityAnswers?.length,
    parsed.additionalRecords?.length,
  ]));

  for (const q of parsed.questions) {
    assert(types[q.type]);
    buffers.push(writeDomainName(q.domain));
    buffers.push(new Uint16Array([
      types[q.type],
      q.cls === undefined ? classes.IN : q.cls,
    ]));
  }

  for (const rr of [].concat(parsed.answers,
                             parsed.authorityAnswers,
                             parsed.additionalRecords)) {
    if (!rr) continue;

    assert(types[rr.type]);
    buffers.push(writeDomainName(rr.domain));
    buffers.push(new Uint16Array([
      types[rr.type],
      rr.cls === undefined ? classes.IN : rr.cls,
    ]));
    buffers.push(new Int32Array([rr.ttl]));

    const rdLengthBuf = new Uint16Array(1);
    buffers.push(rdLengthBuf);

    switch (rr.type) {
      case 'A':
        rdLengthBuf[0] = 4;
        buffers.push(new Uint8Array(rr.address.split('.')));
        break;
      case 'AAAA':
        rdLengthBuf[0] = 16;
        buffers.push(writeIPv6(rr.address));
        break;
      case 'TXT': {
        const total = rr.entries.map((s) => s.length).reduce((a, b) => a + b);
        // Total length of all strings + 1 byte each for their lengths.
        rdLengthBuf[0] = rr.entries.length + total;
        for (const txt of rr.entries) {
          buffers.push(new Uint8Array([Buffer.byteLength(txt)]));
          buffers.push(Buffer.from(txt));
        }
        break;
      }
      case 'MX':
        rdLengthBuf[0] = 2;
        buffers.push(new Uint16Array([rr.priority]));
        // fall through
      case 'NS':
      case 'CNAME':
      case 'PTR':
      {
        const domain = writeDomainName(rr.exchange || rr.value);
        rdLengthBuf[0] += domain.length;
        buffers.push(domain);
        break;
      }
      case 'SOA':
      {
        const mname = writeDomainName(rr.nsname);
        const rname = writeDomainName(rr.hostmaster);
        rdLengthBuf[0] = mname.length + rname.length + 20;
        buffers.push(mname, rname);
        buffers.push(new Uint32Array([
          rr.serial, rr.refresh, rr.retry, rr.expire, rr.minttl,
        ]));
        break;
      }
      case 'CAA':
      {
        rdLengthBuf[0] = 5 + rr.issue.length + 2;
        buffers.push(Buffer.from([Number(rr.critical)]));
        buffers.push(Buffer.from([Number(5)]));
        buffers.push(Buffer.from('issue' + rr.issue));
        break;
      }
      default:
        throw new Error(`Unknown RR type ${rr.type}`);
    }
  }

  return Buffer.concat(buffers.map((typedArray) => {
    const buf = Buffer.from(typedArray.buffer,
                            typedArray.byteOffset,
                            typedArray.byteLength);
    if (os.endianness() === 'LE') {
      if (typedArray.BYTES_PER_ELEMENT === 2) buf.swap16();
      if (typedArray.BYTES_PER_ELEMENT === 4) buf.swap32();
    }
    return buf;
  }));
}

const mockedErrorCode = 'ENOTFOUND';
const mockedSysCall = 'getaddrinfo';

function errorLookupMock(code = mockedErrorCode, syscall = mockedSysCall) {
  return function lookupWithError(hostname, dnsopts, cb) {
    const err = new Error(`${syscall} ${code} ${hostname}`);
    err.code = code;
    err.errno = code;
    err.syscall = syscall;
    err.hostname = hostname;
    cb(err);
  };
}

function createMockedLookup(...addresses) {
  addresses = addresses.map((address) => ({ address: address, family: isIP(address) }));

  // Create a DNS server which replies with a AAAA and a A record for the same host
  return function lookup(hostname, options, cb) {
    if (options.all === true) {
      process.nextTick(() => {
        cb(null, addresses);
      });

      return;
    }

    process.nextTick(() => {
      cb(null, addresses[0].address, addresses[0].family);
    });
  };
}

module.exports = {
  types,
  classes,
  writeDNSPacket,
  parseDNSPacket,
  errorLookupMock,
  mockedErrorCode,
  mockedSysCall,
  createMockedLookup,
};
