'use strict';
const { before, beforeEach, after, afterEach, test, suite } = require('node:test');

function record(data) {
  globalThis.GLOBAL_ORDER.push(data);
  console.log(data);
}

before(function() {
  record(`before two: ${this.name}`);
});

beforeEach(function() {
  record(`beforeEach two: ${this.name}`);
});

after(function() {
  record(`after two: ${this.name}`);
});

afterEach(function() {
  record(`afterEach two: ${this.name}`);
});

suite('suite two', function() {
  record(this.name);

  before(function() {
    record(`before suite two: ${this.name}`);
  });

  test('suite two - test', { only: true }, function() {
    record(this.name);
  });
});
