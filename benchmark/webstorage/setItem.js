'use strict';
const { join } = require('node:path');
const common = require('../common.js');
const tmpdir = require('../../test/common/tmpdir');

let cnt = 0;

tmpdir.refresh();

function nextLocalStorage() {
  return join(tmpdir.path, `${++cnt}.localstorage`);
}

const options = {
  flags: ['--experimental-webstorage', `--localstorage-file=${nextLocalStorage()}`],
};

const bench = common.createBenchmark(main, {
  type: ['localStorage-setItem',
         'localStorage-setter',
         'sessionStorage-setItem',
         'sessionStorage-setter'],
  // Note: web storage has only 10mb quota
  n: [1e5],
}, options);

function main({ n, type }) {
  const localStorage = globalThis.localStorage;
  const sessionStorage = globalThis.sessionStorage;

  switch (type) {
    case 'localStorage-setItem':
      bench.start();
      for (let i = 0; i < n; i++) {
        localStorage.setItem(i, i);
      }
      bench.end(n);
      break;
    case 'localStorage-setter':
      bench.start();
      for (let i = 0; i < n; i++) {
        localStorage[i] = i;
      }
      bench.end(n);
      break;
    case 'sessionStorage-setItem':
      bench.start();
      for (let i = 0; i < n; i++) {
        sessionStorage.setItem(i, i);
      }
      bench.end(n);
      break;
    case 'sessionStorage-setter':
      bench.start();
      for (let i = 0; i < n; i++) {
        sessionStorage[i] = i;
      }
      bench.end(n);
      break;
    default:
      new Error('Invalid type');
  }
}
