const t = require('tap')
const mockNpm = require('../../fixtures/mock-npm')

const mockOrg = async (t, { orgSize = 1, orgList = {}, ...npmOpts } = {}) => {
  let setArgs = null
  let rmArgs = null
  let lsArgs = null

  const libnpmorg = {
    set: async (org, user, role, opts) => {
      setArgs = { org, user, role, opts }
      return {
        org: {
          name: org,
          size: orgSize,
        },
        user,
        role,
      }
    },
    rm: async (org, user, opts) => {
      rmArgs = { org, user, opts }
    },
    ls: async (org, opts) => {
      lsArgs = { org, opts }
      return orgList
    },
  }

  const mock = await mockNpm(t, {
    ...npmOpts,
    command: 'org',
    mocks: {
      libnpmorg,
      ...npmOpts.mocks,
    },
  })

  return {
    ...mock,
    setArgs: () => setArgs,
    rmArgs: () => rmArgs,
    lsArgs: () => lsArgs,
  }
}

t.test('completion', async t => {
  const { org } = await mockOrg(t)
  const completion = argv => org.completion({ conf: { argv: { remain: argv } } })

  const assertions = [
    [
      ['npm', 'org'],
      ['set', 'rm', 'ls'],
    ],
    [['npm', 'org', 'ls'], []],
    [['npm', 'org', 'add'], []],
    [['npm', 'org', 'rm'], []],
    [['npm', 'org', 'set'], []],
  ]

  for (const [argv, expected] of assertions) {
    t.resolveMatch(completion(argv), expected, `completion for: ${argv.join(', ')}`)
  }

  t.rejects(
    completion(['npm', 'org', 'flurb']),
    /flurb not recognized/,
    'errors for unknown subcommand'
  )
})

t.test('npm org - invalid subcommand', async t => {
  const { org } = await mockOrg(t)
  await t.rejects(org.exec(['foo']), org.usage)
})

t.test('npm org add', async t => {
  const { npm, org, setArgs, outputs } = await mockOrg(t)

  await org.exec(['add', 'orgname', 'username'])

  t.match(
    setArgs(),
    {
      org: 'orgname',
      user: 'username',
      role: 'developer',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.equal(
    outputs[0],
    'Added username as developer to orgname. You now have 1 member in this org.',
    'printed the correct output'
  )
})

t.test('npm org add - no org', async t => {
  const { org } = await mockOrg(t)

  await t.rejects(
    org.exec(['add', '', 'username']),
    /`orgname` is required/,
    'returns the correct error'
  )
})

t.test('npm org add - no user', async t => {
  const { org } = await mockOrg(t)
  await t.rejects(
    org.exec(['add', 'orgname', '']),
    /`username` is required/,
    'returns the correct error'
  )
})

t.test('npm org add - invalid role', async t => {
  const { org } = await mockOrg(t)
  await t.rejects(
    org.exec(['add', 'orgname', 'username', 'person']),
    /`role` must be one of/,
    'returns the correct error'
  )
})

t.test('npm org add - more users', async t => {
  const orgSize = 5
  const { npm, org, outputs, setArgs } = await mockOrg(t, { orgSize })

  await org.exec(['add', 'orgname', 'username'])
  t.match(
    setArgs(),
    {
      org: 'orgname',
      user: 'username',
      role: 'developer',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.equal(
    outputs[0],
    'Added username as developer to orgname. You now have 5 members in this org.',
    'printed the correct output'
  )
})

t.test('npm org add - json output', async t => {
  const { npm, org, outputs, setArgs } = await mockOrg(t, {
    config: { json: true },
  })

  await org.exec(['add', 'orgname', 'username'])

  t.match(
    setArgs(),
    {
      org: 'orgname',
      user: 'username',
      role: 'developer',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(
    JSON.parse(outputs[0]),
    {
      org: {
        name: 'orgname',
        size: 1,
      },
      user: 'username',
      role: 'developer',
    },
    'printed the correct output'
  )
})

t.test('npm org add - parseable output', async t => {
  const config = { parseable: true }
  const { npm, org, outputs, setArgs } = await mockOrg(t, {
    config,
  })

  await org.exec(['add', 'orgname', 'username'])

  t.match(
    setArgs(),
    {
      org: 'orgname',
      user: 'username',
      role: 'developer',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(
    outputs.map(line => line.split(/\t/)),
    [
      ['org', 'orgsize', 'user', 'role'],
      ['orgname', '1', 'username', 'developer'],
    ],
    'printed the correct output'
  )
})

t.test('npm org add - silent output', async t => {
  const config = { loglevel: 'silent' }
  const { npm, org, outputs, setArgs } = await mockOrg(t, {
    config,
  })

  await org.exec(['add', 'orgname', 'username'])

  t.match(
    setArgs(),
    {
      org: 'orgname',
      user: 'username',
      role: 'developer',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs, [], 'prints no output')
})

t.test('npm org rm', async t => {
  const { npm, org, outputs, lsArgs, rmArgs } = await mockOrg(t)

  await org.exec(['rm', 'orgname', 'username'])

  t.match(
    rmArgs(),
    {
      org: 'orgname',
      user: 'username',
      opts: npm.flatOptions,
    },
    'libnpmorg.rm received the correct args'
  )
  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'libnpmorg.ls received the correct args'
  )
  t.equal(
    outputs[0],
    'Successfully removed username from orgname. You now have 0 members in this org.',
    'printed the correct output'
  )
})

t.test('npm org rm - no org', async t => {
  const { org } = await mockOrg(t)

  await t.rejects(
    org.exec(['rm', '', 'username']),
    /`orgname` is required/,
    'threw the correct error'
  )
})

t.test('npm org rm - no user', async t => {
  const { org } = await mockOrg(t)

  await t.rejects(org.exec(['rm', 'orgname']), /`username` is required/, 'threw the correct error')
})

t.test('npm org rm - one user left', async t => {
  const orgList = {
    one: 'developer',
  }
  const { npm, org, outputs, lsArgs, rmArgs } = await mockOrg(t, {
    orgList,
  })

  await org.exec(['rm', 'orgname', 'username'])

  t.match(
    rmArgs(),
    {
      org: 'orgname',
      user: 'username',
      opts: npm.flatOptions,
    },
    'libnpmorg.rm received the correct args'
  )
  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'libnpmorg.ls received the correct args'
  )
  t.equal(
    outputs[0],
    'Successfully removed username from orgname. You now have 1 member in this org.',
    'printed the correct output'
  )
})

t.test('npm org rm - json output', async t => {
  const config = { json: true }
  const { npm, org, outputs, lsArgs, rmArgs } = await mockOrg(t, {
    config,
  })

  await org.exec(['rm', 'orgname', 'username'])

  t.match(
    rmArgs(),
    {
      org: 'orgname',
      user: 'username',
      opts: npm.flatOptions,
    },
    'libnpmorg.rm received the correct args'
  )
  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'libnpmorg.ls received the correct args'
  )
  t.strictSame(
    JSON.parse(outputs[0]),
    {
      user: 'username',
      org: 'orgname',
      userCount: 0,
      deleted: true,
    },
    'printed the correct output'
  )
})

t.test('npm org rm - parseable output', async t => {
  const config = { parseable: true }
  const { npm, org, outputs, lsArgs, rmArgs } = await mockOrg(t, {
    config,
  })

  await org.exec(['rm', 'orgname', 'username'])

  t.match(
    rmArgs(),
    {
      org: 'orgname',
      user: 'username',
      opts: npm.flatOptions,
    },
    'libnpmorg.rm received the correct args'
  )
  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'libnpmorg.ls received the correct args'
  )
  t.strictSame(
    outputs.map(line => line.split(/\t/)),
    [
      ['user', 'org', 'userCount', 'deleted'],
      ['username', 'orgname', '0', 'true'],
    ],
    'printed the correct output'
  )
})

t.test('npm org rm - silent output', async t => {
  const config = { loglevel: 'silent' }
  const { npm, org, outputs, lsArgs, rmArgs } = await mockOrg(t, {
    config,
  })

  await org.exec(['rm', 'orgname', 'username'])

  t.match(
    rmArgs(),
    {
      org: 'orgname',
      user: 'username',
      opts: npm.flatOptions,
    },
    'libnpmorg.rm received the correct args'
  )
  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'libnpmorg.ls received the correct args'
  )
  t.strictSame(outputs, [], 'printed no output')
})

t.test('npm org ls', async t => {
  const orgList = {
    one: 'developer',
    two: 'admin',
    three: 'owner',
  }
  const { npm, org, outputs, lsArgs } = await mockOrg(t, {
    orgList,
  })

  await org.exec(['ls', 'orgname'])

  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'receieved the correct args'
  )
  t.strictSame(outputs, [
    'one - developer',
    'three - owner',
    'two - admin',
  ])
})

t.test('npm org ls - user filter', async t => {
  const orgList = {
    username: 'admin',
    missing: 'admin',
  }
  const { npm, org, outputs, lsArgs } = await mockOrg(t, {
    orgList,
  })

  await org.exec(['ls', 'orgname', 'username'])

  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'receieved the correct args'
  )
  t.strictSame(outputs, [
    'username - admin',
  ])
})

t.test('npm org ls - user filter, missing user', async t => {
  const orgList = {
    missing: 'admin',
  }
  const { npm, org, outputs, lsArgs } = await mockOrg(t, {
    orgList,
  })

  await org.exec(['ls', 'orgname', 'username'])

  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'receieved the correct args'
  )
  t.strictSame(outputs, [])
})

t.test('npm org ls - no org', async t => {
  const { org } = await mockOrg(t)
  await t.rejects(org.exec(['ls']), /`orgname` is required/, 'throws the correct error')
})

t.test('npm org ls - json output', async t => {
  const config = { json: true }
  const orgList = {
    one: 'developer',
    two: 'admin',
    three: 'owner',
  }
  const { npm, org, outputs, lsArgs } = await mockOrg(t, {
    orgList,
    config,
  })

  await org.exec(['ls', 'orgname'])

  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'receieved the correct args'
  )
  t.strictSame(JSON.parse(outputs[0]), orgList, 'prints the correct output')
})

t.test('npm org ls - parseable output', async t => {
  const config = { parseable: true }
  const orgList = {
    one: 'developer',
    two: 'admin',
    three: 'owner',
  }
  const { npm, org, outputs, lsArgs } = await mockOrg(t, {
    orgList,
    config,
  })

  await org.exec(['ls', 'orgname'])

  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'receieved the correct args'
  )
  t.strictSame(
    outputs.map(line => line.split(/\t/)),
    [
      ['user', 'role'],
      ['one', 'developer'],
      ['two', 'admin'],
      ['three', 'owner'],
    ],
    'printed the correct output'
  )
})

t.test('npm org ls - silent output', async t => {
  const config = { loglevel: 'silent' }
  const orgList = {
    one: 'developer',
    two: 'admin',
    three: 'owner',
  }
  const { npm, org, outputs, lsArgs } = await mockOrg(t, {
    orgList,
    config,
  })

  await org.exec(['ls', 'orgname'])

  t.match(
    lsArgs(),
    {
      org: 'orgname',
      opts: npm.flatOptions,
    },
    'receieved the correct args'
  )
  t.strictSame(outputs, [], 'printed no output')
})
