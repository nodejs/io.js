const { log, output } = require('proc-log')

const outputError = ({ standard = [], verbose = [], error = [], summary = [], detail = [] }) => {
  for (const line of standard) {
    // Each output line is just a single string
    output.standard(line)
  }
  for (const line of verbose) {
    log.verbose(...line)
  }
  for (const line of [...error, ...summary, ...detail]) {
    log.error(...line)
  }
}

const jsonError = (error, npm) => {
  if (error && npm?.loaded && npm?.config.get('json')) {
    return {
      code: error.code,
      summary: (error.summary || []).map(l => l.slice(1).join(' ')).join('\n').trim(),
      detail: (error.detail || []).map(l => l.slice(1).join(' ')).join('\n').trim(),
    }
  }
}

module.exports = {
  outputError,
  jsonError,
}
