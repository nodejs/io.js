// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict"

function $(id) {
  return document.getElementById(id);
}

function removeAllChildren(element) {
  while (element.firstChild) {
    element.removeChild(element.firstChild);
  }
}

let components;
function createViews() {
  components = [
    new CallTreeView(),
    new TimelineView(),
    new HelpView(),
    new SummaryView(),
    new ModeBarView(),
    new ScriptSourceView(),
  ];
}

function emptyState() {
  return {
    file : null,
    mode : null,
    currentCodeId : null,
    viewingSource: false,
    start : 0,
    end : Infinity,
    timelineSize : {
      width : 0,
      height : 0
    },
    callTree : {
      attribution : "js-exclude-bc",
      categories : "code-type",
      sort : "time"
    },
    sourceData: null,
    showLogging: false,
  };
}

function setCallTreeState(state, callTreeState) {
  state = Object.assign({}, state);
  state.callTree = callTreeState;
  return state;
}

let main = {
  currentState : emptyState(),
  renderPending : false,

  setMode(mode) {
    if (mode !== main.currentState.mode) {

      function setCallTreeModifiers(attribution, categories, sort) {
        let callTreeState = Object.assign({}, main.currentState.callTree);
        callTreeState.attribution = attribution;
        callTreeState.categories = categories;
        callTreeState.sort = sort;
        return callTreeState;
      }

      let state = Object.assign({}, main.currentState);

      switch (mode) {
        case "bottom-up":
          state.callTree =
              setCallTreeModifiers("js-exclude-bc", "code-type", "time");
          break;
        case "top-down":
          state.callTree =
              setCallTreeModifiers("js-exclude-bc", "none", "time");
          break;
        case "function-list":
          state.callTree =
              setCallTreeModifiers("js-exclude-bc", "code-type", "own-time");
          break;
      }

      state.mode = mode;

      main.currentState = state;
      main.delayRender();
    }
  },

  setCallTreeAttribution(attribution) {
    if (attribution !== main.currentState.attribution) {
      let callTreeState = Object.assign({}, main.currentState.callTree);
      callTreeState.attribution = attribution;
      main.currentState = setCallTreeState(main.currentState,  callTreeState);
      main.delayRender();
    }
  },

  setCallTreeSort(sort) {
    if (sort !== main.currentState.sort) {
      let callTreeState = Object.assign({}, main.currentState.callTree);
      callTreeState.sort = sort;
      main.currentState = setCallTreeState(main.currentState,  callTreeState);
      main.delayRender();
    }
  },

  setCallTreeCategories(categories) {
    if (categories !== main.currentState.categories) {
      let callTreeState = Object.assign({}, main.currentState.callTree);
      callTreeState.categories = categories;
      main.currentState = setCallTreeState(main.currentState,  callTreeState);
      main.delayRender();
    }
  },

  setViewInterval(start, end) {
    if (start !== main.currentState.start ||
        end !== main.currentState.end) {
      main.currentState = Object.assign({}, main.currentState);
      main.currentState.start = start;
      main.currentState.end = end;
      main.delayRender();
    }
  },

  updateSources(file) {
    let statusDiv = $("source-status");
    if (!file) {
      statusDiv.textContent = "";
      return;
    }
    if (!file.scripts || file.scripts.length === 0) {
      statusDiv.textContent =
          "Script source not available. Run profiler with --log-source-code.";
      return;
    }
    statusDiv.textContent = "Script source is available.";
    main.currentState.sourceData = new SourceData(file);
  },

  filterFileTicks(file, showLogging) {
    if (showLogging) return file;

    // Filter out ticks with the logging VMState, if necessary. This is done
    // directly on the file ticks, rather than as a filter on the tick
    // processors, so that the timestamps can be adjusted as if the stacks were
    // never there.
    let filtered_ticks = [];
    let currentTimestampCorrection = 0;
    let timestampCorrections = [];
    for (let i = 0; i < file.ticks.length; ++i) {
      let tick = file.ticks[i];
      // Fitler VMState == CPP_LOGGING.
      if (tick.vm == 9) {
        if (i > 0) {
          currentTimestampCorrection += tick.tm - file.ticks[i - 1].tm;
          timestampCorrections.push({
            tm: tick.tm,
            correction: currentTimestampCorrection
          });
        }
      } else if (currentTimestampCorrection == 0) {
        filtered_ticks.push(tick);
      } else {
        let new_tick = {...tick};
        new_tick.tm -= currentTimestampCorrection;
        filtered_ticks.push(new_tick);
      }
    }

    // Now fix up creation timestamps of code objects.

    // Binary search for the lower bound timestamp correction.
    function applyTimestampCorrection(tm) {
      let corrections = timestampCorrections;

      if (corrections.length == 0) return tm;
      let start = 0;
      let end = corrections.length - 1;
      while (start <= end) {
        let middle = (start + end) >> 1;
        if (corrections[middle].tm <= tm) {
          start = middle + 1;
        } else {
          end = middle - 1;
        }
      }
      if (start == 0) return tm;
      return tm - corrections[start - 1].correction
    }

    let filtered_code = [];
    for (let i = 0; i < file.code.length; ++i) {
      let code = file.code[i];
      if (code.type == "JS") {
        let new_code = { ...code };
        new_code.tm =
            applyTimestampCorrection(new_code.tm, timestampCorrections);

        if (code.deopt) {
          let new_deopt = { ...code.deopt };
          new_deopt.tm =
              applyTimestampCorrection(new_deopt.tm, timestampCorrections);
          new_code.deopt = new_deopt;
        }

        filtered_code.push(new_code);
      } else {
        filtered_code.push(code);
      }
    }
    return {...file, code: filtered_code, ticks: filtered_ticks};
  },

  setFile(file) {
    if (file !== main.currentState.unfilteredFile) {
      let lastMode = main.currentState.mode || "summary";
      main.currentState = emptyState();
      main.currentState.unfilteredFile = file;
      main.currentState.file =
          main.filterFileTicks(file, main.currentState.showLogging);
      main.updateSources(file);
      main.setMode(lastMode);
      main.delayRender();
    }
  },

  setCurrentCode(codeId) {
    if (codeId !== main.currentState.currentCodeId) {
      main.currentState = Object.assign({}, main.currentState);
      main.currentState.currentCodeId = codeId;
      main.delayRender();
    }
  },

  setViewingSource(value) {
    if (main.currentState.viewingSource !== value) {
      main.currentState = Object.assign({}, main.currentState);
      main.currentState.viewingSource = value;
      main.delayRender();
    }
  },

  setShowLogging(value) {
    if (main.currentState.showLogging !== value) {
      main.currentState = Object.assign({}, main.currentState);
      main.currentState.showLogging = value;
      main.currentState.file = main.filterFileTicks(main.currentState.unfilteredFile, value);
      main.delayRender();
    }
  },

  onResize() {
    main.delayRender();
  },

  onLoad() {
    function loadHandler(evt) {
      let f = evt.target.files[0];
      if (f) {
        let reader = new FileReader();
        reader.onload = function(event) {
          main.setFile(JSON.parse(event.target.result));
        };
        reader.onerror = function(event) {
          console.error(
              "File could not be read! Code " + event.target.error.code);
        };
        reader.readAsText(f);
      } else {
        main.setFile(null);
      }
    }
    $("fileinput").addEventListener(
        "change", loadHandler, false);
    createViews();
  },

  delayRender()  {
    if (main.renderPending) return;
    main.renderPending = true;

    window.requestAnimationFrame(() => {
      main.renderPending = false;
      for (let c of components) {
        c.render(main.currentState);
      }
    });
  }
};

const CATEGORY_COLOR = "#f5f5f5";
const bucketDescriptors =
  [
  {
    kinds: ["JS_IGNITION", "BC"],
    color: "#dd2c00",
    backgroundColor: "#ff9e80",
    text: "JS Ignition"
  },
  {
    kinds: ["JS_SPARKPLUG"],
    color: "#b3005b",
    backgroundColor: "#ff9e80",
    text: "JS Sparkplug"
  },
  {
    kinds: ["JS_MAGLEV"],
    color: "#693eb8",
    backgroundColor: "#d80093",
    text: "JS Maglev"
  },
  {
    kinds: ["JS_TURBOFAN"],
    color: "#64dd17",
    backgroundColor: "#80e27e",
    text: "JS Turbofan"
  },
  {
    kinds: ["IC"],
    color: "#ff6d00",
    backgroundColor: "#ffab40",
    text: "IC"
  },
  {
    kinds: ["STUB", "BUILTIN", "REGEXP"],
    color: "#ffd600",
    backgroundColor: "#ffea00",
    text: "Other generated"
  },
  {
    kinds: ["CPP", "LIB"],
    color: "#304ffe",
    backgroundColor: "#6ab7ff",
    text: "C++"
  },
  {
    kinds: ["CPP_EXT"],
    color: "#003c8f",
    backgroundColor: "#c0cfff",
    text: "C++/external"
  },
  {
    kinds: ["CPP_PARSE"],
    color: "#aa00ff",
    backgroundColor: "#ffb2ff",
    text: "C++/Parser"
  },
  {
    kinds: ["CPP_COMP_BC"],
    color: "#43a047",
    backgroundColor: "#88c399",
    text: "C++/Bytecode compiler"
  },
  {
    kinds: ["CPP_COMP_BASELINE"],
    color: "#8fba29",
    backgroundColor: "#5a8000",
    text: "C++/Baseline compiler"
  },
  {
    kinds: ["CPP_COMP"],
    color: "#00e5ff",
    backgroundColor: "#6effff",
    text: "C++/Compiler"
  },
  {
    kinds: ["CPP_GC"],
    color: "#6200ea",
    backgroundColor: "#e1bee7",
    text: "C++/GC"
  },
  {
    kinds: ["CPP_LOGGING"],
    color: "#dedede",
    backgroundColor: "#efefef",
    text: "C++/Logging"
  },
  {
    kinds: ["UNKNOWN"],
    color: "#bdbdbd",
    backgroundColor: "#efefef",
    text: "Unknown"
  }
  ];

let kindToBucketDescriptor = {};
for (let i = 0; i < bucketDescriptors.length; i++) {
  let bucket = bucketDescriptors[i];
  for (let j = 0; j < bucket.kinds.length; j++) {
    kindToBucketDescriptor[bucket.kinds[j]] = bucket;
  }
}

function bucketFromKind(kind) {
  for (let i = 0; i < bucketDescriptors.length; i++) {
    let bucket = bucketDescriptors[i];
    for (let j = 0; j < bucket.kinds.length; j++) {
      if (bucket.kinds[j] === kind) {
        return bucket;
      }
    }
  }
  return null;
}

function codeTypeToText(type) {
  switch (type) {
    case "UNKNOWN":
      return "Unknown";
    case "CPP_PARSE":
      return "C++ Parser";
    case "CPP_COMP_BASELINE":
      return "C++ Baseline Compiler";
    case "CPP_COMP_BC":
      return "C++ Bytecode Compiler";
    case "CPP_COMP":
      return "C++ Compiler";
    case "CPP_GC":
      return "C++ GC";
    case "CPP_EXT":
      return "C++ External";
    case "CPP_LOGGING":
      return "C++ Logging";
    case "CPP":
      return "C++";
    case "LIB":
      return "Library";
    case "IC":
      return "IC";
    case "BC":
      return "Bytecode";
    case "STUB":
      return "Stub";
    case "BUILTIN":
      return "Builtin";
    case "REGEXP":
      return "RegExp";
    case "JS_IGNITION":
      return "JS Ignition";
    case "JS_SPARKPLUG":
      return "JS Sparkplug";
    case "JS_MAGLEV":
      return "JS Maglev";
    case "JS_TURBOFAN":
      return "JS Turbofan";
  }
  console.error("Unknown type: " + type);
}

function createTypeNode(type) {
  if (type === "CAT") {
    return document.createTextNode("");
  }
  let span = document.createElement("span");
  span.classList.add("code-type-chip");
  span.textContent = codeTypeToText(type);

  return span;
}

function filterFromFilterId(id) {
  switch (id) {
    case "full-tree":
      return (type, kind) => true;
    case "js-funs":
      return (type, kind) => type !== 'CODE';
    case "js-exclude-bc":
      return (type, kind) =>
          type !== 'CODE' || kind !== "BytecodeHandler";
  }
}

function createIndentNode(indent) {
  let div = document.createElement("div");
  div.style.display = "inline-block";
  div.style.width = (indent + 0.5) + "em";
  return div;
}

function createArrowNode() {
  let span = document.createElement("span");
  span.classList.add("tree-row-arrow");
  return span;
}

function createFunctionNode(name, codeId) {
  let nameElement = document.createElement("span");
  nameElement.appendChild(document.createTextNode(name));
  nameElement.classList.add("tree-row-name");
  if (codeId !== -1) {
    nameElement.classList.add("codeid-link");
    nameElement.onclick = (event) => {
      main.setCurrentCode(codeId);
      // Prevent the click from bubbling to the row and causing it to
      // collapse/expand.
      event.stopPropagation();
    };
  }
  return nameElement;
}

function createViewSourceNode(codeId) {
  let linkElement = document.createElement("span");
  linkElement.appendChild(document.createTextNode("View source"));
  linkElement.classList.add("view-source-link");
  linkElement.onclick = (event) => {
    main.setCurrentCode(codeId);
    main.setViewingSource(true);
    // Prevent the click from bubbling to the row and causing it to
    // collapse/expand.
    event.stopPropagation();
  };
  return linkElement;
}

const COLLAPSED_ARROW = "\u25B6";
const EXPANDED_ARROW = "\u25BC";

class CallTreeView {
  constructor() {
    this.element = $("calltree");
    this.treeElement = $("calltree-table");
    this.selectAttribution = $("calltree-attribution");
    this.selectCategories = $("calltree-categories");
    this.selectSort = $("calltree-sort");

    this.selectAttribution.onchange = () => {
      main.setCallTreeAttribution(this.selectAttribution.value);
    };

    this.selectCategories.onchange = () => {
      main.setCallTreeCategories(this.selectCategories.value);
    };

    this.selectSort.onchange = () => {
      main.setCallTreeSort(this.selectSort.value);
    };

    this.currentState = null;
  }

  sortFromId(id) {
    switch (id) {
      case "time":
        return (c1, c2) => {
          if (c1.ticks < c2.ticks) return 1;
          else if (c1.ticks > c2.ticks) return -1;
          return c2.ownTicks - c1.ownTicks;
        };
      case "own-time":
        return (c1, c2) => {
          if (c1.ownTicks < c2.ownTicks) return 1;
          else if (c1.ownTicks > c2.ownTicks) return -1;
          return c2.ticks - c1.ticks;
        };
      case "category-time":
        return (c1, c2) => {
          if (c1.type === c2.type) return c2.ticks - c1.ticks;
          if (c1.type < c2.type) return 1;
          return -1;
        };
      case "category-own-time":
        return (c1, c2) => {
          if (c1.type === c2.type) return c2.ownTicks - c1.ownTicks;
          if (c1.type < c2.type) return 1;
          return -1;
        };
    }
  }

  expandTree(tree, indent) {
    let index = 0;
    let id = "R/";
    let row = tree.row;

    if (row) {
      index = row.rowIndex;
      id = row.id;

      tree.arrow.textContent = EXPANDED_ARROW;
      // Collapse the children when the row is clicked again.
      let expandHandler = row.onclick;
      row.onclick = () => {
        this.collapseRow(tree, expandHandler);
      }
    }

    // Collect the children, and sort them by ticks.
    let children = [];
    let filter =
        filterFromFilterId(this.currentState.callTree.attribution);
    for (let childId in tree.children) {
      let child = tree.children[childId];
      if (child.ticks > 0) {
        children.push(child);
        if (child.delayedExpansion) {
          expandTreeNode(this.currentState.file, child, filter);
        }
      }
    }
    children.sort(this.sortFromId(this.currentState.callTree.sort));

    for (let i = 0; i < children.length; i++) {
      let node = children[i];
      let row = this.rows.insertRow(index);
      row.id = id + i + "/";

      if (node.type === "CAT") {
        row.style.backgroundColor = CATEGORY_COLOR;
      } else {
        row.style.backgroundColor = bucketFromKind(node.type).backgroundColor;
      }

      // Inclusive time % cell.
      let c = row.insertCell();
      c.textContent = (node.ticks * 100 / this.tickCount).toFixed(2) + "%";
      c.style.textAlign = "right";
      // Percent-of-parent cell.
      c = row.insertCell();
      c.textContent = (node.ticks * 100 / tree.ticks).toFixed(2) + "%";
      c.style.textAlign = "right";
      // Exclusive time % cell.
      if (this.currentState.mode !== "bottom-up") {
        c = row.insertCell(-1);
        c.textContent = (node.ownTicks * 100 / this.tickCount).toFixed(2) + "%";
        c.style.textAlign = "right";
      }

      // Create the name cell.
      let nameCell = row.insertCell();
      nameCell.appendChild(createIndentNode(indent + 1));
      let arrow = createArrowNode();
      nameCell.appendChild(arrow);
      nameCell.appendChild(createTypeNode(node.type));
      nameCell.appendChild(createFunctionNode(node.name, node.codeId));
      if (main.currentState.sourceData &&
          node.codeId >= 0 &&
          main.currentState.sourceData.hasSource(
              this.currentState.file.code[node.codeId].func)) {
        nameCell.appendChild(createViewSourceNode(node.codeId));
      }

      // Inclusive ticks cell.
      c = row.insertCell();
      c.textContent = node.ticks;
      c.style.textAlign = "right";
      if (this.currentState.mode !== "bottom-up") {
        // Exclusive ticks cell.
        c = row.insertCell(-1);
        c.textContent = node.ownTicks;
        c.style.textAlign = "right";
      }
      if (node.children.length > 0) {
        arrow.textContent = COLLAPSED_ARROW;
        row.onclick = () => { this.expandTree(node, indent + 1); };
      }

      node.row = row;
      node.arrow = arrow;

      index++;
    }
  }

  collapseRow(tree, expandHandler) {
    let row = tree.row;
    let id = row.id;
    let index = row.rowIndex;
    while (row.rowIndex < this.rows.rows.length &&
        this.rows.rows[index].id.startsWith(id)) {
      this.rows.deleteRow(index);
    }

    tree.arrow.textContent = COLLAPSED_ARROW;
    row.onclick = expandHandler;
  }

  fillSelects(mode, calltree) {
    function addOptions(e, values, current) {
      while (e.options.length > 0) {
        e.remove(0);
      }
      for (let i = 0; i < values.length; i++) {
        let option = document.createElement("option");
        option.value = values[i].value;
        option.textContent = values[i].text;
        e.appendChild(option);
      }
      e.value = current;
    }

    let attributions = [
        { value : "js-exclude-bc",
          text : "Attribute bytecode handlers to caller" },
        { value : "full-tree",
          text : "Count each code object separately" },
        { value : "js-funs",
          text : "Attribute non-functions to JS functions"  }
    ];

    switch (mode) {
      case "bottom-up":
        addOptions(this.selectAttribution, attributions, calltree.attribution);
        addOptions(this.selectCategories, [
            { value : "code-type", text : "Code type" },
            { value : "none", text : "None" }
        ], calltree.categories);
        addOptions(this.selectSort, [
            { value : "time", text : "Time (including children)" },
            { value : "category-time", text : "Code category, time" },
        ], calltree.sort);
        return;
      case "top-down":
        addOptions(this.selectAttribution, attributions, calltree.attribution);
        addOptions(this.selectCategories, [
            { value : "none", text : "None" },
            { value : "rt-entry", text : "Runtime entries" }
        ], calltree.categories);
        addOptions(this.selectSort, [
            { value : "time", text : "Time (including children)" },
            { value : "own-time", text : "Own time" },
            { value : "category-time", text : "Code category, time" },
            { value : "category-own-time", text : "Code category, own time"}
        ], calltree.sort);
        return;
      case "function-list":
        addOptions(this.selectAttribution, attributions, calltree.attribution);
        addOptions(this.selectCategories, [
            { value : "code-type", text : "Code type" },
            { value : "none", text : "None" }
        ], calltree.categories);
        addOptions(this.selectSort, [
            { value : "own-time", text : "Own time" },
            { value : "time", text : "Time (including children)" },
            { value : "category-own-time", text : "Code category, own time"},
            { value : "category-time", text : "Code category, time" },
        ], calltree.sort);
        return;
    }
    console.error("Unexpected mode");
  }

  static isCallTreeMode(mode) {
    switch (mode) {
      case "bottom-up":
      case "top-down":
      case "function-list":
        return true;
      default:
        return false;
    }
  }

  render(newState) {
    let oldState = this.currentState;
    if (!newState.file || !CallTreeView.isCallTreeMode(newState.mode)) {
      this.element.style.display = "none";
      this.currentState = null;
      return;
    }

    this.currentState = newState;
    if (oldState) {
      if (newState.file === oldState.file &&
          newState.start === oldState.start &&
          newState.end === oldState.end &&
          newState.mode === oldState.mode &&
          newState.callTree.attribution === oldState.callTree.attribution &&
          newState.callTree.categories === oldState.callTree.categories &&
          newState.callTree.sort === oldState.callTree.sort) {
        // No change => just return.
        return;
      }
    }

    this.element.style.display = "inherit";

    let mode = this.currentState.mode;
    if (!oldState || mode !== oldState.mode) {
      // Technically, we should also call this if attribution, categories or
      // sort change, but the selection is already highlighted by the combobox
      // itself, so we do need to do anything here.
      this.fillSelects(newState.mode, newState.callTree);
    }

    let ownTimeClass = (mode === "bottom-up") ? "numeric-hidden" : "numeric";
    let ownTimeTh = $(this.treeElement.id + "-own-time-header");
    ownTimeTh.classList = ownTimeClass;
    let ownTicksTh = $(this.treeElement.id + "-own-ticks-header");
    ownTicksTh.classList = ownTimeClass;

    // Build the tree.
    let stackProcessor;
    let filter = filterFromFilterId(this.currentState.callTree.attribution);
    if (mode === "top-down") {
      if (this.currentState.callTree.categories === "rt-entry") {
        stackProcessor =
            new RuntimeCallTreeProcessor();
      } else {
        stackProcessor =
            new PlainCallTreeProcessor(filter, false);
      }
    } else if (mode === "function-list") {
      stackProcessor = new FunctionListTree(
          filter, this.currentState.callTree.categories === "code-type");

    } else {
      console.assert(mode === "bottom-up");
      if (this.currentState.callTree.categories === "none") {
        stackProcessor =
            new PlainCallTreeProcessor(filter, true);
      } else {
        console.assert(this.currentState.callTree.categories === "code-type");
        stackProcessor =
            new CategorizedCallTreeProcessor(filter, true);
      }
    }
    this.tickCount =
        generateTree(this.currentState.file,
                     this.currentState.start,
                     this.currentState.end,
                     stackProcessor);
    // TODO(jarin) Handle the case when tick count is negative.

    this.tree = stackProcessor.tree;

    // Remove old content of the table, replace with new one.
    let oldRows = this.treeElement.getElementsByTagName("tbody");
    let newRows = document.createElement("tbody");
    this.rows = newRows;

    // Populate the table.
    this.expandTree(this.tree, 0);

    // Swap in the new rows.
    this.treeElement.replaceChild(newRows, oldRows[0]);
  }
}

class TimelineView {
  constructor() {
    this.element = $("timeline");
    this.canvas = $("timeline-canvas");
    this.legend = $("timeline-legend");
    this.currentCode = $("timeline-currentCode");

    this.canvas.onmousedown = this.onMouseDown.bind(this);
    this.canvas.onmouseup = this.onMouseUp.bind(this);
    this.canvas.onmousemove = this.onMouseMove.bind(this);

    this.selectionStart = null;
    this.selectionEnd = null;
    this.selecting = false;

    this.fontSize = 12;
    this.imageOffset = Math.round(this.fontSize * 1.2);
    this.functionTimelineHeight = 24;
    this.functionTimelineTickHeight = 16;

    this.currentState = null;

    this.showLoggingInput = $("show-logging");
    this.showLoggingInput.onchange = () => {
      main.setShowLogging(this.showLoggingInput.checked);
    };
  }

  onMouseDown(e) {
    this.selectionStart =
        e.clientX - this.canvas.getBoundingClientRect().left;
    this.selectionEnd = this.selectionStart + 1;
    this.selecting = true;
  }

  onMouseMove(e) {
    if (this.selecting) {
      this.selectionEnd =
          e.clientX - this.canvas.getBoundingClientRect().left;
      this.drawSelection();
    }
  }

  onMouseUp(e) {
    if (this.selectionStart !== null) {
      let x = e.clientX - this.canvas.getBoundingClientRect().left;
      if (Math.abs(x - this.selectionStart) < 10) {
        this.selectionStart = null;
        this.selectionEnd = null;
        let ctx = this.canvas.getContext("2d");
        ctx.drawImage(this.buffer, 0, this.imageOffset);
      } else {
        this.selectionEnd = x;
        this.drawSelection();
      }
      let file = this.currentState.file;
      if (file) {
        let start = this.selectionStart === null ? 0 : this.selectionStart;
        let end = this.selectionEnd === null ? Infinity : this.selectionEnd;
        let firstTime = file.ticks[0].tm;
        let lastTime = file.ticks[file.ticks.length - 1].tm;

        let width = this.buffer.width;

        start = (start / width) * (lastTime - firstTime) + firstTime;
        end = (end / width) * (lastTime - firstTime) + firstTime;

        if (end < start) {
          let temp = start;
          start = end;
          end = temp;
        }

        main.setViewInterval(start, end);
      }
    }
    this.selecting = false;
  }

  drawSelection() {
    let ctx = this.canvas.getContext("2d");

    // Draw the timeline image.
    ctx.drawImage(this.buffer, 0, this.imageOffset);

    // Draw the current interval highlight.
    let left;
    let right;
    if (this.selectionStart !== null && this.selectionEnd !== null) {
      ctx.fillStyle = "rgba(0, 0, 0, 0.3)";
      left = Math.min(this.selectionStart, this.selectionEnd);
      right = Math.max(this.selectionStart, this.selectionEnd);
      let height = this.buffer.height - this.functionTimelineHeight;
      ctx.fillRect(0, this.imageOffset, left, height);
      ctx.fillRect(right, this.imageOffset, this.buffer.width - right, height);
    } else {
      left = 0;
      right = this.buffer.width;
    }

    // Draw the scale text.
    let file = this.currentState.file;
    ctx.fillStyle = "white";
    ctx.fillRect(0, 0, this.canvas.width, this.imageOffset);
    if (file && file.ticks.length > 0) {
      let firstTime = file.ticks[0].tm;
      let lastTime = file.ticks[file.ticks.length - 1].tm;

      let leftTime =
          firstTime + left / this.canvas.width * (lastTime - firstTime);
      let rightTime =
          firstTime + right / this.canvas.width * (lastTime - firstTime);

      let leftText = (leftTime / 1000000).toFixed(3) + "s";
      let rightText = (rightTime / 1000000).toFixed(3) + "s";

      ctx.textBaseline = 'top';
      ctx.font = this.fontSize + "px Arial";
      ctx.fillStyle = "black";

      let leftWidth = ctx.measureText(leftText).width;
      let rightWidth = ctx.measureText(rightText).width;

      let leftStart = left - leftWidth / 2;
      let rightStart = right - rightWidth / 2;

      if (leftStart < 0) leftStart = 0;
      if (rightStart + rightWidth > this.canvas.width) {
        rightStart = this.canvas.width - rightWidth;
      }
      if (leftStart + leftWidth > rightStart) {
        if (leftStart > this.canvas.width - (rightStart - rightWidth)) {
          rightStart = leftStart + leftWidth;

        } else {
          leftStart = rightStart - leftWidth;
        }
      }

      ctx.fillText(leftText, leftStart, 0);
      ctx.fillText(rightText, rightStart, 0);
    }
  }

  render(newState) {
    let oldState = this.currentState;

    if (!newState.file) {
      this.element.style.display = "none";
      return;
    }

    let width = Math.round(document.documentElement.clientWidth - 20);
    let height = Math.round(document.documentElement.clientHeight / 5);

    if (oldState) {
      if (width === oldState.timelineSize.width &&
          height === oldState.timelineSize.height &&
          newState.file === oldState.file &&
          newState.currentCodeId === oldState.currentCodeId &&
          newState.callTree.attribution === oldState.callTree.attribution &&
          newState.start === oldState.start &&
          newState.end === oldState.end &&
          newState.showLogging === oldState.showLogging) {
        // No change, nothing to do.
        return;
      }
    }
    this.currentState = newState;
    this.currentState.timelineSize.width = width;
    this.currentState.timelineSize.height = height;

    this.element.style.display = "inherit";

    let file = this.currentState.file;

    const minPixelsPerBucket = 10;
    const minTicksPerBucket = 8;
    let maxBuckets = Math.round(file.ticks.length / minTicksPerBucket);
    let bucketCount = Math.min(
        Math.round(width / minPixelsPerBucket), maxBuckets);

    // Make sure the canvas has the right dimensions.
    this.canvas.width = width;
    this.canvas.height  = height;

    // Make space for the selection text.
    height -= this.imageOffset;

    let currentCodeId = this.currentState.currentCodeId;

    let firstTime = file.ticks[0].tm;
    let lastTime = file.ticks[file.ticks.length - 1].tm;
    let start = Math.max(this.currentState.start, firstTime);
    let end = Math.min(this.currentState.end, lastTime);

    this.selectionStart = (start - firstTime) / (lastTime - firstTime) * width;
    this.selectionEnd = (end - firstTime) / (lastTime - firstTime) * width;

    let filter = filterFromFilterId(this.currentState.callTree.attribution);
    let stackProcessor = new CategorySampler(file, bucketCount, filter);
    generateTree(file, 0, Infinity, stackProcessor);
    let codeIdProcessor = new FunctionTimelineProcessor(currentCodeId, filter);
    generateTree(file, 0, Infinity, codeIdProcessor);

    let buffer = document.createElement("canvas");

    buffer.width = width;
    buffer.height = height;

    // Calculate the bar heights for each bucket.
    let graphHeight = height - this.functionTimelineHeight;
    let buckets = stackProcessor.buckets;
    let bucketsGraph = [];
    for (let i = 0; i < buckets.length; i++) {
      let sum = 0;
      let bucketData = [];
      let total = buckets[i].total;
      if (total > 0) {
        for (let j = 0; j < bucketDescriptors.length; j++) {
          let desc = bucketDescriptors[j];
          for (let k = 0; k < desc.kinds.length; k++) {
            sum += buckets[i][desc.kinds[k]];
          }
          bucketData.push(Math.round(graphHeight * sum / total));
        }
      } else {
        // No ticks fell into this bucket. Fill with "Unknown."
        for (let j = 0; j < bucketDescriptors.length; j++) {
          let desc = bucketDescriptors[j];
          bucketData.push(desc.text === "Unknown" ? graphHeight : 0);
        }
      }
      bucketsGraph.push(bucketData);
    }

    // Draw the category graph into the buffer.
    let bucketWidth = width / (bucketsGraph.length - 1);
    let ctx = buffer.getContext('2d');
    for (let i = 0; i < bucketsGraph.length - 1; i++) {
      let bucketData = bucketsGraph[i];
      let nextBucketData = bucketsGraph[i + 1];
      let x1 = Math.round(i * bucketWidth);
      let x2 = Math.round((i + 1) * bucketWidth);
      for (let j = 0; j < bucketData.length; j++) {
        ctx.beginPath();
        ctx.moveTo(x1, j > 0 ? bucketData[j - 1] : 0);
        ctx.lineTo(x2, j > 0 ? nextBucketData[j - 1] : 0);
        ctx.lineTo(x2, nextBucketData[j]);
        ctx.lineTo(x1, bucketData[j]);
        ctx.closePath();
        ctx.fillStyle = bucketDescriptors[j].color;
        ctx.fill();
      }
    }

    // Draw the function ticks.
    let functionTimelineYOffset = graphHeight;
    let functionTimelineTickHeight = this.functionTimelineTickHeight;
    let functionTimelineHalfHeight =
        Math.round(functionTimelineTickHeight / 2);
    let timestampScaler = width / (lastTime - firstTime);
    let timestampToX = (t) => Math.round((t - firstTime) * timestampScaler);
    ctx.fillStyle = "white";
    ctx.fillRect(
      0,
      functionTimelineYOffset,
      buffer.width,
      this.functionTimelineHeight);
    for (let i = 0; i < codeIdProcessor.blocks.length; i++) {
      let block = codeIdProcessor.blocks[i];
      let bucket = kindToBucketDescriptor[block.kind];
      ctx.fillStyle = bucket.color;
      ctx.fillRect(
        timestampToX(block.start),
        functionTimelineYOffset,
        Math.max(1, Math.round((block.end - block.start) * timestampScaler)),
        block.topOfStack ?
            functionTimelineTickHeight : functionTimelineHalfHeight);
    }
    ctx.strokeStyle = "black";
    ctx.lineWidth = "1";
    ctx.beginPath();
    ctx.moveTo(0, functionTimelineYOffset + 0.5);
    ctx.lineTo(buffer.width, functionTimelineYOffset + 0.5);
    ctx.stroke();
    ctx.strokeStyle = "rgba(0,0,0,0.2)";
    ctx.lineWidth = "1";
    ctx.beginPath();
    ctx.moveTo(0, functionTimelineYOffset + functionTimelineHalfHeight - 0.5);
    ctx.lineTo(buffer.width,
        functionTimelineYOffset + functionTimelineHalfHeight - 0.5);
    ctx.stroke();

    // Draw marks for optimizations and deoptimizations in the function
    // timeline.
    if (currentCodeId && currentCodeId >= 0 &&
        file.code[currentCodeId].func) {
      let y = Math.round(functionTimelineYOffset + functionTimelineTickHeight +
          (this.functionTimelineHeight - functionTimelineTickHeight) / 2);
      let func = file.functions[file.code[currentCodeId].func];
      for (let i = 0; i < func.codes.length; i++) {
        let code = file.code[func.codes[i]];
        if (code.kind === "Opt" || code.kind === "Maglev" ||
            code.kind === "Sparkplug") {
          if (code.deopt) {
            // Draw deoptimization mark.
            let x = timestampToX(code.deopt.tm);
            ctx.lineWidth = 1;
            ctx.strokeStyle = "red";
            ctx.beginPath();
            ctx.moveTo(x - 3, y - 3);
            ctx.lineTo(x + 3, y + 3);
            ctx.stroke();
            ctx.beginPath();
            ctx.moveTo(x - 3, y + 3);
            ctx.lineTo(x + 3, y - 3);
            ctx.stroke();
          }
          let code_type = "UNKNOWN";
          switch (code.kind) {
            case "Opt":
              code_type = "JS_TURBOFAN";
              break;
            case "Maglev":
              code_type = "JS_MAGLEV";
              break;
            case "Sparkplug":
              code_type = "JS_SPARKPLUG";
              break;
          }

          // Draw optimization mark.
          let x = timestampToX(code.tm);
          ctx.lineWidth = 1;
          ctx.strokeStyle = kindToBucketDescriptor[code_type].color;
          ctx.beginPath();
          ctx.moveTo(x - 3, y - 3);
          ctx.lineTo(x, y);
          ctx.stroke();
          ctx.beginPath();
          ctx.moveTo(x - 3, y + 3);
          ctx.lineTo(x, y);
          ctx.stroke();
        } else {
          // Draw code creation mark.
          let x = Math.round(timestampToX(code.tm));
          ctx.beginPath();
          ctx.fillStyle = "black";
          ctx.arc(x, y, 3, 0, 2 * Math.PI);
          ctx.fill();
        }
      }
    }

    // Remember stuff for later.
    this.buffer = buffer;

    // Draw the buffer.
    this.drawSelection();

    // (Re-)Populate the graph legend.
    this.legend.innerHTML = "";
    for (let i = 0; i < bucketDescriptors.length; i++) {
      let desc = bucketDescriptors[i];
      let box = document.createElement("div");
      box.style.display = "inline-block";
      box.style.width = "0.6em";
      box.style.height = "1.2ex";
      box.style.backgroundColor = desc.color;
      box.style.borderStyle = "solid";
      box.style.borderWidth = "1px";
      box.style.borderColor = "Black";
      let cell = document.createElement("div");
      cell.appendChild(box);
      cell.appendChild(document.createTextNode(" " + desc.text));
      this.legend.appendChild(cell);
    }

    removeAllChildren(this.currentCode);
    if (currentCodeId) {
      let currentCode = file.code[currentCodeId];
      this.currentCode.appendChild(document.createTextNode(currentCode.name));
    } else {
      this.currentCode.appendChild(document.createTextNode("<none>"));
    }
  }
}

class ModeBarView {
  constructor() {
    let modeBar = this.element = $("mode-bar");

    function addMode(id, text, active) {
      let div = document.createElement("div");
      div.classList = "mode-button" + (active ? " active-mode-button" : "");
      div.id = "mode-" + id;
      div.textContent = text;
      div.onclick = () => {
        if (main.currentState.mode === id) return;
        let old = $("mode-" + main.currentState.mode);
        old.classList = "mode-button";
        div.classList = "mode-button active-mode-button";
        main.setMode(id);
      };
      modeBar.appendChild(div);
    }

    addMode("summary", "Summary", true);
    addMode("bottom-up", "Bottom up");
    addMode("top-down", "Top down");
    addMode("function-list", "Functions");
  }

  render(newState) {
    if (!newState.file) {
      this.element.style.display = "none";
      return;
    }

    this.element.style.display = "inherit";
  }
}

class SummaryView {
  constructor() {
    this.element = $("summary");
    this.currentState = null;
  }

  render(newState) {
    let oldState = this.currentState;

    if (!newState.file || newState.mode !== "summary") {
      this.element.style.display = "none";
      this.currentState = null;
      return;
    }

    this.currentState = newState;
    if (oldState) {
      if (newState.file === oldState.file &&
          newState.start === oldState.start &&
          newState.end === oldState.end) {
        // No change, nothing to do.
        return;
      }
    }

    this.element.style.display = "inherit";
    removeAllChildren(this.element);

    let stats = computeOptimizationStats(
        this.currentState.file, newState.start, newState.end);

    let table = document.createElement("table");
    let rows = document.createElement("tbody");

    function addRow(text, number, indent) {
      let row = rows.insertRow(-1);
      let textCell = row.insertCell(-1);
      textCell.textContent = text;
      let numberCell = row.insertCell(-1);
      numberCell.textContent = number;
      if (indent) {
        textCell.style.textIndent = indent + "em";
        numberCell.style.textIndent = indent + "em";
      }
      return row;
    }

    function makeCollapsible(row, arrow) {
      arrow.textContent = EXPANDED_ARROW;
      let expandHandler = row.onclick;
      row.onclick = () => {
        let id = row.id;
        let index = row.rowIndex + 1;
        while (index < rows.rows.length &&
          rows.rows[index].id.startsWith(id)) {
          rows.deleteRow(index);
        }
        arrow.textContent = COLLAPSED_ARROW;
        row.onclick = expandHandler;
      }
    }

    function expandDeoptInstances(row, arrow, instances, indent, kind) {
      let index = row.rowIndex;
      for (let i = 0; i < instances.length; i++) {
        let childRow = rows.insertRow(index + 1);
        childRow.id = row.id + i + "/";

        let deopt = instances[i].deopt;

        let textCell = childRow.insertCell(-1);
        textCell.appendChild(document.createTextNode(deopt.posText));
        textCell.style.textIndent = indent + "em";
        let reasonCell = childRow.insertCell(-1);
        reasonCell.appendChild(
            document.createTextNode("Reason: " + deopt.reason));
        reasonCell.style.textIndent = indent + "em";
      }
      makeCollapsible(row, arrow);
    }

    function expandDeoptFunctionList(row, arrow, list, indent, kind) {
      let index = row.rowIndex;
      for (let i = 0; i < list.length; i++) {
        let childRow = rows.insertRow(index + 1);
        childRow.id = row.id + i + "/";

        let textCell = childRow.insertCell(-1);
        textCell.appendChild(createIndentNode(indent));
        let childArrow = createArrowNode();
        textCell.appendChild(childArrow);
        textCell.appendChild(
            createFunctionNode(list[i].f.name, list[i].f.codes[0]));

        let numberCell = childRow.insertCell(-1);
        numberCell.textContent = list[i].instances.length;
        numberCell.style.textIndent = indent + "em";

        childArrow.textContent = COLLAPSED_ARROW;
        childRow.onclick = () => {
          expandDeoptInstances(
              childRow, childArrow, list[i].instances, indent + 1);
        };
      }
      makeCollapsible(row, arrow);
    }

    function expandOptimizedFunctionList(row, arrow, list, indent) {
      let index = row.rowIndex;
      for (let i = 0; i < list.length; i++) {
        let childRow = rows.insertRow(index + 1);
        childRow.id = row.id + i + "/";

        let textCell = childRow.insertCell(-1);
        textCell.appendChild(
            createFunctionNode(list[i].f.name, list[i].f.codes[0]));
        textCell.style.textIndent = indent + "em";

        let numberCell = childRow.insertCell(-1);
        numberCell.textContent = list[i].instances.length;
        numberCell.style.textIndent = indent + "em";
      }
      makeCollapsible(row, arrow);
    }

    function addExpandableRow(text, list, indent, kind) {
      let row = rows.insertRow(-1);

      row.id = "opt-table/" + kind + "/";
      row.style.backgroundColor = CATEGORY_COLOR;

      let textCell = row.insertCell(-1);
      textCell.appendChild(createIndentNode(indent));
      let arrow = createArrowNode();
      textCell.appendChild(arrow);
      textCell.appendChild(document.createTextNode(text));

      let numberCell = row.insertCell(-1);
      numberCell.textContent = list.count;
      if (indent) {
        numberCell.style.textIndent = indent + "em";
      }

      if (list.count > 0) {
        arrow.textContent = COLLAPSED_ARROW;
        if (kind === "opt") {
          row.onclick = () => {
            expandOptimizedFunctionList(row, arrow, list.functions, indent + 1);
          };
        } else {
          row.onclick = () => {
            expandDeoptFunctionList(
                row, arrow, list.functions, indent + 1, kind);
          };
        }
      }
      return row;
    }

    addRow("Total function count:", stats.functionCount);
    addRow("Baseline function count:", stats.baselineFunctionCount);
    addRow("Optimized function count:", stats.optimizedFunctionCount, 1);
    addRow(
        "Maglev optimized function count:", stats.maglevOptimizedFunctionCount,
        1);
    addRow("Deoptimized function count:", stats.deoptimizedFunctionCount, 2);

    addExpandableRow(
        "Baseline compilation count:", stats.baselineCompilations, 0, "opt");
    addExpandableRow("Optimization count:", stats.optimizations, 0, "opt");
    addExpandableRow(
        "Maglev Optimization count:", stats.maglevOptimizations, 0, "opt");

    let deoptCount = stats.eagerDeoptimizations.count +
        stats.lazyDeoptimizations.count +
        stats.dependencyChangeDeoptimizations.count;
    addRow("Deoptimization count:", deoptCount);
    addExpandableRow("Eager:", stats.eagerDeoptimizations, 1, "eager");
    addExpandableRow("Lazy:", stats.lazyDeoptimizations, 1, "lazy");
    addExpandableRow(
        "Dependency change:", stats.dependencyChangeDeoptimizations, 1, "deps");

    table.appendChild(rows);
    this.element.appendChild(table);
  }
}

class ScriptSourceView {
  constructor() {
    this.table = $("source-viewer");
    this.hideButton = $("source-viewer-hide-button");
    this.hideButton.onclick = () => {
      main.setViewingSource(false);
    };
  }

  render(newState) {
    let oldState = this.currentState;
    if (!newState.file || !newState.viewingSource) {
      this.table.style.display = "none";
      this.hideButton.style.display = "none";
      this.currentState = null;
      return;
    }
    if (oldState) {
      if (newState.file === oldState.file &&
          newState.currentCodeId === oldState.currentCodeId &&
          newState.viewingSource === oldState.viewingSource) {
        // No change, nothing to do.
        return;
      }
    }
    this.currentState = newState;

    this.table.style.display = "inline-block";
    this.hideButton.style.display = "inline";
    removeAllChildren(this.table);

    let functionId =
        this.currentState.file.code[this.currentState.currentCodeId].func;
    let sourceView =
        this.currentState.sourceData.generateSourceView(functionId);
    for (let i = 0; i < sourceView.source.length; i++) {
      let sampleCount = sourceView.lineSampleCounts[i] || 0;
      let sampleProportion = sourceView.samplesTotal > 0 ?
                             sampleCount / sourceView.samplesTotal : 0;
      let heatBucket;
      if (sampleProportion === 0) {
        heatBucket = "line-none";
      } else if (sampleProportion < 0.2) {
        heatBucket = "line-cold";
      } else if (sampleProportion < 0.4) {
        heatBucket = "line-mediumcold";
      } else if (sampleProportion < 0.6) {
        heatBucket = "line-mediumhot";
      } else if (sampleProportion < 0.8) {
        heatBucket = "line-hot";
      } else {
        heatBucket = "line-superhot";
      }

      let row = this.table.insertRow(-1);

      let lineNumberCell = row.insertCell(-1);
      lineNumberCell.classList.add("source-line-number");
      lineNumberCell.textContent = i + sourceView.firstLineNumber;

      let sampleCountCell = row.insertCell(-1);
      sampleCountCell.classList.add(heatBucket);
      sampleCountCell.textContent = sampleCount;

      let sourceLineCell = row.insertCell(-1);
      sourceLineCell.classList.add(heatBucket);
      sourceLineCell.textContent = sourceView.source[i];
    }

    $("timeline-currentCode").scrollIntoView();
  }
}

class SourceData {
  constructor(file) {
    this.scripts = new Map();
    for (let i = 0; i < file.scripts.length; i++) {
      const scriptBlock = file.scripts[i];
      if (scriptBlock === null) continue; // Array may be sparse.
      if (scriptBlock.source === undefined) continue;
      let source = scriptBlock.source.split("\n");
      this.scripts.set(i, source);
    }

    this.functions = new Map();
    for (let codeId = 0; codeId < file.code.length; ++codeId) {
      let codeBlock = file.code[codeId];
      if (codeBlock.source && codeBlock.func !== undefined) {
        let data = this.functions.get(codeBlock.func);
        if (!data) {
          data = new FunctionSourceData(codeBlock.source.script,
                                        codeBlock.source.start,
                                        codeBlock.source.end);
          this.functions.set(codeBlock.func, data);
        }
        data.addSourceBlock(codeId, codeBlock.source);
      }
    }

    for (let tick of file.ticks) {
      let stack = tick.s;
      for (let i = 0; i < stack.length; i += 2) {
        let codeId = stack[i];
        if (codeId < 0) continue;
        let functionId = file.code[codeId].func;
        if (this.functions.has(functionId)) {
          let codeOffset = stack[i + 1];
          this.functions.get(functionId).addOffsetSample(codeId, codeOffset);
        }
      }
    }
  }

  getScript(scriptId) {
    return this.scripts.get(scriptId);
  }

  getLineForScriptOffset(script, scriptOffset) {
    let line = 0;
    let charsConsumed = 0;
    for (; line < script.length; ++line) {
      charsConsumed += script[line].length + 1; // Add 1 for newline.
      if (charsConsumed > scriptOffset) break;
    }
    return line;
  }

  hasSource(functionId) {
    return this.functions.has(functionId);
  }

  generateSourceView(functionId) {
    console.assert(this.hasSource(functionId));
    let data = this.functions.get(functionId);
    let scriptId = data.scriptId;
    let script = this.getScript(scriptId);
    let firstLineNumber =
        this.getLineForScriptOffset(script, data.startScriptOffset);
    let lastLineNumber =
        this.getLineForScriptOffset(script, data.endScriptOffset);
    let lines = script.slice(firstLineNumber, lastLineNumber + 1);
    normalizeLeadingWhitespace(lines);

    let samplesTotal = 0;
    let lineSampleCounts = [];
    for (let [codeId, block] of data.codes) {
      block.offsets.forEach((sampleCount, codeOffset) => {
        let sourceOffset = block.positionTable.getScriptOffset(codeOffset);
        let lineNumber =
            this.getLineForScriptOffset(script, sourceOffset) - firstLineNumber;
        samplesTotal += sampleCount;
        lineSampleCounts[lineNumber] =
            (lineSampleCounts[lineNumber] || 0) + sampleCount;
      });
    }

    return {
      source: lines,
      lineSampleCounts: lineSampleCounts,
      samplesTotal: samplesTotal,
      firstLineNumber: firstLineNumber + 1  // Source code is 1-indexed.
    };
  }
}

class FunctionSourceData {
  constructor(scriptId, startScriptOffset, endScriptOffset) {
    this.scriptId = scriptId;
    this.startScriptOffset = startScriptOffset;
    this.endScriptOffset = endScriptOffset;

    this.codes = new Map();
  }

  addSourceBlock(codeId, source) {
    this.codes.set(codeId, {
      positionTable: new SourcePositionTable(source.positions),
      offsets: []
    });
  }

  addOffsetSample(codeId, codeOffset) {
    let codeIdOffsets = this.codes.get(codeId).offsets;
    codeIdOffsets[codeOffset] = (codeIdOffsets[codeOffset] || 0) + 1;
  }
}

class SourcePositionTable {
  constructor(encodedTable) {
    this.offsetTable = [];
    let offsetPairRegex = /C([0-9]+)O([0-9]+)/g;
    while (true) {
      let regexResult = offsetPairRegex.exec(encodedTable);
      if (!regexResult) break;
      let codeOffset = parseInt(regexResult[1]);
      let scriptOffset = parseInt(regexResult[2]);
      if (isNaN(codeOffset) || isNaN(scriptOffset)) continue;
      this.offsetTable.push(codeOffset, scriptOffset);
    }
  }

  getScriptOffset(codeOffset) {
    console.assert(codeOffset >= 0);
    for (let i = this.offsetTable.length - 2; i >= 0; i -= 2) {
      if (this.offsetTable[i] <= codeOffset) {
        return this.offsetTable[i + 1];
      }
    }
    return this.offsetTable[1];
  }
}

class HelpView {
  constructor() {
    this.element = $("help");
  }

  render(newState) {
    this.element.style.display = newState.file ? "none" : "inherit";
  }
}
