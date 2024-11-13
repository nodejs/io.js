const META = Symbol('proc-log.meta')
module.exports = {
  META: META,
  output: {
    LEVELS: [
      'standard',
      'error',
      'buffer',
      'flush',
    ],
    KEYS: {
      standard: 'standard',
      error: 'error',
      buffer: 'buffer',
      flush: 'flush',
    },
    standard: function (...args) {
      return process.emit('output', 'standard', ...args)
    },
    error: function (...args) {
      return process.emit('output', 'error', ...args)
    },
    buffer: function (...args) {
      return process.emit('output', 'buffer', ...args)
    },
    flush: function (...args) {
      return process.emit('output', 'flush', ...args)
    },
  },
  log: {
    LEVELS: [
      'notice',
      'error',
      'warn',
      'info',
      'verbose',
      'http',
      'silly',
      'timing',
      'pause',
      'resume',
    ],
    KEYS: {
      notice: 'notice',
      error: 'error',
      warn: 'warn',
      info: 'info',
      verbose: 'verbose',
      http: 'http',
      silly: 'silly',
      timing: 'timing',
      pause: 'pause',
      resume: 'resume',
    },
    error: function (...args) {
      return process.emit('log', 'error', ...args)
    },
    notice: function (...args) {
      return process.emit('log', 'notice', ...args)
    },
    warn: function (...args) {
      return process.emit('log', 'warn', ...args)
    },
    info: function (...args) {
      return process.emit('log', 'info', ...args)
    },
    verbose: function (...args) {
      return process.emit('log', 'verbose', ...args)
    },
    http: function (...args) {
      return process.emit('log', 'http', ...args)
    },
    silly: function (...args) {
      return process.emit('log', 'silly', ...args)
    },
    timing: function (...args) {
      return process.emit('log', 'timing', ...args)
    },
    pause: function () {
      return process.emit('log', 'pause')
    },
    resume: function () {
      return process.emit('log', 'resume')
    },
  },
  time: {
    LEVELS: [
      'start',
      'end',
    ],
    KEYS: {
      start: 'start',
      end: 'end',
    },
    start: function (name, fn) {
      process.emit('time', 'start', name)
      function end () {
        return process.emit('time', 'end', name)
      }
      if (typeof fn === 'function') {
        const res = fn()
        if (res && res.finally) {
          return res.finally(end)
        }
        end()
        return res
      }
      return end
    },
    end: function (name) {
      return process.emit('time', 'end', name)
    },
  },
  input: {
    LEVELS: [
      'start',
      'end',
      'read',
    ],
    KEYS: {
      start: 'start',
      end: 'end',
      read: 'read',
    },
    start: function (fn) {
      process.emit('input', 'start')
      function end () {
        return process.emit('input', 'end')
      }
      if (typeof fn === 'function') {
        const res = fn()
        if (res && res.finally) {
          return res.finally(end)
        }
        end()
        return res
      }
      return end
    },
    end: function () {
      return process.emit('input', 'end')
    },
    read: function (...args) {
      let resolve, reject
      const promise = new Promise((_resolve, _reject) => {
        resolve = _resolve
        reject = _reject
      })
      process.emit('input', 'read', resolve, reject, ...args)
      return promise
    },
  },
}
