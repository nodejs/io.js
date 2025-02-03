const t = require('tap')
const { resolve, join } = require('node:path')
const fs = require('graceful-fs')
const { log, time } = require('proc-log')
const tmock = require('../../fixtures/tmock')

const mockTimers = (t) => {
  const logs = log.LEVELS.reduce((acc, l) => {
    acc[l] = []
    return acc
  }, {})
  const logHandler = (level, ...args) => {
    logs[level].push(args.join(' '))
  }
  process.on('log', logHandler)
  const Timers = tmock(t, '{LIB}/utils/timers')
  const timers = new Timers()
  t.teardown(() => {
    timers.off()
    process.off('log', logHandler)
  })
  return { timers, logs }
}

t.test('logs timing events', async (t) => {
  const { timers, logs } = mockTimers(t)
  time.start('foo')
  time.start('bar')
  time.end('bar')
  timers.off()
  time.end('foo')
  t.equal(logs.timing.length, 1)
  t.match(logs.timing[0], /^bar Completed in [0-9]+m?s/)
})

t.test('finish unstarted timer', async (t) => {
  const { logs } = mockTimers(t)
  time.end('foo')
  t.match(logs.silly, ["timing Tried to end timer that doesn't exist: foo"])
})

t.test('writes file', async (t) => {
  const { timers } = mockTimers(t)
  const dir = t.testdir()
  time.start('foo')
  time.end('foo')
  time.start('ohno')
  timers.load({ timing: true, path: resolve(dir, `TIMING_FILE-`) })
  timers.finish({ some: 'data' })
  const data = JSON.parse(fs.readFileSync(resolve(dir, 'TIMING_FILE-timing.json')))
  t.match(data, {
    metadata: { some: 'data' },
    timers: { foo: Number, npm: Number },
    unfinishedTimers: {
      ohno: [Number, Number],
    },
  })
})

t.test('fails to write file', async (t) => {
  const { logs, timers } = mockTimers(t)
  const dir = t.testdir()

  timers.load({ timing: true, path: join(dir, 'does', 'not', 'exist') })
  timers.finish()

  t.match(logs.warn, ['timing could not write timing file:'])
})

t.test('no dir and no file', async (t) => {
  const { logs, timers } = mockTimers(t)

  timers.load()
  timers.finish()

  t.strictSame(logs.warn, [])
  t.strictSame(logs.silly, [])
})
