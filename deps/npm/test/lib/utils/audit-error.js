const t = require('tap')
const mockNpm = require('../../fixtures/mock-npm')
const tmock = require('../../fixtures/tmock')

const auditError = async (t, { command, error, ...config } = {}) => {
  const mockAuditError = tmock(t, '{LIB}/utils/audit-error')

  const mock = await mockNpm(t, {
    command,
    config,
    exec: true,
    prefixDir: { 'package.json': '{}', 'package-lock.json': '{}' },
  })

  const res = {}
  try {
    res.result = mockAuditError(mock.npm, error ? { error } : {})
  } catch (err) {
    res.error = err
  }

  mock.npm.finish()

  return {
    ...res,
    logs: mock.logs.warn.byTitle('audit'),
    output: mock.joinedOutput(),
  }
}

t.test('no error, not audit command', async t => {
  const { result, error, logs, output } = await auditError(t, { command: 'install' })

  t.equal(result, false, 'no error')
  t.notOk(error, 'no error')

  t.match(output.trim(), /up to date/, 'install output')
  t.match(output.trim(), /found 0 vulnerabilities/, 'install output')
  t.strictSame(logs, [], 'no warnings')
})

t.test('error, not audit command', async t => {
  const { result, error, logs, output } = await auditError(t, {
    command: 'install',
    error: {
      message: 'message',
      body: Buffer.from('body'),
      method: 'POST',
      uri: 'https://example.com/not/a/registry',
      headers: {
        head: ['ers'],
      },
      statusCode: '420',
    },
  })

  t.equal(result, true, 'had error')
  t.notOk(error, 'no error')
  t.match(output.trim(), /up to date/, 'install output')
  t.match(output.trim(), /found 0 vulnerabilities/, 'install output')
  t.strictSame(logs, [], 'no warnings')
})

t.test('error, audit command, not json', async t => {
  const { result, error, logs, output } = await auditError(t, {
    command: 'audit',
    error: {
      message: 'message',
      body: Buffer.from('body error text'),
      method: 'POST',
      uri: 'https://example.com/not/a/registry',
      headers: {
        head: ['ers'],
      },
      statusCode: '420',
    },
  })

  t.equal(result, undefined)

  t.ok(error, 'throws error')
  t.match(output, 'body error text', 'some output')
  t.strictSame(logs, ['audit message'], 'some warnings')
})

t.test('error, audit command, json', async t => {
  const { result, error, logs, output } = await auditError(t, {
    json: true,
    command: 'audit',
    error: {
      message: 'message',
      body: { response: 'body' },
      method: 'POST',
      uri: 'https://username:password@example.com/not/a/registry',
      headers: {
        head: ['ers'],
      },
      statusCode: '420',
    },
  })

  t.equal(result, undefined)
  t.ok(error, 'throws error')
  t.match(output,
    '{\n' +
      '  "message": "message",\n' +
      '  "method": "POST",\n' +
      '  "uri": "https://username:***@example.com/not/a/registry",\n' +
      '  "headers": {\n' +
      '    "head": [\n' +
      '      "ers"\n' +
      '    ]\n' +
      '  },\n' +
      '  "statusCode": "420",\n' +
      '  "body": {\n' +
      '    "response": "body"\n' +
      '  }\n' +
      '}'
    , 'some output')
  t.strictSame(logs, ['audit message'], 'some warnings')
})
