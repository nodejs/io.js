"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.normalizeBabelParseConfig = normalizeBabelParseConfig;
exports.normalizeBabelParseConfigSync = normalizeBabelParseConfigSync;
function asyncGeneratorStep(gen, resolve, reject, _next, _throw, key, arg) { try { var info = gen[key](arg); var value = info.value; } catch (error) { reject(error); return; } if (info.done) { resolve(value); } else { Promise.resolve(value).then(_next, _throw); } }
function _asyncToGenerator(fn) { return function () { var self = this, args = arguments; return new Promise(function (resolve, reject) { var gen = fn.apply(self, args); function _next(value) { asyncGeneratorStep(gen, resolve, reject, _next, _throw, "next", value); } function _throw(err) { asyncGeneratorStep(gen, resolve, reject, _next, _throw, "throw", err); } _next(undefined); }); }; }
const babel = require("./babel-core.cjs");
const ESLINT_VERSION = require("../utils/eslint-version.cjs");
function getParserPlugins(babelOptions) {
  var _babelOptions$parserO, _babelOptions$parserO2;
  const babelParserPlugins = (_babelOptions$parserO = (_babelOptions$parserO2 = babelOptions.parserOpts) == null ? void 0 : _babelOptions$parserO2.plugins) != null ? _babelOptions$parserO : [];
  const estreeOptions = {
    classFeatures: ESLINT_VERSION >= 8
  };
  for (const plugin of babelParserPlugins) {
    if (Array.isArray(plugin) && plugin[0] === "estree") {
      Object.assign(estreeOptions, plugin[1]);
      break;
    }
  }
  return [["estree", estreeOptions], ...babelParserPlugins];
}
function normalizeParserOptions(options) {
  var _options$allowImportE, _options$ecmaFeatures, _options$ecmaFeatures2;
  return Object.assign({
    sourceType: options.sourceType,
    filename: options.filePath
  }, options.babelOptions, {
    parserOpts: Object.assign({}, {
      allowImportExportEverywhere: (_options$allowImportE = options.allowImportExportEverywhere) != null ? _options$allowImportE : false,
      allowSuperOutsideMethod: true
    }, {
      allowReturnOutsideFunction: (_options$ecmaFeatures = (_options$ecmaFeatures2 = options.ecmaFeatures) == null ? void 0 : _options$ecmaFeatures2.globalReturn) != null ? _options$ecmaFeatures : true
    }, options.babelOptions.parserOpts, {
      plugins: getParserPlugins(options.babelOptions),
      attachComment: false,
      ranges: true,
      tokens: true
    }),
    caller: Object.assign({
      name: "@babel/eslint-parser"
    }, options.babelOptions.caller)
  });
}
function validateResolvedConfig(config, options, parseOptions) {
  if (config !== null) {
    if (options.requireConfigFile !== false) {
      if (!config.hasFilesystemConfig()) {
        let error = `No Babel config file detected for ${config.options.filename}. Either disable config file checking with requireConfigFile: false, or configure Babel so that it can find the config files.`;
        if (config.options.filename.includes("node_modules")) {
          error += `\nIf you have a .babelrc.js file or use package.json#babel, keep in mind that it's not used when parsing dependencies. If you want your config to be applied to your whole app, consider using babel.config.js or babel.config.json instead.`;
        }
        throw new Error(error);
      }
    }
    if (config.options) return config.options;
  }
  return getDefaultParserOptions(parseOptions);
}
function getDefaultParserOptions(options) {
  return Object.assign({
    plugins: []
  }, options, {
    babelrc: false,
    configFile: false,
    browserslistConfigFile: false,
    ignore: null,
    only: null
  });
}
function normalizeBabelParseConfig(_x) {
  return _normalizeBabelParseConfig.apply(this, arguments);
}
function _normalizeBabelParseConfig() {
  _normalizeBabelParseConfig = _asyncToGenerator(function* (options) {
    const parseOptions = normalizeParserOptions(options);
    const config = yield babel.loadPartialConfigAsync(parseOptions);
    return validateResolvedConfig(config, options, parseOptions);
  });
  return _normalizeBabelParseConfig.apply(this, arguments);
}
function normalizeBabelParseConfigSync(options) {
  const parseOptions = normalizeParserOptions(options);
  const config = babel.loadPartialConfigSync(parseOptions);
  return validateResolvedConfig(config, options, parseOptions);
}

//# sourceMappingURL=configuration.cjs.map
