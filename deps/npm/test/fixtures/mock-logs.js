const { log: { LEVELS } } = require('proc-log')
const { stripVTControlCharacters: stripAnsi } = require('node:util')

const logPrefix = new RegExp(`^npm (${LEVELS.join('|')})\\s`)
const isLog = (str) => logPrefix.test(stripAnsi(str))

// We only strip trailing newlines since some output will
// have significant tabs and spaces
const trimTrailingNewline = (str) => str.replace(/\n$/, '')

const joinAndTrimTrailingNewlines = (arr) =>
  trimTrailingNewline(arr.map(trimTrailingNewline).join('\n'))

const logsByTitle = (logs) => ({
  byTitle: {
    value: (title) => {
      return logs
        .filter((l) => stripAnsi(l.message).startsWith(`${title} `))
        .map((l) => l.message)
    },
  },
})

module.exports = () => {
  const outputs = []
  const outputErrors = []
  const fullOutput = []

  const levelLogs = []
  const logs = Object.defineProperties([], {
    ...logsByTitle(levelLogs),
    ...LEVELS.reduce((acc, level) => {
      acc[level] = {
        get () {
          const byLevel = levelLogs.filter((l) => l.level === level)
          return Object.defineProperties(byLevel.map((l) => l.message), logsByTitle(byLevel))
        },
      }
      return acc
    }, {}),
  })

  const streams = {
    stderr: {
      cursorTo: () => {},
      clearLine: () => {},
      write: (str) => {
        str = trimTrailingNewline(str)

        // Use the beginning of each line to determine if its a log
        // or an output error since we write both of those to stderr.
        // This couples logging format to this test but we only need
        // to do it in a single place so hopefully its easy to change
        // in the future if/when we refactor what logs look like.
        if (!isLog(str)) {
          outputErrors.push(str)
          fullOutput.push(str)
          return
        }

        // Split on spaces for the heading and level/label. We know that
        // none of those have spaces but could be colorized so there's no
        // other good way to get each of those including control chars
        const [rawHeading, rawLevel] = str.split(' ')
        const rawPrefix = `${rawHeading} ${rawLevel} `
        // If message is colorized we can just replaceAll with the string since
        // it will be unique due to control chars. Otherwise we create a regex
        // that will only match the beginning of each line.
        const prefix = stripAnsi(str) !== str ? rawPrefix : new RegExp(`^${rawPrefix}`, 'gm')

        // The level needs color stripped always because we use it to filter logs
        const level = stripAnsi(rawLevel)

        logs.push(str.replaceAll(prefix, `${level} `))
        fullOutput.push(str.replaceAll(prefix, `${level} `))
        levelLogs.push({ level, message: str.replaceAll(prefix, '') })
      },
    },
    stdout: {
      write: (str) => {
        outputs.push(trimTrailingNewline(str))
        fullOutput.push(trimTrailingNewline(str))
      },
    },
  }

  return {
    streams,
    logs: {
      outputs,
      joinedOutput: () => joinAndTrimTrailingNewlines(outputs),
      clearOutput: () => {
        outputs.length = 0
        outputErrors.length = 0
        fullOutput.length = 0
      },
      outputErrors,
      joinedOutputError: () => joinAndTrimTrailingNewlines(outputs),
      fullOutput,
      joinedFullOutput: () => joinAndTrimTrailingNewlines(fullOutput),
      logs,
      clearLogs: () => {
        levelLogs.length = 0
        logs.length = 0
      },
    },
  }
}
