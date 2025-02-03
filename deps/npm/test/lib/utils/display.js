const t = require('tap')
const timers = require('node:timers/promises')
const tmock = require('../../fixtures/tmock')
const mockLogs = require('../../fixtures/mock-logs')
const mockGlobals = require('@npmcli/mock-globals')
const { inspect } = require('node:util')

const mockDisplay = async (t, { mocks, load } = {}) => {
  const procLog = require('proc-log')

  const logs = mockLogs()

  const Display = tmock(t, '{LIB}/utils/display', mocks)
  const display = new Display(logs.streams)
  const displayLoad = async (opts) => display.load({
    loglevel: 'silly',
    stderrColor: false,
    stdoutColot: false,
    heading: 'npm',
    ...opts,
  })

  if (load !== false) {
    await displayLoad(load)
  }

  t.teardown(() => display.off())
  return {
    ...procLog,
    display,
    displayLoad,
    ...logs.logs,
  }
}

t.test('can log cleanly', async (t) => {
  const { log, logs } = await mockDisplay(t)

  log.error('', 'test\x00message')
  t.match(logs.error, ['test^@message'])
})

t.test('can handle special eresolves', async (t) => {
  const explains = []
  const { log, logs } = await mockDisplay(t, {
    mocks: {
      '{LIB}/utils/explain-eresolve.js': {
        explain: (...args) => {
          explains.push(args)
          return 'EXPLAIN'
        },
      },
    },
  })

  log.warn('ERESOLVE', 'hello', { some: 'object' })
  t.strictSame(logs.warn, ['ERESOLVE hello', 'EXPLAIN'])
  t.match(explains, [[{ some: 'object' }, Function, 2]])
})

t.test('can buffer output when paused', async t => {
  const { displayLoad, outputs, output } = await mockDisplay(t, {
    load: false,
  })

  output.buffer('Message 1')
  output.standard('Message 2')

  t.strictSame(outputs, [])
  await displayLoad()
  t.strictSame(outputs, ['Message 1', 'Message 2'])
})

t.test('can do progress', async (t) => {
  const { log, logs, outputs, outputErrors, output, input } = await mockDisplay(t, {
    load: {
      progress: true,
    },
  })

  // wait for initial timer interval to load
  await timers.setTimeout(200)

  log.error('', 'before input')
  output.standard('before input')

  const end = input.start()
  log.error('', 'during input')
  output.standard('during input')
  end()

  // wait long enough for all spinner frames to render
  await timers.setTimeout(800)
  log.error('', 'after input')
  output.standard('after input')

  t.strictSame([...new Set(outputErrors)].sort(), ['-', '/', '\\', '|'])
  t.strictSame(logs, ['error before input', 'error during input', 'error after input'])
  t.strictSame(outputs, ['before input', 'during input', 'after input'])
})

t.test('handles log throwing', async (t) => {
  class ThrowInspect {
    #crashes = 0;

    [inspect.custom] () {
      throw new Error(`Crashed ${++this.#crashes}`)
    }
  }

  const errors = []
  mockGlobals(t, { 'console.error': (...msg) => errors.push(msg) })

  const { log, logs } = await mockDisplay(t)

  log.error('woah', new ThrowInspect())

  t.strictSame(logs.error, [])
  t.equal(errors.length, 1)
  t.match(errors[0], [
    'attempt to log crashed',
    new Error('Crashed 1'),
    new Error('Crashed 2'),
  ])
})

t.test('incorrect levels', async t => {
  const { outputs } = await mockDisplay(t)
  process.emit('output', 'not a real level')
  t.strictSame(outputs, [], 'output is ignored')
})

t.test('Display.clean', async (t) => {
  const { output, outputs, clearOutput } = await mockDisplay(t)

  class CustomObj {
    #inspected

    constructor (val) {
      this.#inspected = val
    }

    [inspect.custom] () {
      return this.#inspected
    }
  }

  const tests = [
    [477, '477'],
    [null, 'null'],
    [NaN, 'NaN'],
    [true, 'true'],
    [undefined, 'undefined'],
    ['🚀', '🚀'],
    // Cover the bounds of each range and a few characters from inside each range
    // \x00 through \x1f
    ['hello\x00world', 'hello^@world'],
    ['hello\x07world', 'hello^Gworld'],
    ['hello\x1bworld', 'hello^[world'],
    ['hello\x1eworld', 'hello^^world'],
    ['hello\x1fworld', 'hello^_world'],
    // \x7f is C0
    ['hello\x7fworld', 'hello^?world'],
    // \x80 through \x9f
    ['hello\x80world', 'hello^@world'],
    ['hello\x87world', 'hello^Gworld'],
    ['hello\x9eworld', 'hello^^world'],
    ['hello\x9fworld', 'hello^_world'],
    // Allowed C0
    ['hello\tworld', 'hello\tworld'],
    ['hello\nworld', 'hello\nworld'],
    ['hello\vworld', 'hello\vworld'],
    ['hello\rworld', 'hello\rworld'],
    // Allowed SGR
    ['hello\x1b[38;5;254mworld', 'hello\x1b[38;5;254mworld'],
    ['hello\x1b[mworld', 'hello\x1b[mworld'],
    // Unallowed CSI / OSC
    ['hello\x1b[2Aworld', 'hello^[[2Aworld'],
    ['hello\x9b[2Aworld', 'hello^[[2Aworld'],
    ['hello\x9decho goodbye\x9cworld', 'hello^]echo goodbye^\\world'],
    // This is done twice to ensure we define inspect.custom as writable
    [{ test: 'object' }, "{ test: 'object' }"],
    // Make sure custom util.inspect doesn't bypass our cleaning
    [new CustomObj(NaN), 'NaN'],
    [new CustomObj(null), 'null'],
    [new CustomObj(477), '477'],
    [new CustomObj({ custom: 'rend\x00ering' }), "{ custom: 'rend\\x00ering' }"],
    [new CustomObj('custom\x00rendering'), 'custom^@rendering'],
    [new CustomObj(undefined), 'undefined'],
    // UTF-16 form of 8-bit C1
    ['hello\xc2\x9bworld', 'hello\xc2^[world'],
  ]

  for (const [dirty, clean] of tests) {
    output.standard(dirty)
    t.equal(outputs[0], clean)
    clearOutput()
  }
})
