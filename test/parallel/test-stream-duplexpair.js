'use strict';

const common = require('../common');
const assert = require('assert');
const { Duplex, duplexPair } = require('stream');

{
  const pair = duplexPair();

  assert(pair[0] instanceof Duplex);
  assert(pair[1] instanceof Duplex);
  assert.notStrictEqual(pair[0], pair[1]);
}

{
  // Verify that the iterable for array assignment works
  const [ clientSide, serverSide ] = duplexPair();
  assert(clientSide instanceof Duplex);
  assert(serverSide instanceof Duplex);
  clientSide.on(
    'data',
    common.mustCall((d) => assert.strictEqual(`${d}`, 'foo'))
  );
  clientSide.on('end', common.mustNotCall());
  serverSide.write('foo');
}

{
  const [ clientSide, serverSide ] = duplexPair();
  assert(clientSide instanceof Duplex);
  assert(serverSide instanceof Duplex);
  serverSide.on(
    'data',
    common.mustCall((d) => assert.strictEqual(`${d}`, 'foo'))
  );
  serverSide.on('end', common.mustCall());
  clientSide.end('foo');
}

{
  const [ serverSide, clientSide ] = duplexPair();
  serverSide.cork();
  serverSide.write('abc');
  serverSide.write('12');
  serverSide.end('\n');
  serverSide.uncork();
  let characters = '';
  clientSide.on('readable', function() {
    for (let segment; (segment = this.read()) !== null;)
      characters += segment;
  });
  clientSide.on('end', common.mustCall(function() {
    assert.strictEqual(characters, 'abc12\n');
  }));
}

// Test the case where the the _write never calls [kCallback]
// because a zero-size push doesn't trigger a _read
{
  const [ serverSide, clientSide ] = duplexPair();
  serverSide.write('');
  serverSide.write('12');
  serverSide.write('');
  serverSide.write('');
  serverSide.end('\n');
  let characters = '';
  clientSide.on('readable', function() {
    for (let segment; (segment = this.read()) !== null;)
      characters += segment;
  });
  clientSide.on('end', common.mustCall(function() {
    assert.strictEqual(characters, '12\n');
  }));
}
