// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export class BaseArgumentsProcessor {
  constructor(args) {
    this.args_ = args.slice();
    this.result_ = this.getDefaultResults();
    console.assert(this.result_ !== undefined)
    console.assert(this.result_.logFileName !== undefined);
    this.argsDispatch_ = this.getArgsDispatch();
    console.assert(this.argsDispatch_ !== undefined);
  }

  getDefaultResults() {
    throw "Implement in getDefaultResults in subclass";
  }

  getArgsDispatch() {
    throw "Implement getArgsDispatch in subclass";
  }

  result() { return this.result_ }

  static process(args) {
    const processor = new this(args);
    if (processor.parse()) {
      return processor.result();
    } else {
      processor.printUsageAndExit();
      return false;
    }
  }

  printUsageAndExit() {
    console.log('Cmdline args: [options] [log-file-name]\n' +
          'Default log file name is "' +
          this.result_.logFileName + '".\n');
          console.log('Options:');
    for (const arg in this.argsDispatch_) {
      const synonyms = [arg];
      const dispatch = this.argsDispatch_[arg];
      for (const synArg in this.argsDispatch_) {
        if (arg !== synArg && dispatch === this.argsDispatch_[synArg]) {
          synonyms.push(synArg);
          delete this.argsDispatch_[synArg];
        }
      }
      console.log(`  ${synonyms.join(', ').padEnd(20)} ${dispatch[2]}`);
    }
    quit(2);
  }

  parse() {
    while (this.args_.length) {
      let arg = this.args_.shift();
      if (arg.charAt(0) != '-') {
        this.result_.logFileName = arg;
        continue;
      }
      let userValue = null;
      const eqPos = arg.indexOf('=');
      if (eqPos != -1) {
        userValue = arg.substr(eqPos + 1);
        arg = arg.substr(0, eqPos);
      }
      if (arg in this.argsDispatch_) {
        const dispatch = this.argsDispatch_[arg];
        const property = dispatch[0];
        const defaultValue = dispatch[1];
        if (typeof defaultValue == "function") {
          userValue = defaultValue(userValue);
        } else if (userValue == null) {
          userValue = defaultValue;
        }
        this.result_[property] = userValue;
      } else {
        return false;
      }
    }
    return true;
  }
}

export function parseBool(str) {
  if (str == "true" || str == "1") return true;
  return false;
}
