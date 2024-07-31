const { log, output } = require('proc-log')
const { redactLog: replaceInfo } = require('@npmcli/redact')

// print an error or just nothing if the audit report has an error
// this is called by the audit command, and by the reify-output util
// prints a JSON version of the error if it's --json
// returns 'true' if there was an error, false otherwise

const auditError = (npm, report) => {
  if (!report || !report.error) {
    return false
  }

  if (npm.command !== 'audit') {
    return true
  }

  const { error } = report

  // ok, we care about it, then
  log.warn('audit', error.message)
  const { body: errBody } = error
  const body = Buffer.isBuffer(errBody) ? errBody.toString() : errBody
  if (npm.flatOptions.json) {
    output.buffer({
      message: error.message,
      method: error.method,
      uri: replaceInfo(error.uri),
      headers: error.headers,
      statusCode: error.statusCode,
      body,
    })
  } else {
    output.standard(body)
  }

  throw 'audit endpoint returned an error'
}

module.exports = auditError
