// Each command has a completion function that takes an options object and a cb
// The callback gets called with an error and an array of possible completions.
// The options object is built up based on the environment variables set by
// zsh or bash when calling a function for completion, based on the cursor
// position and the command line thus far.  These are:
// COMP_CWORD: the index of the "word" in the command line being completed
// COMP_LINE: the full command line thusfar as a string
// COMP_POINT: the cursor index at the point of triggering completion
//
// We parse the command line with nopt, like npm does, and then create an
// options object containing:
// words: array of words in the command line
// w: the index of the word being completed (ie, COMP_CWORD)
// word: the word being completed
// line: the COMP_LINE
// lineLength
// point: the COMP_POINT, usually equal to line length, but not always, eg if
// the user has pressed the left-arrow to complete an earlier word
// partialLine: the line up to the point
// partialWord: the word being completed (which might be ''), up to the point
// conf: a nopt parse of the command line
//
// When the implementation completion method returns its list of strings,
// and arrays of strings, we filter that by any that start with the
// partialWord, since only those can possibly be valid matches.
//
// Matches are wrapped with ' to escape them, if necessary, and then printed
// one per line for the shell completion method to consume in IFS=$'\n' mode
// as an array.

const fs = require('node:fs/promises')
const nopt = require('nopt')
const { resolve } = require('node:path')
const { output } = require('proc-log')
const Npm = require('../npm.js')
const { definitions, shorthands } = require('@npmcli/config/lib/definitions')
const { commands, aliases, deref } = require('../utils/cmd-list.js')
const { isWindowsShell } = require('../utils/is-windows.js')
const BaseCommand = require('../base-cmd.js')

const fileExists = (file) => fs.stat(file).then(s => s.isFile()).catch(() => false)

const configNames = Object.keys(definitions)
const shorthandNames = Object.keys(shorthands)
const allConfs = configNames.concat(shorthandNames)

class Completion extends BaseCommand {
  static description = 'Tab Completion for npm'
  static name = 'completion'

  // completion for the completion command
  static async completion (opts) {
    if (opts.w > 2) {
      return
    }

    const [bashExists, zshExists] = await Promise.all([
      fileExists(resolve(process.env.HOME, '.bashrc')),
      fileExists(resolve(process.env.HOME, '.zshrc')),
    ])
    const out = []
    if (zshExists) {
      out.push(['>>', '~/.zshrc'])
    }

    if (bashExists) {
      out.push(['>>', '~/.bashrc'])
    }

    return out
  }

  async exec (args) {
    if (isWindowsShell) {
      const msg = 'npm completion supported only in MINGW / Git bash on Windows'
      throw Object.assign(new Error(msg), {
        code: 'ENOTSUP',
      })
    }

    const { COMP_CWORD, COMP_LINE, COMP_POINT, COMP_FISH } = process.env

    // if the COMP_* isn't in the env, then just dump the script.
    if (COMP_CWORD === undefined || COMP_LINE === undefined || COMP_POINT === undefined) {
      return dumpScript(resolve(this.npm.npmRoot, 'lib', 'utils', 'completion.sh'))
    }

    // ok we're actually looking at the envs and outputting the suggestions
    // get the partial line and partial word,
    // if the point isn't at the end.
    // ie, tabbing at: npm foo b|ar
    const w = +COMP_CWORD
    const words = args.map(unescape)
    const word = words[w]
    const line = COMP_LINE
    const point = +COMP_POINT
    const partialLine = line.slice(0, point)
    const partialWords = words.slice(0, w)

    // figure out where in that last word the point is.
    const partialWordRaw = args[w]
    let i = partialWordRaw.length
    while (partialWordRaw.slice(0, i) !== partialLine.slice(-1 * i) && i > 0) {
      i--
    }

    const partialWord = unescape(partialWordRaw.slice(0, i))
    partialWords.push(partialWord)

    const opts = {
      isFish: COMP_FISH === 'true',
      words,
      w,
      word,
      line,
      lineLength: line.length,
      point,
      partialLine,
      partialWords,
      partialWord,
      raw: args,
    }

    if (partialWords.slice(0, -1).indexOf('--') === -1) {
      if (word.charAt(0) === '-') {
        return this.wrap(opts, configCompl(opts))
      }

      if (words[w - 1] &&
        words[w - 1].charAt(0) === '-' &&
        !isFlag(words[w - 1])) {
        // awaiting a value for a non-bool config.
        // don't even try to do this for now
        return this.wrap(opts, configValueCompl(opts))
      }
    }

    // try to find the npm command.
    // it's the first thing after all the configs.
    // take a little shortcut and use npm's arg parsing logic.
    // don't have to worry about the last arg being implicitly
    // boolean'ed, since the last block will catch that.
    const types = Object.entries(definitions).reduce((acc, [key, def]) => {
      acc[key] = def.type
      return acc
    }, {})
    const parsed = opts.conf =
      nopt(types, shorthands, partialWords.slice(0, -1), 0)
    // check if there's a command already.
    const cmd = parsed.argv.remain[1]
    if (!cmd) {
      return this.wrap(opts, cmdCompl(opts, this.npm))
    }

    Object.keys(parsed).forEach(k => this.npm.config.set(k, parsed[k]))

    // at this point, if words[1] is some kind of npm command,
    // then complete on it.
    // otherwise, do nothing
    try {
      const { completion } = Npm.cmd(cmd)
      if (completion) {
        const comps = await completion(opts, this.npm)
        return this.wrap(opts, comps)
      }
    } catch {
      // it wasnt a valid command, so do nothing
    }
  }

  // The command should respond with an array.  Loop over that,
  // wrapping quotes around any that have spaces, and writing
  // them to stdout.
  // If any of the items are arrays, then join them with a space.
  // Ie, returning ['a', 'b c', ['d', 'e']] would allow it to expand
  // to: 'a', 'b c', or 'd' 'e'
  wrap (opts, compls) {
    // TODO this was dead code, leaving it in case we find some command we
    // forgot that requires this. if so *that command should fix its
    // completions*
    // compls = compls.map(w => !/\s+/.test(w) ? w : '\'' + w + '\'')

    if (opts.partialWord) {
      compls = compls.filter(c => c.startsWith(opts.partialWord))
    }

    if (compls.length > 0) {
      output.standard(compls.join('\n'))
    }
  }
}

const dumpScript = async (p) => {
  const d = (await fs.readFile(p, 'utf8')).replace(/^#!.*?\n/, '')
  await new Promise((res, rej) => {
    let done = false
    process.stdout.on('error', er => {
      if (done) {
        return
      }

      done = true

      // Darwin is a pain sometimes.
      //
      // This is necessary because the "source" or "." program in
      // bash on OS X closes its file argument before reading
      // from it, meaning that you get exactly 1 write, which will
      // work most of the time, and will always raise an EPIPE.
      //
      // Really, one should not be tossing away EPIPE errors, or any
      // errors, so casually.  But, without this, `. <(npm completion)`
      // can never ever work on OS X.
      // TODO Ignoring coverage, see 'non EPIPE errors cause failures' test.
      /* istanbul ignore next */
      if (er.errno === 'EPIPE') {
        res()
      } else {
        rej(er)
      }
    })

    process.stdout.write(d, () => {
      if (done) {
        return
      }

      done = true
      res()
    })
  })
}

const unescape = w => w.charAt(0) === '\'' ? w.replace(/^'|'$/g, '')
  : w.replace(/\\ /g, ' ')

// the current word has a dash.  Return the config names,
// with the same number of dashes as the current word has.
const configCompl = opts => {
  const word = opts.word
  const split = word.match(/^(-+)((?:no-)*)(.*)$/)
  const dashes = split[1]
  const no = split[2]
  const flags = configNames.filter(isFlag)
  return allConfs.map(c => dashes + c)
    .concat(flags.map(f => dashes + (no || 'no-') + f))
}

// expand with the valid values of various config values.
// not yet implemented.
const configValueCompl = () => []

// check if the thing is a flag or not.
const isFlag = word => {
  // shorthands never take args.
  const split = word.match(/^(-*)((?:no-)+)?(.*)$/)
  const no = split[2]
  const conf = split[3]
  const { type } = definitions[conf]
  return no ||
    type === Boolean ||
    (Array.isArray(type) && type.includes(Boolean)) ||
    shorthands[conf]
}

// complete against the npm commands
// if they all resolve to the same thing, just return the thing it already is
const cmdCompl = (opts) => {
  const allCommands = commands.concat(Object.keys(aliases))
  const matches = allCommands.filter(c => c.startsWith(opts.partialWord))
  if (!matches.length) {
    return matches
  }

  const derefs = new Set([...matches.map(c => deref(c))])
  if (derefs.size === 1) {
    return [...derefs]
  }

  return allCommands
}

module.exports = Completion
