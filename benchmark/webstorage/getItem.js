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
  type: ['localStorage-getItem',
         'localStorage-getter',
         'sessionStorage-getItem',
         'sessionStorage-getter'],
  // Note: web storage has only 10mb quota
  n: [1e5],
}, options);

function fillStorage(storage, n) {
  for (let i = 0; i < n; i++) {
    storage.setItem(i, i);
  }
}

function main({ n, type }) {
  const localStorage = globalThis.localStorage;
  const sessionStorage = globalThis.sessionStorage;

  switch (type) {
    case 'localStorage-getItem':
      fillStorage(localStorage, n);
      bench.start();
      for (let i = 0; i < n; i++) {
        localStorage.getItem(i);
      }
      bench.end(n);
      break;
    case 'localStorage-getter':
      fillStorage(localStorage, n);
      bench.start();
      for (let i = 0; i < n; i++) {
        // eslint-disable-next-line no-unused-expressions
        localStorage[i];
      }
      bench.end(n);
      break;
    case 'sessionStorage-getItem':
      fillStorage(sessionStorage, n);
      bench.start();
      for (let i = 0; i < n; i++) {
        sessionStorage.getItem(i);
      }
      bench.end(n);
      break;
    case 'sessionStorage-getter':
      fillStorage(sessionStorage, n);
      bench.start();
      for (let i = 0; i < n; i++) {
        // eslint-disable-next-line no-unused-expressions
        sessionStorage[i];
      }
      bench.end(n);
      break;
    default:
      new Error('Invalid type');
  }
}
