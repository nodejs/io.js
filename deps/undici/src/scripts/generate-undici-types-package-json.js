'use strict'

const fs = require('node:fs')
const path = require('node:path')

const packageJSONPath = path.join(__dirname, '..', 'package.json')
const packageJSONRaw = fs.readFileSync(packageJSONPath, 'utf-8')
const packageJSON = JSON.parse(packageJSONRaw)

const licensePath = path.join(__dirname, '..', 'LICENSE')
const licenseRaw = fs.readFileSync(licensePath, 'utf-8')

const packageTypesJSON = {
  name: 'undici-types',
  version: packageJSON.version,
  description: 'A stand-alone types package for Undici',
  homepage: packageJSON.homepage,
  bugs: packageJSON.bugs,
  repository: packageJSON.repository,
  license: packageJSON.license,
  types: 'index.d.ts',
  files: ['*.d.ts'],
  contributors: packageJSON.contributors
}

const packageTypesPath = path.join(__dirname, '..', 'types', 'package.json')
const licenseTypesPath = path.join(__dirname, '..', 'types', 'LICENSE')

fs.writeFileSync(packageTypesPath, JSON.stringify(packageTypesJSON, null, 2))
fs.writeFileSync(licenseTypesPath, licenseRaw)
