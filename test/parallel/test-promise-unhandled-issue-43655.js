'use strict';

require('../common');
const assert = require('assert');

function delay(time) {
  return new Promise((resolve) => {
    setTimeout(resolve, time);
  });
}

async function test() {
  for (let i = 0; i < 100000; i++) {
    await new Promise((resolve, reject) => {
      reject('value');
    })
      .then(() => { }, () => { });
  }

  const time0 = Date.now();
  await delay(0);

  const diff = Date.now() - time0;
  assert.ok(Date.now() - time0 < 500, `Expected less than 500ms, got ${diff}ms`);
}

test();
