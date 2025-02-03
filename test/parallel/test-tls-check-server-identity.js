// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const util = require('util');

const tls = require('tls');

const tests = [
  // False-y values.
  {
    host: false,
    cert: { subject: { CN: 'a.com' } },
    error: 'Host: false. is not cert\'s CN: a.com'
  },
  {
    host: null,
    cert: { subject: { CN: 'a.com' } },
    error: 'Host: null. is not cert\'s CN: a.com'
  },
  {
    host: undefined,
    cert: { subject: { CN: 'a.com' } },
    error: 'Host: undefined. is not cert\'s CN: a.com'
  },

  // Basic CN handling
  { host: 'a.com', cert: { subject: { CN: 'a.com' } } },
  { host: 'a.com', cert: { subject: { CN: 'A.COM' } } },
  {
    host: 'a.com',
    cert: { subject: { CN: 'b.com' } },
    error: 'Host: a.com. is not cert\'s CN: b.com'
  },
  { host: 'a.com', cert: { subject: { CN: 'a.com.' } } },
  {
    host: 'a.com',
    cert: { subject: { CN: '.a.com' } },
    error: 'Host: a.com. is not cert\'s CN: .a.com'
  },

  // IP address in CN. Technically allowed but so rare that we reject
  // it anyway. If we ever do start allowing them, we should take care
  // to only allow public (non-internal, non-reserved) IP addresses,
  // because that's what the spec mandates.
  {
    host: '8.8.8.8',
    cert: { subject: { CN: '8.8.8.8' } },
    error: 'IP: 8.8.8.8 is not in the cert\'s list: '
  },

  // The spec suggests that a "DNS:" Subject Alternative Name containing an
  // IP address is valid but it seems so suspect that we currently reject it.
  {
    host: '8.8.8.8',
    cert: { subject: { CN: '8.8.8.8' }, subjectaltname: 'DNS:8.8.8.8' },
    error: 'IP: 8.8.8.8 is not in the cert\'s list: '
  },

  // Likewise for "URI:" Subject Alternative Names.
  // See also https://github.com/nodejs/node/issues/8108.
  {
    host: '8.8.8.8',
    cert: { subject: { CN: '8.8.8.8' }, subjectaltname: 'URI:http://8.8.8.8/' },
    error: 'IP: 8.8.8.8 is not in the cert\'s list: '
  },

  // An "IP Address:" Subject Alternative Name however is acceptable.
  {
    host: '8.8.8.8',
    cert: { subject: { CN: '8.8.8.8' }, subjectaltname: 'IP Address:8.8.8.8' }
  },

  // But not when it's a CIDR.
  {
    host: '8.8.8.8',
    cert: {
      subject: { CN: '8.8.8.8' },
      subjectaltname: 'IP Address:8.8.8.0/24'
    },
    error: 'IP: 8.8.8.8 is not in the cert\'s list: '
  },

  // Wildcards in CN
  { host: 'b.a.com', cert: { subject: { CN: '*.a.com' } } },
  {
    host: 'ba.com',
    cert: { subject: { CN: '*.a.com' } },
    error: 'Host: ba.com. is not cert\'s CN: *.a.com'
  },
  {
    host: '\n.b.com',
    cert: { subject: { CN: '*n.b.com' } },
    error: 'Host: \n.b.com. is not cert\'s CN: *n.b.com'
  },
  { host: 'b.a.com',
    cert: {
      subjectaltname: 'DNS:omg.com',
      subject: { CN: '*.a.com' },
    },
    error: 'Host: b.a.com. is not in the cert\'s altnames: ' +
           'DNS:omg.com' },
  {
    host: 'b.a.com',
    cert: { subject: { CN: 'b*b.a.com' } },
    error: 'Host: b.a.com. is not cert\'s CN: b*b.a.com'
  },

  // Empty Cert
  {
    host: 'a.com',
    cert: { },
    error: 'Cert does not contain a DNS name'
  },

  // Empty Subject w/DNS name
  {
    host: 'a.com', cert: {
      subjectaltname: 'DNS:a.com',
    }
  },

  // Empty Subject w/URI name
  {
    host: 'a.b.a.com', cert: {
      subjectaltname: 'URI:http://a.b.a.com/',
    },
    error: 'Cert does not contain a DNS name'
  },

  // Multiple CN fields
  {
    host: 'foo.com', cert: {
      subject: { CN: ['foo.com', 'bar.com'] } // CN=foo.com; CN=bar.com;
    }
  },

  // DNS names and CN
  {
    host: 'a.com', cert: {
      subjectaltname: 'DNS:*',
      subject: { CN: 'b.com' }
    },
    error: 'Host: a.com. is not in the cert\'s altnames: ' +
           'DNS:*'
  },
  {
    host: 'a.com', cert: {
      subjectaltname: 'DNS:*.com',
      subject: { CN: 'b.com' }
    },
    error: 'Host: a.com. is not in the cert\'s altnames: ' +
           'DNS:*.com'
  },
  {
    host: 'a.co.uk', cert: {
      subjectaltname: 'DNS:*.co.uk',
      subject: { CN: 'b.com' }
    }
  },
  {
    host: 'a.com', cert: {
      subjectaltname: 'DNS:*.a.com',
      subject: { CN: 'a.com' }
    },
    error: 'Host: a.com. is not in the cert\'s altnames: ' +
           'DNS:*.a.com'
  },
  {
    host: 'a.com', cert: {
      subjectaltname: 'DNS:*.a.com',
      subject: { CN: 'b.com' }
    },
    error: 'Host: a.com. is not in the cert\'s altnames: ' +
           'DNS:*.a.com'
  },
  {
    host: 'a.com', cert: {
      subjectaltname: 'DNS:a.com',
      subject: { CN: 'b.com' }
    }
  },
  {
    host: 'a.com', cert: {
      subjectaltname: 'DNS:A.COM',
      subject: { CN: 'b.com' }
    }
  },

  // DNS names
  {
    host: 'a.com', cert: {
      subjectaltname: 'DNS:*.a.com',
      subject: {}
    },
    error: 'Host: a.com. is not in the cert\'s altnames: ' +
           'DNS:*.a.com'
  },
  {
    host: 'b.a.com', cert: {
      subjectaltname: 'DNS:*.a.com',
      subject: {}
    }
  },
  {
    host: 'c.b.a.com', cert: {
      subjectaltname: 'DNS:*.a.com',
      subject: {}
    },
    error: 'Host: c.b.a.com. is not in the cert\'s altnames: ' +
           'DNS:*.a.com'
  },
  {
    host: 'b.a.com', cert: {
      subjectaltname: 'DNS:*b.a.com',
      subject: {}
    }
  },
  {
    host: 'a-cb.a.com', cert: {
      subjectaltname: 'DNS:*b.a.com',
      subject: {}
    }
  },
  {
    host: 'a.b.a.com', cert: {
      subjectaltname: 'DNS:*b.a.com',
      subject: {}
    },
    error: 'Host: a.b.a.com. is not in the cert\'s altnames: ' +
           'DNS:*b.a.com'
  },
  // Multiple DNS names
  {
    host: 'a.b.a.com', cert: {
      subjectaltname: 'DNS:*b.a.com, DNS:a.b.a.com',
      subject: {}
    }
  },
  // URI names
  {
    host: 'a.b.a.com', cert: {
      subjectaltname: 'URI:http://a.b.a.com/',
      subject: {}
    },
    error: 'Cert does not contain a DNS name'
  },
  {
    host: 'a.b.a.com', cert: {
      subjectaltname: 'URI:http://*.b.a.com/',
      subject: {}
    },
    error: 'Cert does not contain a DNS name'
  },
  // IP addresses
  {
    host: 'a.b.a.com', cert: {
      subjectaltname: 'IP Address:127.0.0.1',
      subject: {}
    },
    error: 'Cert does not contain a DNS name'
  },
  {
    host: '127.0.0.1', cert: {
      subjectaltname: 'IP Address:127.0.0.1',
      subject: {}
    }
  },
  {
    host: '127.0.0.2', cert: {
      subjectaltname: 'IP Address:127.0.0.1',
      subject: {}
    },
    error: 'IP: 127.0.0.2 is not in the cert\'s list: ' +
           '127.0.0.1'
  },
  {
    host: '127.0.0.1', cert: {
      subjectaltname: 'DNS:a.com',
      subject: {}
    },
    error: 'IP: 127.0.0.1 is not in the cert\'s list: '
  },
  {
    host: 'localhost', cert: {
      subjectaltname: 'DNS:a.com',
      subject: { CN: 'localhost' }
    },
    error: 'Host: localhost. is not in the cert\'s altnames: ' +
           'DNS:a.com'
  },
  // IDNA
  {
    host: 'xn--bcher-kva.example.com',
    cert: { subject: { CN: '*.example.com' } },
  },
  // RFC 6125, section 6.4.3: "[...] the client SHOULD NOT attempt to match
  // a presented identifier where the wildcard character is embedded within
  // an A-label [...]"
  {
    host: 'xn--bcher-kva.example.com',
    cert: { subject: { CN: 'xn--*.example.com' } },
    error: 'Host: xn--bcher-kva.example.com. is not cert\'s CN: ' +
            'xn--*.example.com',
  },
];

tests.forEach(function(test, i) {
  const err = tls.checkServerIdentity(test.host, test.cert);
  assert.strictEqual(err?.reason,
                     test.error,
                     `Test# ${i} failed: ${util.inspect(test)} \n` +
                     `${test.error} != ${(err?.reason)}`);
});
