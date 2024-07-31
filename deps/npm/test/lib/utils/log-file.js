const t = require('tap')
const _fs = require('node:fs')
const fs = _fs.promises
const path = require('node:path')
const os = require('node:os')
const fsMiniPass = require('fs-minipass')
const tmock = require('../../fixtures/tmock')
const LogFile = require('../../../lib/utils/log-file.js')
const { cleanCwd, cleanDate } = require('../../fixtures/clean-snapshot')

t.cleanSnapshot = (s) => cleanDate(cleanCwd(s))

const getId = (d = new Date()) => d.toISOString().replace(/[.:]/g, '_')
const last = arr => arr[arr.length - 1]
const range = (n) => Array.from(Array(n).keys())
const makeOldLogs = (count, oldStyle) => {
  const d = new Date()
  d.setHours(-1)
  d.setSeconds(0)
  return range(oldStyle ? count : (count / 2)).reduce((acc, i) => {
    const cloneDate = new Date(d.getTime())
    cloneDate.setSeconds(i)
    const dateId = getId(cloneDate)
    if (oldStyle) {
      acc[`${dateId}-debug.log`] = 'hello'
    } else {
      acc[`${dateId}-debug-0.log`] = 'hello'
      acc[`${dateId}-debug-1.log`] = 'hello'
    }
    return acc
  }, {})
}

const cleanErr = (message) => {
  const err = new Error(message)
  const stack = err.stack.split('\n')
  err.stack = stack[0] + '\n' + range(10)
    .map((__, i) => stack[1].replace(/^(\s+at\s).*/, `$1stack trace line ${i}`))
    .join('\n')
  return err
}

const loadLogFile = async (t, { buffer = [], mocks, testdir = {}, ...options } = {}) => {
  const root = t.testdir(testdir)

  const MockLogFile = tmock(t, '{LIB}/utils/log-file.js', mocks)
  const logFile = new MockLogFile(Object.keys(options).length ? options : undefined)

  // Create a fake public method since there is not one on logFile anymore
  logFile.log = (...b) => process.emit('log', ...b)
  buffer.forEach((b) => logFile.log(...b))

  const id = getId()
  await logFile.load({ path: path.join(root, `${id}-`), ...options })

  t.teardown(() => logFile.off())
  return {
    root,
    logFile,
    LogFile,
    readLogs: async () => {
      const logDir = await fs.readdir(root, { withFileTypes: true })
      const logFiles = logDir
        .filter(f => f.isFile())
        .map((f) => path.join(root, f.name))
        .filter((f) => _fs.existsSync(f))
      return Promise.all(logFiles.map(async (f) => {
        const content = await fs.readFile(f, 'utf8')
        const rawLogs = content.split(os.EOL)
        return {
          filename: f,
          content,
          rawLogs,
          logs: rawLogs.filter(Boolean),
        }
      }))
    },
  }
}

t.test('init', async t => {
  const maxLogsPerFile = 10
  const { root, logFile, readLogs } = await loadLogFile(t, {
    maxLogsPerFile,
    maxFilesPerProcess: 20,
    buffer: [['error', 'buffered']],
  })

  for (const i of range(50)) {
    logFile.log('error', `log ${i}`)
  }

  // Ignored
  logFile.log('pause')
  logFile.log('resume')
  logFile.log('pause')

  for (const i of range(50)) {
    logFile.log('verb', `log ${i}`)
  }

  logFile.off()
  logFile.log('error', 'ignored')

  const logs = await readLogs()
  t.equal(logs.length, 11, 'total log files')
  t.ok(logs.slice(0, 10).every(f => f.logs.length === maxLogsPerFile), 'max logs per file')
  t.ok(last(logs).logs.length, 1, 'last file has remaining logs')
  t.ok(logs.every(f => last(f.rawLogs) === ''), 'all logs end with newline')
  t.strictSame(
    logFile.files,
    logs.map((l) => path.resolve(root, l.filename))
  )
})

t.test('max files per process', async t => {
  const maxLogsPerFile = 10
  const maxFilesPerProcess = 5
  const { logFile, readLogs } = await loadLogFile(t, {
    maxLogsPerFile,
    maxFilesPerProcess,
  })

  for (const i of range(maxLogsPerFile * maxFilesPerProcess)) {
    logFile.log('error', `log ${i}`)
  }

  for (const i of range(5)) {
    logFile.log('verbose', `ignored after maxlogs hit ${i}`)
  }

  const logs = await readLogs()
  t.equal(logs.length, maxFilesPerProcess, 'total log files')
  t.match(last(last(logs).logs), /49 error log \d+/)
})

t.test('stream error', async t => {
  let times = 0
  const { logFile, readLogs } = await loadLogFile(t, {
    maxLogsPerFile: 1,
    maxFilesPerProcess: 99,
    mocks: {
      'fs-minipass': {
        WriteStreamSync: class {
          constructor (...args) {
            if (times >= 5) {
              throw new Error('bad stream')
            }
            times++
            return new fsMiniPass.WriteStreamSync(...args)
          }
        },
      },
    },
  })

  for (const i of range(10)) {
    logFile.log('verbose', `log ${i}`)
  }

  const logs = await readLogs()
  t.equal(logs.length, 5, 'total log files')
})

t.test('initial stream error', async t => {
  const { logFile, readLogs } = await loadLogFile(t, {
    mocks: {
      'fs-minipass': {
        WriteStreamSync: class {
          constructor () {
            throw new Error('no stream')
          }
        },
      },
    },
  })

  for (const i of range(10)) {
    logFile.log('verbose', `log ${i}`)
  }

  const logs = await readLogs()
  t.equal(logs.length, 0, 'total log files')
})

t.test('turns off', async t => {
  const { logFile, readLogs } = await loadLogFile(t)

  logFile.log('error', 'test')
  logFile.off()
  logFile.log('error', 'test2')
  logFile.load()

  const logs = await readLogs()
  t.match(last(last(logs).logs), /^\d+ error test$/)
})

t.test('cleans logs', async t => {
  const logsMax = 5
  const { readLogs } = await loadLogFile(t, {
    logsMax,
    testdir: makeOldLogs(10),
  })

  const logs = await readLogs()
  t.equal(logs.length, logsMax + 1)
})

t.test('cleans logs even when find folder inside logs folder', async t => {
  const logsMax = 5
  const { readLogs } = await loadLogFile(t, {
    logsMax,
    testdir: {
      ...makeOldLogs(10),
      ignore_folder: {
        'ignored-file.txt': 'hello',
      },
    },
  })

  const logs = await readLogs()
  t.equal(logs.length, logsMax + 1)
})

t.test('doesnt clean current log by default', async t => {
  const logsMax = 1
  const { readLogs, logFile } = await loadLogFile(t, {
    logsMax,
    testdir: makeOldLogs(10),
  })

  logFile.log('error', 'test')

  const logs = await readLogs()
  t.match(last(logs).content, /\d+ error test/)
})

t.test('negative logs max', async t => {
  const logsMax = -10
  const { readLogs, logFile } = await loadLogFile(t, {
    logsMax,
    testdir: makeOldLogs(10),
  })

  logFile.log('error', 'test')

  const logs = await readLogs()
  t.equal(logs.length, 0)
})

t.test('doesnt need to clean', async t => {
  const logsMax = 20
  const oldLogs = 10
  const { readLogs } = await loadLogFile(t, {
    logsMax,
    testdir: makeOldLogs(oldLogs),
  })

  const logs = await readLogs()
  t.equal(logs.length, oldLogs + 1)
})

t.test('cleans old style logs too', async t => {
  const logsMax = 5
  const oldLogs = 10
  const { readLogs } = await loadLogFile(t, {
    logsMax,
    testdir: makeOldLogs(oldLogs, true),
  })

  const logs = await readLogs()
  t.equal(logs.length, logsMax + 1)
})

t.test('rimraf error', async t => {
  const logsMax = 5
  const oldLogs = 10
  let count = 0
  const { readLogs } = await loadLogFile(t, {
    logsMax,
    testdir: makeOldLogs(oldLogs),
    mocks: {
      'node:fs/promises': {
        readdir: fs.readdir,
        rm: async (...args) => {
          if (count >= 3) {
            throw new Error('bad rimraf')
          }
          count++
          return fs.rm(...args)
        },
      },
    },
  })

  const logs = await readLogs()
  t.equal(logs.length, oldLogs - 3 + 1)
  t.match(last(logs).content, /error removing log file .* bad rimraf/)
})

t.test('delete log file while open', async t => {
  const { logFile, root, readLogs } = await loadLogFile(t)

  logFile.log('error', '', 'log 1')
  const [log] = await readLogs(true)
  t.match(log.content, /\d+ error log 1/)

  await fs.unlink(path.resolve(root, log.filename))

  logFile.log('error', '', 'log 2')
  const logs = await readLogs()

  // XXX: do some retry logic after error?
  t.strictSame(logs, [], 'logs arent written after error')
})

t.test('snapshot', async t => {
  const { logFile, readLogs } = await loadLogFile(t, { logsMax: 10 })

  logFile.log('error', '', 'no prefix')
  logFile.log('error', 'prefix', 'with prefix')
  logFile.log('error', 'prefix', 1, 2, 3)

  const nestedObj = { obj: { with: { many: { props: 1 } } } }
  logFile.log('verbose', '', nestedObj)
  logFile.log('verbose', '', JSON.stringify(nestedObj))
  logFile.log('verbose', '', JSON.stringify(nestedObj, null, 2))

  const arr = ['test', 'with', 'an', 'array']
  logFile.log('verbose', '', arr)
  logFile.log('verbose', '', JSON.stringify(arr))
  logFile.log('verbose', '', JSON.stringify(arr, null, 2))

  const nestedArr = ['test', ['with', ['an', ['array']]]]
  logFile.log('verbose', '', nestedArr)
  logFile.log('verbose', '', JSON.stringify(nestedArr))
  logFile.log('verbose', '', JSON.stringify(nestedArr, null, 2))

  // XXX: multiple errors are hard to parse visually
  // the second error should start on a newline
  logFile.log(...[
    'error',
    'pre',
    'has',
    'many',
    'errors',
    cleanErr('message'),
    cleanErr('message2'),
  ])

  const err = new Error('message')
  delete err.stack
  logFile.log(...[
    'error',
    'nostack',
    err,
  ])

  const logs = await readLogs()
  t.matchSnapshot(logs.map(l => l.content).join('\n'))
})
