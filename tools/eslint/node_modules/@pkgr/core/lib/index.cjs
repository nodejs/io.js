'use strict';

var node_module = require('node:module');
var fs = require('node:fs');
var path = require('node:path');

const import_meta = {};
const CWD = process.cwd();
const cjsRequire = typeof require === "undefined" ? node_module.createRequire(import_meta.url) : require;
const EXTENSIONS = [".ts", ".tsx", ...Object.keys(cjsRequire.extensions)];

const tryPkg = (pkg) => {
  try {
    return cjsRequire.resolve(pkg);
  } catch (e) {
  }
};
const isPkgAvailable = (pkg) => !!tryPkg(pkg);
const tryFile = (filePath, includeDir = false) => {
  if (typeof filePath === "string") {
    return fs.existsSync(filePath) && (includeDir || fs.statSync(filePath).isFile()) ? filePath : "";
  }
  for (const file of filePath != null ? filePath : []) {
    if (tryFile(file, includeDir)) {
      return file;
    }
  }
  return "";
};
const tryExtensions = (filepath, extensions = EXTENSIONS) => {
  const ext = [...extensions, ""].find((ext2) => tryFile(filepath + ext2));
  return ext == null ? "" : filepath + ext;
};
const findUp = (searchEntry, searchFileOrIncludeDir, includeDir) => {
  console.assert(path.isAbsolute(searchEntry));
  if (!tryFile(searchEntry, true) || searchEntry !== CWD && !searchEntry.startsWith(CWD + path.sep)) {
    return "";
  }
  searchEntry = path.resolve(
    fs.statSync(searchEntry).isDirectory() ? searchEntry : path.resolve(searchEntry, "..")
  );
  const isSearchFile = typeof searchFileOrIncludeDir === "string";
  const searchFile = isSearchFile ? searchFileOrIncludeDir : "package.json";
  do {
    const searched = tryFile(
      path.resolve(searchEntry, searchFile),
      isSearchFile && includeDir
    );
    if (searched) {
      return searched;
    }
    searchEntry = path.resolve(searchEntry, "..");
  } while (searchEntry === CWD || searchEntry.startsWith(CWD + path.sep));
  return "";
};

exports.CWD = CWD;
exports.EXTENSIONS = EXTENSIONS;
exports.cjsRequire = cjsRequire;
exports.findUp = findUp;
exports.isPkgAvailable = isPkgAvailable;
exports.tryExtensions = tryExtensions;
exports.tryFile = tryFile;
exports.tryPkg = tryPkg;
