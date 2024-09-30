'use strict';

const common = require('../common');
const { spawnSync } = require('child_process');
const assert = require('assert');

common.skipIfWorker();

{
  const example = () => {
    const assert = require('node:assert');
    const dgram = require('dgram');

    const tests = [
      // [port, ip]
      [],
      [9999],
      [9999, '127.0.0.1'],
      [9999, 'localhost'],
    ];

    tests.forEach((test, i) => {
      const socket = dgram.createSocket('udp4');
      socket.bind(test[0], test[1]).on('error', (err) => {
        assert.ok(err.code === 'ERR_ACCESS_DENIED', err.message);
        assert.ok(err.permission === 'NetUDP', err.message);
        assert.ok('resource' in err, err.message);
        socket.close();
      });
    });
  };
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '-e',
      `(${example.toString()})()`,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const example = () => {
    const assert = require('assert');
    const dgram = require('dgram');
    {
      const socket = dgram.createSocket('udp4');
      socket.bind().on('listening', (err) => {
        assert.ok(!err);
        socket.close();
      });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(9999)
        .on('listening', () => {
          socket.close();
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(9999, 'localhost')
        .on('listening', () => {
          socket.close();
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.connect(9999, '127.0.0.1', (err) => {
        if (err) {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
        }
        socket.close();
      });
    }
  };
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=*',
      '-e',
      `(${example.toString()})()`,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const example = () => {
    const assert = require('assert');
    const dgram = require('dgram');
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(5555, '127.0.0.1')
        .on('listening', () => {
          socket.close();
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(6666, 'localhost')
        .on('error', (err) => {
          assert.ok(err.code === 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(7777, '127.0.0.1')
        .on('listening', () => {
          socket.connect(8888, '127.0.0.1', (err) => {
            if (err) {
              assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
            }
            socket.close();
          });
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(9999, '127.0.0.1')
        .on('listening', () => {
          socket.connect(10000, 'localhost', (err) => {
            assert.ok(err.code === 'ERR_ACCESS_DENIED', err.message);
            socket.close();
          });
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
  };
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=127.0.0.1:*',
      '-e',
      `(${example.toString()})()`,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const example = () => {
    const assert = require('assert');
    const dgram = require('dgram');
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(9999, '127.0.0.2')
        .on('listening', () => {
          socket.close();
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(8888, '127.0.0.2')
        .on('error', (err) => {
          assert.ok(err.code === 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(9999, '127.0.0.1')
        .on('listening', () => {
          socket.connect(9999, '127.0.0.1', (err) => {
            if (err) {
              assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
            }
            socket.close();
          });
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(9999, 'localhost')
        .on('listening', () => {
          socket.connect(10000, 'localhost', (err) => {
            assert.ok(err.code === 'ERR_ACCESS_DENIED', err.message);
            socket.close();
          });
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
  };
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=*:9999',
      '-e',
      `(${example.toString()})()`,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const example = () => {
    const assert = require('assert');
    const dgram = require('dgram');
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(12345, '127.0.0.1')
        .on('listening', () => {
          socket.close();
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(7777, 'localhost')
        .on('listening', () => {
          socket.close();
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(9999, 'localhost')
        .on('error', (err) => {
          assert.ok(err.code === 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(8888, '127.0.0.1');
      socket.connect(9999, '127.0.0.2', (err) => {
        assert.ok(err.code === 'ERR_ACCESS_DENIED', err.message);
        socket.close();
      });
    }
  };
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=127.0.0.1:*',
      '--allow-net-udp=localhost:7777,localhost:8888',
      '-e',
      `(${example.toString()})()`,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const example = () => {
    const assert = require('assert');
    const dgram = require('dgram');
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(9999, '127.255.0.2')
        .on('listening', () => {
          socket.close();
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(8888, '1:1:1:1:1:1:1:2')
        .on('listening', () => {
          socket.close();
        })
        .on('error', (err) => {
          assert.ok(err.code !== 'ERR_ACCESS_DENIED', err.message);
          socket.close();
        });
    }
    {
      const socket = dgram.createSocket('udp4');
      socket.bind(7777, '127.0.0.1', (err) => {
        assert.ok(err.code === 'ERR_ACCESS_DENIED', err.message);
        socket.close();
      });
    }
  };
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=127.255.0.1/16:9999',
      '--allow-net-udp=127.255.0.1/16:9999',
      '--allow-net-udp=[1:1:1:1:1:1:1:1]/8:8888',
      '-e',
      `(${example.toString()})()`,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}
