const { relative } = require('node:path')

const explainNode = (node, depth, chalk) =>
  printNode(node, chalk) +
  explainDependents(node, depth, chalk) +
  explainLinksIn(node, depth, chalk)

const colorType = (type, chalk) => {
  const style = type === 'extraneous' ? chalk.red
    : type === 'dev' ? chalk.blue
    : type === 'optional' ? chalk.magenta
    : type === 'peer' ? chalk.magentaBright
    : type === 'bundled' ? chalk.underline.cyan
    : type === 'workspace' ? chalk.blueBright
    : type === 'overridden' ? chalk.dim
    : /* istanbul ignore next */ s => s
  return style(type)
}

const printNode = (node, chalk) => {
  const extra = []

  for (const meta of ['extraneous', 'dev', 'optional', 'peer', 'bundled', 'overridden']) {
    if (node[meta]) {
      extra.push(` ${colorType(meta, chalk)}`)
    }
  }

  const pkgid = node.isWorkspace
    ? chalk.blueBright(`${node.name}@${node.version}`)
    : `${node.name}@${node.version}`

  return `${pkgid}${extra.join('')}` +
    (node.location ? chalk.dim(`\n${node.location}`) : '')
}

const explainLinksIn = ({ linksIn }, depth, chalk) => {
  if (!linksIn || !linksIn.length || depth <= 0) {
    return ''
  }

  const messages = linksIn.map(link => explainNode(link, depth - 1, chalk))
  const str = '\n' + messages.join('\n')
  return str.split('\n').join('\n  ')
}

const explainDependents = ({ dependents }, depth, chalk) => {
  if (!dependents || !dependents.length || depth <= 0) {
    return ''
  }

  const max = Math.ceil(depth / 2)
  const messages = dependents.slice(0, max)
    .map(edge => explainEdge(edge, depth, chalk))

  // show just the names of the first 5 deps that overflowed the list
  if (dependents.length > max) {
    let len = 0
    const maxLen = 50
    const showNames = []
    for (let i = max; i < dependents.length; i++) {
      const { from: { name: depName = 'the root project' } } = dependents[i]
      len += depName.length
      if (len >= maxLen && i < dependents.length - 1) {
        showNames.push('...')
        break
      }
      showNames.push(depName)
    }
    const show = `(${showNames.join(', ')})`
    messages.push(`${dependents.length - max} more ${show}`)
  }

  const str = '\n' + messages.join('\n')
  return str.split('\n').join('\n  ')
}

const explainEdge = ({ name, type, bundled, from, spec, rawSpec, overridden }, depth, chalk) => {
  let dep = type === 'workspace'
    ? chalk.bold(relative(from.location, spec.slice('file:'.length)))
    : `${name}@"${spec}"`
  if (overridden) {
    dep = `${colorType('overridden', chalk)} ${dep} (was "${rawSpec}")`
  }

  const fromMsg = ` from ${explainFrom(from, depth, chalk)}`

  return (type === 'prod' ? '' : `${colorType(type, chalk)} `) +
    (bundled ? `${colorType('bundled', chalk)} ` : '') +
    `${dep}${fromMsg}`
}

const explainFrom = (from, depth, chalk) => {
  if (!from.name && !from.version) {
    return 'the root project'
  }

  return printNode(from, chalk) +
    explainDependents(from, depth - 1, chalk) +
    explainLinksIn(from, depth - 1, chalk)
}

module.exports = { explainNode, printNode, explainEdge }
