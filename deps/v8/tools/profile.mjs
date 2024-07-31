// Copyright 2009 the V8 project authors. All rights reserved.
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

import { CodeMap, CodeEntry } from "./codemap.mjs";
import { ConsArray } from "./consarray.mjs";
import { WebInspector } from "./sourcemap.mjs";

// Used to associate log entries with source positions in scripts.
// TODO: move to separate modules
export class SourcePosition {
  script = null;
  line = -1;
  column = -1;
  entries = [];
  isFunction = false;
  originalPosition = undefined;

  constructor(script, line, column) {
    this.script = script;
    this.line = line;
    this.column = column;
  }

  addEntry(entry) {
    this.entries.push(entry);
  }

  toString() {
    return `${this.script.name}:${this.line}:${this.column}`;
  }

  get functionPosition() {
    // TODO(cbruni)
    return undefined;
  }

  get toolTipDict() {
    return {
      title: this.toString(),
      __this__: this,
      script: this.script,
      entries: this.entries,
    }
  }
}

export class Script {
  url;
  source = "";
  name;
  sourcePosition = undefined;
  // Map<line, Map<column, SourcePosition>>
  lineToColumn = new Map();
  _entries = [];
  _sourceMapState = "unknown";

  constructor(id) {
    this.id = id;
    this.sourcePositions = [];
  }

  update(url, source) {
    this.url = url;
    this.name = Script.getShortestUniqueName(url, this);
    this.source = source;
  }

  get length() {
    return this.source.length;
  }

  get entries() {
    return this._entries;
  }

  get startLine() {
    return this.sourcePosition?.line ?? 1;
  }

  get sourceMapState() {
    return this._sourceMapState;
  }

  findFunctionSourcePosition(sourcePosition) {
    // TODO(cbruni): implement
    return undefined;
  }

  addSourcePosition(line, column, entry) {
    let sourcePosition = this.lineToColumn.get(line)?.get(column);
    if (sourcePosition === undefined) {
      sourcePosition = new SourcePosition(this, line, column,)
      this._addSourcePosition(line, column, sourcePosition);
    }
    if (this.sourcePosition === undefined && entry.entry?.type === "Script") {
      // Mark the source position of scripts, for inline scripts which don't
      // start at line 1.
      this.sourcePosition = sourcePosition;
    }
    sourcePosition.addEntry(entry);
    this._entries.push(entry);
    return sourcePosition;
  }

  _addSourcePosition(line, column, sourcePosition) {
    let columnToSourcePosition;
    if (this.lineToColumn.has(line)) {
      columnToSourcePosition = this.lineToColumn.get(line);
    } else {
      columnToSourcePosition = new Map();
      this.lineToColumn.set(line, columnToSourcePosition);
    }
    this.sourcePositions.push(sourcePosition);
    columnToSourcePosition.set(column, sourcePosition);
  }

  toString() {
    return `Script(${this.id}): ${this.name}`;
  }

  get toolTipDict() {
    return {
      title: this.toString(),
      __this__: this,
      id: this.id,
      url: this.url,
      source: this.source,
      sourcePositions: this.sourcePositions
    }
  }

  static getShortestUniqueName(url, script) {
    const parts = url.split('/');
    const filename = parts[parts.length -1];
    const dict = this._dict ?? (this._dict = new Map());
    const matchingScripts = dict.get(filename);
    if (matchingScripts == undefined) {
      dict.set(filename, [script]);
      return filename;
    }
    // TODO: find shortest unique substring
    // Update all matching scripts to have a unique filename again.
    for (let matchingScript of matchingScripts) {
      matchingScript.name = script.url
    }
    matchingScripts.push(script);
    return url;
  }

  ensureSourceMapCalculated(sourceMapFetchPrefix=undefined) {
    if (this._sourceMapState !== "unknown") return;

    const sourceMapURLMatch =
        this.source.match(/\/\/# sourceMappingURL=(.*)\n/);
    if (!sourceMapURLMatch) {
      this._sourceMapState = "none";
      return;
    }

    this._sourceMapState = "loading";
    let sourceMapURL = sourceMapURLMatch[1];
    (async () => {
      try {
        let sourceMapPayload;
        const options = { timeout: 15 };
        try {
          sourceMapPayload = await fetch(sourceMapURL, options);
        } catch (e) {
          if (e instanceof TypeError && sourceMapFetchPrefix) {
            // Try again with fetch prefix.
            // TODO(leszeks): Remove the retry once the prefix is
            // configurable.
            sourceMapPayload =
                await fetch(sourceMapFetchPrefix + sourceMapURL, options);
          } else {
            throw e;
          }
        }
        sourceMapPayload = await sourceMapPayload.text();

        if (sourceMapPayload.startsWith(')]}')) {
          sourceMapPayload =
              sourceMapPayload.substring(sourceMapPayload.indexOf('\n'));
        }
        sourceMapPayload = JSON.parse(sourceMapPayload);
        const sourceMap =
            new WebInspector.SourceMap(sourceMapURL, sourceMapPayload);

        const startLine = this.startLine;
        for (const sourcePosition of this.sourcePositions) {
          const line = sourcePosition.line - startLine;
          const column = sourcePosition.column - 1;
          const mapping = sourceMap.findEntry(line, column);
          if (mapping) {
            sourcePosition.originalPosition = {
              source: new URL(mapping[2], sourceMapURL).href,
              line: mapping[3] + 1,
              column: mapping[4] + 1
            };
          } else {
            sourcePosition.originalPosition = {source: null, line:0, column:0};
          }
        }
        this._sourceMapState = "loaded";
      } catch (e) {
        console.error(e);
        this._sourceMapState = "failed";
      }
    })();
  }
}


const kOffsetPairRegex = /C([0-9]+)O([0-9]+)/g;
class SourcePositionTable {
  constructor(encodedTable) {
    this._offsets = [];
    while (true) {
      const regexResult = kOffsetPairRegex.exec(encodedTable);
      if (!regexResult) break;
      const codeOffset = parseInt(regexResult[1]);
      const scriptOffset = parseInt(regexResult[2]);
      if (isNaN(codeOffset) || isNaN(scriptOffset)) continue;
      this._offsets.push({code: codeOffset, script: scriptOffset});
    }
  }

  getScriptOffset(codeOffset) {
    if (codeOffset < 0) {
      throw new Exception(`Invalid codeOffset=${codeOffset}, should be >= 0`);
    }
    for (let i = this.offsetTable.length - 1; i >= 0; i--) {
      const offset = this._offsets[i];
      if (offset.code <= codeOffset) {
        return offset.script;
      }
    }
    return this._offsets[0].script;
  }
}


class SourceInfo {
  script;
  start;
  end;
  positions;
  inlined;
  fns;
  disassemble;

  setSourcePositionInfo(
        script, startPos, endPos, sourcePositionTableData, inliningPositions,
        inlinedSFIs) {
    this.script = script;
    this.start = startPos;
    this.end = endPos;
    this.positions = sourcePositionTableData;
    this.inlined = inliningPositions;
    this.fns = inlinedSFIs;
    this.sourcePositionTable = new SourcePositionTable(sourcePositionTableData);
  }

  get sfis() {
    return this.fns;
  }

  setDisassemble(code) {
    this.disassemble = code;
  }

  getSourceCode() {
    return this.script.source?.substring(this.start, this.end);
  }
}

const kProfileOperationMove = 0;
const kProfileOperationDelete = 1;
const kProfileOperationTick = 2;

/**
 * Creates a profile object for processing profiling-related events
 * and calculating function execution times.
 *
 * @constructor
 */
export class Profile {
  topDownTree_ = new CallTree();
  bottomUpTree_ = new CallTree();
  c_entries_ = {__proto__:null};
  scripts_ = [];
  urlToScript_ = new Map();
  warnings = new Set();

  constructor(useBigIntAddresses=false) {
    this.useBigIntAddresses = useBigIntAddresses;
    this.codeMap_ = new CodeMap(useBigIntAddresses);
  }

  serializeVMSymbols() {
    let result = this.codeMap_.getAllStaticEntriesWithAddresses();
    result.concat(this.codeMap_.getAllLibraryEntriesWithAddresses())
    return result.map(([startAddress, codeEntry]) => {
      return [codeEntry.getName(), startAddress, startAddress + codeEntry.size]
    });
  }

  /**
   * Returns whether a function with the specified name must be skipped.
   * Should be overridden by subclasses.
   *
   * @param {string} name Function name.
   */
  skipThisFunction(name) {
    return false;
  }

  /**
   * Enum for profiler operations that involve looking up existing
   * code entries.
   *
   * @enum {number}
   */
  static Operation = {
    MOVE: kProfileOperationMove,
    DELETE: kProfileOperationDelete,
    TICK: kProfileOperationTick
  }

  /**
   * Enum for code state regarding its dynamic optimization.
   *
   * @enum {number}
   */
  static CodeState = {
    COMPILED: 0,
    IGNITION: 1,
    SPARKPLUG: 2,
    MAGLEV: 4,
    TURBOFAN: 5,
  }

  static VMState = {
    JS: 0,
    GC: 1,
    PARSER: 2,
    BYTECODE_COMPILER: 3,
    // TODO(cbruni): add SPARKPLUG_COMPILER
    COMPILER: 4,
    OTHER: 5,
    EXTERNAL: 6,
    IDLE: 7,
  }

  static CodeType = {
    CPP: 0,
    SHARED_LIB: 1
  }

  /**
   * Parser for dynamic code optimization state.
   */
  static parseState(s) {
    switch (s) {
      case '':
        return this.CodeState.COMPILED;
      case '~':
        return this.CodeState.IGNITION;
      case '^':
        return this.CodeState.SPARKPLUG;
      case '+':
        return this.CodeState.MAGLEV;
      case '*':
        return this.CodeState.TURBOFAN;
    }
    throw new Error(`unknown code state: ${s}`);
  }

  static getKindFromState(state) {
    if (state === this.CodeState.COMPILED) {
      return "Builtin";
    } else if (state === this.CodeState.IGNITION) {
      return "Unopt";
    } else if (state === this.CodeState.SPARKPLUG) {
      return "Sparkplug";
    } else if (state === this.CodeState.MAGLEV) {
      return "Maglev";
    } else if (state === this.CodeState.TURBOFAN) {
      return "Opt";
    }
    throw new Error(`unknown code state: ${state}`);
  }

  static vmStateString(state) {
    switch (state) {
      case this.VMState.JS:
        return 'JS';
      case this.VMState.GC:
        return 'GC';
      case this.VMState.PARSER:
        return 'Parse';
      case this.VMState.BYTECODE_COMPILER:
        return 'Compile Bytecode';
      case this.VMState.COMPILER:
        return 'Compile';
      case this.VMState.OTHER:
        return 'Other';
      case this.VMState.EXTERNAL:
        return 'External';
      case this.VMState.IDLE:
        return 'Idle';
    }
    return 'unknown';
  }

  /**
   * Called whenever the specified operation has failed finding a function
   * containing the specified address. Should be overridden by subclasses.
   * See the Profile.Operation enum for the list of
   * possible operations.
   *
   * @param {number} operation Operation.
   * @param {number} addr Address of the unknown code.
   * @param {number} opt_stackPos If an unknown address is encountered
   *     during stack strace processing, specifies a position of the frame
   *     containing the address.
   */
  handleUnknownCode(operation, addr, opt_stackPos) { }

  /**
   * Registers a library.
   *
   * @param {string} name Code entry name.
   * @param {number} startAddr Starting address.
   * @param {number} endAddr Ending address.
   */
  addLibrary(name, startAddr, endAddr) {
    const entry = new CodeEntry(endAddr - startAddr, name, 'SHARED_LIB');
    this.codeMap_.addLibrary(startAddr, entry);
    return entry;
  }

  /**
   * Registers statically compiled code entry.
   *
   * @param {string} name Code entry name.
   * @param {number} startAddr Starting address.
   * @param {number} endAddr Ending address.
   */
  addStaticCode(name, startAddr, endAddr) {
    const entry = new CodeEntry(endAddr - startAddr, name, 'CPP');
    this.codeMap_.addStaticCode(startAddr, entry);
    return entry;
  }

  /**
   * Registers dynamic (JIT-compiled) code entry.
   *
   * @param {string} type Code entry type.
   * @param {string} name Code entry name.
   * @param {number} start Starting address.
   * @param {number} size Code entry size.
   */
  addCode(type, name, timestamp, start, size) {
    const entry = new DynamicCodeEntry(size, type, name);
    this.codeMap_.addCode(start, entry);
    return entry;
  }

  /**
   * Registers dynamic (JIT-compiled) code entry or entries that overlap with
   * static entries (like builtins).
   *
   * @param {string} type Code entry type.
   * @param {string} name Code entry name.
   * @param {number} start Starting address.
   * @param {number} size Code entry size.
   */
  addAnyCode(type, name, timestamp, start, size) {
    const entry = new DynamicCodeEntry(size, type, name);
    this.codeMap_.addAnyCode(start, entry);
    return entry;
  }

  /**
   * Registers dynamic (JIT-compiled) code entry.
   *
   * @param {string} type Code entry type.
   * @param {string} name Code entry name.
   * @param {number} start Starting address.
   * @param {number} size Code entry size.
   * @param {number} sfiAddr Shared function object address.
   * @param {Profile.CodeState} state Optimization state.
   */
  addFuncCode(type, name, timestamp, start, size, sfiAddr, state) {
    // As code and functions are in the same address space,
    // it is safe to put them in a single code map.
    let sfi = this.codeMap_.findDynamicEntryByStartAddress(sfiAddr);
    // Overwrite any old (unused) code objects that overlap with the new SFI.
    const new_sfi_old_code = !(sfi instanceof SharedFunctionInfoEntry)
    if (sfi === null || new_sfi_old_code) {
      sfi = new SharedFunctionInfoEntry(name, this.useBigIntAddresses);
      this.codeMap_.addCode(sfiAddr, sfi);
    } else if (sfi.name !== name) {
      // SFI object has been overwritten with a new one.
      sfi.name = name;
    }
    let entry = this.codeMap_.findDynamicEntryByStartAddress(start);
    if (entry !== null) {
      if (entry.size === size && entry.sfi === sfi) {
        // Entry state has changed.
        entry.state = state;
      } else {
        this.codeMap_.deleteCode(start);
        entry = null;
      }
    }
    if (entry === null) {
      entry = new DynamicFuncCodeEntry(size, type, sfi, state);
      this.codeMap_.addCode(start, entry);
    }
    return entry;
  }

  /**
   * Reports about moving of a dynamic code entry.
   *
   * @param {number} from Current code entry address.
   * @param {number} to New code entry address.
   */
  moveCode(from, to) {
    try {
      this.codeMap_.moveCode(from, to);
    } catch (e) {
      this.handleUnknownCode(kProfileOperationMove, from);
    }
  }

  deoptCode(timestamp, code, inliningId, scriptOffset, bailoutType,
    sourcePositionText, deoptReasonText) {
  }

  /**
   * Reports about deletion of a dynamic code entry.
   *
   * @param {number} start Starting address.
   */
  deleteCode(start) {
    try {
      this.codeMap_.deleteCode(start);
    } catch (e) {
      this.handleUnknownCode(kProfileOperationDelete, start);
    }
  }

  /**
   * Adds source positions for given code.
   */
  addSourcePositions(start, scriptId, startPos, endPos, sourcePositionTable,
        inliningPositions, inlinedSFIs) {
    const script = this.getOrCreateScript(scriptId);
    const entry = this.codeMap_.findDynamicEntryByStartAddress(start);
    if (entry === null) return;
    // Resolve the inlined SharedFunctionInfo list.
    if (inlinedSFIs.length > 0) {
      inlinedSFIs = inlinedSFIs.substring(1).split("S");
      for (let i = 0; i < inlinedSFIs.length; i++) {
        const sfiAddr = parseInt(inlinedSFIs[i]);
        const sfi = this.codeMap_.findDynamicEntryByStartAddress(sfiAddr);
        if (sfi === null || sfi.funcId === undefined) {
          // TODO: fix
          this.warnings.add(`Could not find function ${inlinedSFIs[i]}`);
          inlinedSFIs[i] = null;
        } else {
          inlinedSFIs[i] = sfi.funcId;
        }
      }
    } else {
      inlinedSFIs = [];
    }

    this.getOrCreateSourceInfo(entry).setSourcePositionInfo(
      script, startPos, endPos, sourcePositionTable, inliningPositions,
      inlinedSFIs);
  }

  addDisassemble(start, kind, disassemble) {
    const entry = this.codeMap_.findDynamicEntryByStartAddress(start);
    if (entry !== null) {
      this.getOrCreateSourceInfo(entry).setDisassemble(disassemble);
    }
    return entry;
  }

  getOrCreateSourceInfo(entry) {
    return entry.source ?? (entry.source = new SourceInfo());
  }

  addScriptSource(id, url, source) {
    const script = this.getOrCreateScript(id);
    script.update(url, source);
    this.urlToScript_.set(url, script);
  }

  getOrCreateScript(id) {
    let script = this.scripts_[id];
    if (script === undefined) {
      script = new Script(id);
      this.scripts_[id] = script;
    }
    return script;
  }

  getScript(url) {
    return this.urlToScript_.get(url);
  }

  /**
   * Reports about moving of a dynamic code entry.
   *
   * @param {number} from Current code entry address.
   * @param {number} to New code entry address.
   */
  moveSharedFunctionInfo(from, to) {
    if (this.codeMap_.findDynamicEntryByStartAddress(from)) {
      this.codeMap_.moveCode(from, to);
    }
  }

  /**
   * Retrieves a code entry by an address.
   *
   * @param {number} addr Entry address.
   */
  findEntry(addr) {
    return this.codeMap_.findEntry(addr);
  }

  /**
   * Records a tick event. Stack must contain a sequence of
   * addresses starting with the program counter value.
   *
   * @param {number[]} stack Stack sample.
   */
  recordTick(time_ns, vmState, stack) {
    const {nameStack, entryStack} = this.resolveAndFilterFuncs_(stack);
    this.bottomUpTree_.addPath(nameStack);
    nameStack.reverse();
    this.topDownTree_.addPath(nameStack);
    return entryStack;
  }

  /**
   * Translates addresses into function names and filters unneeded
   * functions.
   *
   * @param {number[]} stack Stack sample.
   */
  resolveAndFilterFuncs_(stack) {
    const nameStack = [];
    const entryStack = [];
    let last_seen_c_function = '';
    let look_for_first_c_function = false;
    for (let i = 0; i < stack.length; ++i) {
      const pc = stack[i];
      const entry = this.codeMap_.findEntry(pc);
      if (entry !== null) {
        entryStack.push(entry);
        const name = entry.getName();
        if (i === 0 && (entry.type === 'CPP' || entry.type === 'SHARED_LIB')) {
          look_for_first_c_function = true;
        }
        if (look_for_first_c_function && entry.type === 'CPP') {
          last_seen_c_function = name;
        }
        if (!this.skipThisFunction(name)) {
          nameStack.push(name);
        }
      } else {
        this.handleUnknownCode(kProfileOperationTick, pc, i);
        if (i === 0) nameStack.push("UNKNOWN");
        entryStack.push(pc);
      }
      if (look_for_first_c_function && i > 0 &&
          (entry === null || entry.type !== 'CPP')
          && last_seen_c_function !== '') {
        if (this.c_entries_[last_seen_c_function] === undefined) {
          this.c_entries_[last_seen_c_function] = 0;
        }
        this.c_entries_[last_seen_c_function]++;
        look_for_first_c_function = false;  // Found it, we're done.
      }
    }
    return {nameStack, entryStack};
  }

  /**
   * Performs a BF traversal of the top down call graph.
   *
   * @param {function(CallTreeNode)} f Visitor function.
   */
  traverseTopDownTree(f) {
    this.topDownTree_.traverse(f);
  }

  /**
   * Performs a BF traversal of the bottom up call graph.
   *
   * @param {function(CallTreeNode)} f Visitor function.
   */
  traverseBottomUpTree(f) {
    this.bottomUpTree_.traverse(f);
  }

  /**
   * Calculates a top down profile for a node with the specified label.
   * If no name specified, returns the whole top down calls tree.
   *
   * @param {string} opt_label Node label.
   */
  getTopDownProfile(opt_label) {
    return this.getTreeProfile_(this.topDownTree_, opt_label);
  }

  /**
   * Calculates a bottom up profile for a node with the specified label.
   * If no name specified, returns the whole bottom up calls tree.
   *
   * @param {string} opt_label Node label.
   */
  getBottomUpProfile(opt_label) {
    return this.getTreeProfile_(this.bottomUpTree_, opt_label);
  }

  /**
   * Helper function for calculating a tree profile.
   *
   * @param {Profile.CallTree} tree Call tree.
   * @param {string} opt_label Node label.
   */
  getTreeProfile_(tree, opt_label) {
    if (!opt_label) {
      tree.computeTotalWeights();
      return tree;
    } else {
      const subTree = tree.cloneSubtree(opt_label);
      subTree.computeTotalWeights();
      return subTree;
    }
  }

  /**
   * Calculates a flat profile of callees starting from a node with
   * the specified label. If no name specified, starts from the root.
   *
   * @param {string} opt_label Starting node label.
   */
  getFlatProfile(opt_label) {
    const counters = new CallTree();
    const rootLabel = opt_label || CallTree.ROOT_NODE_LABEL;
    const precs = {__proto__:null};
    precs[rootLabel] = 0;
    const root = counters.findOrAddChild(rootLabel);

    this.topDownTree_.computeTotalWeights();
    this.topDownTree_.traverseInDepth(
      function onEnter(node) {
        if (!(node.label in precs)) {
          precs[node.label] = 0;
        }
        const nodeLabelIsRootLabel = node.label == rootLabel;
        if (nodeLabelIsRootLabel || precs[rootLabel] > 0) {
          if (precs[rootLabel] == 0) {
            root.selfWeight += node.selfWeight;
            root.totalWeight += node.totalWeight;
          } else {
            const rec = root.findOrAddChild(node.label);
            rec.selfWeight += node.selfWeight;
            if (nodeLabelIsRootLabel || precs[node.label] == 0) {
              rec.totalWeight += node.totalWeight;
            }
          }
          precs[node.label]++;
        }
      },
      function onExit(node) {
        if (node.label == rootLabel || precs[rootLabel] > 0) {
          precs[node.label]--;
        }
      },
      null);

    if (!opt_label) {
      // If we have created a flat profile for the whole program, we don't
      // need an explicit root in it. Thus, replace the counters tree
      // root with the node corresponding to the whole program.
      counters.root_ = root;
    } else {
      // Propagate weights so percents can be calculated correctly.
      counters.getRoot().selfWeight = root.selfWeight;
      counters.getRoot().totalWeight = root.totalWeight;
    }
    return counters;
  }

  getCEntryProfile() {
    const result = [new CEntryNode("TOTAL", 0)];
    let total_ticks = 0;
    for (let f in this.c_entries_) {
      const ticks = this.c_entries_[f];
      total_ticks += ticks;
      result.push(new CEntryNode(f, ticks));
    }
    result[0].ticks = total_ticks;  // Sorting will keep this at index 0.
    result.sort((n1, n2) => n2.ticks - n1.ticks || (n2.name < n1.name ? -1 : 1));
    return result;
  }


  /**
   * Cleans up function entries that are not referenced by code entries.
   */
  cleanUpFuncEntries() {
    const referencedFuncEntries = [];
    const entries = this.codeMap_.getAllDynamicEntriesWithAddresses();
    for (let i = 0, l = entries.length; i < l; ++i) {
      if (entries[i][1].constructor === SharedFunctionInfoEntry) {
        entries[i][1].used = false;
      }
    }
    for (let i = 0, l = entries.length; i < l; ++i) {
      if ("sfi" in entries[i][1]) {
        entries[i][1].sfi.used = true;
      }
    }
    for (let i = 0, l = entries.length; i < l; ++i) {
      if (entries[i][1].constructor === SharedFunctionInfoEntry &&
        !entries[i][1].used) {
        this.codeMap_.deleteCode(entries[i][0]);
      }
    }
  }
}

class CEntryNode {
  constructor(name, ticks) {
    this.name = name;
    this.ticks = ticks;
  }
}


/**
 * Creates a dynamic code entry.
 *
 * @param {number} size Code size.
 * @param {string} type Code type.
 * @param {string} name Function name.
 * @constructor
 */
class DynamicCodeEntry extends CodeEntry {
  constructor(size, type, name) {
    super(size, name, type);
  }

  getName() {
    return this.type + ': ' + this.name;
  }

  /**
   * Returns raw node name (without type decoration).
   */
  getRawName() {
    return this.name;
  }

  isJSFunction() {
    return false;
  }

  toString() {
    return this.getName() + ': ' + this.size.toString(16);
  }
}


/**
 * Creates a dynamic code entry.
 *
 * @param {number} size Code size.
 * @param {string} type Code type.
 * @param {SharedFunctionInfoEntry} sfi Shared function entry.
 * @param {Profile.CodeState} state Code optimization state.
 * @constructor
 */
class DynamicFuncCodeEntry extends CodeEntry {
  constructor(size, type, sfi, state) {
    super(size, '', type);
    this.sfi = sfi;
    sfi.addDynamicCode(this);
    this.state = state;
  }

  get functionName() {
    return this.sfi.functionName;
  }

  getSourceCode() {
    return this.source?.getSourceCode();
  }

  static STATE_PREFIX = ["", "~", "^", "-", "+", "*"];
  getState() {
    return DynamicFuncCodeEntry.STATE_PREFIX[this.state];
  }

  getName() {
    const name = this.sfi.getName();
    return this.type + ': ' + this.getState() + name;
  }

  /**
   * Returns raw node name (without type decoration).
   */
  getRawName() {
    return this.sfi.getName();
  }

  isJSFunction() {
    return true;
  }

  toString() {
    return this.getName() + ': ' + this.size.toString(16);
  }
}

/**
 * Creates a shared function object entry.
 *
 * @param {string} name Function name.
 * @constructor
 */
class SharedFunctionInfoEntry extends CodeEntry {

  // Contains the list of generated code for this function.
  /** @type {Set<DynamicCodeEntry>} */
  _codeEntries = new Set();

  constructor(name, useBigIntAddresses=false) {
    super(useBigIntAddresses ? 0n : 0, name);
    const index = name.lastIndexOf(' ');
    this.functionName = 1 <= index ? name.substring(0, index) : '<anonymous>';
  }

  addDynamicCode(code) {
    if (code.sfi != this) {
      throw new Error("Adding dynamic code to wrong function");
    }
    this._codeEntries.add(code);
  }

  getSourceCode() {
    // All code entries should map to the same source positions.
    return this._codeEntries.values().next().value.getSourceCode();
  }

  get codeEntries() {
    return this._codeEntries;
  }

  /**
   * Returns node name.
   */
  getName() {
    let name = this.name;
    if (name.length == 0) {
      return '<anonymous>';
    } else if (name.charAt(0) == ' ') {
      // An anonymous function with location: " aaa.js:10".
      return `<anonymous>${name}`;
    }
    return name;
  }
}

/**
 * Constructs a call graph.
 *
 * @constructor
 */
class CallTree {
  root_ = new CallTreeNode(CallTree.ROOT_NODE_LABEL);
  totalsComputed_ = false;

  /**
   * The label of the root node.
   */
  static ROOT_NODE_LABEL = '';

  /**
   * Returns the tree root.
   */
  getRoot() {
    return this.root_;
  }

  /**
   * Adds the specified call path, constructing nodes as necessary.
   *
   * @param {string[]} path Call path.
   */
  addPath(path) {
    if (path.length == 0) return;
    let curr = this.root_;
    for (let i = 0; i < path.length; ++i) {
      curr = curr.findOrAddChild(path[i]);
    }
    curr.selfWeight++;
    this.totalsComputed_ = false;
  }

  /**
   * Finds an immediate child of the specified parent with the specified
   * label, creates a child node if necessary. If a parent node isn't
   * specified, uses tree root.
   *
   * @param {string} label Child node label.
   */
  findOrAddChild(label) {
    return this.root_.findOrAddChild(label);
  }

  /**
   * Creates a subtree by cloning and merging all subtrees rooted at nodes
   * with a given label. E.g. cloning the following call tree on label 'A'
   * will give the following result:
   *
   *           <A>--<B>                                     <B>
   *          /                                            /
   *     <root>             == clone on 'A' ==>  <root>--<A>
   *          \                                            \
   *           <C>--<A>--<D>                                <D>
   *
   * And <A>'s selfWeight will be the sum of selfWeights of <A>'s from the
   * source call tree.
   *
   * @param {string} label The label of the new root node.
   */
  cloneSubtree(label) {
    const subTree = new CallTree();
    this.traverse((node, parent) => {
      if (!parent && node.label != label) {
        return null;
      }
      const child = (parent ? parent : subTree).findOrAddChild(node.label);
      child.selfWeight += node.selfWeight;
      return child;
    });
    return subTree;
  }

  /**
   * Computes total weights in the call graph.
   */
  computeTotalWeights() {
    if (this.totalsComputed_) return;
    this.root_.computeTotalWeight();
    this.totalsComputed_ = true;
  }

  /**
   * Traverses the call graph in preorder. This function can be used for
   * building optionally modified tree clones. This is the boilerplate code
   * for this scenario:
   *
   * callTree.traverse(function(node, parentClone) {
   *   var nodeClone = cloneNode(node);
   *   if (parentClone)
   *     parentClone.addChild(nodeClone);
   *   return nodeClone;
   * });
   *
   * @param {function(CallTreeNode, *)} f Visitor function.
   *    The second parameter is the result of calling 'f' on the parent node.
   */
  traverse(f) {
    const pairsToProcess = new ConsArray();
    pairsToProcess.concat([{ node: this.root_, param: null }]);
    while (!pairsToProcess.atEnd()) {
      const pair = pairsToProcess.next();
      const node = pair.node;
      const newParam = f(node, pair.param);
      const morePairsToProcess = [];
      node.forEachChild((child) => {
        morePairsToProcess.push({ node: child, param: newParam });
      });
      pairsToProcess.concat(morePairsToProcess);
    }
  }

  /**
   * Performs an indepth call graph traversal.
   *
   * @param {function(CallTreeNode)} enter A function called
   *     prior to visiting node's children.
   * @param {function(CallTreeNode)} exit A function called
   *     after visiting node's children.
   */
  traverseInDepth(enter, exit) {
    function traverse(node) {
      enter(node);
      node.forEachChild(traverse);
      exit(node);
    }
    traverse(this.root_);
  }
}


/**
 * Constructs a call graph node.
 *
 * @param {string} label Node label.
 * @param {CallTreeNode} opt_parent Node parent.
 */
class CallTreeNode {

  constructor(label, opt_parent) {
    // Node self weight (how many times this node was the last node in
    // a call path).
    this.selfWeight = 0;
    // Node total weight (includes weights of all children).
    this.totalWeight = 0;
    this. children = { __proto__:null };
    this.label = label;
    this.parent = opt_parent;
  }


  /**
   * Adds a child node.
   *
   * @param {string} label Child node label.
   */
  addChild(label) {
    const child = new CallTreeNode(label, this);
    this.children[label] = child;
    return child;
  }

  /**
   * Computes node's total weight.
   */
  computeTotalWeight() {
    let totalWeight = this.selfWeight;
    this.forEachChild(function (child) {
      totalWeight += child.computeTotalWeight();
    });
    return this.totalWeight = totalWeight;
  }

  /**
   * Returns all node's children as an array.
   */
  exportChildren() {
    const result = [];
    this.forEachChild(function (node) { result.push(node); });
    return result;
  }

  /**
   * Finds an immediate child with the specified label.
   *
   * @param {string} label Child node label.
   */
  findChild(label) {
    const found = this.children[label];
    return found === undefined ? null : found;
  }

  /**
   * Finds an immediate child with the specified label, creates a child
   * node if necessary.
   *
   * @param {string} label Child node label.
   */
  findOrAddChild(label) {
    const found = this.findChild(label)
    if (found === null) return this.addChild(label);
    return found;
  }

  /**
   * Calls the specified function for every child.
   *
   * @param {function(CallTreeNode)} f Visitor function.
   */
  forEachChild(f) {
    for (let c in this.children) {
      f(this.children[c]);
    }
  }

  /**
   * Walks up from the current node up to the call tree root.
   *
   * @param {function(CallTreeNode)} f Visitor function.
   */
  walkUpToRoot(f) {
    for (let curr = this; curr !== null; curr = curr.parent) {
      f(curr);
    }
  }

  /**
   * Tries to find a node with the specified path.
   *
   * @param {string[]} labels The path.
   * @param {function(CallTreeNode)} opt_f Visitor function.
   */
  descendToChild(labels, opt_f) {
    let curr = this;
    for (let pos = 0; pos < labels.length && curr != null; pos++) {
      const child = curr.findChild(labels[pos]);
      if (opt_f) {
        opt_f(child, pos);
      }
      curr = child;
    }
    return curr;
  }
}

export function JsonProfile(useBigIntAddresses=false) {
  this.codeMap_ = new CodeMap(useBigIntAddresses);
  this.codeEntries_ = [];
  this.functionEntries_ = [];
  this.ticks_ = [];
  this.scripts_ = [];
}

JsonProfile.prototype.addLibrary = function (
  name, startAddr, endAddr) {
  const entry = new CodeEntry(
    endAddr - startAddr, name, 'SHARED_LIB');
  this.codeMap_.addLibrary(startAddr, entry);

  entry.codeId = this.codeEntries_.length;
  this.codeEntries_.push({ name: entry.name, type: entry.type });
  return entry;
};

JsonProfile.prototype.addStaticCode = function (
  name, startAddr, endAddr) {
  const entry = new CodeEntry(
    endAddr - startAddr, name, 'CPP');
  this.codeMap_.addStaticCode(startAddr, entry);

  entry.codeId = this.codeEntries_.length;
  this.codeEntries_.push({ name: entry.name, type: entry.type });
  return entry;
};

JsonProfile.prototype.addCode = function (
  kind, name, timestamp, start, size) {
  let codeId = this.codeEntries_.length;
  // Find out if we have a static code entry for the code. If yes, we will
  // make sure it is written to the JSON file just once.
  let staticEntry = this.codeMap_.findAddress(start);
  if (staticEntry && staticEntry.entry.type === 'CPP') {
    codeId = staticEntry.entry.codeId;
  }

  const entry = new CodeEntry(size, name, 'CODE');
  this.codeMap_.addCode(start, entry);

  entry.codeId = codeId;
  this.codeEntries_[codeId] = {
    name: entry.name,
    timestamp: timestamp,
    type: entry.type,
    kind: kind,
  };

  return entry;
};

JsonProfile.prototype.addFuncCode = function (
  kind, name, timestamp, start, size, sfiAddr, state) {
  // As code and functions are in the same address space,
  // it is safe to put them in a single code map.
  let sfi = this.codeMap_.findDynamicEntryByStartAddress(sfiAddr);
  if (!sfi) {
    sfi = new CodeEntry(0, name, 'SFI');
    this.codeMap_.addCode(sfiAddr, sfi);

    sfi.funcId = this.functionEntries_.length;
    this.functionEntries_.push({ name, codes: [] });
  } else if (sfi.name !== name) {
    // Function object has been overwritten with a new one.
    sfi.name = name;

    sfi.funcId = this.functionEntries_.length;
    this.functionEntries_.push({ name, codes: [] });
  }
  // TODO(jarin): Insert the code object into the SFI's code list.
  let entry = this.codeMap_.findDynamicEntryByStartAddress(start);
  if (entry) {
    if (entry.size === size && entry.sfi === sfi) {
      // Entry state has changed.
      entry.state = state;
    } else {
      this.codeMap_.deleteCode(start);
      entry = null;
    }
  }
  if (!entry) {
    entry = new CodeEntry(size, name, 'JS');
    this.codeMap_.addCode(start, entry);

    entry.codeId = this.codeEntries_.length;

    this.functionEntries_[sfi.funcId].codes.push(entry.codeId);

    kind = Profile.getKindFromState(state);

    this.codeEntries_.push({
      name: entry.name,
      type: entry.type,
      kind: kind,
      func: sfi.funcId,
      tm: timestamp,
    });
  }
  return entry;
};

JsonProfile.prototype.moveCode = function (from, to) {
  try {
    this.codeMap_.moveCode(from, to);
  } catch (e) {
    printErr(`Move: unknown source ${from}`);
  }
};

JsonProfile.prototype.addSourcePositions = function (
  start, script, startPos, endPos, sourcePositions, inliningPositions,
  inlinedSFIs) {
  const entry = this.codeMap_.findDynamicEntryByStartAddress(start);
  if (!entry) return;
  const codeId = entry.codeId;

  // Resolve the inlined functions list.
  if (inlinedSFIs.length > 0) {
    inlinedSFIs = inlinedSFIs.substring(1).split("S");
    for (let i = 0; i < inlinedSFIs.length; i++) {
      const sfiAddr = parseInt(inlinedSFIs[i]);
      const sfi = this.codeMap_.findDynamicEntryByStartAddress(sfiAddr);
      if (!sfi || sfi.funcId === undefined) {
        printErr(`Could not find SFI ${inlinedSFIs[i]}`);
        inlinedSFIs[i] = null;
      } else {
        inlinedSFIs[i] = sfi.funcId;
      }
    }
  } else {
    inlinedSFIs = [];
  }

  this.codeEntries_[entry.codeId].source = {
    script: script,
    start: startPos,
    end: endPos,
    positions: sourcePositions,
    inlined: inliningPositions,
    fns: inlinedSFIs
  };
};

JsonProfile.prototype.addScriptSource = function (id, url, source) {
  const script = new Script(id);
  script.update(url, source);
  this.scripts_[id] = script;
};

JsonProfile.prototype.deoptCode = function (
  timestamp, code, inliningId, scriptOffset, bailoutType,
  sourcePositionText, deoptReasonText) {
  let entry = this.codeMap_.findDynamicEntryByStartAddress(code);
  if (entry) {
    let codeId = entry.codeId;
    if (!this.codeEntries_[codeId].deopt) {
      // Only add the deopt if there was no deopt before.
      // The subsequent deoptimizations should be lazy deopts for
      // other on-stack activations.
      this.codeEntries_[codeId].deopt = {
        tm: timestamp,
        inliningId: inliningId,
        scriptOffset: scriptOffset,
        posText: sourcePositionText,
        reason: deoptReasonText,
        bailoutType: bailoutType,
      };
    }
  }
};

JsonProfile.prototype.deleteCode = function (start) {
  try {
    this.codeMap_.deleteCode(start);
  } catch (e) {
    printErr(`Delete: unknown address ${start}`);
  }
};

JsonProfile.prototype.moveSharedFunctionInfo = function (from, to) {
  if (this.codeMap_.findDynamicEntryByStartAddress(from)) {
    this.codeMap_.moveCode(from, to);
  }
};

JsonProfile.prototype.findEntry = function (addr) {
  return this.codeMap_.findEntry(addr);
};

JsonProfile.prototype.recordTick = function (time_ns, vmState, stack) {
  // TODO(jarin) Resolve the frame-less case (when top of stack is
  // known code).
  const processedStack = [];
  for (let i = 0; i < stack.length; i++) {
    const resolved = this.codeMap_.findAddress(stack[i]);
    if (resolved) {
      processedStack.push(resolved.entry.codeId, resolved.offset);
    } else {
      processedStack.push(-1, stack[i]);
    }
  }
  this.ticks_.push({ tm: time_ns, vm: vmState, s: processedStack });
};

function writeJson(s) {
  write(JSON.stringify(s, null, 2));
}

JsonProfile.prototype.writeJson = function () {
  // Write out the JSON in a partially manual way to avoid creating too-large
  // strings in one JSON.stringify call when there are a lot of ticks.
  write('{\n')

  write('  "code": ');
  writeJson(this.codeEntries_);
  write(',\n');

  write('  "functions": ');
  writeJson(this.functionEntries_);
  write(',\n');

  write('  "ticks": [\n');
  for (let i = 0; i < this.ticks_.length; i++) {
    write('    ');
    writeJson(this.ticks_[i]);
    if (i < this.ticks_.length - 1) {
      write(',\n');
    } else {
      write('\n');
    }
  }
  write('  ],\n');

  write('  "scripts": ');
  writeJson(this.scripts_);

  write('}\n');
};
