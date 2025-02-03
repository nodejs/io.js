'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const { hasOpenSSL3 } = require('../common/crypto');

if (hasOpenSSL3)
  // TODO(danbev) This test fails with the following error:
  // error:0D00008F:asn1 encoding routines::no matching choice type
  //
  // I've not been able to figure out the reason for this but there
  // is a note in https://wiki.openssl.org/index.php/OpenSSL_3.0 which
  // indicates that this might not work at the moment:
  // "OCSP, PEM, ASN.1 have some very limited library context support"
  common.skip('when using OpenSSL 3.x');

// NOTE: This certificate is hand-generated, hence it is not located in
// `test/fixtures/keys` to avoid confusion.
//
// The key property of this cert is that subjectAltName contains a string with
// a type `23` which cannot be encoded into string by `X509V3_EXT_print`.
const pem = `
-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAzrmfPz5M3wTq2/CwMeSQr/N+R1FCJ+O5n+SMleKvBqaK63eJ
kL4BnySMc+ZLKCt4UQSsPFIBK63QFq8n6/vjuTDMJiBTsvzytw8zJt1Zr2HA71N3
VIPt6NdJ/w5lgddTYxR7XudJZJ5lk3PkG8ZgrhuenPYP80UJYVzAC2YZ9KYe3r2B
rVbut1j+8h0TwVcx2Zg5PorsC/EVxHwo4dCmIHceodikr3UVqHneRcrDBytdG6Mo
IqHhZJwBeii/EES9tpWwWbzYYh+38aGGLIF2h5UlVpr0bdBVVUg+uVX3y/Qmu2Qv
4CrAO2IPV6JER9Niwl3ktzNjOMAUQG6BCRSqRQIDAQABAoIBAAmB0+cOsG5ZRYvT
5+aDgnv1EMuq2wYGnRTTZ/vErxP5OM5XcwYrFtwAzEzQPIieZywisOEdTFx74+QH
LijWLsTnj5v5RKAorejpVArnhyZfsoXPKt/CKYDZ1ddbDCQKiRU3be0RafisqDM9
0zHLz8pyDrtdPaKMfD/0Cgj8KxlrLTmfD4otPXds8fZpQe1hR1y12XKVp47l1siW
qFGTaUPDJpQ67xybR08x5DOqmyo4cNMOuReRWrc/qRbWint9U1882eOH09gVfpJZ
Gp6FZVPSgz10MZdLSPLhXqZkY4IxIvNltjBDqkmivd12CD+GVr0qUmTJHzTpk+kG
/CWuRQkCgYEA4EFf8SJHEl0fLDJnOQFyUPY3MalMuopUkQ5CBUe3QXjQhHXsRDfj
Ci/lyzShJkHPbMDHb/rx3lYZB0xNhwnMWKS1gCFVgOCOTZLfD0K1Anxc1hOSgVxI
y5FdO9VW7oQNlsMH/WuDHps0HhJW/00lcrmdyoUM1+fE/3yPQndhUmMCgYEA6/z6
8Gq4PHHNql+gwunAH2cZKNdmcP4Co8MvXCZwIJsLenUuLIZQ/YBKZoM/y5X/cFAG
WFJJuUe6KFetPaDm6NgZgpOmawyUwd5czDjJ6wWgsRywiTISInfJlgWLBVMOuba7
iBL9Xuy0hmcbj0ByoRW9l3gCiBX3yJw3I6wqXTcCgYBnjei22eRF15iIeTHvQfq+
5iNwnEQhM7V/Uj0sYQR/iEGJmUaj7ca6somDf2cW2nblOlQeIpxD1jAyjYqTW/Pv
zwc9BqeMHqW3rqWwT1Z0smbQODOD5tB6qEKMWaSN+Y6o2qC65kWjAXpclI110PME
+i+iEDRxEsaGT8d7otLfDwKBgQCs+xBaQG/x5p2SAGzP0xYALstzc4jk1FzM+5rw
mkBgtiXQyqpg+sfNOkfPIvAVZEsMYax0+0SNKrWbMsGLRjFchmMUovQ+zccQ4NT2
4b2op8Rlbxk8R9ahK1s5u7Bu47YMjZSjJwBQn4OobVX3SI994njJ2a9JX4j0pQWK
AX5AOwKBgAfOsr8HSHTcxSW4F9gegj+hXsRYbdA+eUkFhEGrYyRJgIlQrk/HbuZC
mKd/bQ5R/vwd1cxgV6A0APzpZtbwdhvP0RWji+WnPPovgGcfK0AHFstHnga67/uu
h2LHnKQZ1qWHn+BXWo5d7hBRwWVaK66g3GDN0blZpSz1kKcpy1Pl
-----END RSA PRIVATE KEY-----
-----BEGIN CERTIFICATE-----
MIICwjCCAaqgAwIBAgIDAQABMA0GCSqGSIb3DQEBDQUAMBUxEzARBgNVBAMWCmxv
Y2FsLmhvc3QwHhcNMTkxMjA1MDQyODMzWhcNNDQxMTI5MDQyODMzWjAVMRMwEQYD
VQQDFgpsb2NhbC5ob3N0MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA
zrmfPz5M3wTq2/CwMeSQr/N+R1FCJ+O5n+SMleKvBqaK63eJkL4BnySMc+ZLKCt4
UQSsPFIBK63QFq8n6/vjuTDMJiBTsvzytw8zJt1Zr2HA71N3VIPt6NdJ/w5lgddT
YxR7XudJZJ5lk3PkG8ZgrhuenPYP80UJYVzAC2YZ9KYe3r2BrVbut1j+8h0TwVcx
2Zg5PorsC/EVxHwo4dCmIHceodikr3UVqHneRcrDBytdG6MoIqHhZJwBeii/EES9
tpWwWbzYYh+38aGGLIF2h5UlVpr0bdBVVUg+uVX3y/Qmu2Qv4CrAO2IPV6JER9Ni
wl3ktzNjOMAUQG6BCRSqRQIDAQABoxswGTAXBgNVHREEEDAOlwwqLmxvY2FsLmhv
c3QwDQYJKoZIhvcNAQENBQADggEBAH5ThRLDLwOGuhKsifyiq7k8gbx1FqRegO7H
SIiIYYB35v5Pk0ZPN8QBJwNQzJEjUMjCpHXNdBxknBXRaA8vkbnryMfJm37gPTwA
m6r0uEG78WgcEAe8bgf9iKtQGP/iydKXpSSpDgKoHbswIxD5qtzT+o6VNnkRTSfK
/OGwakluFSoJ/Q9rLpR8lKjA01BhetXMmHbETiY8LSkxOymMldXSzUTD1WdrVn8U
L3dobxT//R/0GraKXG02mf3gZNlb0MMTvW0pVwVy39YmcPEGh8L0hWh1rpAA/VXC
f79uOowv3lLTzQ9na5EThA0tp8d837hdYrrIHh5cfTqBDxG0Tu8=
-----END CERTIFICATE-----
`;

const tls = require('tls');

const options = {
  key: pem,
  cert: pem,
};

const server = tls.createServer(options, (socket) => {
  socket.end();
});
server.listen(0, common.mustCall(function() {
  const client = tls.connect({
    port: this.address().port,
    rejectUnauthorized: false
  }, common.mustCall(() => {
    // This should not crash process:
    client.getPeerCertificate();

    server.close();
    client.end();
  }));
}));
