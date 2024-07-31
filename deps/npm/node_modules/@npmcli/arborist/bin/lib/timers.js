const { bin: options } = require('./options.js')
const log = require('./logging.js')

const timers = new Map()
const finished = new Map()

process.on('time', (level, name) => {
  if (level === 'start') {
    if (timers.has(name)) {
      throw new Error('conflicting timer! ' + name)
    }
    timers.set(name, process.hrtime.bigint())
  } else if (level === 'end') {
    if (!timers.has(name)) {
      throw new Error('timer not started! ' + name)
    }
    const elapsed = Number(process.hrtime.bigint() - timers.get(name))
    timers.delete(name)
    finished.set(name, elapsed)
    if (options.timing) {
      log.info('timeEnd', `${name} ${elapsed / 1e9}s`, log.meta({ force: options.timing === 'always' }))
    }
  }
})

process.on('exit', () => {
  for (const name of timers.keys()) {
    log.error('timeError', 'Dangling timer:', name)
    process.exitCode = 1
  }
})

module.exports = finished
