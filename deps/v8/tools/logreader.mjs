// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/**
 * @fileoverview Log Reader is used to process log file produced by V8.
 */
 import { CsvParser } from "./csvparser.mjs";


// Parses dummy variable for readability;
export function parseString(field) { return field };
export const parseVarArgs = 'parse-var-args';

// Checks fields for numbers that are not safe integers. Returns true if any are
// found.
function containsUnsafeInts(fields) {
  for (let i = 0; i < fields.length; i++) {
    let field = fields[i];
    if ('number' == typeof(field) && !Number.isSafeInteger(field)) return true;
  }
  return false;
}

/**
 * Base class for processing log files.
 *
 * @param {boolean} timedRange Ignore ticks outside timed range.
 * @param {boolean} pairwiseTimedRange Ignore ticks outside pairs of timer
 *     markers.
 * @constructor
 */
export class LogReader {
  constructor(
        timedRange=false, pairwiseTimedRange=false, useBigIntAddresses=false) {
    this.dispatchTable_ = new Map();
    this.timedRange_ = timedRange;
    this.pairwiseTimedRange_ = pairwiseTimedRange;
    if (pairwiseTimedRange) this.timedRange_ = true;
    this.lineNum_ = 0;
    this.csvParser_ = new CsvParser();
    // Variables for tracking of 'current-time' log entries:
    this.hasSeenTimerMarker_ = false;
    this.logLinesSinceLastTimerMarker_ = [];
    // Flag to parse all numeric fields as BigInt to avoid arithmetic errors
    // caused by memory addresses being greater than MAX_SAFE_INTEGER
    this.useBigIntAddresses = useBigIntAddresses;
    this.parseFrame = useBigIntAddresses ? BigInt : parseInt;
    this.hasSeenUnsafeIntegers = false;
  }

/**
 * @param {Object} table A table used for parsing and processing
 *     log records.
 *     exampleDispatchTable = {
 *       "log-entry-XXX": {
 *          parser: [parseString, parseInt, ..., parseVarArgs],
 *          processor: this.processXXX.bind(this)
 *        },
 *        ...
 *      }
 */
  setDispatchTable(table) {
    if (Object.getPrototypeOf(table) !== null) {
      throw new Error("Dispatch expected table.__proto__=null for speedup");
    }
    for (let name in table) {
      const parser = table[name];
      if (parser === undefined) continue;
      if (!parser.isAsync) parser.isAsync = false;
      if (!Array.isArray(parser.parsers)) {
        throw new Error(`Invalid parsers: dispatchTable['${
            name}'].parsers should be an Array.`);
      }
      let type = typeof parser.processor;
      if (type !== 'function') {
       throw new Error(`Invalid processor: typeof dispatchTable['${
          name}'].processor is '${type}' instead of 'function'`);
      }
      if (!parser.processor.name.startsWith('bound ')) {
        parser.processor = parser.processor.bind(this);
      }
      this.dispatchTable_.set(name, parser);
    }
  }


  /**
   * A thin wrapper around shell's 'read' function showing a file name on error.
   */
  readFile(fileName) {
    try {
      return read(fileName);
    } catch (e) {
      printErr(`file="${fileName}": ${e.message || e}`);
      throw e;
    }
  }

  /**
   * Used for printing error messages.
   *
   * @param {string} str Error message.
   */
  printError(str) {
    // Do nothing.
  }

  /**
   * Processes a portion of V8 profiler event log.
   *
   * @param {string} chunk A portion of log.
   */
  async processLogChunk(chunk) {
    let end = chunk.length;
    let current = 0;
    // Kept for debugging in case of parsing errors.
    let lineNumber = 0;
    while (current < end) {
      const next = chunk.indexOf("\n", current);
      if (next === -1) break;
      lineNumber++;
      const line = chunk.substring(current, next);
      current = next + 1;
      await this.processLogLine(line);
    }
  }

  /**
   * Processes a line of V8 profiler event log.
   *
   * @param {string} line A line of log.
   */
  async processLogLine(line) {
    if (!this.timedRange_) {
      await this.processLogLine_(line);
      return;
    }
    if (line.startsWith("current-time")) {
      if (this.hasSeenTimerMarker_) {
        await this.processLog_(this.logLinesSinceLastTimerMarker_);
        this.logLinesSinceLastTimerMarker_ = [];
        // In pairwise mode, a "current-time" line ends the timed range.
        if (this.pairwiseTimedRange_) {
          this.hasSeenTimerMarker_ = false;
        }
      } else {
        this.hasSeenTimerMarker_ = true;
      }
    } else {
      if (this.hasSeenTimerMarker_) {
        this.logLinesSinceLastTimerMarker_.push(line);
      } else if (!line.startsWith("tick")) {
        await this.processLogLine_(line);
      }
    }
  }

  /**
   * Processes stack record.
   *
   * @param {number} pc Program counter.
   * @param {number} func JS Function.
   * @param {string[]} stack String representation of a stack.
   * @return {number[]} Processed stack.
   */
  processStack(pc, func, stack) {
    const fullStack = func ? [pc, func] : [pc];
    let prevFrame = pc;
    const length = stack.length;
    for (let i = 0, n = length; i < n; ++i) {
      const frame = stack[i];
      const firstChar = frame[0];
      if (firstChar === '+' || firstChar === '-') {
        // An offset from the previous frame.
        prevFrame += this.parseFrame(frame);
        fullStack.push(prevFrame);
      // Filter out possible 'overflow' string.
      } else if (firstChar !== 'o') {
        fullStack.push(this.parseFrame(frame));
      } else {
        console.error(`Dropping unknown tick frame: ${frame}`);
      }
    }
    return fullStack;
  }

  /**
   * Does a dispatch of a log record.
   *
   * @param {string[]} fields Log record.
   * @private
   */
  async dispatchLogRow_(fields) {
    // Obtain the dispatch.
    const command = fields[0];
    const dispatch = this.dispatchTable_.get(command);
    if (dispatch === undefined) return;
    const parsers = dispatch.parsers;
    const length = parsers.length;
    // Parse fields.
    const parsedFields = new Array(length);
    for (let i = 0; i < length; ++i) {
      const parser = parsers[i];
      if (parser === parseVarArgs) {
        parsedFields[i] = fields.slice(1 + i);
        break;
      } else {
        parsedFields[i] = parser(fields[1 + i]);
      }
    }
    if (!this.useBigIntAddresses) {
      if (!this.hasSeenUnsafeIntegers && containsUnsafeInts(parsedFields)) {
        console.warn(`Log line contains unsafe integers: ${fields}`);
        this.hasSeenUnsafeIntegers = true;
      }
    }
    // Run the processor.
    await dispatch.processor(...parsedFields);
  }

  /**
   * Processes log lines.
   *
   * @param {string[]} lines Log lines.
   * @private
   */
  async processLog_(lines) {
    for (let i = 0, n = lines.length; i < n; ++i) {
      await this.processLogLine_(lines[i]);
    }
  }

  /**
   * Processes a single log line.
   *
   * @param {String} a log line
   * @private
   */
  async processLogLine_(line) {
    if (line.length > 0) {
      try {
        const fields = this.csvParser_.parseLine(line);
        await this.dispatchLogRow_(fields);
      } catch (e) {
        this.printError(`line ${this.lineNum_ + 1}: ${e.message || e}\n${e.stack}`);
      }
    }
    this.lineNum_++;
  }
}
