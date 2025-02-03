const t = require('tap')
const mockGlobals = require('@npmcli/mock-globals')
const tmock = require('../../fixtures/tmock')

const mockValidateEngines = (t) => {
  const validateEngines = tmock(t, '{LIB}/cli/validate-engines.js', {
    '{ROOT}/package.json': { version: '1.2.3', engines: { node: '>=0' } },
  })
  mockGlobals(t, { 'process.version': 'v4.5.6' })
  return validateEngines(process, () => (_, r) => r)
}

t.test('validate engines', async t => {
  t.equal(process.listenerCount('uncaughtException'), 0)
  t.equal(process.listenerCount('unhandledRejection'), 0)

  const result = mockValidateEngines(t)

  t.equal(process.listenerCount('uncaughtException'), 1)
  t.equal(process.listenerCount('unhandledRejection'), 1)

  t.match(result, {
    node: 'v4.5.6',
    npm: 'v1.2.3',
    engines: '>=0',
    unsupportedMessage: 'npm v1.2.3 does not support Node.js v4.5.6. This version of npm supports the following node versions: `>=0`. You can find the latest version at https://nodejs.org/.',
  })

  result.off()

  t.equal(process.listenerCount('uncaughtException'), 0)
  t.equal(process.listenerCount('unhandledRejection'), 0)
})
