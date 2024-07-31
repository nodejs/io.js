const { open } = require('@npmcli/promise-spawn')
const { output, input } = require('proc-log')
const { URL } = require('node:url')
const readline = require('node:readline/promises')
const { once } = require('node:events')

const assertValidUrl = (url) => {
  try {
    if (!/^https?:$/.test(new URL(url).protocol)) {
      throw new Error()
    }
  } catch {
    throw new Error('Invalid URL: ' + url)
  }
}

const outputMsg = (json, title, url) => {
  if (json) {
    output.buffer({ title, url })
  } else {
    output.standard(`${title}:\n${url}`)
  }
}

// attempt to open URL in web-browser, print address otherwise:
const openUrl = async (npm, url, title, isFile) => {
  url = encodeURI(url)
  const browser = npm.config.get('browser')
  const json = npm.config.get('json')

  if (browser === false) {
    outputMsg(json, title, url)
    return
  }

  // We pass this in as true from the help command so we know we don't have to
  // check the protocol
  if (!isFile) {
    assertValidUrl(url)
  }

  try {
    await input.start(() => open(url, {
      command: browser === true ? null : browser,
    }))
  } catch (err) {
    if (err.code !== 127) {
      throw err
    }
    outputMsg(json, title, url)
  }
}

// Prompt to open URL in browser if possible
const openUrlPrompt = async (npm, url, title, prompt, { signal }) => {
  const browser = npm.config.get('browser')
  const json = npm.config.get('json')

  assertValidUrl(url)
  outputMsg(json, title, url)

  if (browser === false || !process.stdin.isTTY || !process.stdout.isTTY) {
    return
  }

  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
  })

  try {
    await input.read(() => Promise.race([
      rl.question(prompt, { signal }),
      once(rl, 'error'),
      once(rl, 'SIGINT').then(() => {
        throw new Error('canceled')
      }),
    ]))
    rl.close()
    await openUrl(npm, url, 'Browser unavailable. Please open the URL manually')
  } catch (err) {
    rl.close()
    if (err.name !== 'AbortError') {
      throw err
    }
  }
}

// Rearrange arguments and return a function that takes the two arguments
// returned from the npm-profile methods that take an opener
const createOpener = (npm, title, prompt = 'Press ENTER to open in the browser...') =>
  (url, opts) => openUrlPrompt(npm, url, title, prompt, opts)

module.exports = {
  openUrl,
  openUrlPrompt,
  createOpener,
}
