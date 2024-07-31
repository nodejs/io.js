const t = require('tap')
const mockNpm = require('../../fixtures/mock-npm')

const mockHook = async (t, { hookResponse, ...npmOpts } = {}) => {
  const now = Date.now()

  let hookArgs = null

  const pkgTypes = {
    semver: 'package',
    '@npmcli': 'scope',
    npm: 'owner',
  }

  const libnpmhook = {
    add: async (pkg, uri, secret, opts) => {
      hookArgs = { pkg, uri, secret, opts }
      return { id: 1, name: pkg, type: pkgTypes[pkg], endpoint: uri }
    },
    ls: async opts => {
      hookArgs = opts
      let id = 0
      if (hookResponse) {
        return hookResponse
      }

      return Object.keys(pkgTypes).map(name => ({
        id: ++id,
        name,
        type: pkgTypes[name],
        endpoint: 'https://google.com',
        last_delivery: id % 2 === 0 ? now : undefined,
        response_code: 200,
      }))
    },
    rm: async (id, opts) => {
      hookArgs = { id, opts }
      const pkg = Object.keys(pkgTypes)[0]
      return {
        id: 1,
        name: pkg,
        type: pkgTypes[pkg],
        endpoint: 'https://google.com',
      }
    },
    update: async (id, uri, secret, opts) => {
      hookArgs = { id, uri, secret, opts }
      const pkg = Object.keys(pkgTypes)[0]
      return { id, name: pkg, type: pkgTypes[pkg], endpoint: uri }
    },
  }

  const mock = await mockNpm(t, {
    ...npmOpts,
    command: 'hook',
    mocks: {
      libnpmhook,
      ...npmOpts.mocks,
    },
  })

  return {
    ...mock,
    now,
    hookArgs: () => hookArgs,
  }
}

t.test('npm hook no args', async t => {
  const { hook } = await mockHook(t)
  await t.rejects(hook.exec([]), hook.usage, 'throws usage with no arguments')
})

t.test('npm hook add', async t => {
  const { npm, hook, outputs, hookArgs } = await mockHook(t)
  await hook.exec(['add', 'semver', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      pkg: 'semver',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'provided the correct arguments to libnpmhook'
  )
  t.strictSame(outputs[0], '+ semver  ->  https://google.com', 'prints the correct output')
})

t.test('npm hook add - correct owner hook output', async t => {
  const { npm, hook, outputs, hookArgs } = await mockHook(t)
  await hook.exec(['add', '~npm', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      pkg: '~npm',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'provided the correct arguments to libnpmhook'
  )
  t.strictSame(outputs[0], '+ ~npm  ->  https://google.com', 'prints the correct output')
})

t.test('npm hook add - correct scope hook output', async t => {
  const { npm, hook, outputs, hookArgs } = await mockHook(t)
  await hook.exec(['add', '@npmcli', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      pkg: '@npmcli',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'provided the correct arguments to libnpmhook'
  )
  t.strictSame(outputs[0], '+ @npmcli  ->  https://google.com', 'prints the correct output')
})

t.test('npm hook add - unicode output', async t => {
  const config = {
    unicode: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['add', 'semver', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      pkg: 'semver',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'provided the correct arguments to libnpmhook'
  )
  t.strictSame(outputs[0], '+ semver  ➜  https://google.com', 'prints the correct output')
})

t.test('npm hook add - json output', async t => {
  const config = {
    json: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['add', '@npmcli', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      pkg: '@npmcli',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'provided the correct arguments to libnpmhook'
  )
  t.strictSame(
    JSON.parse(outputs[0]),
    {
      id: 1,
      name: '@npmcli',
      endpoint: 'https://google.com',
      type: 'scope',
    },
    'prints the correct json output'
  )
})

t.test('npm hook add - parseable output', async t => {
  const config = {
    parseable: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['add', '@npmcli', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      pkg: '@npmcli',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'provided the correct arguments to libnpmhook'
  )

  t.strictSame(
    outputs[0].split(/\t/),
    ['id', 'name', 'type', 'endpoint'],
    'prints the correct parseable output headers'
  )
  t.strictSame(
    outputs[1].split(/\t/),
    ['1', '@npmcli', 'scope', 'https://google.com'],
    'prints the correct parseable values'
  )
})

t.test('npm hook add - silent output', async t => {
  const config = { loglevel: 'silent' }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['add', '@npmcli', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      pkg: '@npmcli',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'provided the correct arguments to libnpmhook'
  )
  t.strictSame(outputs, [], 'printed no output')
})

t.test('npm hook ls', async t => {
  const { npm, hook, outputs, hookArgs } = await mockHook(t)
  await hook.exec(['ls'])

  t.match(
    hookArgs(),
    {
      ...npm.flatOptions,
      package: undefined,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs, [
    'You have 3 hooks configured.',
    'Hook 1: semver',
    'Endpoint: https://google.com',
    'Never triggered\n',
    'Hook 2: @npmcli',
    'Endpoint: https://google.com',
    'Triggered just now, response code was "200"\n',
    'Hook 3: ~npm',
    'Endpoint: https://google.com',
    'Never triggered\n',
  ])
})

t.test('npm hook ls, no results', async t => {
  const hookResponse = []
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    hookResponse,
  })

  await hook.exec(['ls'])

  t.match(
    hookArgs(),
    {
      ...npm.flatOptions,
      package: undefined,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs, [`You don't have any hooks configured yet.`])
})

t.test('npm hook ls, single result', async t => {
  const hookResponse = [
    {
      id: 1,
      name: 'semver',
      type: 'package',
      endpoint: 'https://google.com',
    },
  ]
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    hookResponse,
  })

  await hook.exec(['ls'])

  t.match(
    hookArgs(),
    {
      ...npm.flatOptions,
      package: undefined,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs, [
    'You have 1 hook configured.',
    'Hook 1: semver',
    'Endpoint: https://google.com',
    'Never triggered\n',
  ])
})

t.test('npm hook ls - json output', async t => {
  const config = {
    json: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['ls'])

  t.match(
    hookArgs(),
    {
      ...npm.flatOptions,
      package: undefined,
    },
    'received the correct arguments'
  )
  const out = JSON.parse(outputs[0])
  t.match(
    out,
    [
      {
        id: 1,
        name: 'semver',
        type: 'package',
        endpoint: 'https://google.com',
      },
      {
        id: 2,
        name: 'npmcli',
        type: 'scope',
        endpoint: 'https://google.com',
      },
      {
        id: 3,
        name: 'npm',
        type: 'owner',
        endpoint: 'https://google.com',
      },
    ],
    'prints the correct output'
  )
})

t.test('npm hook ls - parseable output', async t => {
  const config = {
    parseable: true,
  }
  const { npm, hook, outputs, hookArgs, now } = await mockHook(t, {
    config,
  })

  await hook.exec(['ls'])

  t.match(
    hookArgs(),
    {
      ...npm.flatOptions,
      package: undefined,
    },
    'received the correct arguments'
  )
  t.strictSame(
    outputs.map(line => line.split(/\t/)),
    [
      ['id', 'name', 'type', 'endpoint', 'last_delivery', 'response_code'],
      ['1', 'semver', 'package', 'https://google.com', '', '200'],
      ['2', '@npmcli', 'scope', 'https://google.com', `${now}`, '200'],
      ['3', 'npm', 'owner', 'https://google.com', '', '200'],
    ],
    'prints the correct result'
  )
})

t.test('npm hook ls - silent output', async t => {
  const config = { loglevel: 'silent' }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['ls'])

  t.match(
    hookArgs(),
    {
      ...npm.flatOptions,
      package: undefined,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs, [], 'printed no output')
})

t.test('npm hook rm', async t => {
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
  })
  await hook.exec(['rm', '1'])

  t.match(
    hookArgs(),
    {
      id: '1',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs[0], '- semver  X  https://google.com', 'printed the correct output')
})

t.test('npm hook rm - unicode output', async t => {
  const config = {
    unicode: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['rm', '1'])

  t.match(
    hookArgs(),
    {
      id: '1',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs[0], '- semver  ✘  https://google.com', 'printed the correct output')
})

t.test('npm hook rm - silent output', async t => {
  const config = { loglevel: 'silent' }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['rm', '1'])

  t.match(
    hookArgs(),
    {
      id: '1',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs, [], 'printed no output')
})

t.test('npm hook rm - json output', async t => {
  const config = {
    json: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['rm', '1'])

  t.match(
    hookArgs(),
    {
      id: '1',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(
    JSON.parse(outputs[0]),
    {
      id: 1,
      name: 'semver',
      type: 'package',
      endpoint: 'https://google.com',
    },
    'printed correct output'
  )
})

t.test('npm hook rm - parseable output', async t => {
  const config = {
    parseable: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['rm', '1'])

  t.match(
    hookArgs(),
    {
      id: '1',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(
    outputs.map(line => line.split(/\t/)),
    [
      ['id', 'name', 'type', 'endpoint'],
      ['1', 'semver', 'package', 'https://google.com'],
    ],
    'printed correct output'
  )
})

t.test('npm hook update', async t => {
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
  })
  await hook.exec(['update', '1', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      id: '1',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs[0], '+ semver  ->  https://google.com', 'printed the correct output')
})

t.test('npm hook update - unicode', async t => {
  const config = {
    unicode: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['update', '1', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      id: '1',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs[0], '+ semver  ➜  https://google.com', 'printed the correct output')
})

t.test('npm hook update - json output', async t => {
  const config = {
    json: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['update', '1', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      id: '1',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(
    JSON.parse(outputs[0]),
    {
      id: '1',
      name: 'semver',
      type: 'package',
      endpoint: 'https://google.com',
    },
    'printed the correct output'
  )
})

t.test('npm hook update - parseable output', async t => {
  const config = {
    parseable: true,
  }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['update', '1', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      id: '1',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(
    outputs.map(line => line.split(/\t/)),
    [
      ['id', 'name', 'type', 'endpoint'],
      ['1', 'semver', 'package', 'https://google.com'],
    ],
    'printed the correct output'
  )
})

t.test('npm hook update - silent output', async t => {
  const config = { loglevel: 'silent' }
  const { npm, hook, outputs, hookArgs } = await mockHook(t, {
    config,
  })

  await hook.exec(['update', '1', 'https://google.com', 'some-secret'])

  t.match(
    hookArgs(),
    {
      id: '1',
      uri: 'https://google.com',
      secret: 'some-secret',
      opts: npm.flatOptions,
    },
    'received the correct arguments'
  )
  t.strictSame(outputs, [], 'printed no output')
})
