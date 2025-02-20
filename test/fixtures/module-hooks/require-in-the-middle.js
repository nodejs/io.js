'use strict';

const { registerHooks } = require('module');
const { getStats } = require('./get-stats');

// This is a basic mock of https://www.npmjs.com/package/require-in-the-middle
// to test that the module.registerHooks() can be used to replace the
// CJS loader monkey-patching approach.

class Hook {
  constructor(modules, callback) {
    function evaluate(context, nextEvaluate) {
      let evaluated = nextEvaluate(context);
      const mod = context.module;
      const filepath = mod.filename || '';
      const stats = getStats(filepath);
      if (stats.name && modules.includes(stats.name)) {
        callback(mod.exports, stats.name, stats.basedir);
      }
      return evaluated;
    }
    this.hook = registerHooks({ evaluate });
  }
  unhook() {
    this.hook.deregister();
  }
}

module.exports = {
  Hook
};
