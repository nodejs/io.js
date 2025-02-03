const t = require('tap')
const procLog = require('proc-log')
const tmock = require('../../fixtures/tmock')

let readOpts = null
let readResult = null
let logMsg = null

const readUserInfo = tmock(t, '{LIB}/utils/read-user-info.js', {
  read: {
    read: async (opts) => {
      readOpts = opts
      return readResult
    },
  },
  'proc-log': {
    ...procLog,
    log: {
      ...procLog.log,
      warn: (msg) => logMsg = msg,
    },
    input: {
      ...procLog.input,
      read: (fn) => fn(),
    },
  },
  'npm-user-validate': {
    username: (username) => {
      if (username === 'invalid') {
        return new Error('invalid username')
      }

      return null
    },
    email: (email) => {
      if (email.startsWith('invalid')) {
        return new Error('invalid email')
      }

      return null
    },
  },
})

t.beforeEach(() => {
  logMsg = null
})

t.test('otp', async (t) => {
  readResult = '1234'
  t.teardown(() => {
    readResult = null
    readOpts = null
  })
  const result = await readUserInfo.otp()
  t.equal(result, '1234', 'received the otp')
})

t.test('password', async (t) => {
  readResult = 'password'
  t.teardown(() => {
    readResult = null
    readOpts = null
  })
  const result = await readUserInfo.password()
  t.equal(result, 'password', 'received the password')
  t.match(readOpts, {
    silent: true,
  }, 'got the correct options')
})

t.test('username', async (t) => {
  readResult = 'username'
  t.teardown(() => {
    readResult = null
    readOpts = null
  })
  const result = await readUserInfo.username()
  t.equal(result, 'username', 'received the username')
})

t.test('username - invalid warns and retries', async (t) => {
  readResult = 'invalid'
  t.teardown(() => {
    readResult = null
    readOpts = null
  })

  const pResult = readUserInfo.username(null, null)
  // have to swap it to a valid username after execution starts
  // or it will loop forever
  readResult = 'valid'
  const result = await pResult
  t.equal(result, 'valid', 'received the username')
  t.equal(logMsg, 'invalid username')
})

t.test('email', async (t) => {
  readResult = 'foo@bar.baz'
  t.teardown(() => {
    readResult = null
    readOpts = null
  })
  const result = await readUserInfo.email()
  t.equal(result, 'foo@bar.baz', 'received the email')
})

t.test('email - invalid warns and retries', async (t) => {
  readResult = 'invalid@bar.baz'
  t.teardown(() => {
    readResult = null
    readOpts = null
  })

  const pResult = readUserInfo.email(null, null)
  readResult = 'foo@bar.baz'
  const result = await pResult
  t.equal(result, 'foo@bar.baz', 'received the email')
  t.equal(logMsg, 'invalid email')
})
