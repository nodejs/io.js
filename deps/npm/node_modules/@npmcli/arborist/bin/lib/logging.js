const { log } = require('proc-log')
const fs = require('node:fs')
const { dirname } = require('node:path')
const os = require('node:os')
const { inspect, format } = require('node:util')

const { bin: options } = require('./options.js')

// add a meta method to proc-log for passing optional
// metadata through to log handlers
const META = Symbol('meta')
const parseArgs = (...args) => {
  const { [META]: isMeta } = args[args.length - 1] || {}
  return isMeta
    ? [args[args.length - 1], ...args.slice(0, args.length - 1)]
    : [{}, ...args]
}
log.meta = (meta = {}) => ({ [META]: true, ...meta })

const levels = new Map([
  'silly',
  'verbose',
  'info',
  'http',
  'notice',
  'warn',
  'error',
  'silent',
].map((level, index) => [level, index]))

const addLogListener = (write, { eol = os.EOL, loglevel = 'silly', colors = false } = {}) => {
  const levelIndex = levels.get(loglevel)

  const magenta = m => colors ? `\x1B[35m${m}\x1B[39m` : m
  const dim = m => colors ? `\x1B[2m${m}\x1B[22m` : m
  const red = m => colors ? `\x1B[31m${m}\x1B[39m` : m

  const formatter = (level, ...args) => {
    const depth = level === 'error' && args[0] && args[0].code === 'ERESOLVE' ? Infinity : 10

    if (level === 'info' && args[0] === 'timeEnd') {
      args[1] = dim(args[1])
    } else if (level === 'error' && args[0] === 'timeError') {
      args[1] = red(args[1])
    }

    const messages = args.map(a => typeof a === 'string' ? a : inspect(a, { depth, colors }))
    const pref = `${process.pid} ${magenta(level)} `

    return pref + format(...messages).trim().split('\n').join(`${eol}${pref}`) + eol
  }

  process.on('log', (...args) => {
    const [meta, level, ...logArgs] = parseArgs(...args)

    if (levelIndex <= levels.get(level) || meta.force) {
      write(formatter(level, ...logArgs))
    }
  })
}

if (options.loglevel !== 'silent') {
  addLogListener((v) => process.stderr.write(v), {
    eol: '\n',
    colors: options.colors,
    loglevel: options.loglevel,
  })
}

if (options.logfile) {
  log.silly('logfile', options.logfile)
  fs.mkdirSync(dirname(options.logfile), { recursive: true })
  const fd = fs.openSync(options.logfile, 'a')
  addLogListener((str) => fs.writeSync(fd, str))
}

module.exports = log
