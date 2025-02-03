'use strict';
const { before, beforeEach, after, afterEach, test, suite } = require('node:test');

globalThis.GLOBAL_ORDER = [];

function record(data) {
  globalThis.GLOBAL_ORDER.push(data);
  console.log(data);
}

before(function() {
  record(`before one: ${this.name}`);
});

beforeEach(function() {
  record(`beforeEach one: ${this.name}`);
});

after(function() {
  record(`after one: ${this.name}`);
});

afterEach(function() {
  record(`afterEach one: ${this.name}`);
});

suite('suite one', function() {
  record(this.name);

  test('suite one - test', { only: true }, function() {
    record(this.name);
  });
});

test('test one', function() {
  record(this.name);
});
