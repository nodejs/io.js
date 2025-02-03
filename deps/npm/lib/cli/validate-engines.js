// This is separate to indicate that it should contain code we expect to work in
// all versions of node >= 6.  This is a best effort to catch syntax errors to
// give users a good error message if they are using a node version that doesn't
// allow syntax we are using such as private properties, etc. This file is
// linted with ecmaVersion=6 so we don't use invalid syntax, which is set in the
// .eslintrc.local.json file

const { engines: { node: engines }, version } = require('../../package.json')
const npm = `v${version}`

module.exports = (process, getCli) => {
  const node = process.version

  const unsupportedMessage = `npm ${npm} does not support Node.js ${node}. This version of npm supports the following node versions: \`${engines}\`. You can find the latest version at https://nodejs.org/.`

  const brokenMessage = `ERROR: npm ${npm} is known not to run on Node.js ${node}.  This version of npm supports the following node versions: \`${engines}\`. You can find the latest version at https://nodejs.org/.`

  // coverage ignored because this is only hit in very unsupported node versions
  // and it's a best effort attempt to show something nice in those cases
  /* istanbul ignore next */
  const syntaxErrorHandler = (err) => {
    if (err instanceof SyntaxError) {
      // eslint-disable-next-line no-console
      console.error(`${brokenMessage}\n\nERROR:`)
      // eslint-disable-next-line no-console
      console.error(err)
      return process.exit(1)
    }
    throw err
  }

  process.on('uncaughtException', syntaxErrorHandler)
  process.on('unhandledRejection', syntaxErrorHandler)

  // require this only after setting up the error handlers
  const cli = getCli()
  return cli(process, {
    node,
    npm,
    engines,
    unsupportedMessage,
    off: () => {
      process.off('uncaughtException', syntaxErrorHandler)
      process.off('unhandledRejection', syntaxErrorHandler)
    },
  })
}
