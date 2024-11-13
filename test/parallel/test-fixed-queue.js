// Flags: --expose-internals
'use strict';

require('../common');

const assert = require('assert');
const FixedQueue = require('internal/fixed_queue');

{
  const queue = new FixedQueue();
  assert.strictEqual(queue.head, queue.tail);
  assert(queue.isEmpty());
  queue.push('a');
  assert(!queue.isEmpty());
  assert.strictEqual(queue.shift(), 'a');
  assert.strictEqual(queue.shift(), null);
}

{
  const queue = new FixedQueue();
  for (let i = 0; i < 2047; i++)
    queue.push('a');
  assert(queue.head.isFull());
  queue.push('a');
  assert(!queue.head.isFull());

  assert.notStrictEqual(queue.head, queue.tail);
  for (let i = 0; i < 2047; i++)
    assert.strictEqual(queue.shift(), 'a');
  assert.strictEqual(queue.head, queue.tail);
  assert(!queue.isEmpty());
  assert.strictEqual(queue.shift(), 'a');
  assert(queue.isEmpty());
}

{
  // FixedQueue must not be holey array
  // Refs: https://github.com/nodejs/node/issues/54472
  const queue = new FixedQueue();
  for (let i = 0; i < queue.head.list.length; i++) {
    assert(i in queue.head.list);
  }
  for (let i = 0; i < queue.tail.list.length; i++) {
    assert(i in queue.tail.list);
  }
}
