'use strict';

const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const assert = require('assert');
const { X509Certificate } = require('crypto');
const tls = require('tls');
const fixtures = require('../common/fixtures');

const { hasOpenSSL3 } = require('../common/crypto');

// Test that all certificate chains provided by the reporter are rejected.
{
  const rootPEM = fixtures.readSync('x509-escaping/google/root.pem');
  const intermPEM = fixtures.readSync('x509-escaping/google/intermediate.pem');
  const keyPEM = fixtures.readSync('x509-escaping/google/key.pem');

  const numLeaves = 5;

  for (let i = 0; i < numLeaves; i++) {
    const name = `x509-escaping/google/leaf${i}.pem`;
    const leafPEM = fixtures.readSync(name, 'utf8');

    const server = tls.createServer({
      key: keyPEM,
      cert: leafPEM + intermPEM,
    }, common.mustNotCall()).listen(common.mustCall(() => {
      const { port } = server.address();
      const socket = tls.connect(port, {
        ca: rootPEM,
        servername: 'nodejs.org',
      }, common.mustNotCall());
      socket.on('error', common.mustCall());
    })).unref();
  }
}

// Test escaping rules for subject alternative names.
{
  const expectedSANs = [
    'DNS:"good.example.com\\u002c DNS:evil.example.com"',
    // URIs should not require escaping.
    'URI:http://example.com/',
    'URI:http://example.com/?a=b&c=d',
    // Unless they contain commas.
    'URI:"http://example.com/a\\u002cb"',
    // Percent encoding should not require escaping.
    'URI:http://example.com/a%2Cb',
    // Malicious attempts should be escaped.
    'URI:"http://example.com/a\\u002c DNS:good.example.com"',
    // Non-ASCII characters in DNS names should be treated as Latin-1.
    'DNS:"ex\\u00e4mple.com"',
    // It should not be possible to cause unescaping without escaping.
    'DNS:"\\"evil.example.com\\""',
    // IPv4 addresses should be represented as usual.
    'IP Address:8.8.8.8',
    'IP Address:8.8.4.4',
    // For backward-compatibility, include invalid IP address lengths.
    hasOpenSSL3 ? 'IP Address:<invalid length=5>' : 'IP Address:<invalid>',
    hasOpenSSL3 ? 'IP Address:<invalid length=6>' : 'IP Address:<invalid>',
    // IPv6 addresses are represented as OpenSSL does.
    'IP Address:A0B:C0D:E0F:0:0:0:7A7B:7C7D',
    // Regular email addresses don't require escaping.
    'email:foo@example.com',
    // ... but should be escaped if they contain commas.
    'email:"foo@example.com\\u002c DNS:good.example.com"',
    // New versions of Node.js use RFC2253 to print DirName entries, which
    // almost always results in commas, which should be escaped properly.
    'DirName:"L=Hannover\\u002cC=DE"',
    // Node.js unsets ASN1_STRFLGS_ESC_MSB to prevent unnecessarily escaping
    // Unicode characters, so Unicode characters should be preserved.
    'DirName:"L=München\\u002cC=DE"',
    'DirName:"L=Berlin\\\\\\u002c DNS:good.example.com\\u002cC=DE"',
    // Node.js also unsets ASN1_STRFLGS_ESC_CTRL and relies on JSON-compatible
    // escaping rules to safely escape control characters.
    'DirName:"L=Berlin\\\\\\u002c DNS:good.example.com\\u0000' +
      'evil.example.com\\u002cC=DE"',
    'DirName:"L=Berlin\\\\\\u002c DNS:good.example.com\\\\\\\\\\u0000' +
      'evil.example.com\\u002cC=DE"',
    'DirName:"L=Berlin\\u000d\\u000a\\u002cC=DE"',
    'DirName:"L=Berlin/CN=good.example.com\\u002cC=DE"',
    // Even OIDs that are well-known (such as the following, which is
    // sha256WithRSAEncryption) should be represented numerically only.
    'Registered ID:1.2.840.113549.1.1.11',
    // This is an OID that will likely never be assigned to anything, thus
    // OpenSSL should not know it.
    'Registered ID:1.3.9999.12.34',
    hasOpenSSL3 ?
      'othername:XmppAddr:abc123' :
      'othername:<unsupported>',
    hasOpenSSL3 ?
      'othername:"XmppAddr:abc123\\u002c DNS:good.example.com"' :
      'othername:<unsupported>',
    hasOpenSSL3 ?
      'othername:"XmppAddr:good.example.com\\u0000abc123"' :
      'othername:<unsupported>',
    // This is unsupported because the OID is not recognized.
    'othername:<unsupported>',
    hasOpenSSL3 ? 'othername:SRVName:abc123' : 'othername:<unsupported>',
    // This is unsupported because it is an SRVName with a UTF8String value,
    // which is not allowed for SRVName.
    'othername:<unsupported>',
    hasOpenSSL3 ?
      'othername:"SRVName:abc\\u0000def"' :
      'othername:<unsupported>',
  ];

  const serverKey = fixtures.readSync('x509-escaping/server-key.pem', 'utf8');

  for (let i = 0; i < expectedSANs.length; i++) {
    const pem = fixtures.readSync(`x509-escaping/alt-${i}-cert.pem`, 'utf8');

    // Test the subjectAltName property of the X509Certificate API.
    const cert = new X509Certificate(pem);
    assert.strictEqual(cert.subjectAltName, expectedSANs[i]);

    // Test that the certificate obtained by checkServerIdentity has the correct
    // subjectaltname property.
    const server = tls.createServer({
      key: serverKey,
      cert: pem,
    }, common.mustCall((conn) => {
      conn.destroy();
      server.close();
    })).listen(common.mustCall(() => {
      const { port } = server.address();
      tls.connect(port, {
        ca: pem,
        servername: 'example.com',
        checkServerIdentity: (hostname, peerCert) => {
          assert.strictEqual(hostname, 'example.com');
          assert.strictEqual(peerCert.subjectaltname, expectedSANs[i]);
        },
      }, common.mustCall());
    }));
  }
}

// Test escaping rules for authority info access.
{
  const expectedInfoAccess = [
    {
      text: 'OCSP - URI:"http://good.example.com/\\u000a' +
            'OCSP - URI:http://evil.example.com/"',
      legacy: {
        'OCSP - URI': [
          'http://good.example.com/\nOCSP - URI:http://evil.example.com/',
        ],
      },
    },
    {
      text: 'CA Issuers - URI:"http://ca.example.com/\\u000a' +
            'OCSP - URI:http://evil.example.com"\n' +
            'OCSP - DNS:"good.example.com\\u000a' +
            'OCSP - URI:http://ca.nodejs.org/ca.cert"',
      legacy: {
        'CA Issuers - URI': [
          'http://ca.example.com/\nOCSP - URI:http://evil.example.com',
        ],
        'OCSP - DNS': [
          'good.example.com\nOCSP - URI:http://ca.nodejs.org/ca.cert',
        ],
      },
    },
    {
      text: '1.3.9999.12.34 - URI:http://ca.example.com/',
      legacy: {
        '1.3.9999.12.34 - URI': [
          'http://ca.example.com/',
        ],
      },
    },
    hasOpenSSL3 ? {
      text: 'OCSP - othername:XmppAddr:good.example.com\n' +
            'OCSP - othername:<unsupported>\n' +
            'OCSP - othername:SRVName:abc123',
      legacy: {
        'OCSP - othername': [
          'XmppAddr:good.example.com',
          '<unsupported>',
          'SRVName:abc123',
        ],
      },
    } : {
      text: 'OCSP - othername:<unsupported>\n' +
            'OCSP - othername:<unsupported>\n' +
            'OCSP - othername:<unsupported>',
      legacy: {
        'OCSP - othername': [
          '<unsupported>',
          '<unsupported>',
          '<unsupported>',
        ],
      },
    },
    hasOpenSSL3 ? {
      text: 'OCSP - othername:"XmppAddr:good.example.com\\u0000abc123"',
      legacy: {
        'OCSP - othername': [
          'XmppAddr:good.example.com\0abc123',
        ],
      },
    } : {
      text: 'OCSP - othername:<unsupported>',
      legacy: {
        'OCSP - othername': [
          '<unsupported>',
        ],
      },
    },
  ];

  const serverKey = fixtures.readSync('x509-escaping/server-key.pem', 'utf8');

  for (let i = 0; i < expectedInfoAccess.length; i++) {
    const pem = fixtures.readSync(`x509-escaping/info-${i}-cert.pem`, 'utf8');
    const expected = expectedInfoAccess[i];

    // Test the subjectAltName property of the X509Certificate API.
    const cert = new X509Certificate(pem);
    assert.strictEqual(cert.infoAccess,
                       `${expected.text}${hasOpenSSL3 ? '' : '\n'}`);

    // Test that the certificate obtained by checkServerIdentity has the correct
    // subjectaltname property.
    const server = tls.createServer({
      key: serverKey,
      cert: pem,
    }, common.mustCall((conn) => {
      conn.destroy();
      server.close();
    })).listen(common.mustCall(() => {
      const { port } = server.address();
      tls.connect(port, {
        ca: pem,
        servername: 'example.com',
        checkServerIdentity: (hostname, peerCert) => {
          assert.strictEqual(hostname, 'example.com');
          assert.deepStrictEqual(peerCert.infoAccess,
                                 Object.assign({ __proto__: null },
                                               expected.legacy));

          // toLegacyObject() should also produce the same properties. However,
          // the X509Certificate is not aware of the chain, so we need to add
          // the circular issuerCertificate reference manually for the assertion
          // to be true.
          const obj = cert.toLegacyObject();
          assert.strictEqual(obj.issuerCertificate, undefined);
          obj.issuerCertificate = obj;
          assert.deepStrictEqual(peerCert, obj);
        },
      }, common.mustCall());
    }));
  }
}

// Test escaping rules for the subject field.
{
  const expectedSubjects = [
    {
      text: 'L=Somewhere\nCN=evil.example.com',
      legacy: {
        L: 'Somewhere',
        CN: 'evil.example.com',
      },
    },
    {
      text: 'L=Somewhere\\00evil.example.com',
      legacy: {
        L: 'Somewhere\0evil.example.com',
      },
    },
    {
      text: 'L=Somewhere\\0ACN=evil.example.com',
      legacy: {
        L: 'Somewhere\nCN=evil.example.com'
      },
    },
    {
      text: 'L=Somewhere\\, CN = evil.example.com',
      legacy: {
        L: 'Somewhere, CN = evil.example.com'
      },
    },
    {
      text: 'L=Somewhere/CN=evil.example.com',
      legacy: {
        L: 'Somewhere/CN=evil.example.com'
      },
    },
    {
      text: 'L=München\\\\\\0ACN=evil.example.com',
      legacy: {
        L: 'München\\\nCN=evil.example.com'
      }
    },
    {
      text: 'L=Somewhere + CN=evil.example.com',
      legacy: {
        L: 'Somewhere',
        CN: 'evil.example.com',
      }
    },
    {
      text: 'L=Somewhere \\+ CN=evil.example.com',
      legacy: {
        L: 'Somewhere + CN=evil.example.com'
      }
    },
    // Observe that the legacy representation cannot properly distinguish
    // between multi-value RDNs and multiple single-value RDNs.
    {
      text: 'L=L1 + L=L2\nL=L3',
      legacy: {
        L: ['L1', 'L2', 'L3']
      },
    },
    {
      text: 'L=L1\nL=L2\nL=L3',
      legacy: {
        L: ['L1', 'L2', 'L3']
      },
    },
  ];

  const serverKey = fixtures.readSync('x509-escaping/server-key.pem', 'utf8');

  for (let i = 0; i < expectedSubjects.length; i++) {
    const pem = fixtures.readSync(`x509-escaping/subj-${i}-cert.pem`, 'utf8');
    const expected = expectedSubjects[i];

    // Test the subject property of the X509Certificate API.
    const cert = new X509Certificate(pem);
    assert.strictEqual(cert.subject, expected.text);
    // The issuer MUST be the same as the subject since the cert is self-signed.
    assert.strictEqual(cert.issuer, expected.text);

    // Test that the certificate obtained by checkServerIdentity has the correct
    // subject property.
    const server = tls.createServer({
      key: serverKey,
      cert: pem,
    }, common.mustCall((conn) => {
      conn.destroy();
      server.close();
    })).listen(common.mustCall(() => {
      const { port } = server.address();
      tls.connect(port, {
        ca: pem,
        servername: 'example.com',
        checkServerIdentity: (hostname, peerCert) => {
          assert.strictEqual(hostname, 'example.com');
          const expectedObject = Object.assign({ __proto__: null },
                                               expected.legacy);
          assert.deepStrictEqual(peerCert.subject, expectedObject);
          // The issuer MUST be the same as the subject since the cert is
          // self-signed. Otherwise, OpenSSL would have already rejected the
          // certificate while connecting to the TLS server.
          assert.deepStrictEqual(peerCert.issuer, expectedObject);

          // toLegacyObject() should also produce the same properties. However,
          // the X509Certificate is not aware of the chain, so we need to add
          // the circular issuerCertificate reference manually for the assertion
          // to be true.
          const obj = cert.toLegacyObject();
          assert.strictEqual(obj.issuerCertificate, undefined);
          obj.issuerCertificate = obj;
          assert.deepStrictEqual(peerCert, obj);
        },
      }, common.mustCall());
    }));
  }
}

// The internal parsing logic must match the JSON specification exactly.
{
  // This list is partially based on V8's own JSON tests.
  const invalidJSON = [
    '"\\a invalid escape"',
    '"\\v invalid escape"',
    '"\\\' invalid escape"',
    '"\\x42 invalid escape"',
    '"\\u202 invalid escape"',
    '"\\012 invalid escape"',
    '"Unterminated string',
    '"Unterminated string\\"',
    '"Unterminated string\\\\\\"',
    '"\u0000 control character"',
    '"\u001e control character"',
    '"\u001f control character"',
  ];

  for (const invalidStringLiteral of invalidJSON) {
    // Usually, checkServerIdentity returns an error upon verification failure.
    // In this case, however, it should throw an error since this is not a
    // verification error. Node.js itself will never produce invalid JSON string
    // literals, so this can only happen when users construct invalid subject
    // alternative name strings (that do not follow escaping rules).
    assert.throws(() => {
      tls.checkServerIdentity('example.com', {
        subjectaltname: `DNS:${invalidStringLiteral}`,
      });
    }, {
      code: 'ERR_TLS_CERT_ALTNAME_FORMAT',
      message: 'Invalid subject alternative name string'
    });
  }
}

// While node does not produce commas within SAN entries, it should parse them
// correctly (i.e., not simply split at commas).
{
  // Regardless of the quotes, splitting this SAN string at commas would
  // cause checkServerIdentity to see 'DNS:b.example.com' and thus to accept
  // the certificate for b.example.com.
  const san = 'DNS:"a.example.com, DNS:b.example.com, DNS:c.example.com"';

  // This is what node used to do, and which is not correct!
  const hostname = 'b.example.com';
  assert.strictEqual(san.split(', ')[1], `DNS:${hostname}`);

  // The new implementation should parse the string correctly.
  const err = tls.checkServerIdentity(hostname, { subjectaltname: san });
  assert(err);
  assert.strictEqual(err.code, 'ERR_TLS_CERT_ALTNAME_INVALID');
  assert.strictEqual(err.message, 'Hostname/IP does not match certificate\'s ' +
                                  'altnames: Host: b.example.com. is not in ' +
                                  'the cert\'s altnames: DNS:"a.example.com, ' +
                                  'DNS:b.example.com, DNS:c.example.com"');
}

// The subject MUST be ignored if a dNSName subject alternative name exists.
{
  const key = fixtures.readKey('incorrect_san_correct_subject-key.pem');
  const cert = fixtures.readKey('incorrect_san_correct_subject-cert.pem');

  // The hostname is the CN, but not a SAN entry.
  const servername = 'good.example.com';
  const certX509 = new X509Certificate(cert);
  assert.strictEqual(certX509.subject, `CN=${servername}`);
  assert.strictEqual(certX509.subjectAltName, 'DNS:evil.example.com');

  // The newer X509Certificate API allows customizing this behavior:
  assert.strictEqual(certX509.checkHost(servername), undefined);
  assert.strictEqual(certX509.checkHost(servername, { subject: 'default' }),
                     undefined);
  assert.strictEqual(certX509.checkHost(servername, { subject: 'always' }),
                     servername);
  assert.strictEqual(certX509.checkHost(servername, { subject: 'never' }),
                     undefined);

  // Try connecting to a server that uses the self-signed certificate.
  const server = tls.createServer({ key, cert }, common.mustNotCall());
  server.listen(common.mustCall(() => {
    const { port } = server.address();
    const socket = tls.connect(port, {
      ca: cert,
      servername,
    }, common.mustNotCall());
    socket.on('error', common.mustCall((err) => {
      assert.strictEqual(err.code, 'ERR_TLS_CERT_ALTNAME_INVALID');
      assert.strictEqual(err.message, 'Hostname/IP does not match ' +
                                      "certificate's altnames: Host: " +
                                      "good.example.com. is not in the cert's" +
                                      ' altnames: DNS:evil.example.com');
    }));
  })).unref();
}

// The subject MUST NOT be ignored if no dNSName subject alternative name
// exists, even if other subject alternative names exist.
{
  const key = fixtures.readKey('irrelevant_san_correct_subject-key.pem');
  const cert = fixtures.readKey('irrelevant_san_correct_subject-cert.pem');

  // The hostname is the CN, but there is no dNSName SAN entry.
  const servername = 'good.example.com';
  const certX509 = new X509Certificate(cert);
  assert.strictEqual(certX509.subject, `CN=${servername}`);
  assert.strictEqual(certX509.subjectAltName, 'IP Address:1.2.3.4');

  // The newer X509Certificate API allows customizing this behavior:
  assert.strictEqual(certX509.checkHost(servername), servername);
  assert.strictEqual(certX509.checkHost(servername, { subject: 'default' }),
                     servername);
  assert.strictEqual(certX509.checkHost(servername, { subject: 'always' }),
                     servername);
  assert.strictEqual(certX509.checkHost(servername, { subject: 'never' }),
                     undefined);

  // Connect to a server that uses the self-signed certificate.
  const server = tls.createServer({ key, cert }, common.mustCall((socket) => {
    socket.destroy();
    server.close();
  })).listen(common.mustCall(() => {
    const { port } = server.address();
    tls.connect(port, {
      ca: cert,
      servername,
    }, common.mustCall());
  }));
}
