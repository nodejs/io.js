const t = require('tap')
const tmock = require('../fixtures/tmock')

t.test('returns cli-entry function', async t => {
  const cli = tmock(t, '{LIB}/cli.js', {
    '{LIB}/cli/entry.js': () => 'ENTRY',
  })

  t.equal(cli(process), 'ENTRY')
})
