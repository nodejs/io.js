// Originally normalize-package-data

const url = require('node:url')
const hostedGitInfo = require('hosted-git-info')
const validateLicense = require('validate-npm-package-license')

const typos = {
  dependancies: 'dependencies',
  dependecies: 'dependencies',
  depdenencies: 'dependencies',
  devEependencies: 'devDependencies',
  depends: 'dependencies',
  'dev-dependencies': 'devDependencies',
  devDependences: 'devDependencies',
  devDepenencies: 'devDependencies',
  devdependencies: 'devDependencies',
  repostitory: 'repository',
  repo: 'repository',
  prefereGlobal: 'preferGlobal',
  hompage: 'homepage',
  hampage: 'homepage',
  autohr: 'author',
  autor: 'author',
  contributers: 'contributors',
  publicationConfig: 'publishConfig',
  script: 'scripts',
}

const isEmail = str => str.includes('@') && (str.indexOf('@') < str.lastIndexOf('.'))

// Extracts description from contents of a readme file in markdown format
function extractDescription (description) {
  // the first block of text before the first heading that isn't the first line heading
  const lines = description.trim().split('\n')
  let start = 0
  // skip initial empty lines and lines that start with #
  while (lines[start]?.trim().match(/^(#|$)/)) {
    start++
  }
  let end = start + 1
  // keep going till we get to the end or an empty line
  while (end < lines.length && lines[end].trim()) {
    end++
  }
  return lines.slice(start, end).join(' ').trim()
}

function stringifyPerson (person) {
  if (typeof person !== 'string') {
    const name = person.name || ''
    const u = person.url || person.web
    const wrappedUrl = u ? (' (' + u + ')') : ''
    const e = person.email || person.mail
    const wrappedEmail = e ? (' <' + e + '>') : ''
    person = name + wrappedEmail + wrappedUrl
  }
  const matchedName = person.match(/^([^(<]+)/)
  const matchedUrl = person.match(/\(([^()]+)\)/)
  const matchedEmail = person.match(/<([^<>]+)>/)
  const parsed = {}
  if (matchedName?.[0].trim()) {
    parsed.name = matchedName[0].trim()
  }
  if (matchedEmail) {
    parsed.email = matchedEmail[1]
  }
  if (matchedUrl) {
    parsed.url = matchedUrl[1]
  }
  return parsed
}

function normalizeData (data, changes) {
  // fixDescriptionField
  if (data.description && typeof data.description !== 'string') {
    changes?.push(`'description' field should be a string`)
    delete data.description
  }
  if (data.readme && !data.description && data.readme !== 'ERROR: No README data found!') {
    data.description = extractDescription(data.readme)
  }
  if (data.description === undefined) {
    delete data.description
  }
  if (!data.description) {
    changes?.push('No description')
  }

  // fixModulesField
  if (data.modules) {
    changes?.push(`modules field is deprecated`)
    delete data.modules
  }

  // fixFilesField
  const files = data.files
  if (files && !Array.isArray(files)) {
    changes?.push(`Invalid 'files' member`)
    delete data.files
  } else if (data.files) {
    data.files = data.files.filter(function (file) {
      if (!file || typeof file !== 'string') {
        changes?.push(`Invalid filename in 'files' list: ${file}`)
        return false
      } else {
        return true
      }
    })
  }

  // fixManField
  if (data.man && typeof data.man === 'string') {
    data.man = [data.man]
  }

  // fixBugsField
  if (!data.bugs && data.repository?.url) {
    const hosted = hostedGitInfo.fromUrl(data.repository.url)
    if (hosted && hosted.bugs()) {
      data.bugs = { url: hosted.bugs() }
    }
  } else if (data.bugs) {
    if (typeof data.bugs === 'string') {
      if (isEmail(data.bugs)) {
        data.bugs = { email: data.bugs }
        /* eslint-disable-next-line node/no-deprecated-api */
      } else if (url.parse(data.bugs).protocol) {
        data.bugs = { url: data.bugs }
      } else {
        changes?.push(`Bug string field must be url, email, or {email,url}`)
      }
    } else {
      for (const k in data.bugs) {
        if (['web', 'name'].includes(k)) {
          changes?.push(`bugs['${k}'] should probably be bugs['url'].`)
          data.bugs.url = data.bugs[k]
          delete data.bugs[k]
        }
      }
      const oldBugs = data.bugs
      data.bugs = {}
      if (oldBugs.url) {
        /* eslint-disable-next-line node/no-deprecated-api */
        if (typeof (oldBugs.url) === 'string' && url.parse(oldBugs.url).protocol) {
          data.bugs.url = oldBugs.url
        } else {
          changes?.push('bugs.url field must be a string url. Deleted.')
        }
      }
      if (oldBugs.email) {
        if (typeof (oldBugs.email) === 'string' && isEmail(oldBugs.email)) {
          data.bugs.email = oldBugs.email
        } else {
          changes?.push('bugs.email field must be a string email. Deleted.')
        }
      }
    }
    if (!data.bugs.email && !data.bugs.url) {
      delete data.bugs
      changes?.push('Normalized value of bugs field is an empty object. Deleted.')
    }
  }
  // fixKeywordsField
  if (typeof data.keywords === 'string') {
    data.keywords = data.keywords.split(/,\s+/)
  }
  if (data.keywords && !Array.isArray(data.keywords)) {
    delete data.keywords
    changes?.push(`keywords should be an array of strings`)
  } else if (data.keywords) {
    data.keywords = data.keywords.filter(function (kw) {
      if (typeof kw !== 'string' || !kw) {
        changes?.push(`keywords should be an array of strings`)
        return false
      } else {
        return true
      }
    })
  }
  // fixBundleDependenciesField
  const bdd = 'bundledDependencies'
  const bd = 'bundleDependencies'
  if (data[bdd] && !data[bd]) {
    data[bd] = data[bdd]
    delete data[bdd]
  }
  if (data[bd] && !Array.isArray(data[bd])) {
    changes?.push(`Invalid 'bundleDependencies' list. Must be array of package names`)
    delete data[bd]
  } else if (data[bd]) {
    data[bd] = data[bd].filter(function (filtered) {
      if (!filtered || typeof filtered !== 'string') {
        changes?.push(`Invalid bundleDependencies member: ${filtered}`)
        return false
      } else {
        if (!data.dependencies) {
          data.dependencies = {}
        }
        if (!Object.prototype.hasOwnProperty.call(data.dependencies, filtered)) {
          changes?.push(`Non-dependency in bundleDependencies: ${filtered}`)
          data.dependencies[filtered] = '*'
        }
        return true
      }
    })
  }
  // fixHomepageField
  if (!data.homepage && data.repository && data.repository.url) {
    const hosted = hostedGitInfo.fromUrl(data.repository.url)
    if (hosted) {
      data.homepage = hosted.docs()
    }
  }
  if (data.homepage) {
    if (typeof data.homepage !== 'string') {
      changes?.push('homepage field must be a string url. Deleted.')
      delete data.homepage
    } else {
      /* eslint-disable-next-line node/no-deprecated-api */
      if (!url.parse(data.homepage).protocol) {
        data.homepage = 'http://' + data.homepage
      }
    }
  }
  // fixReadmeField
  if (!data.readme) {
    changes?.push('No README data')
    data.readme = 'ERROR: No README data found!'
  }
  // fixLicenseField
  const license = data.license || data.licence
  if (!license) {
    changes?.push('No license field.')
  } else if (typeof (license) !== 'string' || license.length < 1 || license.trim() === '') {
    changes?.push('license should be a valid SPDX license expression')
  } else if (!validateLicense(license).validForNewPackages) {
    changes?.push('license should be a valid SPDX license expression')
  }
  // fixPeople
  if (data.author) {
    data.author = stringifyPerson(data.author)
  }
  ['maintainers', 'contributors'].forEach(function (set) {
    if (!Array.isArray(data[set])) {
      return
    }
    data[set] = data[set].map(stringifyPerson)
  })
  // fixTypos
  for (const d in typos) {
    if (Object.prototype.hasOwnProperty.call(data, d)) {
      changes?.push(`${d} should probably be ${typos[d]}.`)
    }
  }
}

module.exports = { normalizeData }
