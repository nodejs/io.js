const { read: _read } = require('read')
const userValidate = require('npm-user-validate')
const { log, input } = require('proc-log')

const otpPrompt = `This command requires a one-time password (OTP) from your authenticator app.
Enter one below. You can also pass one on the command line by appending --otp=123456.
For more information, see:
https://docs.npmjs.com/getting-started/using-two-factor-authentication
Enter OTP: `
const passwordPrompt = 'npm password: '
const usernamePrompt = 'npm username: '
const emailPrompt = 'email (this IS public): '

const read = (...args) => input.read(() => _read(...args))

function readOTP (msg = otpPrompt, otp, isRetry) {
  if (isRetry && otp && /^[\d ]+$|^[A-Fa-f0-9]{64,64}$/.test(otp)) {
    return otp.replace(/\s+/g, '')
  }

  return read({ prompt: msg, default: otp || '' })
    .then((rOtp) => readOTP(msg, rOtp, true))
}

function readPassword (msg = passwordPrompt, password, isRetry) {
  if (isRetry && password) {
    return password
  }

  return read({ prompt: msg, silent: true, default: password || '' })
    .then((rPassword) => readPassword(msg, rPassword, true))
}

function readUsername (msg = usernamePrompt, username, isRetry) {
  if (isRetry && username) {
    const error = userValidate.username(username)
    if (error) {
      log.warn(error.message)
    } else {
      return Promise.resolve(username.trim())
    }
  }

  return read({ prompt: msg, default: username || '' })
    .then((rUsername) => readUsername(msg, rUsername, true))
}

function readEmail (msg = emailPrompt, email, isRetry) {
  if (isRetry && email) {
    const error = userValidate.email(email)
    if (error) {
      log.warn(error.message)
    } else {
      return email.trim()
    }
  }

  return read({ prompt: msg, default: email || '' })
    .then((username) => readEmail(msg, username, true))
}

module.exports = {
  otp: readOTP,
  password: readPassword,
  username: readUsername,
  email: readEmail,
}
