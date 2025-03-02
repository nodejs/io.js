'use strict';
const {
  ArrayPrototypePush,
  ArrayPrototypeShift,
  NumberMAX_SAFE_INTEGER,
  Symbol,
} = primordials;
const Readable = require('internal/streams/readable');

const kEmitMessage = Symbol('kEmitMessage');
class TestsStream extends Readable {
  #buffer;
  #canPush;

  constructor() {
    super({
      __proto__: null,
      objectMode: true,
      highWaterMark: NumberMAX_SAFE_INTEGER,
    });
    this.#buffer = [];
    this.#canPush = true;
  }

  _read() {
    this.#canPush = true;

    while (this.#buffer.length > 0) {
      const obj = ArrayPrototypeShift(this.#buffer);

      if (!this.#tryPush(obj)) {
        return;
      }
    }
  }

  fail(nesting, loc, testNumber, name, details, directive) {
    this[kEmitMessage]('test:fail', {
      __proto__: null,
      name,
      nesting,
      testNumber,
      details,
      ...loc,
      ...directive,
    });
  }

  ok(nesting, loc, testNumber, name, details, directive) {
    this[kEmitMessage]('test:pass', {
      __proto__: null,
      name,
      nesting,
      testNumber,
      details,
      ...loc,
      ...directive,
    });
  }

  complete(nesting, loc, testNumber, name, details, directive) {
    this[kEmitMessage]('test:complete', {
      __proto__: null,
      name,
      nesting,
      testNumber,
      details,
      ...loc,
      ...directive,
    });
  }

  plan(nesting, loc, count) {
    this[kEmitMessage]('test:plan', {
      __proto__: null,
      nesting,
      count,
      ...loc,
    });
  }

  getSkip(reason = undefined) {
    return { __proto__: null, skip: reason ?? true };
  }

  getTodo(reason = undefined) {
    return { __proto__: null, todo: reason ?? true };
  }

  enqueue(nesting, loc, name, type) {
    this[kEmitMessage]('test:enqueue', {
      __proto__: null,
      nesting,
      name,
      type,
      ...loc,
    });
  }

  dequeue(nesting, loc, name, type) {
    this[kEmitMessage]('test:dequeue', {
      __proto__: null,
      nesting,
      name,
      type,
      ...loc,
    });
  }

  start(nesting, loc, name) {
    this[kEmitMessage]('test:start', {
      __proto__: null,
      nesting,
      name,
      ...loc,
    });
  }

  diagnostic(nesting, loc, message, level = 'info') {
    this[kEmitMessage]('test:diagnostic', {
      __proto__: null,
      nesting,
      message,
      level,
      ...loc,
    });
  }

  coverage(nesting, loc, summary) {
    this[kEmitMessage]('test:coverage', {
      __proto__: null,
      nesting,
      summary,
      ...loc,
    });
  }

  summary(nesting, file, success, counts, duration_ms) {
    this[kEmitMessage]('test:summary', {
      __proto__: null,
      success,
      counts,
      duration_ms,
      file,
    });
  }

  end() {
    this.#tryPush(null);
  }

  [kEmitMessage](type, data) {
    this.emit(type, data);
    // Disabling as this going to the user-land
    // eslint-disable-next-line node-core/set-proto-to-null-in-object
    this.#tryPush({ type, data });
  }

  #tryPush(message) {
    if (this.#canPush) {
      this.#canPush = this.push(message);
    } else {
      ArrayPrototypePush(this.#buffer, message);
    }

    return this.#canPush;
  }
}

module.exports = { TestsStream, kEmitMessage };
