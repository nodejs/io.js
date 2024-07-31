// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {LogReader, parseString, parseVarArgs} from '../logreader.mjs';
import {Profile} from '../profile.mjs';
import {RemoteLinuxCppEntriesProvider, RemoteMacOSCppEntriesProvider} from '../tickprocessor.mjs'

import {CodeLogEntry, CPPCodeLogEntry, DeoptLogEntry, FeedbackVectorEntry, SharedLibLogEntry} from './log/code.mjs';
import {IcLogEntry} from './log/ic.mjs';
import {Edge, MapLogEntry} from './log/map.mjs';
import {TickLogEntry} from './log/tick.mjs';
import {TimerLogEntry} from './log/timer.mjs';
import {Timeline} from './timeline.mjs';

// ===========================================================================

class AsyncConsumer {
  constructor(consumer_fn) {
    this._chunks = [];
    this._consumer = consumer_fn;
    this._pendingWork = Promise.resolve();
    this._isConsuming = false;
  }

  get pendingWork() {
    return this._pendingWork;
  }

  push(chunk) {
    this._chunks.push(chunk);
    this.consumeAll();
  }

  async consumeAll() {
    if (!this._isConsuming) this._pendingWork = this._consumeAll();
    return await this._pendingWork;
  }

  async _consumeAll() {
    this._isConsuming = true;
    while (this._chunks.length > 0) {
      await this._consumer(this._chunks.shift());
    }
    this._isConsuming = false;
  }
}

export class Processor extends LogReader {
  _codeTimeline = new Timeline();
  _deoptTimeline = new Timeline();
  _icTimeline = new Timeline();
  _mapTimeline = new Timeline();
  _tickTimeline = new Timeline();
  _timerTimeline = new Timeline();
  _formatPCRegexp = /(.*):[0-9]+:[0-9]+$/;
  _lastTimestamp = 0;
  _lastCodeLogEntry;
  _lastTickLogEntry;

  _cppEntriesProvider;

  _chunkRemainder = '';
  _lineNumber = 1;

  _totalInputBytes = 0;
  _processedInputChars = 0;
  _progressCallback;

  MAJOR_VERSION = 7;
  MINOR_VERSION = 6;
  constructor(useBigIntAddresses = false) {
    super(false, false, useBigIntAddresses);
    this.useBigIntAddresses = useBigIntAddresses;
    this.kZero = useBigIntAddresses ? 0n : 0;
    this.parseAddress = useBigIntAddresses ? BigInt : parseInt;
    this._chunkConsumer =
        new AsyncConsumer((chunk) => this._processChunk(chunk));
    this._profile = new Profile(useBigIntAddresses);
    const propertyICParser = [
      this.parseAddress, parseInt, parseInt, parseInt, parseString, parseString,
      parseString, parseString, parseString, parseString
    ];
    this.setDispatchTable({
      __proto__: null,
      'v8-version': {
        parsers: [
          parseInt,
          parseInt,
        ],
        processor: this.processV8Version,
      },
      'shared-library': {
        parsers: [
          parseString, this.parseAddress, this.parseAddress, this.parseAddress
        ],
        processor: this.processSharedLibrary.bind(this),
        isAsync: true,
      },
      'code-creation': {
        parsers: [
          parseString, parseInt, parseInt, this.parseAddress, this.parseAddress,
          parseString, parseVarArgs
        ],
        processor: this.processCodeCreation
      },
      'code-deopt': {
        parsers: [
          parseInt, parseInt, this.parseAddress, parseInt, parseInt,
          parseString, parseString, parseString
        ],
        processor: this.processCodeDeopt
      },
      'code-move': {
        parsers: [this.parseAddress, this.parseAddress],
        processor: this.processCodeMove
      },
      'code-source-info': {
        parsers: [
          this.parseAddress, parseInt, parseInt, parseInt, parseString,
          parseString, parseString
        ],
        processor: this.processCodeSourceInfo
      },
      'code-disassemble': {
        parsers: [this.parseAddress, parseString, parseString],
        processor: this.processCodeDisassemble
      },
      'feedback-vector': {
        parsers: [
          parseInt, parseString, parseInt, this.parseAddress, parseString,
          parseString, parseInt, parseInt, parseString
        ],
        processor: this.processFeedbackVector
      },
      'script-source': {
        parsers: [parseInt, parseString, parseString],
        processor: this.processScriptSource
      },
      'sfi-move': {
        parsers: [this.parseAddress, this.parseAddress],
        processor: this.processSFIMove
      },
      'tick': {
        parsers: [
          this.parseAddress, parseInt, parseInt, this.parseAddress, parseInt,
          parseVarArgs
        ],
        processor: this.processTick
      },
      'active-runtime-timer': undefined,
      'heap-sample-begin': undefined,
      'heap-sample-end': undefined,
      'timer-event-start': {
        parsers: [parseString, parseInt],
        processor: this.processTimerEventStart
      },
      'timer-event-end': {
        parsers: [parseString, parseInt],
        processor: this.processTimerEventEnd
      },
      'map-create':
          {parsers: [parseInt, parseString], processor: this.processMapCreate},
      'map': {
        parsers: [
          parseString, parseInt, parseString, parseString, this.parseAddress,
          parseInt, parseInt, parseString, parseString
        ],
        processor: this.processMap
      },
      'map-details': {
        parsers: [parseInt, parseString, parseString],
        processor: this.processMapDetails
      },
      'LoadGlobalIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'LoadGlobalIC')
      },
      'StoreGlobalIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'StoreGlobalIC')
      },
      'LoadIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'LoadIC')
      },
      'StoreIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'StoreIC')
      },
      'KeyedLoadIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'KeyedLoadIC')
      },
      'KeyedStoreIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'KeyedStoreIC')
      },
      'StoreInArrayLiteralIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'StoreInArrayLiteralIC')
      },
      'api': {
        parsers: [parseString, parseVarArgs],
        processor: this.processApiEvent
      },
    });
  }

  printError(str) {
    console.error(str);
    throw str
  }

  processChunk(chunk) {
    this._chunkConsumer.push(chunk)
  }

  setProgressCallback(totalSize, callback) {
    this._totalInputBytes = totalSize;
    this._progressCallback = callback;
  }

  async _updateProgress() {
    if (!this._progressCallback) return;
    // We use chars and bytes interchangeably for simplicity. This causes us to
    // slightly underestimate progress.
    this._progressCallback(
        this._processedInputChars / this._totalInputBytes,
        this._processedInputChars);
  }

  async _processChunk(chunk) {
    const prevProcessedInputChars = this._processedInputChars;
    let end = chunk.length;
    let current = 0;
    let next = 0;
    let line;
    try {
      while (current < end) {
        next = chunk.indexOf('\n', current);
        if (next === -1) {
          this._chunkRemainder += chunk.substring(current);
          break;
        }
        line = chunk.substring(current, next);
        if (this._chunkRemainder) {
          line = this._chunkRemainder + line;
          this._chunkRemainder = '';
        }
        current = next + 1;
        this._lineNumber++;
        await this.processLogLine(line);
        this._processedInputChars = prevProcessedInputChars + current;
      }
      this._updateProgress();
    } catch (e) {
      console.error(`Could not parse log line ${
          this._lineNumber}, trying to continue: ${e}`);
    }
  }

  async processLogFile(fileName) {
    this.collectEntries = true;
    this.lastLogFileName_ = fileName;
    let i = 1;
    let line;
    try {
      while (line = readline()) {
        await this.processLogLine(line);
        i++;
      }
    } catch (e) {
      console.error(
          `Error occurred during parsing line ${i}` +
          ', trying to continue: ' + e);
    }
    this.finalize();
  }

  async finalize() {
    await this._chunkConsumer.consumeAll();
    if (this._profile.warnings.size > 0) {
      console.warn('Found profiler warnings:', this._profile.warnings);
    }
    // TODO(cbruni): print stats;
    this._mapTimeline.transitions = new Map();
    let id = 0;
    this._mapTimeline.forEach(map => {
      if (map.isRoot()) id = map.finalizeRootMap(id + 1);
      if (map.edge && map.edge.name) {
        const edge = map.edge;
        const list = this._mapTimeline.transitions.get(edge.name);
        if (list === undefined) {
          this._mapTimeline.transitions.set(edge.name, [edge]);
        } else {
          list.push(edge);
        }
      }
    });
  }

  processV8Version(majorVersion, minorVersion) {
    if ((majorVersion == this.MAJOR_VERSION &&
         minorVersion <= this.MINOR_VERSION) ||
        (majorVersion < this.MAJOR_VERSION)) {
      window.alert(
          `Unsupported version ${majorVersion}.${minorVersion}. \n` +
          `Please use the matching tool for given the V8 version.`);
    }
  }

  async processSharedLibrary(name, startAddr, endAddr, aslrSlide) {
    const entry = this._profile.addLibrary(name, startAddr, endAddr);
    entry.logEntry = new SharedLibLogEntry(entry);
    // Many events rely on having a script around, creating fake entries for
    // shared libraries.
    this._profile.addScriptSource(-1, name, '');

    if (this._cppEntriesProvider == undefined) {
      await this._setupCppEntriesProvider();
    }

    await this._cppEntriesProvider.parseVmSymbols(
        name, startAddr, endAddr, aslrSlide, (fName, fStart, fEnd) => {
          const entry = this._profile.addStaticCode(fName, fStart, fEnd);
          entry.logEntry = new CPPCodeLogEntry(entry);
        });
  }

  async _setupCppEntriesProvider() {
    // Probe the local symbol server for the platform:
    const url = new URL('http://localhost:8000/v8/info/platform')
    let platform = {name: 'linux'};
    try {
      const response = await fetch(url, {timeout: 1});
      if (response.status == 404) {
        throw new Error(
            `Local symbol server returned 404: ${await response.text()}`);
      }
      platform = await response.json();
    } catch (e) {
      console.warn(`Local symbol server is not running on ${url}`);
      console.warn(e);
    }
    let CppEntriesProvider = RemoteLinuxCppEntriesProvider;
    if (platform.name === 'darwin') {
      CppEntriesProvider = RemoteMacOSCppEntriesProvider;
    }
    this._cppEntriesProvider = new CppEntriesProvider(
        platform.nmExec, platform.objdumpExec, platform.targetRootFS,
        platform.apkEmbeddedLibrary);
  }

  processCodeCreation(
      type, kind, timestamp, start, size, nameAndPosition, maybe_func) {
    this._lastTimestamp = timestamp;
    let profilerEntry;
    let stateName = '';
    if (type != 'RegExp' && maybe_func.length) {
      const sfiAddr = this.parseAddress(maybe_func[0]);
      stateName = maybe_func[1] ?? '';
      const state = Profile.parseState(maybe_func[1]);
      profilerEntry = this._profile.addFuncCode(
          type, nameAndPosition, timestamp, start, size, sfiAddr, state);
    } else {
      profilerEntry =
          this._profile.addCode(type, nameAndPosition, timestamp, start, size);
    }
    const name = nameAndPosition.slice(0, nameAndPosition.indexOf(' '));
    this._lastCodeLogEntry = new CodeLogEntry(
        type + stateName, timestamp,
        Profile.getKindFromState(Profile.parseState(stateName)), kind, name,
        profilerEntry);
    this._codeTimeline.push(this._lastCodeLogEntry);
  }

  processCodeDeopt(
      timestamp, codeSize, instructionStart, inliningId, scriptOffset,
      deoptKind, deoptLocation, deoptReason) {
    this._lastTimestamp = timestamp;
    const profCodeEntry = this._profile.findEntry(instructionStart);
    const logEntry = new DeoptLogEntry(
        deoptKind, timestamp, profCodeEntry, deoptReason, deoptLocation,
        scriptOffset, instructionStart, codeSize, inliningId);
    profCodeEntry.logEntry.add(logEntry);
    this._deoptTimeline.push(logEntry);
    this.addSourcePosition(profCodeEntry, logEntry);
    logEntry.functionSourcePosition = logEntry.sourcePosition;
    // custom parse deopt location
    if (deoptLocation === '<unknown>') return;
    // Handle deopt location for inlined code: <location> inlined at <location>
    const inlinedPos = deoptLocation.indexOf(' inlined at ');
    if (inlinedPos > 0) {
      deoptLocation = deoptLocation.substring(0, inlinedPos)
    }
    const script = this.getProfileEntryScript(profCodeEntry);
    if (!script) return;
    const colSeparator = deoptLocation.lastIndexOf(':');
    const rowSeparator = deoptLocation.lastIndexOf(':', colSeparator - 1);
    const line =
        parseInt(deoptLocation.substring(rowSeparator + 1, colSeparator));
    const column = parseInt(
        deoptLocation.substring(colSeparator + 1, deoptLocation.length - 1));
    logEntry.sourcePosition = script.addSourcePosition(line, column, logEntry);
  }

  processFeedbackVector(
      timestamp, fbv_address, fbv_length, instructionStart, optimization_marker,
      optimization_tier, invocation_count, profiler_ticks, fbv_string) {
    const profCodeEntry = this._profile.findEntry(instructionStart);
    if (!profCodeEntry) {
      console.warn('Didn\'t find code for FBV', {fbv_string, instructionStart});
      return;
    }
    const fbv = new FeedbackVectorEntry(
        timestamp, profCodeEntry.logEntry, fbv_address, fbv_length,
        optimization_marker, optimization_tier, invocation_count,
        profiler_ticks, fbv_string);
    profCodeEntry.logEntry.setFeedbackVector(fbv);
  }

  processScriptSource(scriptId, url, source) {
    this._profile.addScriptSource(scriptId, url, source);
  }

  processCodeMove(from, to) {
    this._profile.moveCode(from, to);
  }

  processCodeDelete(start) {
    this._profile.deleteCode(start);
  }

  processSFIMove(from, to) {
    this._profile.moveSharedFunctionInfo(from, to);
  }

  processTick(
      pc, time_ns, is_external_callback, tos_or_external_callback, vmState,
      stack) {
    if (is_external_callback) {
      // Don't use PC when in external callback code, as it can point
      // inside callback's code, and we will erroneously report
      // that a callback calls itself. Instead we use tos_or_external_callback,
      // as simply resetting PC will produce unaccounted ticks.
      pc = tos_or_external_callback;
      tos_or_external_callback = this.kZero;
    } else if (tos_or_external_callback) {
      // Find out, if top of stack was pointing inside a JS function
      // meaning that we have encountered a frameless invocation.
      const funcEntry = this._profile.findEntry(tos_or_external_callback);
      if (!funcEntry?.isJSFunction?.()) {
        tos_or_external_callback = this.kZero;
      }
    }
    const entryStack = this._profile.recordTick(
        time_ns, vmState,
        this.processStack(pc, tos_or_external_callback, stack));
    const newEntry = new TickLogEntry(time_ns, vmState, entryStack);
    this._tickTimeline.push(newEntry);
    if (this._lastTickLogEntry !== undefined) {
      this._lastTickLogEntry.end(time_ns);
    }
    this._lastTickLogEntry = newEntry;
  }

  processCodeSourceInfo(
      start, scriptId, startPos, endPos, sourcePositions, inliningPositions,
      inlinedFunctions) {
    this._profile.addSourcePositions(
        start, scriptId, startPos, endPos, sourcePositions, inliningPositions,
        inlinedFunctions);
    if (this._lastCodeLogEntry === undefined) return;
    let profileEntry = this._profile.findEntry(start);
    if (profileEntry !== this._lastCodeLogEntry._entry) return;
    this.addSourcePosition(profileEntry, this._lastCodeLogEntry);
    this._lastCodeLogEntry = undefined;
  }

  addSourcePosition(profileEntry, logEntry) {
    let script = this.getProfileEntryScript(profileEntry);
    const parts = profileEntry.getRawName().split(':');
    if (parts.length < 3) return;
    const line = parseInt(parts[parts.length - 2]);
    const column = parseInt(parts[parts.length - 1]);
    logEntry.sourcePosition = script.addSourcePosition(line, column, logEntry);
  }

  processCodeDisassemble(start, kind, disassemble) {
    this._profile.addDisassemble(start, kind, disassemble);
  }

  processPropertyIC(
      type, pc, time, line, column, old_state, new_state, mapId, key, modifier,
      slow_reason) {
    this._lastTimestamp = time;
    const codeEntry = this._profile.findEntry(pc);
    const fnName = this.formatProfileEntry(codeEntry);
    const script = this.getProfileEntryScript(codeEntry);
    const map = this.getOrCreateMapEntry(mapId, time);
    // TODO: Use SourcePosition here directly
    let entry = new IcLogEntry(
        type, fnName, time, line, column, key, old_state, new_state, map,
        slow_reason, modifier, codeEntry);
    if (script) {
      entry.sourcePosition = script.addSourcePosition(line, column, entry);
    }
    this._icTimeline.push(entry);
  }

  formatProfileEntry(profileEntry, line, column) {
    if (!profileEntry) return '<unknown>';
    if (profileEntry.type === 'Builtin') return profileEntry.name;
    const name = profileEntry.sfi.getName();
    const array = this._formatPCRegexp.exec(name);
    const formatted =
        (array === null) ? name : profileEntry.getState() + array[1];
    if (line === undefined || column === undefined) return formatted;
    return `${formatted}:${line}:${column}`;
  }

  getProfileEntryScript(profileEntry) {
    if (!profileEntry) return undefined;
    if (profileEntry.type === 'Builtin') return undefined;
    const script = profileEntry.source?.script;
    if (script !== undefined) return script;
    let fileName;
    if (profileEntry.type === 'SHARED_LIB') {
      fileName = profileEntry.name;
    } else {
      // Slow path, try to get the script from the url:
      const fnName = this.formatProfileEntry(profileEntry);
      let parts = fnName.split(' ');
      fileName = parts[parts.length - 1];
    }
    return this.getScript(fileName);
  }

  processMap(type, time, from, to, pc, line, column, reason, name) {
    this._lastTimestamp = time;
    const time_ = parseInt(time);
    if (type === 'Deprecate') return this.deprecateMap(type, time_, from);
    // Skip normalized maps that were cached so we don't introduce multiple
    // edges with the same source and target map.
    if (type === 'NormalizeCached') return;
    const from_ = this.getOrCreateMapEntry(from, time_);
    const to_ = this.getOrCreateMapEntry(to, time_);
    if (type === 'Normalize') {
      // Fix a bug where we log "Normalize" transitions for maps created from
      // the NormalizedMapCache.
      if (to_.parent?.id === from || to_.time < from_.time || to_.depth > 0) {
        console.log(`Skipping transition to cached normalized map`);
        return;
      }
    }
    if (pc) {
      const profCodeEntry = this._profile.findEntry(pc);
      if (profCodeEntry) {
        to_.entry = profCodeEntry;
        profCodeEntry.logEntry.add(to_);
        let script = this.getProfileEntryScript(profCodeEntry);
        if (script) {
          to_.sourcePosition = script.addSourcePosition(line, column, to_);
        }
      }
    }
    let edge = new Edge(type, name, reason, time, from_, to_);
    if (to_.parent !== undefined && to_.parent === from_) {
      // Fix bug where we double log transitions.
      console.warn('Fixing up double transition');
      to_.edge.updateFrom(edge);
    } else {
      edge.finishSetup();
    }
  }

  deprecateMap(type, time, id) {
    this._lastTimestamp = time;
    this.getOrCreateMapEntry(id, time).deprecate();
  }

  processMapCreate(time, id) {
    // map-create events might override existing maps if the addresses get
    // recycled. Hence we do not check for existing maps.
    this._lastTimestamp = time;
    this.createMapEntry(id, time);
  }

  processMapDetails(time, id, string) {
    // TODO(cbruni): fix initial map logging.
    const map = this.getOrCreateMapEntry(id, time);
    map.description = string;
  }

  createMapEntry(id, time) {
    this._lastTimestamp = time;
    const map = new MapLogEntry(id, time);
    this._mapTimeline.push(map);
    return map;
  }

  getOrCreateMapEntry(id, time) {
    if (id === '0x000000000000') return undefined;
    const map = MapLogEntry.get(id, time);
    if (map !== undefined) return map;
    console.warn(`No map details provided: id=${id}`);
    // Manually patch in a map to continue running.
    return this.createMapEntry(id, time);
  }

  getScript(url) {
    const script = this._profile.getScript(url);
    // TODO create placeholder script for empty urls.
    if (script === undefined) {
      console.error(`Could not find script for url: '${url}'`)
    }
    return script;
  }

  processApiEvent(type, varArgs) {
    // legacy events that are no longer supported
  }

  processTimerEventStart(type, time) {
    const entry = new TimerLogEntry(type, time);
    this._timerTimeline.push(entry);
  }

  processTimerEventEnd(type, time) {
    // Timer-events are infrequent, and not deeply nested, doing a linear walk
    // is usually good enough.
    for (let i = this._timerTimeline.length - 1; i >= 0; i--) {
      const timer = this._timerTimeline.at(i);
      if (timer.type == type && !timer.isInitialized) {
        timer.end(time);
        return;
      }
    }
    console.error('Couldn\'t find matching timer event start', {type, time});
  }

  get icTimeline() {
    return this._icTimeline;
  }

  get mapTimeline() {
    return this._mapTimeline;
  }

  get deoptTimeline() {
    return this._deoptTimeline;
  }

  get codeTimeline() {
    return this._codeTimeline;
  }

  get tickTimeline() {
    return this._tickTimeline;
  }

  get timerTimeline() {
    return this._timerTimeline;
  }

  get scripts() {
    return this._profile.scripts_.filter(script => script !== undefined);
  }

  get profile() {
    return this._profile;
  }
}
