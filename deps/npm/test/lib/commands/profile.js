const t = require('tap')
const mockNpm = require('../../fixtures/mock-npm')

const mockProfile = async (t, { npmProfile, readUserInfo, qrcode, config, ...opts } = {}) => {
  const mocks = {
    'npm-profile': npmProfile || {
      async get () {},
      async set () {},
      async createToken () {},
    },
    'qrcode-terminal': qrcode || { generate: (url, cb) => cb() },
    '{LIB}/utils/read-user-info.js': readUserInfo || {
      async password () {},
      async otp () {},
    },
  }

  const mock = await mockNpm(t, {
    ...opts,
    command: 'profile',
    config: {
      color: false,
      ...config,
    },
    mocks: {
      ...mocks,
      ...opts.mocks,
    },
  })

  return {
    ...mock,
    result: () => mock.joinedOutput(),
  }
}

const userProfile = {
  tfa: { pending: false, mode: 'auth-and-writes' },
  name: 'foo',
  email: 'foo@github.com',
  email_verified: true,
  created: '2015-02-26T01:26:37.384Z',
  updated: '2020-08-12T16:19:35.326Z',
  cidr_whitelist: null,
  fullname: 'Foo Bar',
  homepage: 'https://github.com',
  freenode: 'foobar',
  twitter: 'https://twitter.com/npmjs',
  github: 'https://github.com/npm',
}

t.test('no args', async t => {
  const { profile } = await mockProfile(t)
  await t.rejects(profile.exec([]), await profile.usage)
})

t.test('profile get no args', async t => {
  const defaultNpmProfile = {
    async get () {
      return userProfile
    },
  }

  t.test('default output', async t => {
    const { profile, result } = await mockProfile(t, { npmProfile: defaultNpmProfile })
    await profile.exec(['get'])

    t.matchSnapshot(result(), 'should output table with contents')
  })

  t.test('--json', async t => {
    const { profile, result } = await mockProfile(t, {
      npmProfile: defaultNpmProfile,
      config: { json: true },
    })

    await profile.exec(['get'])

    t.same(JSON.parse(result()), userProfile, 'should output json profile result')
  })

  t.test('--parseable', async t => {
    const { profile, result } = await mockProfile(t, {
      npmProfile: defaultNpmProfile,
      config: { parseable: true },
    })

    await profile.exec(['get'])
    t.matchSnapshot(result(), 'should output all profile info as parseable result')
  })

  t.test('no tfa enabled', async t => {
    const npmProfile = {
      async get () {
        return {
          ...userProfile,
          tfa: null,
        }
      },
    }
    const { profile, result } = await mockProfile(t, { npmProfile })

    await profile.exec(['get'])
    t.matchSnapshot(result(), 'should output expected profile values')
  })

  t.test('unverified email', async t => {
    const npmProfile = {
      async get () {
        return {
          ...userProfile,
          email_verified: false,
        }
      },
    }

    const { profile, result } = await mockProfile(t, { npmProfile })

    await profile.exec(['get'])

    t.matchSnapshot(result(), 'should output table with contents')
  })

  t.test('profile has cidr_whitelist item', async t => {
    const npmProfile = {
      async get () {
        return {
          ...userProfile,
          cidr_whitelist: ['192.168.1.1'],
        }
      },
    }

    const { profile, result } = await mockProfile(t, { npmProfile })

    await profile.exec(['get'])

    t.matchSnapshot(result(), 'should output table with contents')
  })
})

t.test('profile get <key>', async t => {
  const npmProfile = {
    async get () {
      return userProfile
    },
  }

  t.test('default output', async t => {
    const { profile, result } = await mockProfile(t, { npmProfile })

    await profile.exec(['get', 'name'])

    t.equal(result(), 'foo', 'should output value result')
  })

  t.test('--json', async t => {
    const { profile, result } = await mockProfile(t, {
      npmProfile,
      config: { json: true },
    })

    await profile.exec(['get', 'name'])

    t.same(
      JSON.parse(result()),
      userProfile,
      'should output json profile result ignoring args filter'
    )
  })

  t.test('--parseable', async t => {
    const { profile, result } = await mockProfile(t, {
      npmProfile,
      config: { parseable: true },
    })

    await profile.exec(['get', 'name'])

    t.matchSnapshot(result(), 'should output parseable result value')
  })
})

t.test('profile get multiple args', async t => {
  const npmProfile = {
    async get () {
      return userProfile
    },
  }

  t.test('default output', async t => {
    const { profile, result } = await mockProfile(t, {
      npmProfile,
    })
    await profile.exec(['get', 'name', 'email', 'github'])

    t.matchSnapshot(result(), 'should output all keys')
  })

  t.test('--json', async t => {
    const config = { json: true }
    const { profile, result } = await mockProfile(t, {
      npmProfile,
      config,
    })

    await profile.exec(['get', 'name', 'email', 'github'])

    t.same(JSON.parse(result()), userProfile, 'should output json profile result and ignore args')
  })

  t.test('--parseable', async t => {
    const config = { parseable: true }
    const { profile, result } = await mockProfile(t, {
      npmProfile,
      config,
    })

    await profile.exec(['get', 'name', 'email', 'github'])

    t.matchSnapshot(result(), 'should output parseable profile value results')
  })

  t.test('comma separated', async t => {
    const { profile, result } = await mockProfile(t, {
      npmProfile,
    })

    await profile.exec(['get', 'name,email,github'])

    t.matchSnapshot(result(), 'should output all keys')
  })
})

t.test('profile set <key> <value>', async t => {
  t.test('no key', async t => {
    const { profile } = await mockProfile(t)

    await t.rejects(
      profile.exec(['set']),
      /npm profile set <prop> <value>/,
      'should throw proper usage message'
    )
  })

  t.test('no value', async t => {
    const { profile } = await mockProfile(t)
    await t.rejects(
      profile.exec(['set', 'email']),
      /npm profile set <prop> <value>/,
      'should throw proper usage message'
    )
  })

  t.test('set password', async t => {
    const { profile } = await mockProfile(t)
    await t.rejects(
      profile.exec(['set', 'password', '1234']),
      /Do not include your current or new passwords on the command line./,
      'should throw an error refusing to set password from args'
    )
  })

  t.test('unwritable key', async t => {
    const { profile } = await mockProfile(t)
    await await t.rejects(
      profile.exec(['set', 'name', 'foo']),
      /"name" is not a property we can set./,
      'should throw the unwritable key error'
    )
  })

  const defaultNpmProfile = t => ({
    async get () {
      return userProfile
    },
    async set (newUser) {
      t.match(
        newUser,
        {
          fullname: 'Lorem Ipsum',
        },
        'should set new value to key'
      )
      return {
        ...userProfile,
        ...newUser,
      }
    },
  })

  t.test('writable key', async t => {
    t.test('default output', async t => {
      t.plan(2)

      const { profile, result } = await mockProfile(t, {
        npmProfile: defaultNpmProfile(t),
      })

      await profile.exec(['set', 'fullname', 'Lorem Ipsum'])
      t.equal(result(), 'Set fullname to Lorem Ipsum', 'should output set key success msg')
    })

    t.test('--json', async t => {
      t.plan(2)

      const config = { json: true }

      const { profile, result } = await mockProfile(t, {
        npmProfile: defaultNpmProfile(t),
        config,
      })

      await profile.exec(['set', 'fullname', 'Lorem Ipsum'])

      t.same(
        JSON.parse(result()),
        {
          fullname: 'Lorem Ipsum',
        },
        'should output json set key success msg'
      )
    })

    t.test('--parseable', async t => {
      t.plan(2)

      const config = { parseable: true }
      const { profile, result } = await mockProfile(t, {
        npmProfile: defaultNpmProfile(t),
        config,
      })

      await profile.exec(['set', 'fullname', 'Lorem Ipsum'])

      t.matchSnapshot(result(), 'should output parseable set key success msg')
    })
  })

  t.test('write new email', async t => {
    t.plan(2)

    const npmProfile = {
      async get () {
        return userProfile
      },
      async set (newUser) {
        t.match(
          newUser,
          {
            email: 'foo@npmjs.com',
          },
          'should set new value to email'
        )
        return {
          ...userProfile,
          ...newUser,
        }
      },
    }

    const { profile, result } = await mockProfile(t, {
      npmProfile,
    })

    await profile.exec(['set', 'email', 'foo@npmjs.com'])
    t.equal(result(), 'Set email to foo@npmjs.com', 'should output set key success msg')
  })

  t.test('change password', async t => {
    t.plan(5)

    const npmProfile = {
      async get () {
        return userProfile
      },
      async set (newUser) {
        t.match(
          newUser,
          {
            password: {
              old: 'currentpassword1234',
              new: 'newpassword1234',
            },
          },
          'should set new password'
        )
        return {
          ...userProfile,
        }
      },
    }

    const readUserInfo = {
      async password (label) {
        if (label === 'Current password: ') {
          t.ok('should interactively ask for password confirmation')
        } else if (label === 'New password: ') {
          t.ok('should interactively ask for new password')
        } else if (label === '       Again:     ') {
          t.ok('should interactively ask for new password confirmation')
        } else {
          throw new Error('Unexpected label: ' + label)
        }

        return label === 'Current password: ' ? 'currentpassword1234' : 'newpassword1234'
      },
    }

    const { profile, result } = await mockProfile(t, {
      npmProfile,
      readUserInfo,
    })

    await profile.exec(['set', 'password'])

    t.equal(result(), 'Set password', 'should output set password success msg')
  })

  t.test('password confirmation mismatch', async t => {
    t.plan(2)

    let passwordPromptCount = 0

    const npmProfile = {
      async get () {
        return userProfile
      },
      async set () {
        return { ...userProfile }
      },
    }

    const readUserInfo = {
      async password (label) {
        passwordPromptCount++

        switch (label) {
          case 'Current password: ':
            return 'currentpassword1234'
          case 'New password: ':
            return passwordPromptCount < 3 ? 'password-that-will-not-be-confirmed' : 'newpassword'
          case '       Again:     ':
            return 'newpassword'
          default:
            return 'password1234'
        }
      },
    }

    const { profile, result, logs } = await mockProfile(t, {
      npmProfile,
      readUserInfo,
    })

    await profile.exec(['set', 'password'])

    t.equal(
      logs.warn.byTitle('profile')[0],
      'profile Passwords do not match, please try again.',
      'should log password mismatch message'
    )

    t.equal(result(), 'Set password', 'should output set password success msg')
  })
})

t.test('enable-2fa', async t => {
  t.test('invalid args', async t => {
    const { profile } = await mockProfile(t)
    await t.rejects(
      profile.exec(['enable-2fa', 'foo', 'bar']),
      /npm profile enable-2fa \[auth-and-writes|auth-only\]/,
      'should throw usage error'
    )
  })

  t.test('invalid two factor auth mode', async t => {
    const { profile } = await mockProfile(t)
    await t.rejects(
      profile.exec(['enable-2fa', 'foo']),
      /Invalid two-factor authentication mode "foo"/,
      'should throw invalid auth mode error'
    )
  })

  t.test('no support for --json output', async t => {
    const config = { json: true }
    const { profile } = await mockProfile(t, { config })

    await t.rejects(
      profile.exec(['enable-2fa', 'auth-only']),
      'Enabling two-factor authentication is an interactive ' +
        'operation and JSON output mode is not available',
      'should throw no support msg'
    )
  })

  t.test('no support for --parseable output', async t => {
    const config = { parseable: true }
    const { profile } = await mockProfile(t, { config })

    await t.rejects(
      profile.exec(['enable-2fa', 'auth-only']),
      'Enabling two-factor authentication is an interactive ' +
        'operation and parseable output mode is not available',
      'should throw no support msg'
    )
  })

  t.test('no bearer tokens returned by registry', async t => {
    t.plan(3)

    const npmProfile = {
      async createToken (pass) {
        t.match(pass, 'bar', 'should use password for basic auth')
        return {}
      },
    }

    const { npm, profile } = await mockProfile(t, {
      npmProfile,
    })

    // mock legacy basic auth style
    // XXX: use mock registry
    npm.config.getCredentialsByURI = reg => {
      t.equal(reg, npm.flatOptions.registry, 'should use expected registry')
      return { auth: Buffer.from('foo:bar').toString('base64') }
    }

    await t.rejects(
      profile.exec(['enable-2fa', 'auth-only']),
      'Your registry https://registry.npmjs.org/ does ' +
        'not seem to support bearer tokens. Bearer tokens ' +
        'are required for two-factor authentication',
      'should throw no support msg'
    )
  })

  t.test('from basic username/password auth', async t => {
    const npmProfile = {
      async createToken () {
        return {}
      },
    }

    const { npm, profile } = await mockProfile(t, {
      npmProfile,
    })

    // mock legacy basic auth style with user/pass
    // XXX: use mock registry
    npm.config.getCredentialsByURI = () => {
      return { username: 'foo', password: 'bar' }
    }

    await t.rejects(
      profile.exec(['enable-2fa', 'auth-only']),
      'Your registry https://registry.npmjs.org/ does ' +
        'not seem to support bearer tokens. Bearer tokens ' +
        'are required for two-factor authentication',
      'should throw no support msg'
    )
  })

  t.test('no auth found', async t => {
    const { npm, profile } = await mockProfile(t)

    // XXX: use mock registry
    npm.config.getCredentialsByURI = () => ({})

    await t.rejects(
      profile.exec(['enable-2fa', 'auth-only']),
      'You need to be logged in to registry ' + 'https://registry.npmjs.org/ in order to enable 2fa'
    )
  })

  t.test('from basic auth, asks for otp', async t => {
    t.plan(9)

    const npmProfile = {
      async createToken (pass) {
        t.match(pass, 'bar', 'should use password for basic auth')
        return { token: 'token' }
      },
      async get () {
        return userProfile
      },
      async set (newProfile) {
        t.match(
          newProfile,
          {
            tfa: {
              mode: 'auth-only',
            },
          },
          'should set tfa mode'
        )
        return {
          ...userProfile,
          tfa: null,
        }
      },
    }

    const readUserInfo = {
      async password () {
        t.ok('should interactively ask for password confirmation')
        return 'password1234'
      },
      async otp (label) {
        t.equal(
          label,
          'Enter one-time password: ',
          'should ask for otp confirmation'
        )
        return '123456'
      },
    }

    const { npm, profile, result } = await mockProfile(t, {
      npmProfile,
      readUserInfo,
    })

    // mock legacy basic auth style
    // XXX: use mock registry
    npm.config.getCredentialsByURI = reg => {
      t.equal(reg, npm.flatOptions.registry, 'should use expected registry')
      return { auth: Buffer.from('foo:bar').toString('base64') }
    }
    npm.config.setCredentialsByURI = (registry, { token }) => {
      t.equal(registry, npm.flatOptions.registry, 'should set expected registry')
      t.equal(token, 'token', 'should set expected token')
    }
    npm.config.save = type => {
      t.equal(type, 'user', 'should save to user config')
    }

    await profile.exec(['enable-2fa', 'auth-only'])
    t.equal(
      result(),
      'Two factor authentication mode changed to: auth-only',
      'should output success msg'
    )
  })

  t.test('from token and set otp, retries on pending and verifies with qrcode', async t => {
    t.plan(4)

    let setCount = 0
    const npmProfile = {
      async get () {
        return {
          ...userProfile,
          tfa: {
            pending: true,
          },
        }
      },
      async set (newProfile) {
        setCount++

        // when profile response shows that 2fa is pending the
        // first time calling npm-profile.set should reset 2fa
        if (setCount === 1) {
          t.match(
            newProfile,
            {
              tfa: {
                password: 'password1234',
                mode: 'disable',
              },
            },
            'should reset 2fa'
          )
        } else if (setCount === 2) {
          t.match(
            newProfile,
            {
              tfa: {
                mode: 'auth-only',
              },
            },
            'should set tfa mode approprietly in follow-up call'
          )
        } else if (setCount === 3) {
          t.match(
            newProfile,
            {
              tfa: ['123456'],
            },
            'should set tfa as otp code?'
          )
          return {
            ...userProfile,
            tfa: ['123456', '789101'],
          }
        }

        return {
          ...userProfile,
          tfa: 'otpauth://foo?secret=1234',
        }
      },
    }

    const readUserInfo = {
      async password () {
        return 'password1234'
      },
      async otp () {
        return '123456'
      },
    }

    const qrcode = {
      /* eslint-disable-next-line node/no-callback-literal */
      generate: (url, cb) => cb('qrcode'),
    }

    const { npm, profile, result } = await mockProfile(t, {
      npmProfile,
      qrcode,
      readUserInfo,
      config: { otp: '1234' },
    })

    // XXX: use mock registry
    npm.config.getCredentialsByURI = () => {
      return { token: 'token' }
    }

    await profile.exec(['enable-2fa', 'auth-only'])

    t.matchSnapshot(result(), 'should output 2fa enablement success msgs')
  })

  t.test('from token and set otp, retrieves invalid otp', async t => {
    const npmProfile = {
      async get () {
        return {
          ...userProfile,
          tfa: {
            pending: true,
          },
        }
      },
      async set () {
        return {
          ...userProfile,
          tfa: 'http://foo?secret=1234',
        }
      },
    }

    const readUserInfo = {
      async password () {
        return 'password1234'
      },
      async otp () {
        return '123456'
      },
    }

    const { npm, profile } = await mockProfile(t, {
      npmProfile,
      readUserInfo,
      config: { otp: '1234' },
    })

    npm.config.getCredentialsByURI = () => {
      return { token: 'token' }
    }

    await t.rejects(
      profile.exec(['enable-2fa', 'auth-only']),
      /Unknown error enabling two-factor authentication./,
      'should throw invalid 2fa auth url error'
    )
  })

  t.test('from token auth provides --otp config arg', async t => {
    const npmProfile = {
      async get () {
        return userProfile
      },
      async set () {
        return {
          ...userProfile,
          tfa: null,
        }
      },
    }

    const readUserInfo = {
      async password () {
        return 'password1234'
      },
      async otp () {
        throw new Error('should not ask for otp')
      },
    }

    const { npm, profile, result } = await mockProfile(t, {
      npmProfile,
      readUserInfo,
      config: { otp: '123456' },
    })

    npm.config.getCredentialsByURI = () => {
      return { token: 'token' }
    }

    await profile.exec(['enable-2fa', 'auth-and-writes'])

    t.equal(
      result(),
      'Two factor authentication mode changed to: auth-and-writes',
      'should output success msg'
    )
  })

  t.test('missing tfa from user profile', async t => {
    const npmProfile = {
      async get () {
        return {
          ...userProfile,
          tfa: undefined,
        }
      },
      async set () {
        return {
          ...userProfile,
          tfa: null,
        }
      },
    }

    const readUserInfo = {
      async password () {
        return 'password1234'
      },
      async otp () {
        return '123456'
      },
    }

    const { npm, profile, result } = await mockProfile(t, {
      npmProfile,
      readUserInfo,
    })

    npm.config.getCredentialsByURI = () => {
      return { token: 'token' }
    }

    await profile.exec(['enable-2fa', 'auth-only'])

    t.equal(
      result(),
      'Two factor authentication mode changed to: auth-only',
      'should output success msg'
    )
  })

  t.test('defaults to auth-and-writes permission if no mode specified', async t => {
    const npmProfile = {
      async get () {
        return {
          ...userProfile,
          tfa: undefined,
        }
      },
      async set () {
        return {
          ...userProfile,
          tfa: null,
        }
      },
    }

    const readUserInfo = {
      async password () {
        return 'password1234'
      },
      async otp () {
        return '123456'
      },
    }

    const { npm, profile, result } = await mockProfile(t, {
      npmProfile,
      readUserInfo,
    })

    npm.config.getCredentialsByURI = () => {
      return { token: 'token' }
    }

    await profile.exec(['enable-2fa'])
    t.equal(
      result(),
      'Two factor authentication mode changed to: auth-and-writes',
      'should enable 2fa with auth-and-writes permission'
    )
  })
})

t.test('disable-2fa', async t => {
  t.test('no tfa enabled', async t => {
    const npmProfile = {
      async get () {
        return {
          ...userProfile,
          tfa: null,
        }
      },
    }

    const { profile, result } = await mockProfile(t, {
      npmProfile,
    })

    await profile.exec(['disable-2fa'])
    t.equal(result(), 'Two factor authentication not enabled.',
      'should output already disalbed msg')
  })

  t.test('requests otp', async t => {
    const npmProfile = t => ({
      async get () {
        return userProfile
      },
      async set (newProfile) {
        t.same(
          newProfile,
          {
            tfa: {
              password: 'password1234',
              mode: 'disable',
            },
          },
          'should send the new info for setting in profile'
        )
      },
    })

    const readUserInfo = t => ({
      async password () {
        t.ok('should interactively ask for password confirmation')
        return 'password1234'
      },
      async otp (label) {
        t.equal(
          label,
          'Enter one-time password: ',
          'should ask for otp confirmation'
        )
        return '1234'
      },
    })

    t.test('default output', async t => {
      t.plan(4)

      const { profile, result } = await mockProfile(t, {
        npmProfile: npmProfile(t),
        readUserInfo: readUserInfo(t),
      })

      await profile.exec(['disable-2fa'])
      t.equal(result(), 'Two factor authentication disabled.', 'should output already disabled msg')
    })

    t.test('--json', async t => {
      t.plan(4)

      const config = { json: true }

      const { profile, result } = await mockProfile(t, {
        npmProfile: npmProfile(t),
        readUserInfo: readUserInfo(t),
        config,
      })

      await profile.exec(['disable-2fa'])

      t.same(JSON.parse(result()), { tfa: false }, 'should output json already disabled msg')
    })

    t.test('--parseable', async t => {
      t.plan(4)

      const config = { parseable: true }

      const { profile, result } = await mockProfile(t, {
        npmProfile: npmProfile(t),
        readUserInfo: readUserInfo(t),
        config,
      })

      await profile.exec(['disable-2fa'])

      t.equal(result(), 'tfa\tfalse', 'should output parseable already disabled msg')
    })
  })

  t.test('--otp config already set', async t => {
    t.plan(2)

    const npmProfile = {
      async get () {
        return userProfile
      },
      async set (newProfile) {
        t.same(
          newProfile,
          {
            tfa: {
              password: 'password1234',
              mode: 'disable',
            },
          },
          'should send the new info for setting in profile'
        )
      },
    }

    const readUserInfo = {
      async password () {
        return 'password1234'
      },
      async otp () {
        throw new Error('should not ask for otp')
      },
    }

    const { profile, result } = await mockProfile(t, {
      npmProfile,
      readUserInfo,
      config: { otp: '123456' },
    })

    await profile.exec(['disable-2fa'])

    t.equal(result(), 'Two factor authentication disabled.', 'should output already disalbed msg')
  })
})

t.test('unknown subcommand', async t => {
  const { profile } = await mockProfile(t)

  await t.rejects(
    profile.exec(['asfd']),
    /Unknown profile command: asfd/,
    'should throw unknown cmd error'
  )
})

t.test('completion', async t => {
  const testComp = async (t, { argv, expect, title } = {}) => {
    const { profile } = await mockProfile(t)
    t.resolveMatch(profile.completion({ conf: { argv: { remain: argv } } }), expect, title)
  }

  t.test('npm profile autocomplete', async t => {
    await testComp(t, {
      argv: ['npm', 'profile'],
      expect: ['enable-2fa', 'disable-2fa', 'get', 'set'],
      title: 'should auto complete with subcommands',
    })
  })

  t.test('npm profile enable autocomplete', async t => {
    await testComp(t, {
      argv: ['npm', 'profile', 'enable-2fa'],
      expect: ['auth-and-writes', 'auth-only'],
      title: 'should auto complete with auth types',
    })
  })

  t.test('npm profile <subcmd> no autocomplete', async t => {
    const noAutocompleteCmds = ['disable-2fa', 'disable-tfa', 'get', 'set']
    for (const subcmd of noAutocompleteCmds) {
      await t.test(subcmd, t => testComp(t, {
        argv: ['npm', 'profile', subcmd],
        expect: [],
        title: `${subcmd} should have no autocomplete`,
      }))
    }
  })

  t.test('npm profile unknown subcommand autocomplete', async t => {
    const { profile } = await mockProfile(t)
    t.rejects(
      profile.completion({ conf: { argv: { remain: ['npm', 'profile', 'asdf'] } } }),
      { message: 'asdf not recognized' },
      'should throw unknown cmd error'
    )
  })
})
