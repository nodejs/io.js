'use strict';

const fs = require('fs');
const path = require('path');

let cache;
module.exports = {
  get rules() {
    const RULES_DIR = module.exports.RULES_DIR;
    if (!RULES_DIR)
      return {};

    if (!cache) {
      cache = {};
      const files = fs.readdirSync(RULES_DIR)
        .filter((filename) => filename.endsWith('.js'));
      for (const file of files) {
        const name = file.slice(0, -3);
        cache[name] = require(path.resolve(RULES_DIR, file));
      }
    }
    return cache;
  },
};
