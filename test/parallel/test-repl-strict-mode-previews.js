// Previews in strict mode should indicate ReferenceErrors.

'use strict';

const common = require('../common');

common.skipIfInspectorDisabled();

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

if (process.argv[2] === 'child') {
  const stream = require('stream');
  const repl = require('repl');
  class ActionStream extends stream.Stream {
    readable = true;
    run(data) {
      this.emit('data', `${data}`);
      this.emit('keypress', '', { ctrl: true, name: 'd' });
    }
    resume() {}
    pause() {}
  }

  repl.start({
    input: new ActionStream(),
    output: new stream.Writable({
      write(chunk, _, next) {
        console.log(chunk.toString());
        next();
      }
    }),
    useColors: false,
    terminal: true
  }).inputStream.run('xyz');
} else {
  const assert = require('assert');
  const { spawnSync } = require('child_process');

  const result = spawnSync(
    process.execPath,
    ['--use-strict', `${__filename}`, 'child']
  );

  assert.match(
    result.stdout.toString(),
    /\/\/ ReferenceError: xyz is not defined/
  );
}
