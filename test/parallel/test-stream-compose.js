'use strict';

const common = require('../common');
const {
  Duplex,
  Readable,
  Transform,
  Writable,
  finished,
  compose,
  PassThrough
} = require('stream');
const assert = require('assert');

{
  let res = '';
  compose(
    new Transform({
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, chunk + chunk);
      })
    }),
    new Transform({
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, chunk.toString().toUpperCase());
      })
    })
  )
  .end('asd')
  .on('data', common.mustCall((buf) => {
    res += buf;
  }))
  .on('end', common.mustCall(() => {
    assert.strictEqual(res, 'ASDASD');
  }));
}

{
  let res = '';
  compose(
    async function*(source) {
      for await (const chunk of source) {
        yield chunk + chunk;
      }
    },
    async function*(source) {
      for await (const chunk of source) {
        yield chunk.toString().toUpperCase();
      }
    }
  )
  .end('asd')
  .on('data', common.mustCall((buf) => {
    res += buf;
  }))
  .on('end', common.mustCall(() => {
    assert.strictEqual(res, 'ASDASD');
  }));
}

{
  let res = '';
  compose(
    async function*(source) {
      for await (const chunk of source) {
        yield chunk + chunk;
      }
    }
  )
  .end('asd')
  .on('data', common.mustCall((buf) => {
    res += buf;
  }))
  .on('end', common.mustCall(() => {
    assert.strictEqual(res, 'asdasd');
  }));
}

{
  let res = '';
  compose(
    Readable.from(['asd']),
    new Transform({
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, chunk.toString().toUpperCase());
      })
    })
  )
  .on('data', common.mustCall((buf) => {
    res += buf;
  }))
  .on('end', common.mustCall(() => {
    assert.strictEqual(res, 'ASD');
  }));
}

{
  let res = '';
  compose(
    async function* () {
      yield 'asd';
    }(),
    new Transform({
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, chunk.toString().toUpperCase());
      })
    })
  )
  .on('data', common.mustCall((buf) => {
    res += buf;
  }))
  .on('end', common.mustCall(() => {
    assert.strictEqual(res, 'ASD');
  }));
}

{
  let res = '';
  compose(
    new Transform({
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, chunk.toString().toUpperCase());
      })
    }),
    async function*(source) {
      for await (const chunk of source) {
        yield chunk;
      }
    },
    new Writable({
      write: common.mustCall((chunk, encoding, callback) => {
        res += chunk;
        callback(null);
      })
    })
  )
  .end('asd')
  .on('finish', common.mustCall(() => {
    assert.strictEqual(res, 'ASD');
  }));
}

{
  let res = '';
  compose(
    new Transform({
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, chunk.toString().toUpperCase());
      })
    }),
    async function*(source) {
      for await (const chunk of source) {
        yield chunk;
      }
    },
    async function(source) {
      for await (const chunk of source) {
        res += chunk;
      }
    }
  )
  .end('asd')
  .on('finish', common.mustCall(() => {
    assert.strictEqual(res, 'ASD');
  }));
}

{
  let res;
  compose(
    new Transform({
      objectMode: true,
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, { chunk });
      })
    }),
    async function*(source) {
      for await (const chunk of source) {
        yield chunk;
      }
    },
    new Transform({
      objectMode: true,
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, { chunk });
      })
    })
  )
  .end(true)
  .on('data', common.mustCall((buf) => {
    res = buf;
  }))
  .on('end', common.mustCall(() => {
    assert.strictEqual(res.chunk.chunk, true);
  }));
}

{
  const _err = new Error('asd');
  compose(
    new Transform({
      objectMode: true,
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(_err);
      })
    }),
    async function*(source) {
      for await (const chunk of source) {
        yield chunk;
      }
    },
    new Transform({
      objectMode: true,
      transform: common.mustNotCall((chunk, encoding, callback) => {
        callback(null, { chunk });
      })
    })
  )
  .end(true)
  .on('data', common.mustNotCall())
  .on('end', common.mustNotCall())
  .on('error', (err) => {
    assert.strictEqual(err, _err);
  });
}

{
  const _err = new Error('asd');
  compose(
    new Transform({
      objectMode: true,
      transform: common.mustCall((chunk, encoding, callback) => {
        callback(null, chunk);
      })
    }),
    async function*(source) { // eslint-disable-line require-yield
      let tmp = '';
      for await (const chunk of source) {
        tmp += chunk;
        throw _err;
      }
      return tmp;
    },
    new Transform({
      objectMode: true,
      transform: common.mustNotCall((chunk, encoding, callback) => {
        callback(null, { chunk });
      })
    })
  )
  .end(true)
  .on('data', common.mustNotCall())
  .on('end', common.mustNotCall())
  .on('error', (err) => {
    assert.strictEqual(err, _err);
  });
}

{
  let buf = '';

  // Convert into readable Duplex.
  const s1 = compose(async function* () {
    yield 'Hello';
    yield 'World';
  }(), async function* (source) {
    for await (const chunk of source) {
      yield String(chunk).toUpperCase();
    }
  }, async function(source) {
    for await (const chunk of source) {
      buf += chunk;
    }
  });

  assert.strictEqual(s1.writable, false);
  assert.strictEqual(s1.readable, false);

  finished(s1.resume(), common.mustCall((err) => {
    assert(!err);
    assert.strictEqual(buf, 'HELLOWORLD');
  }));
}

{
  let buf = '';
  // Convert into transform duplex.
  const s2 = compose(async function* (source) {
    for await (const chunk of source) {
      yield String(chunk).toUpperCase();
    }
  });
  s2.end('helloworld');
  s2.resume();
  s2.on('data', (chunk) => {
    buf += chunk;
  });

  finished(s2.resume(), common.mustCall((err) => {
    assert(!err);
    assert.strictEqual(buf, 'HELLOWORLD');
  }));
}

{
  let buf = '';

  // Convert into readable Duplex.
  const s1 = compose(async function* () {
    yield 'Hello';
    yield 'World';
  }());

  // Convert into transform duplex.
  const s2 = compose(async function* (source) {
    for await (const chunk of source) {
      yield String(chunk).toUpperCase();
    }
  });

  // Convert into writable duplex.
  const s3 = compose(async function(source) {
    for await (const chunk of source) {
      buf += chunk;
    }
  });

  const s4 = compose(s1, s2, s3);

  finished(s4, common.mustCall((err) => {
    assert(!err);
    assert.strictEqual(buf, 'HELLOWORLD');
  }));
}

{
  let buf = '';

  // Convert into readable Duplex.
  const s1 = compose(async function* () {
    yield 'Hello';
    yield 'World';
  }(), async function* (source) {
    for await (const chunk of source) {
      yield String(chunk).toUpperCase();
    }
  }, async function(source) {
    for await (const chunk of source) {
      buf += chunk;
    }
  });

  finished(s1, common.mustCall((err) => {
    assert(!err);
    assert.strictEqual(buf, 'HELLOWORLD');
  }));
}

{
  assert.throws(
    () => compose(),
    { code: 'ERR_MISSING_ARGS' }
  );
}

{
  assert.throws(
    () => compose(new Writable(), new PassThrough()),
    { code: 'ERR_INVALID_ARG_VALUE' }
  );
}

{
  assert.throws(
    () => compose(new PassThrough(), new Readable({ read() {} }), new PassThrough()),
    { code: 'ERR_INVALID_ARG_VALUE' }
  );
}

{
  let buf = '';

  // Convert into readable Duplex.
  const s1 = compose(async function* () {
    yield 'Hello';
    yield 'World';
  }(), async function* (source) {
    for await (const chunk of source) {
      yield String(chunk).toUpperCase();
    }
  }, async function(source) {
    for await (const chunk of source) {
      buf += chunk;
    }
    return buf;
  });

  finished(s1, common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_INVALID_RETURN_VALUE');
  }));
}

{
  let buf = '';

  // Convert into readable Duplex.
  const s1 = compose('HelloWorld', async function* (source) {
    for await (const chunk of source) {
      yield String(chunk).toUpperCase();
    }
  }, async function(source) {
    for await (const chunk of source) {
      buf += chunk;
    }
  });

  finished(s1, common.mustCall((err) => {
    assert(!err);
    assert.strictEqual(buf, 'HELLOWORLD');
  }));
}

{
  // In the new stream than should use the writeable of the first stream and readable of the last stream
  // #46829
  (async () => {
    const newStream = compose(
      new PassThrough({
        // reading FROM you in object mode or not
        readableObjectMode: false,

        // writing TO you in object mode or not
        writableObjectMode: false,
      }),
      new Transform({
        // reading FROM you in object mode or not
        readableObjectMode: true,

        // writing TO you in object mode or not
        writableObjectMode: false,
        transform: (chunk, encoding, callback) => {
          callback(null, {
            value: chunk.toString()
          });
        }
      })
    );

    assert.strictEqual(newStream.writableObjectMode, false);
    assert.strictEqual(newStream.readableObjectMode, true);

    newStream.write('Steve Rogers');
    newStream.write('On your left');

    newStream.end();

    assert.deepStrictEqual(await newStream.toArray(), [{ value: 'Steve Rogers' }, { value: 'On your left' }]);
  })().then(common.mustCall());
}

{
  // In the new stream than should use the writeable of the first stream and readable of the last stream
  // #46829
  (async () => {
    const newStream = compose(
      new PassThrough({
        // reading FROM you in object mode or not
        readableObjectMode: true,

        // writing TO you in object mode or not
        writableObjectMode: true,
      }),
      new Transform({
        // reading FROM you in object mode or not
        readableObjectMode: false,

        // writing TO you in object mode or not
        writableObjectMode: true,
        transform: (chunk, encoding, callback) => {
          callback(null, chunk.value);
        }
      })
    );

    assert.strictEqual(newStream.writableObjectMode, true);
    assert.strictEqual(newStream.readableObjectMode, false);

    newStream.write({ value: 'Steve Rogers' });
    newStream.write({ value: 'On your left' });

    newStream.end();

    assert.deepStrictEqual(await newStream.toArray(), [Buffer.from('Steve RogersOn your left')]);
  })().then(common.mustCall());
}

{
  class DuplexProcess extends Duplex {
    constructor(options) {
      super({ ...options, objectMode: true });
      this.stuff = [];
    }

    _write(message, _, callback) {
      this.stuff.push(message);
      callback();
    }

    _destroy(err, cb) {
      cb(err);
    }

    _read() {
      if (this.stuff.length) {
        this.push(this.stuff.shift());
      } else if (this.writableEnded) {
        this.push(null);
      } else {
        this._read();
      }
    }
  }

  const pass = new PassThrough({ objectMode: true });
  const duplex = new DuplexProcess();

  const composed = compose(
    pass,
    duplex
  ).on('error', () => {});

  composed.write('hello');
  composed.write('world');
  composed.end();

  composed.destroy(new Error('an unexpected error'));
  assert.strictEqual(duplex.destroyed, true);

}
