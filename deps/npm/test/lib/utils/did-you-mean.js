const t = require('tap')
const dym = require('../../../lib/utils/did-you-mean.js')

t.test('did-you-mean', async t => {
  t.test('with package.json', async t => {
    const pkg = {
      bin: {
        npx: 'exists',
      },
      scripts: {
        install: 'exists',
        posttest: 'exists',
      },
    }
    t.test('nistall', async t => {
      const result = dym(pkg, 'nistall')
      t.match(result, 'npm install')
    })
    t.test('sttest', async t => {
      const result = dym(pkg, 'sttest')
      t.match(result, 'npm test')
      t.match(result, 'npm run posttest')
    })
    t.test('npz', async t => {
      const result = dym(pkg, 'npxx')
      t.match(result, 'npm exec npx')
    })
    t.test('qwuijbo', async t => {
      const result = dym(pkg, 'qwuijbo')
      t.match(result, '')
    })
  })
  t.test('with no package.json', t => {
    t.test('nistall', async t => {
      const result = dym(null, 'nistall')
      t.match(result, 'npm install')
    })
    t.end()
  })
  t.test('missing bin and script properties', async t => {
    const pkg = {
      name: 'missing-bin',
    }
    const result = dym(pkg, 'nistall')
    t.match(result, 'npm install')
  })
})
