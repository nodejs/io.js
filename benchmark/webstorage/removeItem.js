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
  type: ['localStorage-removeItem',
         'localStorage-delete',
         'sessionStorage-removeItem',
         'sessionStorage-delete',
  ],
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
    case 'localStorage-removeItem':
      fillStorage(localStorage, n);
      bench.start();
      for (let i = 0; i < n; i++) {
        localStorage.removeItem(i);
      }
      bench.end(n);
      break;
    case 'localStorage-delete':
      fillStorage(localStorage, n);
      bench.start();
      for (let i = 0; i < n; i++) {
        delete localStorage[i];
      }
      bench.end(n);
      break;
    case 'sessionStorage-removeItem':
      fillStorage(sessionStorage, n);
      bench.start();
      for (let i = 0; i < n; i++) {
        sessionStorage.removeItem(i);
      }
      bench.end(n);
      break;

    case 'sessionStorage-delete':
      fillStorage(localStorage, n);
      bench.start();
      for (let i = 0; i < n; i++) {
        delete sessionStorage[i];
      }
      bench.end(n);
      break;
    default:
      new Error('Invalid type');
  }
}
