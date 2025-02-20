'use strict';

const path = require('path');
const { Hook } = require('./require-in-the-middle');

const hook = new Hook(['foo', 'bar'], function (exports, name, basedir) {
  const version = require(path.join(basedir, 'package.json')).version;
  exports._version = version;
  return exports;
});

module.exports = {
  load(id) {
    return require(id);
  },
  hook,
};

