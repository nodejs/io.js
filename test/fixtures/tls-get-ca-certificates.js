'use strict';
// This fixture just writes tls.getCACertificates() outputs to process.env.CA_OUT
const tls = require('tls');
const fs = require('fs');
const assert = require('assert');
assert(process.env.CA_TYPE);
assert(process.env.CA_OUT);

const certs = tls.getCACertificates(process.env.CA_TYPE);

fs.writeFileSync(process.env.CA_OUT, JSON.stringify(certs), 'utf8');
