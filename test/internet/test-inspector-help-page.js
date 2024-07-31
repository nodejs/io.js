'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const https = require('https');
const { spawnSync } = require('child_process');
const child = spawnSync(process.execPath, ['--inspect', '-e', '""']);
const stderr = child.stderr.toString();
const helpUrl = stderr.match(/For help, see: (.+)/)[1];

function check(url, cb) {
  https.get(url, { agent: new https.Agent() }, common.mustCall((res) => {
    assert(res.statusCode >= 200 && res.statusCode < 400);

    if (res.statusCode >= 300)
      return check(new URL(res.headers.location, url), cb);

    let result = '';

    res.setEncoding('utf8');
    res.on('data', (data) => {
      result += data;
    });

    res.on('end', common.mustCall(() => {
      assert.match(result, />Debugging Node\.js</);
      cb();
    }));
  })).on('error', common.mustNotCall);
}

check(helpUrl, common.mustCall());
