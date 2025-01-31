/* eslint-disable max-len */

'use strict'

const maybeJoin = (...args) => args.every(arg => arg) ? args.join('') : ''
const maybeEncode = (arg) => arg ? encodeURIComponent(arg) : ''
const formatHashFragment = (f) => f.toLowerCase()
  .replace(/^\W+/g, '') // strip leading non-characters
  .replace(/(?<!\W)\W+$/, '') // strip trailing non-characters
  .replace(/\//g, '') // strip all slashes
  .replace(/\W+/g, '-') // replace remaining non-characters with '-'

const defaults = {
  sshtemplate: ({ domain, user, project, committish }) =>
    `git@${domain}:${user}/${project}.git${maybeJoin('#', committish)}`,
  sshurltemplate: ({ domain, user, project, committish }) =>
    `git+ssh://git@${domain}/${user}/${project}.git${maybeJoin('#', committish)}`,
  edittemplate: ({ domain, user, project, committish, editpath, path }) =>
    `https://${domain}/${user}/${project}${maybeJoin('/', editpath, '/', maybeEncode(committish || 'HEAD'), '/', path)}`,
  browsetemplate: ({ domain, user, project, committish, treepath }) =>
    `https://${domain}/${user}/${project}${maybeJoin('/', treepath, '/', maybeEncode(committish))}`,
  browsetreetemplate: ({ domain, user, project, committish, treepath, path, fragment, hashformat }) =>
    `https://${domain}/${user}/${project}/${treepath}/${maybeEncode(committish || 'HEAD')}/${path}${maybeJoin('#', hashformat(fragment || ''))}`,
  browseblobtemplate: ({ domain, user, project, committish, blobpath, path, fragment, hashformat }) =>
    `https://${domain}/${user}/${project}/${blobpath}/${maybeEncode(committish || 'HEAD')}/${path}${maybeJoin('#', hashformat(fragment || ''))}`,
  docstemplate: ({ domain, user, project, treepath, committish }) =>
    `https://${domain}/${user}/${project}${maybeJoin('/', treepath, '/', maybeEncode(committish))}#readme`,
  httpstemplate: ({ auth, domain, user, project, committish }) =>
    `git+https://${maybeJoin(auth, '@')}${domain}/${user}/${project}.git${maybeJoin('#', committish)}`,
  filetemplate: ({ domain, user, project, committish, path }) =>
    `https://${domain}/${user}/${project}/raw/${maybeEncode(committish || 'HEAD')}/${path}`,
  shortcuttemplate: ({ type, user, project, committish }) =>
    `${type}:${user}/${project}${maybeJoin('#', committish)}`,
  pathtemplate: ({ user, project, committish }) =>
    `${user}/${project}${maybeJoin('#', committish)}`,
  bugstemplate: ({ domain, user, project }) =>
    `https://${domain}/${user}/${project}/issues`,
  hashformat: formatHashFragment,
}

const hosts = {}
hosts.github = {
  // First two are insecure and generally shouldn't be used any more, but
  // they are still supported.
  protocols: ['git:', 'http:', 'git+ssh:', 'git+https:', 'ssh:', 'https:'],
  domain: 'github.com',
  treepath: 'tree',
  blobpath: 'blob',
  editpath: 'edit',
  filetemplate: ({ auth, user, project, committish, path }) =>
    `https://${maybeJoin(auth, '@')}raw.githubusercontent.com/${user}/${project}/${maybeEncode(committish || 'HEAD')}/${path}`,
  gittemplate: ({ auth, domain, user, project, committish }) =>
    `git://${maybeJoin(auth, '@')}${domain}/${user}/${project}.git${maybeJoin('#', committish)}`,
  tarballtemplate: ({ domain, user, project, committish }) =>
    `https://codeload.${domain}/${user}/${project}/tar.gz/${maybeEncode(committish || 'HEAD')}`,
  extract: (url) => {
    let [, user, project, type, committish] = url.pathname.split('/', 5)
    if (type && type !== 'tree') {
      return
    }

    if (!type) {
      committish = url.hash.slice(1)
    }

    if (project && project.endsWith('.git')) {
      project = project.slice(0, -4)
    }

    if (!user || !project) {
      return
    }

    return { user, project, committish }
  },
}

hosts.bitbucket = {
  protocols: ['git+ssh:', 'git+https:', 'ssh:', 'https:'],
  domain: 'bitbucket.org',
  treepath: 'src',
  blobpath: 'src',
  editpath: '?mode=edit',
  edittemplate: ({ domain, user, project, committish, treepath, path, editpath }) =>
    `https://${domain}/${user}/${project}${maybeJoin('/', treepath, '/', maybeEncode(committish || 'HEAD'), '/', path, editpath)}`,
  tarballtemplate: ({ domain, user, project, committish }) =>
    `https://${domain}/${user}/${project}/get/${maybeEncode(committish || 'HEAD')}.tar.gz`,
  extract: (url) => {
    let [, user, project, aux] = url.pathname.split('/', 4)
    if (['get'].includes(aux)) {
      return
    }

    if (project && project.endsWith('.git')) {
      project = project.slice(0, -4)
    }

    if (!user || !project) {
      return
    }

    return { user, project, committish: url.hash.slice(1) }
  },
}

hosts.gitlab = {
  protocols: ['git+ssh:', 'git+https:', 'ssh:', 'https:'],
  domain: 'gitlab.com',
  treepath: 'tree',
  blobpath: 'tree',
  editpath: '-/edit',
  httpstemplate: ({ auth, domain, user, project, committish }) =>
    `git+https://${maybeJoin(auth, '@')}${domain}/${user}/${project}.git${maybeJoin('#', committish)}`,
  tarballtemplate: ({ domain, user, project, committish }) =>
    `https://${domain}/${user}/${project}/repository/archive.tar.gz?ref=${maybeEncode(committish || 'HEAD')}`,
  extract: (url) => {
    const path = url.pathname.slice(1)
    if (path.includes('/-/') || path.includes('/archive.tar.gz')) {
      return
    }

    const segments = path.split('/')
    let project = segments.pop()
    if (project.endsWith('.git')) {
      project = project.slice(0, -4)
    }

    const user = segments.join('/')
    if (!user || !project) {
      return
    }

    return { user, project, committish: url.hash.slice(1) }
  },
}

hosts.gist = {
  protocols: ['git:', 'git+ssh:', 'git+https:', 'ssh:', 'https:'],
  domain: 'gist.github.com',
  editpath: 'edit',
  sshtemplate: ({ domain, project, committish }) =>
    `git@${domain}:${project}.git${maybeJoin('#', committish)}`,
  sshurltemplate: ({ domain, project, committish }) =>
    `git+ssh://git@${domain}/${project}.git${maybeJoin('#', committish)}`,
  edittemplate: ({ domain, user, project, committish, editpath }) =>
    `https://${domain}/${user}/${project}${maybeJoin('/', maybeEncode(committish))}/${editpath}`,
  browsetemplate: ({ domain, project, committish }) =>
    `https://${domain}/${project}${maybeJoin('/', maybeEncode(committish))}`,
  browsetreetemplate: ({ domain, project, committish, path, hashformat }) =>
    `https://${domain}/${project}${maybeJoin('/', maybeEncode(committish))}${maybeJoin('#', hashformat(path))}`,
  browseblobtemplate: ({ domain, project, committish, path, hashformat }) =>
    `https://${domain}/${project}${maybeJoin('/', maybeEncode(committish))}${maybeJoin('#', hashformat(path))}`,
  docstemplate: ({ domain, project, committish }) =>
    `https://${domain}/${project}${maybeJoin('/', maybeEncode(committish))}`,
  httpstemplate: ({ domain, project, committish }) =>
    `git+https://${domain}/${project}.git${maybeJoin('#', committish)}`,
  filetemplate: ({ user, project, committish, path }) =>
    `https://gist.githubusercontent.com/${user}/${project}/raw${maybeJoin('/', maybeEncode(committish))}/${path}`,
  shortcuttemplate: ({ type, project, committish }) =>
    `${type}:${project}${maybeJoin('#', committish)}`,
  pathtemplate: ({ project, committish }) =>
    `${project}${maybeJoin('#', committish)}`,
  bugstemplate: ({ domain, project }) =>
    `https://${domain}/${project}`,
  gittemplate: ({ domain, project, committish }) =>
    `git://${domain}/${project}.git${maybeJoin('#', committish)}`,
  tarballtemplate: ({ project, committish }) =>
    `https://codeload.github.com/gist/${project}/tar.gz/${maybeEncode(committish || 'HEAD')}`,
  extract: (url) => {
    let [, user, project, aux] = url.pathname.split('/', 4)
    if (aux === 'raw') {
      return
    }

    if (!project) {
      if (!user) {
        return
      }

      project = user
      user = null
    }

    if (project.endsWith('.git')) {
      project = project.slice(0, -4)
    }

    return { user, project, committish: url.hash.slice(1) }
  },
  hashformat: function (fragment) {
    return fragment && 'file-' + formatHashFragment(fragment)
  },
}

hosts.sourcehut = {
  protocols: ['git+ssh:', 'https:'],
  domain: 'git.sr.ht',
  treepath: 'tree',
  blobpath: 'tree',
  filetemplate: ({ domain, user, project, committish, path }) =>
    `https://${domain}/${user}/${project}/blob/${maybeEncode(committish) || 'HEAD'}/${path}`,
  httpstemplate: ({ domain, user, project, committish }) =>
    `https://${domain}/${user}/${project}.git${maybeJoin('#', committish)}`,
  tarballtemplate: ({ domain, user, project, committish }) =>
    `https://${domain}/${user}/${project}/archive/${maybeEncode(committish) || 'HEAD'}.tar.gz`,
  bugstemplate: () => null,
  extract: (url) => {
    let [, user, project, aux] = url.pathname.split('/', 4)

    // tarball url
    if (['archive'].includes(aux)) {
      return
    }

    if (project && project.endsWith('.git')) {
      project = project.slice(0, -4)
    }

    if (!user || !project) {
      return
    }

    return { user, project, committish: url.hash.slice(1) }
  },
}

for (const [name, host] of Object.entries(hosts)) {
  hosts[name] = Object.assign({}, defaults, host)
}

module.exports = hosts
