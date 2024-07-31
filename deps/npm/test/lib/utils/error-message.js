const t = require('tap')
const { resolve } = require('node:path')
const fs = require('node:fs/promises')
const { load: _loadMockNpm } = require('../../fixtures/mock-npm.js')
const mockGlobals = require('@npmcli/mock-globals')
const tmock = require('../../fixtures/tmock')
const { cleanCwd, cleanDate } = require('../../fixtures/clean-snapshot.js')

t.formatSnapshot = (p) => {
  if (Array.isArray(p.files) && !p.files.length) {
    delete p.files
  }
  if (p?.json === undefined) {
    delete p.json
  }
  return p
}
t.cleanSnapshot = p => cleanDate(cleanCwd(p))

mockGlobals(t, {
  process: {
    getuid: () => 867,
    getgid: () => 5309,
    arch: 'x64',
    version: '123.456.789-node',
    platform: 'posix',
  },
})

const loadMockNpm = async (t, { errorMocks, ...opts } = {}) => {
  const mockError = tmock(t, '{LIB}/utils/error-message.js', errorMocks)
  const res = await _loadMockNpm(t, {
    ...opts,
    mocks: {
      ...opts.mocks,
      '{ROOT}/package.json': {
        version: '123.456.789-npm',
      },
    },
  })
  return {
    ...res,
    errorMessage: (er) => mockError.errorMessage(er, res.npm),
  }
}

t.test('just simple messages', async t => {
  const { errorMessage } = await loadMockNpm(t, {
    prefixDir: { 'package-lock.json': '{}' },
    command: 'audit',
    exec: true,
  })
  const codes = [
    'ENOAUDIT',
    'ENOLOCK',
    'ECONNREFUSED',
    'ENOGIT',
    'EPUBLISHCONFLICT',
    'EISGIT',
    'EEXIST',
    'ENEEDAUTH',
    'ECONNRESET',
    'ENOTFOUND',
    'ETIMEDOUT',
    'EAI_FAIL',
    'EBADENGINE',
    'ENOSPC',
    'EROFS',
    'ENOENT',
    'EMISSINGARG',
    'EUNKNOWNTYPE',
    'EINVALIDTYPE',
    'ETOOMANYARGS',
    'ETARGET',
    'E403',
    'ERR_SOCKET_TIMEOUT',
  ]
  for (const code of codes) {
    const path = '/some/path'
    const pkgid = 'some@package'
    const file = '/some/file'
    const stack = 'dummy stack trace'
    const er = Object.assign(new Error('foo'), {
      code,
      path,
      pkgid,
      file,
      stack,
    })
    t.matchSnapshot(errorMessage(er))
  }
})

t.test('replace message/stack sensistive info', async t => {
  const { errorMessage } = await loadMockNpm(t, { command: 'audit' })
  const er = Object.assign(new Error('Error at registry: https://user:pass@registry.npmjs.org/'), {
    code: 'ENOAUDIT',
    path: '/some/path',
    pkgid: 'some@package',
    file: '/some/file',
    stack: 'dummy stack trace at https://user:pass@registry.npmjs.org/',
  })
  t.matchSnapshot(errorMessage(er))
})

t.test('bad engine without config loaded', async t => {
  const { errorMessage } = await loadMockNpm(t, { load: false })
  const path = '/some/path'
  const pkgid = 'some@package'
  const file = '/some/file'
  const stack = 'dummy stack trace'
  const er = Object.assign(new Error('foo'), {
    code: 'EBADENGINE',
    path,
    pkgid,
    file,
    stack,
  })
  t.matchSnapshot(errorMessage(er))
})

t.test('enoent without a file', async t => {
  const { errorMessage } = await loadMockNpm(t)
  const path = '/some/path'
  const pkgid = 'some@package'
  const stack = 'dummy stack trace'
  const er = Object.assign(new Error('foo'), {
    code: 'ENOENT',
    path,
    pkgid,
    stack,
  })
  t.matchSnapshot(errorMessage(er))
})

t.test('enolock without a command', async t => {
  const { errorMessage } = await loadMockNpm(t, { command: null })
  const path = '/some/path'
  const pkgid = 'some@package'
  const file = '/some/file'
  const stack = 'dummy stack trace'
  const er = Object.assign(new Error('foo'), {
    code: 'ENOLOCK',
    path,
    pkgid,
    file,
    stack,
  })
  t.matchSnapshot(errorMessage(er))
})

t.test('default message', async t => {
  const { errorMessage } = await loadMockNpm(t)
  t.matchSnapshot(errorMessage(new Error('error object')))
  t.matchSnapshot(errorMessage('error string'))
  t.matchSnapshot(errorMessage(Object.assign(new Error('cmd err'), {
    cmd: 'some command',
    signal: 'SIGYOLO',
    args: ['a', 'r', 'g', 's'],
    stdout: 'stdout',
    stderr: 'stderr',
  })))
})

t.test('args are cleaned', async t => {
  const { errorMessage } = await loadMockNpm(t)
  t.matchSnapshot(errorMessage(Object.assign(new Error('cmd err'), {
    cmd: 'some command',
    signal: 'SIGYOLO',
    args: ['a', 'r', 'g', 's', 'https://evil:password@npmjs.org'],
    stdout: 'stdout',
    stderr: 'stderr',
  })))
})

t.test('eacces/eperm', async t => {
  const runTest = (windows, loaded, cachePath, cacheDest) => async t => {
    const { errorMessage, logs, cache } = await loadMockNpm(t, {
      windows,
      load: loaded,
      globals: windows ? { 'process.platform': 'win32' } : [],
    })

    const path = `${cachePath ? cache : '/not/cache/dir'}/path`
    const dest = `${cacheDest ? cache : '/not/cache/dir'}/dest`
    const er = Object.assign(new Error('whoopsie'), {
      code: 'EACCES',
      path,
      dest,
      stack: 'dummy stack trace',
    })

    t.matchSnapshot(errorMessage(er))
    t.matchSnapshot(logs.verbose)
  }

  for (const windows of [true, false]) {
    for (const loaded of [true, false]) {
      for (const cachePath of [true, false]) {
        for (const cacheDest of [true, false]) {
          const m = JSON.stringify({ windows, loaded, cachePath, cacheDest })
          t.test(m, runTest(windows, loaded, cachePath, cacheDest))
        }
      }
    }
  }
})

t.test('json parse', t => {
  mockGlobals(t, { 'process.argv': ['arg', 'v'] })

  t.test('merge conflict in package.json', async t => {
    const prefixDir = {
      'package.json': await fs.readFile(
        resolve(__dirname, '../../fixtures/merge-conflict.json'), 'utf-8'),
    }
    const { errorMessage, npm } = await loadMockNpm(t, { prefixDir })
    t.matchSnapshot(errorMessage(Object.assign(new Error('conflicted'), {
      code: 'EJSONPARSE',
      path: resolve(npm.prefix, 'package.json'),
    })))
    t.end()
  })

  t.test('just regular bad json in package.json', async t => {
    const prefixDir = {
      'package.json': 'not even slightly json',
    }
    const { errorMessage, npm } = await loadMockNpm(t, { prefixDir })
    t.matchSnapshot(errorMessage(Object.assign(new Error('not json'), {
      code: 'EJSONPARSE',
      path: resolve(npm.prefix, 'package.json'),
    })))
    t.end()
  })

  t.test('json somewhere else', async t => {
    const prefixDir = {
      'blerg.json': 'not even slightly json',
    }
    const { npm, errorMessage } = await loadMockNpm(t, { prefixDir })
    t.matchSnapshot(errorMessage(Object.assign(new Error('not json'), {
      code: 'EJSONPARSE',
      path: resolve(npm.prefix, 'blerg.json'),
    })))
    t.end()
  })

  t.end()
})

t.test('eotp/e401', async t => {
  const { errorMessage } = await loadMockNpm(t)

  t.test('401, no auth headers', t => {
    t.matchSnapshot(errorMessage(Object.assign(new Error('nope'), {
      code: 'E401',
    })))
    t.end()
  })

  t.test('401, no message', t => {
    t.matchSnapshot(errorMessage({
      code: 'E401',
    }))
    t.end()
  })

  t.test('one-time pass challenge code', t => {
    t.matchSnapshot(errorMessage(Object.assign(new Error('nope'), {
      code: 'EOTP',
    })))
    t.end()
  })

  t.test('one-time pass challenge message', t => {
    const message = 'one-time pass'
    t.matchSnapshot(errorMessage(Object.assign(new Error(message), {
      code: 'E401',
    })))
    t.end()
  })

  t.test('www-authenticate challenges', t => {
    const auths = [
      'Bearer realm=do, charset="UTF-8", challenge="yourself"',
      'Basic realm=by, charset="UTF-8", challenge="your friends"',
      'PickACardAnyCard realm=friday, charset="UTF-8"',
      'WashYourHands, charset="UTF-8"',
    ]
    t.plan(auths.length)
    for (const auth of auths) {
      t.test(auth, t => {
        const er = Object.assign(new Error('challenge!'), {
          headers: {
            'www-authenticate': [auth],
          },
          code: 'E401',
        })
        t.matchSnapshot(errorMessage(er))
        t.end()
      })
    }
  })
})

t.test('404', async t => {
  const { errorMessage } = await loadMockNpm(t)

  t.test('no package id', t => {
    const er = Object.assign(new Error('404 not found'), { code: 'E404' })
    t.matchSnapshot(errorMessage(er))
    t.end()
  })
  t.test('you should publish it', t => {
    const er = Object.assign(new Error('404 not found'), {
      pkgid: 'yolo',
      code: 'E404',
    })
    t.matchSnapshot(errorMessage(er))
    t.end()
  })
  t.test('name with warning', t => {
    const er = Object.assign(new Error('404 not found'), {
      pkgid: new Array(215).fill('x').join(''),
      code: 'E404',
    })
    t.matchSnapshot(errorMessage(er))
    t.end()
  })
  t.test('name with error', t => {
    const er = Object.assign(new Error('404 not found'), {
      pkgid: 'node_modules',
      code: 'E404',
    })
    t.matchSnapshot(errorMessage(er))
    t.end()
  })
  t.test('cleans sensitive info from package id', t => {
    const er = Object.assign(new Error('404 not found'), {
      pkgid: 'http://evil:password@npmjs.org/not-found',
      code: 'E404',
    })
    t.matchSnapshot(errorMessage(er))
    t.end()
  })
})

t.test('bad platform', async t => {
  const { errorMessage } = await loadMockNpm(t)

  t.test('string os/arch', t => {
    const er = Object.assign(new Error('a bad plat'), {
      pkgid: 'lodash@1.0.0',
      current: {
        os: 'posix',
        cpu: 'x64',
      },
      required: {
        os: '!yours',
        cpu: 'x420',
      },
      code: 'EBADPLATFORM',
    })
    t.matchSnapshot(errorMessage(er))
    t.end()
  })
  t.test('array os/arch', t => {
    const er = Object.assign(new Error('a bad plat'), {
      pkgid: 'lodash@1.0.0',
      current: {
        os: 'posix',
        cpu: 'x64',
      },
      required: {
        os: ['!yours', 'mine'],
        cpu: ['x867', 'x5309'],
      },
      code: 'EBADPLATFORM',
    })
    t.matchSnapshot(errorMessage(er))
    t.end()
  })
  t.test('omits keys with no required value', t => {
    const er = Object.assign(new Error('a bad plat'), {
      pkgid: 'lodash@1.0.0',
      current: {
        os: 'posix',
        cpu: 'x64',
        libc: 'musl',
      },
      required: {
        os: ['!yours', 'mine'],
        libc: [], // empty arrays should also lead to a key being removed
        cpu: undefined, // XXX npm-install-checks sets unused keys to undefined
      },
      code: 'EBADPLATFORM',
    })
    const msg = errorMessage(er)
    t.matchSnapshot(msg)
    t.notMatch(msg, /Valid cpu/, 'omits cpu from message')
    t.notMatch(msg, /Valid libc/, 'omits libc from message')
    t.end()
  })
})

t.test('explain ERESOLVE errors', async t => {
  const EXPLAIN_CALLED = []

  const { errorMessage } = await loadMockNpm(t, {
    errorMocks: {
      '{LIB}/utils/explain-eresolve.js': {
        report: (...args) => {
          EXPLAIN_CALLED.push(...args)
          return { explanation: 'explanation', file: 'report' }
        },
      },
    },
    config: {
      color: 'always',
    },
  })

  const er = Object.assign(new Error('could not resolve'), {
    code: 'ERESOLVE',
  })

  t.matchSnapshot(errorMessage(er))
  t.equal(EXPLAIN_CALLED.length, 3)
  t.match(EXPLAIN_CALLED, [er, Function, Function])
  t.not(EXPLAIN_CALLED[1].level, 0, 'color chalk level is not 0')
  t.equal(EXPLAIN_CALLED[2].level, 0, 'colorless chalk level is 0')
})
