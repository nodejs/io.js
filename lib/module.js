'use strict';

const { findSourceMap } = require('internal/source_map/source_map_cache');
const { Module } = require('internal/modules/cjs/loader');
const { register } = require('internal/modules/esm/loader');
const { SourceMap } = require('internal/source_map/source_map');
const {
  constants,
  enableCompileCache,
  flushCompileCache,
  getCompileCacheDir,
  resolveLoadAndCache,
} = require('internal/modules/helpers');
const {
  findPackageJSON,
} = require('internal/modules/package_json_reader');
const { stripTypeScriptTypes } = require('internal/modules/typescript');

Module.constants = constants;
Module.enableCompileCache = enableCompileCache;
Module.findPackageJSON = findPackageJSON;
Module.findSourceMap = findSourceMap;
Module.flushCompileCache = flushCompileCache;
Module.getCompileCacheDir = getCompileCacheDir;
Module.register = register;
Module.resolveLoadAndCache = resolveLoadAndCache;
Module.SourceMap = SourceMap;
Module.stripTypeScriptTypes = stripTypeScriptTypes;

module.exports = Module;
