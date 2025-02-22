// Tab completion sometimes uses a separate REPL instance under the hood.
// That REPL instance has its own domain. Make sure domain errors trickle back
// up to the main REPL.
//
// Ref: https://github.com/nodejs/node/issues/21586

'use strict';

const { Stream } = require('stream');
function noop() {}

// A stream to push an array into a REPL
function ArrayStream() {
  this.run = function(data) {
    data.forEach((line) => {
      this.emit('data', `${line}\n`);
    });
  };
}

Object.setPrototypeOf(ArrayStream.prototype, Stream.prototype);
Object.setPrototypeOf(ArrayStream, Stream);
ArrayStream.prototype.readable = true;
ArrayStream.prototype.writable = true;
ArrayStream.prototype.pause = noop;
ArrayStream.prototype.resume = noop;
ArrayStream.prototype.write = noop;

const repl = require('repl');

const putIn = new ArrayStream();
const testMe = repl.start('', putIn);

// Some errors are passed to the domain, but do not callback.
testMe._onEvalError = (err) => { throw err };

// Nesting of structures causes REPL to use a nested REPL for completion.
putIn.run([
  'var top = function() {',
  'r = function test (',
  ' one, two) {',
  'var inner = {',
  ' one:1',
  '};'
]);

// In Node.js 10.11.0, this next line will terminate the repl silently...
testMe.complete('inner.o', () => { throw new Error('fhqwhgads'); });