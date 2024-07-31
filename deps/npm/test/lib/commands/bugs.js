const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm')

const pacote = {
  manifest: async (spec) => {
    return spec === 'nobugs' ? {
      name: 'nobugs',
      version: '1.2.3',
    } : spec === 'nullbugs' ? {
      name: 'nullbugs',
      version: '1.2.3',
      bugs: null,
    } : spec === 'bugsurl' ? {
      name: 'bugsurl',
      version: '1.2.3',
      bugs: 'https://bugzilla.localhost/bugsurl',
    } : spec === 'bugsobj' ? {
      name: 'bugsobj',
      version: '1.2.3',
      bugs: { url: 'https://bugzilla.localhost/bugsobj' },
    } : spec === 'bugsobj-nourl' ? {
      name: 'bugsobj-nourl',
      version: '1.2.3',
      bugs: { no: 'url here' },
    } : spec === 'repourl' ? {
      name: 'repourl',
      version: '1.2.3',
      repository: 'https://github.com/foo/repourl',
    } : spec === 'repoobj' ? {
      name: 'repoobj',
      version: '1.2.3',
      repository: { url: 'https://github.com/foo/repoobj' },
    } : spec === 'mailtest' ? {
      name: 'mailtest',
      version: '3.7.4',
      bugs: { email: 'hello@example.com' },
    } : spec === 'secondmailtest' ? {
      name: 'secondmailtest',
      version: '0.1.1',
      bugs: { email: 'ABC432abc@a.b.example.net' },
    } : spec === '.' ? {
      name: 'thispkg',
      version: '1.2.3',
      bugs: 'https://example.com',
    } : null
  },
}

t.test('usage', async (t) => {
  const { bugs } = await loadMockNpm(t, { command: 'bugs' })
  t.match(bugs.usage, 'bugs', 'usage has command name in it')
})

t.test('open bugs urls & emails', async t => {
  // keep a tally of which urls got opened
  let opened = {}

  const openUrl = async (_, url) => {
    opened[url] = opened[url] || 0
    opened[url]++
  }

  const { npm } = await loadMockNpm(t, {
    mocks: {
      pacote,
      '{LIB}/utils/open-url.js': { openUrl },
    },
  })

  const expected = {
    '.': 'https://example.com',
    nobugs: 'https://www.npmjs.com/package/nobugs',
    nullbugs: 'https://www.npmjs.com/package/nullbugs',
    'bugsobj-nourl': 'https://www.npmjs.com/package/bugsobj-nourl',
    bugsurl: 'https://bugzilla.localhost/bugsurl',
    bugsobj: 'https://bugzilla.localhost/bugsobj',
    repourl: 'https://github.com/foo/repourl/issues',
    repoobj: 'https://github.com/foo/repoobj/issues',
    mailtest: 'mailto:hello@example.com',
    secondmailtest: 'mailto:ABC432abc@a.b.example.net',
  }

  for (const [pkg, expect] of Object.entries(expected)) {
    await t.test(pkg, async t => {
      await npm.exec('bugs', [pkg])
      t.equal(opened[expect], 1, 'opened expected url', { opened })
    })
  }

  opened = {}

  await t.test('open default package if none specified', async t => {
    await npm.exec('bugs', [])
    t.equal(opened['https://example.com'], 1, 'opened expected url', { opened })
  })
})
