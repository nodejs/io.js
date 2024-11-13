'use strict';

const {
  JSONParse,
} = primordials;

const {
  ERR_SYNTHETIC,
} = require('internal/errors').codes;
const { getValidatedPath } = require('internal/fs/utils');
const {
  validateBoolean,
  validateObject,
  validateSignalName,
  validateString,
} = require('internal/validators');
const nr = internalBinding('report');

const report = {
  writeReport(file, err) {
    if (typeof file === 'object' && file !== null) {
      err = file;
      file = undefined;
    } else if (file !== undefined) {
      validateString(file, 'file');
      file = getValidatedPath(file);
    }

    if (err === undefined) {
      err = new ERR_SYNTHETIC();
    } else {
      validateObject(err, 'err');
    }

    return nr.writeReport('JavaScript API', 'API', file, err);
  },
  getReport(err) {
    if (err === undefined)
      err = new ERR_SYNTHETIC();
    else
      validateObject(err, 'err');

    return JSONParse(nr.getReport(err));
  },
  get directory() {
    return nr.getDirectory();
  },
  set directory(dir) {
    validateString(dir, 'directory');
    nr.setDirectory(dir);
  },
  get filename() {
    return nr.getFilename();
  },
  set filename(name) {
    validateString(name, 'filename');
    nr.setFilename(name);
  },
  get compact() {
    return nr.getCompact();
  },
  set compact(b) {
    validateBoolean(b, 'compact');
    nr.setCompact(b);
  },
  get excludeNetwork() {
    return nr.getExcludeNetwork();
  },
  set excludeNetwork(b) {
    validateBoolean(b, 'excludeNetwork');
    nr.setExcludeNetwork(b);
  },
  get signal() {
    return nr.getSignal();
  },
  set signal(sig) {
    validateSignalName(sig, 'signal');
    removeSignalHandler();
    addSignalHandler(sig);
    nr.setSignal(sig);
  },
  get reportOnFatalError() {
    return nr.shouldReportOnFatalError();
  },
  set reportOnFatalError(trigger) {
    validateBoolean(trigger, 'trigger');

    nr.setReportOnFatalError(trigger);
  },
  get reportOnSignal() {
    return nr.shouldReportOnSignal();
  },
  set reportOnSignal(trigger) {
    validateBoolean(trigger, 'trigger');

    nr.setReportOnSignal(trigger);
    removeSignalHandler();
    addSignalHandler();
  },
  get reportOnUncaughtException() {
    return nr.shouldReportOnUncaughtException();
  },
  set reportOnUncaughtException(trigger) {
    validateBoolean(trigger, 'trigger');

    nr.setReportOnUncaughtException(trigger);
  },
  get excludeEnv() {
    return nr.getExcludeEnv();
  },
  set excludeEnv(b) {
    validateBoolean(b, 'excludeEnv');
    nr.setExcludeEnv(b);
  },
};

function addSignalHandler(sig) {
  if (nr.shouldReportOnSignal()) {
    if (typeof sig !== 'string')
      sig = nr.getSignal();

    process.on(sig, signalHandler);
  }
}

function removeSignalHandler() {
  const sig = nr.getSignal();

  if (sig)
    process.removeListener(sig, signalHandler);
}

function signalHandler(sig) {
  nr.writeReport(sig, 'Signal', null, '');
}

module.exports = {
  addSignalHandler,
  report,
};
