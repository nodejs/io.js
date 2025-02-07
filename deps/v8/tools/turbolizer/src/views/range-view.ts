// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../common/constants";
import { createElement, getNumericCssValue,
         setCssValue, storageGetItem, storageSetItem } from "../common/util";
import { SequenceView } from "./sequence-view";
import { Interval } from "../interval";
import { ChildRange, Range, RangeToolTip, SequenceBlock } from "../phases/sequence-phase";
import { SelectionBroker } from "../selection/selection-broker";
import { BlockSelectionHandler,
         RegisterAllocationSelectionHandler } from "../selection/selection-handler";

// This class holds references to the HTMLElements that represent each cell.
class Grid {
  elements: Array<Array<HTMLElement>>;
  background: HTMLCanvasElement;
  backgroundContext: CanvasRenderingContext2D;
  backgroundScrollLeft: number;
  backgroundScrollTop: number;
  backgroundFocusRow: number;
  backgroundFocusColumn: number;
  rows: number;
  columns: number;

  constructor(canvas: HTMLCanvasElement) {
    this.elements = new Array<Array<HTMLElement>>();
    this.background = canvas;
    this.backgroundContext = this.background.getContext("2d");
    this.background.style.setProperty("margin-left", "0px");
    this.backgroundScrollLeft = 0;
    this.backgroundScrollTop = 0;
    this.backgroundFocusRow = -1;
    this.backgroundFocusColumn = -1;
  }

  public setRow(row: number, elementsRow: Array<HTMLElement>): void {
    this.elements[row] = elementsRow;
  }

  public updateBackgroundFocusPoint(row: number, column: number): void {
    this.backgroundFocusRow = row;
    this.backgroundFocusColumn = column;
  }

  public clearBackgroundFocusPoint(): void {
    this.backgroundFocusRow = -1;
    this.backgroundFocusColumn = -1;
  }

  public updateBackground(view: RangeView, scrollTop: number, scrollLeft: number): void {
    // Update canvas size.
    if (view.divs.grid === undefined) return;
    if (view.divs.registers === undefined) return;
    if (view.divs.positionHeaders === null) return;
    const positions = view.divs.positionHeaders.lastChild as HTMLElement;
    const gridBoundRect = view.divs.grid.getBoundingClientRect();
    const yAxisBoundRect = view.divs.yAxis.getBoundingClientRect();
    this.background.style.setProperty(
      "margin-left", yAxisBoundRect.width.toString() + "px");
    this.background.style.setProperty(
      "margin-top", (yAxisBoundRect.top - gridBoundRect.top).toString() + "px");
    const rangePosCellSize = view.divs.getRangePositionCellSize();
    const rangeRegCellSize = view.divs.getRangeRegCellSize();
    const flipped = view.userSettings.get("flipped");
    const canvasWidth = Math.min(gridBoundRect.width,
      flipped ? view.divs.registers.getBoundingClientRect().width :
                positions.getBoundingClientRect().width);
    const canvasHeight = Math.min(gridBoundRect.height,
      flipped ? positions.getBoundingClientRect().height :
                view.divs.registers.getBoundingClientRect().height);
    this.background.setAttribute("width", canvasWidth.toString());
    this.background.setAttribute("height", canvasHeight.toString());
    const firstPosition = view.instructionRangeHandler.getFirstPosition();
    // Draw background lines and highlight select sections.
    if (!flipped) {
      const startRow = Math.floor(scrollTop / rangeRegCellSize[1]);
      const endRow = Math.floor((scrollTop + canvasHeight) / rangeRegCellSize[1]);
      const startColumn = Math.floor(scrollLeft / rangePosCellSize[0]);
      const endColumn = Math.floor((scrollLeft + canvasWidth) / rangePosCellSize[0]);
      // Draw the highlight region.
      this.backgroundContext.fillStyle = C.RANGE_BACKGROUND_SELECT_COLOR;
      let yAxisPos = startRow * rangeRegCellSize[1] - scrollTop + C.RANGE_REGISTER_BORDER_WIDTH;
      for (let row = startRow; row <= endRow; row++) {
        if (view.divs.isRangeRegSelected(row)) {
          this.backgroundContext.fillRect(0, yAxisPos, canvasWidth, rangeRegCellSize[1]);
        }
        yAxisPos += rangeRegCellSize[1];
      }
      let xAxisPos = startColumn * rangePosCellSize[0] - scrollLeft;
      for (let column = startColumn; column <= endColumn; column++) {
        if (view.divs.isRangePositionSelected(column)) {
          this.backgroundContext.fillRect(xAxisPos, 0, rangePosCellSize[0], canvasHeight);
        }
        xAxisPos += rangePosCellSize[0];
      }
      // Draw the focus point.
      if (this.canShowFocus([startRow, endRow,
                             startColumn + firstPosition, endColumn + firstPosition])) {
        this.backgroundContext.fillStyle = C.RANGE_BACKGROUND_FOCUS_COLOR;
        const xPos = (this.backgroundFocusColumn - firstPosition) * rangePosCellSize[0] - scrollLeft;
        const yPos = this.backgroundFocusRow * rangeRegCellSize[1] - scrollTop;
        this.backgroundContext.fillRect(xPos, yPos, rangePosCellSize[0], rangeRegCellSize[1]);
      }
      // Draw the background lines.
      this.backgroundContext.fillStyle = C.RANGE_BACKGROUND_BORDER_COLOR;
      yAxisPos = (startRow + 1) * rangeRegCellSize[1] - scrollTop;
      for (let row = startRow; row <= endRow; row++) {
        this.backgroundContext.fillRect(0, yAxisPos - C.RANGE_REGISTER_BORDER_WIDTH,
                                        canvasWidth, C.RANGE_REGISTER_BORDER_WIDTH_X2);
        yAxisPos += rangeRegCellSize[1];
      }
      xAxisPos = (startColumn + 1) * rangePosCellSize[0] - scrollLeft;
      for (let column = startColumn; column <= endColumn; column++) {
        const borderWidth = this.getBorderWidth(view, column + firstPosition);
        this.backgroundContext.fillRect(xAxisPos - borderWidth, 0, borderWidth, canvasHeight);
        xAxisPos += rangePosCellSize[0];
      }
    } else {
      const startRow = Math.floor(scrollTop / rangePosCellSize[1]);
      const endRow = Math.floor((scrollTop + canvasHeight) / rangePosCellSize[1]);
      const startColumn = Math.floor(scrollLeft / rangeRegCellSize[0]);
      const endColumn = Math.floor((scrollLeft + canvasWidth) / rangeRegCellSize[0]);
      // Draw the highlight region.
      this.backgroundContext.fillStyle = C.RANGE_BACKGROUND_SELECT_COLOR;
      let yAxisPos = startRow * rangePosCellSize[1] - scrollTop;
      for (let row = startRow; row <= endRow; row++) {
        if (view.divs.isRangePositionSelected(row)) {
          this.backgroundContext.fillRect(0, yAxisPos, canvasWidth, rangePosCellSize[1]);
        }
        yAxisPos += rangePosCellSize[1];
      }
      let xAxisPos = startColumn * rangeRegCellSize[0] - scrollLeft + C.RANGE_REGISTER_BORDER_WIDTH;
      for (let column = startColumn; column <= endColumn; column++) {
        if (view.divs.isRangeRegSelected(column)) {
          this.backgroundContext.fillRect(xAxisPos, 0, rangeRegCellSize[0], canvasHeight);
        }
        xAxisPos += rangeRegCellSize[0];
      }
      // Draw the focus point.
      if (this.canShowFocus([startColumn, endColumn,
                             startRow + firstPosition, endRow + firstPosition])) {
        this.backgroundContext.fillStyle = C.RANGE_BACKGROUND_FOCUS_COLOR;
        const xPos = this.backgroundFocusRow * rangeRegCellSize[0] - scrollLeft;
        const yPos = (this.backgroundFocusColumn - firstPosition) * rangePosCellSize[1] - scrollTop;
        this.backgroundContext.fillRect(xPos, yPos, rangeRegCellSize[0], rangePosCellSize[1]);
      }
      // Draw the background lines.
      this.backgroundContext.fillStyle = C.RANGE_BACKGROUND_BORDER_COLOR;
      yAxisPos = (startRow + 1) * rangePosCellSize[1] - scrollTop;
      for (let row = startRow; row <= endRow; row++) {
        const borderWidth = this.getBorderWidth(view, row + firstPosition);
        this.backgroundContext.fillRect(0, yAxisPos - borderWidth, canvasWidth, borderWidth);
        yAxisPos += rangePosCellSize[1];
      }
      xAxisPos = (startColumn + 1) * rangeRegCellSize[0] - scrollLeft;
      for (let column = startColumn; column <= endColumn; column++) {
        this.backgroundContext.fillRect(xAxisPos - C.RANGE_REGISTER_BORDER_WIDTH, 0,
                                        C.RANGE_REGISTER_BORDER_WIDTH_X2, canvasHeight);
        xAxisPos += rangeRegCellSize[0];
      }
    }
    this.backgroundScrollLeft = scrollLeft;
    this.backgroundScrollTop = scrollTop;
  }

  public getCell(row: number, column: number): HTMLElement {
    if (this.elements[row] === undefined) return undefined;
    return this.elements[row][column];
  }

  public getInterval(row: number, column: number): HTMLElement {
    // The cell is within an inner wrapper div which is within the interval div.
    return this.getCell(row, column).parentElement.parentElement;
  }

  private getBorderWidth(view: RangeView, position: number): number {
    return view.blocksData.isBlockBorder(position) ? view.cssVariables.blockBorderWidth :
            view.blocksData.isInstructionBorder(position) ? view.cssVariables.instrBorder :
                                                              C.RANGE_POSITION_BORDER_WIDTH;
  }

  private canShowFocus(bound: Array<number>): boolean {
    return this.backgroundFocusRow >= bound[0] && this.backgroundFocusRow <= bound[1] &&
            this.backgroundFocusColumn >= bound[2] && this.backgroundFocusColumn <= bound[3];
  }
}

// This class is used as a wrapper to hide the switch between the
// two different Grid objects used, one for each phase,
// before and after register allocation.
class GridAccessor {
  sequenceView: SequenceView;
  grids: Map<number, Grid>;

  constructor(sequenceView: SequenceView) {
    this.sequenceView = sequenceView;
    this.grids = new Map<number, Grid>();
  }

  public getAnyGrid(): Grid {
    return this.grids.values().next().value;
  }

  public hasGrid(): boolean {
    return this.grids.has(this.sequenceView.currentPhaseIndex);
  }

  public addGrid(grid: Grid): void {
    if (this.hasGrid()) console.warn("Overwriting existing Grid.");
    this.grids.set(this.sequenceView.currentPhaseIndex, grid);
  }

  public getCell(row: number, column: number): HTMLElement {
    return this.currentGrid().getCell(row, column);
  }

  public getInterval(row: number, column: number): HTMLElement {
    return this.currentGrid().getInterval(row, column);
  }

  public currentGrid(): Grid {
    return this.grids.get(this.sequenceView.currentPhaseIndex);
  }
}

// This class is used as a wrapper to access the interval HTMLElements
class IntervalElementsAccessor {
  sequenceView: SequenceView;
  map: Map<number, Array<HTMLElement>>;

  constructor(sequenceView: SequenceView) {
    this.sequenceView = sequenceView;
    this.map = new Map<number, Array<HTMLElement>>();
  }

  public addInterval(interval: HTMLElement): void {
    this.currentIntervals().push(interval);
  }

  public forEachInterval(callback: (phase: number, interval: HTMLElement) => void) {
    for (const phase of this.map.keys()) {
      for (const interval of this.map.get(phase)) {
        callback(phase, interval);
      }
    }
  }

  private currentIntervals(): Array<HTMLElement> {
    const intervals = this.map.get(this.sequenceView.currentPhaseIndex);
    if (intervals === undefined) {
      this.map.set(this.sequenceView.currentPhaseIndex, new Array<HTMLElement>());
      return this.currentIntervals();
    }
    return intervals;
  }
}

// A number of css variables regarding dimensions of HTMLElements are required by RangeView.
class CSSVariables {
  positionWidth: number;
  positionBorder: number;
  blockBorderWidth: number;
  flippedPositionHeight: number;
  instrBorder: number;

  constructor() {
    this.positionWidth = getNumericCssValue("--range-position-width");
    this.positionBorder = getNumericCssValue("--range-position-border");
    this.blockBorderWidth = getNumericCssValue("--range-block-border");
    this.flippedPositionHeight = getNumericCssValue("--range-flipped-position-height");
    this.instrBorder = getNumericCssValue("--range-instr-border");
  }

  setVariables(numPositions: number, numRegisters: number) {
    setCssValue("--range-num-positions", String(numPositions));
    setCssValue("--range-num-registers", String(numRegisters));
  }
}

class UserSettingsObject {
  value: boolean;
  resetFunction: (param: boolean) => void;

  constructor(value: boolean, resetFunction: (param: boolean) => void) {
    this.value = value;
    this.resetFunction = resetFunction;
  }
}

// Manages the user's setting options regarding how the grid is displayed.
class UserSettings {
  settings: Map<string, UserSettingsObject>;

  constructor() {
    this.settings = new Map<string, UserSettingsObject>();
  }

  public addSetting(settingName: string, value: boolean, resetFunction: (param: boolean) => void) {
    this.settings.set(settingName, new UserSettingsObject(value, resetFunction));
  }

  public getToggleElement(settingName: string, settingLabel: string): HTMLElement {
    const toggleEl = createElement("label", "range-toggle-setting", settingLabel);
    const toggleInput = createElement("input", "range-toggle-setting") as HTMLInputElement;
    toggleInput.id = `range-toggle-${settingName}`;
    toggleInput.setAttribute("type", "checkbox");
    toggleInput.oninput = () => {
      toggleInput.disabled = true;
      this.set(settingName, toggleInput.checked);
      this.reset(settingName);
      toggleInput.disabled = false;
    };
    toggleEl.onclick = (e: MouseEvent) => { e.stopPropagation(); };
    toggleEl.insertBefore(toggleInput, toggleEl.firstChild);
    return toggleEl;
  }

  public reset(settingName: string): void {
    const settingObject = this.settings.get(settingName);
    settingObject.resetFunction(settingObject.value);
    storageSetItem(this.getSettingKey(settingName), settingObject.value);
    window.dispatchEvent(new Event("resize"));
  }

  public get(settingName: string): boolean {
    return this.settings.get(settingName).value;
  }

  public set(settingName: string, value: boolean): void {
    this.settings.get(settingName).value = value;
  }

  public resetFromSessionStorage(): void {
    for (const [settingName, setting] of this.settings.entries()) {
      const storedValue = storageGetItem(this.getSettingKey(settingName));
      if (storedValue === undefined) continue;
      this.set(settingName, storedValue);
      if (storedValue) {
        const toggle = document.getElementById(`range-toggle-${settingName}`) as HTMLInputElement;
        if (!toggle.checked) {
          toggle.checked = storedValue;
          setting.resetFunction(storedValue);
        }
      }
    }
  }

  private getSettingKey(settingName: string): string {
    return `${C.SESSION_STORAGE_PREFIX}${settingName}`;
  }
}

// Store the required data from the blocks JSON.
class BlocksData {
  view: RangeView;
  blockBorders: Set<number>;
  blockInstructionCountMap: Map<number, number>;

  constructor(view: RangeView, blocks: Array<SequenceBlock>) {
    this.view = view;
    this.blockBorders = new Set<number>();
    this.blockInstructionCountMap = new Map<number, number>();
    for (const block of blocks) {
      this.blockInstructionCountMap.set(block.id, block.instructions.length);
      const maxInstructionInBlock = block.instructions[block.instructions.length - 1].id;
      this.blockBorders.add(maxInstructionInBlock);
    }
  }

  public isInstructionBorder(position: number): boolean {
    return ((position + 1) % C.POSITIONS_PER_INSTRUCTION) == 0;
  }

  public isInstructionIdOnBlockBorder(instrId: number) {
    return this.view.instructionRangeHandler.isLastInstruction(instrId)
        || this.blockBorders.has(instrId);
  }

  public isBlockBorder(position: number) {
    const border = Math.floor(position / C.POSITIONS_PER_INSTRUCTION);
    return this.view.instructionRangeHandler.isLastPosition(position)
       || (this.isInstructionBorder(position) && this.blockBorders.has(border));
  }

  public isIndexInstructionBorder(index: number) {
    return this.isInstructionBorder(this.view.instructionRangeHandler.getPositionFromIndex(index));
  }

  public isIndexBlockBorder(index: number) {
    return this.isBlockBorder(this.view.instructionRangeHandler.getPositionFromIndex(index));
  }
}

class Divs {
  // Already existing.
  container: HTMLElement;
  resizerBar: HTMLElement;
  snapper: HTMLElement;

  // Created by constructor.
  content: HTMLElement;
  // showOnLoad contains all content that may change depending on the JSON.
  showOnLoad: HTMLElement;
  xAxisLabel: HTMLElement;
  yAxisLabel: HTMLElement;
  registerHeaders: HTMLElement;
  registersTypeHeader: HTMLElement;
  registers: HTMLElement;

  // Assigned from RangeView.
  wholeHeader: HTMLElement;
  positionHeaders: HTMLElement;
  yAxis: HTMLElement;
  grid: HTMLElement;

  constructor(userSettings: UserSettings, instructionRangeString: string) {
    this.container = document.getElementById(C.RANGES_PANE_ID);
    this.resizerBar = document.getElementById(C.RESIZER_RANGES_ID);
    this.snapper = document.getElementById(C.SHOW_HIDE_RANGES_ID);

    this.content = document.createElement("div");
    this.content.id = "ranges-content";
    this.content.appendChild(this.elementForTitle(userSettings, instructionRangeString));

    this.showOnLoad = document.createElement("div");
    this.showOnLoad.style.visibility = "hidden";
    this.content.appendChild(this.showOnLoad);

    this.xAxisLabel = createElement("div", "range-header-label-x");
    this.xAxisLabel.dataset.notFlipped = "Blocks, Instructions, and Positions";
    this.xAxisLabel.dataset.flipped = "Registers";
    this.xAxisLabel.innerText = this.xAxisLabel.dataset.notFlipped;
    this.showOnLoad.appendChild(this.xAxisLabel);
    this.yAxisLabel = createElement("div", "range-header-label-y");
    this.yAxisLabel.dataset.notFlipped = "Registers";
    this.yAxisLabel.dataset.flipped = "Positions";
    this.yAxisLabel.innerText = this.yAxisLabel.dataset.notFlipped;
    this.showOnLoad.appendChild(this.yAxisLabel);

    this.registerHeaders = createElement("div", "range-register-labels");
    this.registers = createElement("div", "range-registers");
    this.registerHeaders.appendChild(this.registers);
  }

  public elementForTitle(userSettings: UserSettings, instructionRangeString: string): HTMLElement {
    const titleEl = createElement("div", "range-title-div");
    const titleBar = createElement("div", "range-title");
    titleBar.appendChild(createElement("div", "", "Live Ranges for " + instructionRangeString));
    const titleHelp = createElement("div", "range-title-help", "?");
    titleHelp.title = "Each row represents a single TopLevelLiveRange (or two if deferred exists)."
      + "\nEach interval belongs to a LiveRange contained within that row's TopLevelLiveRange."
      + "\nAn interval is identified by i, the index of the LiveRange within the TopLevelLiveRange,"
      + "\nand j, the index of the interval within the LiveRange, to give i:j.";
    titleEl.appendChild(titleBar);
    titleEl.appendChild(titleHelp);
    titleEl.appendChild(userSettings.getToggleElement("landscapeMode", "Landscape Mode"));
    titleEl.appendChild(userSettings.getToggleElement("flipped", "Switched Axes"));
    return titleEl;
  }

  public getRangePositionCellSize(): Array<number> {
    const positions = this.positionHeaders.lastChild as HTMLElement;
    if (positions.children.length == 0) return [0, 0];
    const rangePositionRect = positions.children[0].getBoundingClientRect();
    return [rangePositionRect.width, rangePositionRect.height];
  }

  public getRangeRegCellSize(): Array<number> {
    if (this.registers.children.length == 0) return [0, 0];
    const yAxisRect = this.registers.children[0].getBoundingClientRect();
    return [yAxisRect.width, yAxisRect.height];
  }

  public isRangePositionSelected(column: number): boolean {
    const positions = this.positionHeaders.lastChild as HTMLElement;
    if (positions.children.length < column) return false;
    const rangePositionEl = positions.children[column] as HTMLElement;
    if (rangePositionEl === undefined) return false;
    return rangePositionEl.classList.contains('selected');
  }

  public isRangeRegSelected(row: number): boolean {
    if (this.registers.children.length < row) return false;
    const rangeRegisterEl = this.registers.children[row] as HTMLElement;
    if (rangeRegisterEl === undefined) return false;
    return rangeRegisterEl.classList.contains('selected');
  }
}

class RowConstructor {
  view: RangeView;

  constructor(view: RangeView) {
    this.view = view;
  }

  getGridTemplateRowsValueForGroupDiv(length) {
    return `repeat(${length},calc(${this.view.cssVariables.positionWidth}ch +
            ${2 * this.view.cssVariables.positionBorder}px))`;
  }

  getGridTemplateColumnsValueForInterval(length) {
    const positionSize = (this.view.userSettings.get("flipped")
                          ? `${this.view.cssVariables.flippedPositionHeight}em`
                          : `${this.view.cssVariables.positionWidth}ch`);
    return `repeat(${length},calc(${positionSize} + ${this.view.cssVariables.blockBorderWidth}px))`;
  }

  // Constructs the row of HTMLElements for grid while providing a callback for each position
  // depending on whether that position is the start of an interval or not.
  // RangePair is used to allow the two fixed register live ranges of normal and deferred to be
  // easily combined into a single row.
  construct(grid: Grid, row: number, registerId: string, registerIndex: number,
            ranges: [Range, Range], getElementForEmptyPosition: (position: number) => HTMLElement,
            callbackForInterval: (position: number, interval: HTMLElement) => void): boolean {
    // Construct all of the new intervals.
    const intervalMap = this.elementsForIntervals(registerIndex, ranges);
    if (intervalMap.size == 0) return false;
    const positionsArray = new Array<HTMLElement>(this.view.instructionRangeHandler.numPositions);
    const posOffset = this.view.instructionRangeHandler.getPositionFromIndex(0);
    let blockId = this.view.instructionRangeHandler.getBlockIdFromIndex(0);
    let lastIntervalEndPos = -1;
    for (let column = 0; column < this.view.instructionRangeHandler.numPositions; ++column) {
      const interval = intervalMap.get(column);
      if (interval === undefined) {
        if (column == lastIntervalEndPos || column == this.view.instructionRangeHandler.numPositions - 1) {
          // Add one empty div element at the end of interval to display the use mark.
          positionsArray[column] = getElementForEmptyPosition(column);
          this.view.selectionHandler.addCell(positionsArray[column], row,
                                      column + posOffset, blockId, registerId, undefined, true);
        } else {
          positionsArray[column] = undefined;
        }
        if (this.view.blocksData.isBlockBorder(column + posOffset)) ++blockId;
      } else {
        callbackForInterval(column, interval);
        this.view.intervalsAccessor.addInterval(interval);
        const innerWrapper = this.view.getInnerWrapperFromInterval(interval);
        const intervalNodeId = interval.dataset.nodeId;
        this.view.selectionHandler.addInterval(interval, innerWrapper, intervalNodeId, registerId);
        const intervalPositionElements = innerWrapper.children;
        for (let j = 0; j < intervalPositionElements.length; ++j) {
          const intervalColumn = column + j;
          // Point positions to the new elements.
          positionsArray[intervalColumn] = (intervalPositionElements[j] as HTMLElement);
          this.view.selectionHandler.addCell(positionsArray[intervalColumn], row,
                                      intervalColumn + posOffset, blockId, registerId, intervalNodeId);
          if (this.view.blocksData.isBlockBorder(intervalColumn + posOffset)) ++blockId;
        }
        column += intervalPositionElements.length - 1;
        lastIntervalEndPos = column + 1;
      }
    }

    grid.setRow(row, positionsArray);

    for (const range of ranges) {
      if (!range) continue;
      this.setUses(grid, row, range);
    }
    return true;
  }

  // This is the main function used to build new intervals.
  // Returns a map of LifeTimePositions to intervals.
  private elementsForIntervals(registerIndex: number, ranges: [Range, Range]):
    Map<number, HTMLElement> {
    const intervalMap = new Map<number, HTMLElement>();
    for (const range of ranges) {
      if (!range) continue;
      for (const childRange of range.childRanges) {
        const tooltip = childRange.getTooltip(registerIndex);
        for (const [index, intervalNums] of childRange.intervals.entries()) {
          let interval = new Interval(intervalNums);
          if (!this.view.instructionRangeHandler.showAllPositions) {
            if (!this.view.instructionRangeHandler.isIntervalInRange(interval)) continue;
            interval =
                      this.view.instructionRangeHandler.convertIntervalPositionsToIndexes(interval);
          }
          const intervalEl = this.elementForInterval(childRange, interval, tooltip,
                                                     index, range.isDeferred);
          intervalMap.set(interval.start, intervalEl);
        }
      }
    }
    return intervalMap;
  }

  private elementForInterval(childRange: ChildRange, interval: Interval, tooltip: RangeToolTip,
                             index: number, isDeferred: boolean): HTMLElement
  {
    const intervalEl = createElement("div", "range-interval");
    intervalEl.dataset.tooltip = tooltip.text;
    const title = `${childRange.id}:${index} ${tooltip.text}`;
    intervalEl.setAttribute("title", isDeferred ? `deferred: ${title}` : title);
    this.setIntervalColor(intervalEl, tooltip.text);

    const intervalInnerWrapper = createElement("div", "range-interval-wrapper");
    intervalEl.style.gridColumn = `${(interval.start + 1)} / ${(interval.end + 1)}`;
    const intervalLength = interval.end - interval.start;
    intervalInnerWrapper.style.gridTemplateColumns =
      this.getGridTemplateColumnsValueForInterval(intervalLength);
    const intervalStringEls = this.elementsForIntervalString(tooltip.text, intervalLength);
    intervalEl.appendChild(intervalStringEls.main);
    intervalEl.appendChild(intervalStringEls.behind);

    for (let i = interval.start; i < interval.end; ++i) {
      const classes = "range-position range-interval-position range-empty" +
                (this.view.blocksData.isIndexBlockBorder(i)
                  ? " range-block-border"
                  : this.view.blocksData.isIndexInstructionBorder(i) ? " range-instr-border" : "");
      const positionEl = createElement("div", classes, "_");
      positionEl.style.gridColumn = String(i - interval.start + 1);
      intervalInnerWrapper.appendChild(positionEl);
    }

    intervalEl.appendChild(intervalInnerWrapper);
    // Either the tooltip represents the interval id, or a new id is required.
    const intervalNodeId = tooltip.isId ? tooltip.text
                                        : "interval-" + index + "-" + interval.start;
    intervalEl.dataset.nodeId = intervalNodeId;
    return intervalEl;
  }

  private setIntervalColor(interval: HTMLElement, tooltip: string): void {
    if (tooltip.includes(C.INTERVAL_TEXT_FOR_NONE)) return;
    if (tooltip.includes(`${C.INTERVAL_TEXT_FOR_STACK}-`)) {
      interval.style.backgroundColor = "rgb(250, 158, 168)";
    } else if (tooltip.includes(C.INTERVAL_TEXT_FOR_STACK)) {
      interval.style.backgroundColor = "rgb(250, 158, 100)";
    } else if (tooltip.includes(C.INTERVAL_TEXT_FOR_CONST)) {
      interval.style.backgroundColor = "rgb(153, 158, 230)";
    } else {
      interval.style.backgroundColor = "rgb(153, 220, 168)";
    }
  }

  private elementsForIntervalString(tooltip: string, numCells: number):
                                                        { main: HTMLElement, behind: HTMLElement } {
    const spanEl = createElement("span", "range-interval-text");
    // Allows a cleaner display of the interval text when displayed vertically.
    const spanElBehind = createElement("span", "range-interval-text range-interval-text-behind");
    this.view.stringConstructor.setIntervalString(spanEl, spanElBehind, tooltip, numCells);
    return { main: spanEl, behind: spanElBehind};
  }

  private setUses(grid: Grid, row: number, range: Range): void {
    for (const liveRange of range.childRanges) {
      if (!liveRange.uses) continue;
      for (let use of liveRange.uses) {
        if (!this.view.instructionRangeHandler.showAllPositions) {
          if (!this.view.instructionRangeHandler.isPositionInRange(use)) continue;
          use = this.view.instructionRangeHandler.getIndexFromPosition(use);
        }
        const gridCell = grid.getCell(row, use);
        if (gridCell !== undefined) {
          gridCell.classList.toggle("range-use", true);
        }
      }
    }
  }
}

// A simple storage class for the data used when constructing an interval string.
class IntervalStringData {
  mainString: string;
  textLength: number;
  paddingLeft: string;

  constructor(tooltip: string) {
    this.mainString = tooltip;
    this.textLength = tooltip.length;
    this.paddingLeft = null;
  }
}

class StringConstructor {
  view: RangeView;

  intervalStringData: IntervalStringData;

  constructor(view: RangeView) {
    this.view = view;
  }

  public setRegisterString(registerName: string, isVirtual: boolean, regEl: HTMLElement) {
    if (this.view.userSettings.get("flipped")) {
      const nums = registerName.match(/\d+/);
      const num = nums ? nums[0] : registerName.substring(1);
      let str = num[num.length - 1];
      for (let i = 2; i <= Math.max(this.view.maxLengthVirtualRegisterNumber, 2); ++i) {
        const addition = num.length < i ? "<span class='range-transparent'>_</span>"
                                        : num[num.length - i];
        str = `${addition} ${str}`;
      }
      regEl.innerHTML = str;
    } else if (!isVirtual) {
      const span = "".padEnd(C.FIXED_REGISTER_LABEL_WIDTH - registerName.length, "_");
      regEl.innerHTML = `HW - <span class='range-transparent'>${span}</span>${registerName}`;
    } else {
      regEl.innerText = registerName;
    }
  }

  // Each interval displays a string of information about it.
  public setIntervalString(spanEl: HTMLElement,
                            spanElBehind: HTMLElement, tooltip: string, numCells: number): void {
    const isFlipped = this.view.userSettings.get("flipped");
    const spacePerCell = isFlipped ? 1 : this.view.cssVariables.positionWidth + 0.25;
    // One character space is removed to accommodate for padding.
    const totalSpaceAvailable = (numCells * spacePerCell) - (isFlipped ? 0 : 0.5);
    this.intervalStringData = new IntervalStringData(tooltip);
    spanElBehind.innerHTML = "";
    spanEl.style.width = null;

    this.setIntervalStringPadding(spanEl, spanElBehind, totalSpaceAvailable, isFlipped);
    if (this.intervalStringData.textLength > totalSpaceAvailable) {
      this.intervalStringData.mainString = "";
      spanElBehind.innerHTML = "";
    }
    spanEl.innerHTML = this.intervalStringData.mainString;
  }

  private setIntervalStringPadding(spanEl: HTMLElement, spanElBehind: HTMLElement,
                                   totalSpaceAvailable: number, isFlipped: boolean) {
    // Add padding at start of text if possible
    if (this.intervalStringData.textLength <= totalSpaceAvailable) {
      if (isFlipped) {
        spanEl.style.paddingTop =
          this.intervalStringData.textLength == totalSpaceAvailable ? "0.25em" : "1em";
        spanEl.style.paddingLeft = null;
      } else {
        this.intervalStringData.paddingLeft =
          (this.intervalStringData.textLength == totalSpaceAvailable) ? "0.5ch" : "1ch";
        spanEl.style.paddingTop = null;
      }
    } else {
      spanEl.style.paddingTop = null;
    }
    if (spanElBehind.innerHTML.length > 0) {
      // Apply same styling to spanElBehind as to spanEl.
      spanElBehind.setAttribute("style", spanEl.getAttribute("style"));
      spanElBehind.style.paddingLeft = this.intervalStringData.paddingLeft;
    } else {
      spanEl.style.paddingLeft = this.intervalStringData.paddingLeft;
    }
  }
}

// A simple class to tally the total number of each type of register and store
// the register prefixes for use in the register type header.
class RegisterTypeHeaderData {
  virtualCount: number;
  generalCount: number;
  floatCount: number;

  generalPrefix: string;
  floatPrefix: string;

  constructor() {
    this.virtualCount = 0;
    this.generalCount = 0;
    this.floatCount = 0;
    this.generalPrefix = "x";
    this.floatPrefix = "fp";
  }

  public countFixedRegister(registerName: string, ranges: [Range, Range]) {
    const range = ranges[0] ? ranges[0] : ranges[1];
    if (range.childRanges[0].isFloatingPoint()) {
      ++(this.floatCount);
      this.floatPrefix = this.floatPrefix == "fp" ? registerName.match(/\D+/)[0] : this.floatPrefix;
    } else {
      ++(this.generalCount);
      this.generalPrefix = this.generalPrefix == "x" ? registerName[0] : this.generalPrefix;
    }
  }
}

class RangeViewConstructor {
  view: RangeView;
  grid: Grid;
  registerTypeHeaderData: RegisterTypeHeaderData;
  // Group the rows in divs to make hiding/showing divs more efficient.
  currentGroup: HTMLElement;
  currentPlaceholderGroup: HTMLElement;

  constructor(rangeView: RangeView) {
    this.view = rangeView;
    this.registerTypeHeaderData = new RegisterTypeHeaderData();
  }

  public construct(): void {
    const backgroundCanvas = document.createElement("canvas");
    const backgroundCanvasDiv = createElement("div", "range-background-canvas-div");
    backgroundCanvasDiv.appendChild(backgroundCanvas);

    this.grid = new Grid(backgroundCanvas);
    this.view.gridAccessor.addGrid(this.grid);

    this.view.divs.wholeHeader = this.elementForHeader();
    this.view.divs.showOnLoad.appendChild(this.view.divs.wholeHeader);

    const gridContainer = document.createElement("div");
    this.view.divs.grid = this.elementForGrid();
    this.view.divs.yAxis = createElement("div", "range-y-axis");
    this.view.divs.yAxis.appendChild(this.view.divs.registerHeaders);
    this.view.divs.yAxis.onscroll = () => {
      this.view.scrollHandler.syncScroll(ToSync.TOP, this.view.divs.yAxis, this.view.divs.grid);
      this.view.scrollHandler.saveScroll();
    };

    gridContainer.appendChild(backgroundCanvasDiv);
    gridContainer.appendChild(this.view.divs.yAxis);
    gridContainer.appendChild(this.view.divs.grid);
    this.view.selectionHandler.addBackground(this.view.divs.grid);
    this.view.divs.showOnLoad.appendChild(gridContainer);

    this.resetGroups();
    this.addFixedRanges(this.addVirtualRanges(0));

    this.view.divs.registersTypeHeader = this.elementForRegisterTypeHeader();
    this.view.divs.registerHeaders.insertBefore(this.view.divs.registersTypeHeader,
                                                this.view.divs.registers);
  }

  // The following three functions are for constructing the groups which the rows are contained
  // within and which make up the grid. This is so as to allow groups of rows to easily be displayed
  // and hidden for performance reasons. As rows are constructed, they are added to the currentGroup
  // div. Each row in currentGroup is matched with an equivalent placeholder row in
  // currentPlaceholderGroup that will be shown when currentGroup is hidden so as to maintain the
  // dimensions and scroll positions of the grid.

  private resetGroups(): void {
    this.currentGroup = createElement("div", "range-positions-group range-hidden");
    this.currentPlaceholderGroup = createElement("div", "range-positions-group");
  }

  private appendGroupsToGrid(row: number): void {
    const endRow = row + 2;
    const numRows = this.currentPlaceholderGroup.children.length;
    this.currentGroup.style.gridRow = `${endRow - numRows} / ${numRows == 1 ? "auto" : endRow}`;
    this.currentPlaceholderGroup.style.gridRow = this.currentGroup.style.gridRow;
    this.currentGroup.style.gridTemplateRows =
    this.view.rowConstructor.getGridTemplateRowsValueForGroupDiv(numRows);
    this.currentPlaceholderGroup.style.gridTemplateRows = this.currentGroup.style.gridTemplateRows;
    this.view.divs.grid.appendChild(this.currentPlaceholderGroup);
    this.view.divs.grid.appendChild(this.currentGroup);
  }

  private addRowToGroup(row: number, rowEl: HTMLElement): void {
    this.currentGroup.appendChild(rowEl);
    this.currentPlaceholderGroup.appendChild(
      createElement("div", "range-positions range-positions-placeholder", "_"));

    if ((row + 1) % C.ROW_GROUP_SIZE == 0) {
      this.appendGroupsToGrid(row);
      this.resetGroups();
    }
  }

  private addVirtualRanges(row: number): number {
    return this.view.instructionRangeHandler.forEachLiveRange(row,
      (registerIndex: number, row: number, registerName: string, range: Range) => {
        const registerId = C.VIRTUAL_REGISTER_ID_PREFIX + registerName;
        const rowEl = this.elementForRow(row, registerId, registerIndex, [range, undefined]);
        if (rowEl) {
          const registerEl = this.elementForRegister(row, registerName, registerId, true);
          this.addRowToGroup(row, rowEl);
          this.view.divs.registers.appendChild(registerEl);
          this.view.focusHandler.virtualRegisterToRowMap.set(registerIndex, row);
          ++(this.registerTypeHeaderData.virtualCount);
          return true;
        }
        return false;
      });
  }

  private addFixedRanges(row: number): void {
    row = this.view.instructionRangeHandler.forEachFixedRange(row,
      (registerIndex: number, row: number, registerName: string, ranges: [Range, Range]) => {
        const rowEl = this.elementForRow(row, registerName, registerIndex, ranges);
        if (rowEl) {
          this.registerTypeHeaderData.countFixedRegister(registerName, ranges);
          const registerEl = this.elementForRegister(row, registerName, registerName, false);
          this.addRowToGroup(row, rowEl);
          this.view.divs.registers.appendChild(registerEl);
          return true;
        }
        return false;
      });

    if (row % C.ROW_GROUP_SIZE != 0) {
      this.appendGroupsToGrid(row - 1);
    }
  }

  // Each row of positions and intervals associated with a register is contained in a single
  // HTMLElement. RangePair is used to allow the two fixed register live ranges of normal and
  // deferred to be easily combined into a single row.
  private elementForRow(row: number, registerId: string, registerIndex: number,
                        ranges: [Range, Range]): HTMLElement {
    const rowEl = createElement("div", "range-positions");

    const getElementForEmptyPosition = (column: number) => {
      const position = this.view.instructionRangeHandler.getPositionFromIndex(column);
      const blockBorder = this.view.blocksData.isBlockBorder(position);
      const classes = "range-position range-empty range-background-div " + (blockBorder
        ? "range-block-border" : this.view.blocksData.isInstructionBorder(position)
          ? "range-instr-border" : "range-position-border");

      const positionEl = createElement("div", classes, "_");
      positionEl.style.gridColumn = String(column + 1);
      rowEl.appendChild(positionEl);
      return positionEl;
    };

    const callbackForInterval = (_, interval: HTMLElement) => {
      rowEl.appendChild(interval);
    };

    // Only construct the row if it has any intervals.
    if (this.view.rowConstructor.construct(this.grid, row, registerId, registerIndex, ranges,
                                           getElementForEmptyPosition, callbackForInterval)) {
      return rowEl;
    }
    return undefined;
  }

  private elementForRegister(row: number, registerName: string,
                             registerId: string, isVirtual: boolean) {
    const regEl = createElement("div", "range-reg");
    this.view.stringConstructor.setRegisterString(registerName, isVirtual, regEl);
    regEl.dataset.virtual = isVirtual.toString();
    regEl.setAttribute("title", registerName);
    regEl.style.gridColumn = String(row + 1);
    this.view.selectionHandler.addRegister(regEl, registerId, row);
    return regEl;
  }

  private elementForRegisterTypeHeader() {
    let column = 1;
    const addTypeHeader = (container: HTMLElement, count: number,
                           textOptions: {max: string, med: string, min: string}) => {
      if (count) {
        const element = createElement("div", "range-type-header");
        element.setAttribute("title", textOptions.max);
        element.style.gridColumn = column + " / " + (column + count);
        column += count;
        element.dataset.count = String(count);
        element.dataset.max = textOptions.max;
        element.dataset.med = textOptions.med;
        element.dataset.min = textOptions.min;
        container.appendChild(element);
      }
    };
    const registerTypeHeaderEl = createElement("div", "range-registers-type range-hidden");
    addTypeHeader(registerTypeHeaderEl, this.registerTypeHeaderData.virtualCount,
          {max: "virtual registers", med: "virtual", min: "v"});
    addTypeHeader(registerTypeHeaderEl, this.registerTypeHeaderData.generalCount,
          { max: "general registers",
            med: "general",
            min: this.registerTypeHeaderData.generalPrefix });
    addTypeHeader(registerTypeHeaderEl, this.registerTypeHeaderData.floatCount,
          { max: "floating point registers",
            med: "floating point",
            min: this.registerTypeHeaderData.floatPrefix});
    return registerTypeHeaderEl;
  }

  // The header element contains the three headers for the LifeTimePosition axis.
  private elementForHeader(): HTMLElement {
    const headerEl = createElement("div", "range-header");
    this.view.divs.positionHeaders = createElement("div", "range-position-labels");

    this.view.divs.positionHeaders.appendChild(this.elementForBlockHeader());
    this.view.divs.positionHeaders.appendChild(this.elementForInstructionHeader());
    this.view.divs.positionHeaders.appendChild(this.elementForPositionHeader());

    headerEl.appendChild(this.view.divs.positionHeaders);
    headerEl.onscroll = () => {
      this.view.scrollHandler.syncScroll(ToSync.LEFT,
        this.view.divs.wholeHeader, this.view.divs.grid);
      this.view.scrollHandler.saveScroll();
    };

    return headerEl;
  }

  // The LifeTimePosition axis shows three headers, for positions, instructions, and blocks.

  private elementForBlockHeader(): HTMLElement {
    const headerEl = createElement("div", "range-block-ids");

    let blockId = 0;
    const lastPos = this.view.instructionRangeHandler.getLastPosition();
    for (let position = 0; position <= lastPos;) {
      const instrCount = this.view.blocksData.blockInstructionCountMap.get(blockId);
      if (this.view.instructionRangeHandler.showAllPositions) {
        headerEl.appendChild(this.elementForBlock(blockId, position, instrCount));
      } else {
        let blockInterval =
          new Interval([position, position + (C.POSITIONS_PER_INSTRUCTION * instrCount)]);
        if (this.view.instructionRangeHandler.isIntervalInRange(blockInterval)) {
          blockInterval = this.view.instructionRangeHandler
                                   .convertBlockPositionsToIndexes(blockId, blockInterval);
          headerEl.appendChild(this.elementForBlock(blockId, blockInterval.start,
                  (blockInterval.end - blockInterval.start) / C.POSITIONS_PER_INSTRUCTION));
        }
      }
      ++blockId;
      position += instrCount * C.POSITIONS_PER_INSTRUCTION;
    }

    return headerEl;
  }

  private elementForBlock(blockId: number, firstColumn: number, instrCount: number):
    HTMLElement {
    const element =
      createElement("div", "range-block-id range-header-element range-block-border");
    const str = `B${blockId}`;
    const idEl = createElement("span", "range-block-id-number", str);
    const centre = instrCount * C.POSITIONS_PER_INSTRUCTION;
    idEl.style.gridRow = `${centre} / ${centre + 1}`;
    element.appendChild(idEl);
    element.setAttribute("title", str);
    element.dataset.instrCount = String(instrCount);
    // gridColumns start at 1 rather than 0.
    const firstGridCol = firstColumn + 1;
    const lastGridCol = firstGridCol + (instrCount * C.POSITIONS_PER_INSTRUCTION);
    element.style.gridColumn = `${firstGridCol} / ${lastGridCol}`;
    element.style.gridTemplateRows = `repeat(${8 * instrCount},
      calc((${this.view.cssVariables.flippedPositionHeight}em +
            ${this.view.cssVariables.blockBorderWidth}px)/2))`;
    this.view.selectionHandler.addBlock(element, blockId, firstColumn);
    return element;
  }

  private elementForInstructionHeader(): HTMLElement {
    const headerEl = createElement("div", "range-instruction-ids");
    let blockId = this.view.instructionRangeHandler.getBlockIdFromIndex(0);
    let instrId = this.view.instructionRangeHandler.getInstructionIdFromIndex(0);
    const instrLimit = instrId + this.view.instructionRangeHandler.numInstructions;
    for (; instrId < instrLimit; ++instrId) {
      headerEl.appendChild(this.elementForInstruction(instrId, blockId));
      if (this.view.blocksData.isInstructionIdOnBlockBorder(instrId)) ++blockId;
    }

    return headerEl;
  }

  private elementForInstruction(instrId: number, blockId: number): HTMLElement {
    const isBlockBorder = this.view.blocksData.isInstructionIdOnBlockBorder(instrId);
    const classes = "range-instruction-id range-header-element "
      + (isBlockBorder ? "range-block-border" : "range-instr-border");
    const element = createElement("div", classes);
    element.appendChild(createElement("span", "range-instruction-id-number", String(instrId)));
    element.setAttribute("title", String(instrId));
    const instrIndex = this.view.instructionRangeHandler.getInstructionIndex(instrId);
    const firstGridCol = (instrIndex * C.POSITIONS_PER_INSTRUCTION) + 1;
    element.style.gridColumn = `${firstGridCol} / ${(firstGridCol + C.POSITIONS_PER_INSTRUCTION)}`;
    this.view.selectionHandler.addInstruction(element, instrId, instrIndex, blockId);
    return element;
  }

  private elementForPositionHeader(): HTMLElement {
    const headerEl = createElement("div", "range-positions range-positions-header");

    let blockId = this.view.instructionRangeHandler.getBlockIdFromIndex(0);
    let position = this.view.instructionRangeHandler.getPositionFromIndex(0);
    const lastPos = this.view.instructionRangeHandler.getLastPosition();
    for (; position <= lastPos; ++position) {
      const isBlockBorder = this.view.blocksData.isBlockBorder(position);
      headerEl.appendChild(this.elementForPosition(position, blockId, isBlockBorder));
      if (isBlockBorder) ++blockId;
    }

    return headerEl;
  }

  private elementForPosition(position: number, blockId: number,
                             isBlockBorder: boolean): HTMLElement {
    const classes = "range-position range-header-element " +
      (isBlockBorder ? "range-block-border"
        : this.view.blocksData.isInstructionBorder(position) ? "range-instr-border"
          : "range-position-border");

    const element = createElement("div", classes, String(position));
    element.setAttribute("title", String(position));
    this.view.selectionHandler.addPosition(element, position, blockId);
    return element;
  }

  private elementForGrid(): HTMLElement {
    const gridEl = createElement("div", "range-grid");
    gridEl.onscroll = () => {
      this.view.scrollHandler.syncScroll(ToSync.TOP, this.view.divs.grid, this.view.divs.yAxis);
      this.view.scrollHandler.syncScroll(ToSync.LEFT,
        this.view.divs.grid, this.view.divs.wholeHeader);
      this.view.scrollHandler.saveScroll();
    };
    return gridEl;
  }
}

// Handles the work required when the phase is changed.
// Between before and after register allocation for example.
class PhaseChangeHandler {
  view: RangeView;

  constructor(view: RangeView) {
    this.view = view;
  }

  // Called when the phase view is switched between before and after register allocation.
  public phaseChange(): void {
    if (!this.view.gridAccessor.hasGrid()) {
      // If this phase view has not been seen yet then the intervals need to be constructed.
      this.addNewIntervals();
    }
    // Show all intervals pertaining to the current phase view.
    this.view.intervalsAccessor.forEachInterval((phase, interval) => {
      interval.classList.toggle("range-hidden", phase != this.view.sequenceView.currentPhaseIndex);
    });
    this.view.focusHandler.resetFocus();
  }

  private addNewIntervals(): void {
    // All Grids should point to the same HTMLElement for empty cells in the grid,
    // so as to avoid duplication. The current Grid is used to retrieve these elements.
    const currentGrid = this.view.gridAccessor.getAnyGrid();
    const newGrid = new Grid(currentGrid.background);
    this.view.gridAccessor.addGrid(newGrid);

    let row = 0;
    row = this.view.instructionRangeHandler.forEachLiveRange(row,
      (registerIndex: number, row: number, registerName: string, range: Range) => {
        this.addnewIntervalsInRange(currentGrid, newGrid, row,
                    C.VIRTUAL_REGISTER_ID_PREFIX + registerName, registerIndex, [range, undefined]);
        return true;
    });

    this.view.instructionRangeHandler.forEachFixedRange(row,
      (registerIndex, row, registerName, ranges) => {
        this.addnewIntervalsInRange(currentGrid, newGrid, row, registerName, registerIndex, ranges);
        return true;
      });
  }

  private addnewIntervalsInRange(currentGrid: Grid, newGrid: Grid, row: number, registerId: string,
                                 registerIndex: number, ranges: [Range, Range]): void {
    const numReplacements = new Map<HTMLElement, number>();

    const getElementForEmptyPosition = (column: number) => {
      return currentGrid.getCell(row, column);
    };

    // Inserts new interval beside existing intervals.
    const callbackForInterval = (column: number, interval: HTMLElement) => {
      // Overlapping intervals are placed beside each other and the relevant ones displayed.
      let currentInterval = currentGrid.getInterval(row, column);
      // The number of intervals already inserted is tracked so that the inserted intervals
      // are ordered correctly.
      const intervalsAlreadyInserted = numReplacements.get(currentInterval);
      numReplacements.set(currentInterval, intervalsAlreadyInserted
        ? intervalsAlreadyInserted + 1 : 1);

      if (intervalsAlreadyInserted) {
        for (let j = 0; j < intervalsAlreadyInserted; ++j) {
          currentInterval = (currentInterval.nextElementSibling as HTMLElement);
        }
      }

      interval.classList.add("range-hidden");
      currentInterval.insertAdjacentElement("afterend", interval);
    };

    this.view.rowConstructor.construct(newGrid, row, registerId, registerIndex, ranges,
      getElementForEmptyPosition, callbackForInterval);
  }
}

// Manages the limitation of how many instructions are shown in the grid.
class InstructionRangeHandler {
  view: RangeView;

  numPositions: number;
  numInstructions: number;
  showAllPositions: boolean;

  private positionRange: [number, number];
  private instructionRange: [number, number];
  private blockRange: [number, number];

  constructor(view: RangeView, firstInstr: number, lastInstr: number) {
    this.view = view;
    this.showAllPositions = false;
    this.blockRange = [0, -1];
    this.instructionRange = this.getValidRange(firstInstr, lastInstr);
    if (this.instructionRange[0] == 0
        && this.instructionRange[1] == this.view.sequenceView.numInstructions) {
      this.showAllPositions = true;
    }
    this.updateInstructionRange();
  }

  public isNewRangeViewRequired(firstInstr: number, lastInstr: number): boolean {
    const validRange = this.getValidRange(firstInstr, lastInstr);
    return (this.instructionRange[0] != validRange[0])
        || (this.instructionRange[1] != validRange[1]);
  }

  public getValidRange(firstInstr: number, lastInstr: number): [number, number] {
    const maxInstructions = Math.floor(C.MAX_NUM_POSITIONS / C.POSITIONS_PER_INSTRUCTION);
    const validRange = [firstInstr, lastInstr + 1] as [number, number];
    if (isNaN(lastInstr)) {
      validRange[1] = this.view.sequenceView.numInstructions;
    }
    if (isNaN(firstInstr)) {
      validRange[0] = (isNaN(lastInstr) || validRange[1] < maxInstructions)
                       ? 0 : validRange[1] - maxInstructions;
    }
    if (!this.isValidRange(validRange[0], validRange[1])) {
      console.warn("Invalid range: displaying default view.");
      validRange[0] = 0;
      validRange[1] = this.view.sequenceView.numInstructions;
    }
    const rangeLength = validRange[1] - validRange[0];
    if (C.POSITIONS_PER_INSTRUCTION * rangeLength > C.MAX_NUM_POSITIONS) {
      validRange[1] = validRange[0] + maxInstructions;
      console.warn("Cannot display more than " + maxInstructions
                 + " instructions in the live ranges grid at one time.");
    }
    return validRange;
  }

  public isValidRange(firstInstr: number, instrLimit: number): boolean {
    return 0 <= firstInstr && firstInstr < instrLimit
        && instrLimit <= this.view.sequenceView.numInstructions;
  }

  public updateInstructionRange(): void {
    this.numInstructions = this.showAllPositions
                           ? this.view.sequenceView.numInstructions
                           : this.instructionRange[1] - this.instructionRange[0];
    this.numPositions = this.numInstructions * C.POSITIONS_PER_INSTRUCTION;
    this.positionRange = [C.POSITIONS_PER_INSTRUCTION * this.instructionRange[0],
                          C.POSITIONS_PER_INSTRUCTION * this.instructionRange[1]];
  }

  public getInstructionRangeString(): string {
    if (this.showAllPositions) {
      return "all instructions";
    } else {
      return "instructions [" + this.instructionRange[0]
           + ", " + (this.instructionRange[1] - 1) + "]";
    }
  }

  public getLastPosition(): number {
    return this.positionRange[1] - 1;
  }

  public getFirstPosition(): number {
    return this.positionRange[0];
  }

  public getPositionFromIndex(index: number): number {
    return index + this.positionRange[0];
  }

  public getIndexFromPosition(position: number): number {
    return position - this.positionRange[0];
  }

  public getInstructionIdFromIndex(index: number): number {
    return index + this.instructionRange[0];
  }

  public getInstructionIndex(id: number): number {
    return id - this.instructionRange[0];
  }

  public getBlockIdFromIndex(index: number): number {
    return index + this.blockRange[0];
  }

  public getBlockIndex(id: number): number {
    return id - this.blockRange[0];
  }

  public isPositionInRange(position: number): boolean {
    return position >= this.positionRange[0] && position < this.positionRange[1];
  }

  public isIntervalInRange(interval: Interval): boolean {
    return interval.start < this.positionRange[1] && interval.end > this.positionRange[0];
  }

  public convertIntervalPositionsToIndexes(interval: Interval): Interval {
    return new Interval([Math.max(0, interval.start - this.positionRange[0]),
                         Math.min(this.numPositions, interval.end - this.positionRange[0])]);
  }

  public convertBlockPositionsToIndexes(blockIndex: number, interval: Interval): Interval {
    if (this.blockRange[1] < 0) this.blockRange[0] = blockIndex;
    this.blockRange[1] = blockIndex + 1;
    return this.convertIntervalPositionsToIndexes(interval);
  }

  public isLastPosition(position: number): boolean {
    return !this.showAllPositions && (position == this.getLastPosition());
  }

  public isLastInstruction(instrId: number): boolean {
    return !this.showAllPositions && (instrId == this.instructionRange[1] - 1);
  }

  public forEachLiveRange(row: number, callback: (registerIndex: number, row: number,
                                          registerName: string, range: Range) => boolean): number {
    const source = this.view.sequenceView.sequence.registerAllocation;
    for (const [registerIndex, range] of source.liveRanges.entries()) {
      if (!range ||
          (!this.showAllPositions &&
            (range.instructionRange[0] >= this.positionRange[1]
              || this.positionRange[0] >= range.instructionRange[1]))) {
        continue;
      }
      if (callback(registerIndex, row, `v${registerIndex}`, range)) {
        ++row;
      }
    }
    return row;
  }

  public forEachFixedRange(row: number, callback: (registerIndex: number, row: number,
                                                   registerName: string,
                                                   ranges: [Range, Range]) => boolean): number {
    const forEachRangeInMap = (rangeMap: Array<Range>) => {
      // There are two fixed live ranges for each register, one for normal, another for deferred.
      // These are combined into a single row.
      const fixedRegisterMap = new Map<string, {registerIndex: number, ranges: [Range, Range]}>();
      for (const [registerIndex, range] of rangeMap.entries()) {
        if (!range ||
          (!this.showAllPositions &&
            (range.instructionRange[0] >= this.positionRange[1]
              || this.positionRange[0] >= range.instructionRange[1]))) {
        continue;
      }
        const registerName = range.fixedRegisterName();
        if (fixedRegisterMap.has(registerName)) {
          const entry = fixedRegisterMap.get(registerName);
          entry.ranges[1] = range;
          // Only use the deferred register index if no normal index exists.
          if (!range.isDeferred) {
            entry.registerIndex = registerIndex;
          }
        } else {
          fixedRegisterMap.set(registerName, {registerIndex, ranges: [range, undefined]});
        }
      }
      // Sort the registers by number.
      const sortedMap = new Map([...fixedRegisterMap.entries()].sort(([nameA, _], [nameB, __]) => {
        if (nameA.length > nameB.length) {
          return 1;
        } else if (nameA.length < nameB.length) {
          return -1;
        } else if (nameA > nameB) {
          return 1;
        } else if (nameA < nameB) {
          return -1;
        }
        return 0;
      }));

      for (const [registerName, {ranges, registerIndex}] of sortedMap) {
        if (callback(-registerIndex - 1, row, registerName, ranges)) {
          ++row;
        }
      }
    };

    const source = this.view.sequenceView.sequence.registerAllocation;
    forEachRangeInMap(source.fixedLiveRanges);
    forEachRangeInMap(source.fixedDoubleLiveRanges);

    return row;
  }
}

// This class tracks what element is selected and scrolls it into view.
class FocusHandler implements RegisterAllocationSelectionHandler, BlockSelectionHandler {
  rangeView: RangeView;
  virtualRegisterToRowMap: Map<number, number>;
  elementInFocus: HTMLElement;
  isBackgroundInFocus: boolean;
  // coordsInFocus in used to resetFocus after it may have been cleared as well as
  // allowing for quick access to the coordinates.
  coordsInFocus: [number, number];

  constructor(rangeView: RangeView) {
    this.rangeView = rangeView;
    this.virtualRegisterToRowMap = new Map<number, number>();
    this.elementInFocus = undefined;
    this.isBackgroundInFocus = false;
    this.coordsInFocus = [-1, -1];
  }

  private getHeaderElement(row: number, index: number) {
    return this.rangeView.divs.positionHeaders.children[C.RANGES_NUM_POS_HEADER_ROWS + row]
                                              .children[index] as HTMLElement;
  }

  private moveFocusTo(element: HTMLElement, eventInitDict?: MouseEventInit) {
    if (element !== undefined) {
      element.scrollIntoView({ block: 'nearest' });
    } else if (this.isBackgroundInFocus) {
      // TODO: find a way to locate the background scroll offset.
    }
  }

  public isInFocus(element: HTMLElement) {
    return this.elementInFocus == element;
  }

  public setFocus(element: HTMLElement, row: number, column: number, isBackground?: boolean) {
    this.clear();
    this.coordsInFocus[0] = row;
    this.coordsInFocus[1] = column;
    if (element !== undefined) {
      element.classList.toggle("focused", true);
    }
    this.elementInFocus = element;
    this.isBackgroundInFocus = isBackground;
    if (isBackground) {
      this.rangeView.gridAccessor.currentGrid().updateBackgroundFocusPoint(row, column);
    }
  }

  public setFocusVirtualRegister(register: number) {
    const row = this.virtualRegisterToRowMap.get(register);
    if (row) this.moveFocusTo(this.rangeView.divs.registers.children[row] as HTMLElement);
  }

  public clear() {
    if (this.elementInFocus) {
      this.elementInFocus.classList.toggle("focused", false);
      this.elementInFocus = undefined;
    }
    const curGrid = this.rangeView.gridAccessor.currentGrid();
    if (curGrid !== undefined) {
      curGrid.clearBackgroundFocusPoint();
      curGrid.updateBackground(this.rangeView, this.rangeView.divs.grid.scrollTop,
                               this.rangeView.divs.grid.scrollLeft);
      this.isBackgroundInFocus = false;
    }
  }

  public clearCoordsInFocus() {
    this.coordsInFocus[0] = -1;
    this.coordsInFocus[1] = -1;
  }

  public resetFocus() {
    this.clear();
    // Restore focus if saved.
    if (this.coordsInFocus[0] >= 0 || this.coordsInFocus[1] >= 0) {
      this.moveFocusTo(this.rangeView.gridAccessor.getCell(this.coordsInFocus[0],
                                                          this.coordsInFocus[1]));
    }
  }

  public select(instructionIds: Array<number>, selected: boolean, scrollIntoView: boolean): void {}

  public brokeredClear(): void {
    this.clear();
  }

  public brokeredRegisterAllocationSelect(instructionsOffsets: Array<[number, number]>,
                                          selected: boolean): void {
    if (!selected) return;
    for (const offsetPair of instructionsOffsets) {
      for (const offset of offsetPair) {
        const instr = this.getHeaderElement(C.RANGES_INSTR_GRID_ROW,
                                this.rangeView.instructionRangeHandler.getInstructionIndex(offset));
        if (instr) this.moveFocusTo(instr, { shiftKey: true });
      }
    }
  }

  public brokeredBlockSelect(blockIds: Array<number>, selected: boolean): void {
    if (!selected) return;
    for (const id of blockIds) {
      const block = this.getHeaderElement(C.RANGES_BLOCK_GRID_ROW,
                                          this.rangeView.instructionRangeHandler.getBlockIndex(id));
      if (block) this.moveFocusTo(block, { shiftKey: true });
    }
  }
}

// This class works in tandem with the selectionHandlers defined in text-view.ts
// rather than updating HTMLElements explicitly itself.
export class RangeViewSelectionHandler {
  sequenceView: SequenceView;
  rangeView: RangeView;
  rowToRegisterId: Map<number, string>;
  positionToBlockId: Map<number, number>;

  constructor(rangeView: RangeView) {
    this.rangeView = rangeView;
    this.sequenceView = this.rangeView.sequenceView;
    this.rowToRegisterId = new Map<number, string>();
    this.positionToBlockId = new Map<number, number>();

    // Clear all selections when container is clicked.
    this.rangeView.divs.container.onclick = (e: MouseEvent) => {
      if (!e.shiftKey) this.sequenceView.broker.broadcastClear(null);
      this.rangeView.focusHandler.clearCoordsInFocus();
    };
  }

  public addBlock(element: HTMLElement, id: number, firstColumn: number): void {
    element.onclick = (e: MouseEvent) => {
      e.stopPropagation();
      if (!e.shiftKey) {
        this.clear();
      }
      this.select(null, null, [id], true);
      this.rangeView.focusHandler.setFocus(element, C.RANGES_BLOCK_GRID_ROW, firstColumn);
    };
    this.sequenceView.addHtmlElementForBlockId(id, element);
    this.sequenceView.addHtmlElementForBlockId(this.sequenceView.getSubId(id), element);
  }

  public addInstruction(element: HTMLElement, id: number, index: number, blockId: number): void {
    // Select the block which contains the instruction and all positions and cells
    // that are within this instruction.=
    element.onclick = (e: MouseEvent) => {
      e.stopPropagation();
      if (!e.shiftKey) {
        this.clear();
      }
      this.select(null, [id], [this.sequenceView.getSubId(blockId)], true);
      this.rangeView.focusHandler.setFocus(element, C.RANGES_INSTR_GRID_ROW,
                                           C.POSITIONS_PER_INSTRUCTION * index);
    };
    this.sequenceView.addHtmlElementForBlockId(blockId, element);
    this.sequenceView.addHtmlElementForInstructionId(id, element);
    this.sequenceView.addHtmlElementForInstructionId(this.sequenceView.getSubId(id), element);
  }

  public addPosition(element: HTMLElement, position: number, blockId: number): void {
    const instrId = Math.floor(position / C.POSITIONS_PER_INSTRUCTION);
    const column = this.rangeView.instructionRangeHandler.getIndexFromPosition(position);
    // Select the block and instruction which contains this position.
    element.onclick = (e: MouseEvent) => {
      e.stopPropagation();
      if (!e.shiftKey) {
        this.clear();
      }
      this.select(["position-" + position], [this.sequenceView.getSubId(instrId)],
                                            [this.sequenceView.getSubId(blockId)], true);
      this.rangeView.focusHandler.setFocus(element, C.RANGES_POS_GRID_ROW, column);
    };
    this.sequenceView.addHtmlElementForBlockId(blockId, element);
    this.sequenceView.addHtmlElementForInstructionId(instrId, element);
    this.sequenceView.addHtmlElementForNodeId("position-" + position, element);
    this.positionToBlockId.set(position, blockId);
  }

  public addRegister(element: HTMLElement, registerId: string, row: number): void {
    const rowGroupIndex = (Math.floor(row / C.ROW_GROUP_SIZE) * 2) + 1;
    element.onclick = (e: MouseEvent) => {
      e.stopPropagation();
      if (!this.canSelectRow(element, row, rowGroupIndex)) return;
      if (!e.shiftKey) {
        this.clear();
      }
      // The register also effectively selects the row.
      this.select([registerId], null, null, true);
      this.rangeView.focusHandler.setFocus(element, row, C.RANGES_POS_GRID_ROW);
    };
    this.sequenceView.addHtmlElementForNodeId(registerId, element);
    this.rowToRegisterId.set(row, registerId);
  }

  public addRow(element: HTMLElement, registerId: string): void {
    // Highlight row when register is selected.
    this.rangeView.sequenceView.addHtmlElementForNodeId(registerId, element);
  }

  public addInterval(intervalEl: HTMLElement, intervalInnerWrapperEl: HTMLElement,
                     intervalNodeId: string, registerId: string): void {
    // Highlight interval when the interval is selected.
    this.sequenceView.addHtmlElementForNodeId(intervalNodeId, intervalEl);
    // Highlight inner wrapper when row is selected, allowing for different color highlighting.
    this.sequenceView.addHtmlElementForNodeId(registerId, intervalInnerWrapperEl);
  }

  public addCell(element: HTMLElement, row: number, position: number,
                 blockId: number, registerId: string, intervalNodeId?: string, isEmptyCell?: boolean): void {
    const instrId = Math.floor(position / C.POSITIONS_PER_INSTRUCTION);
    const column = this.rangeView.instructionRangeHandler.getIndexFromPosition(position);
    // Select the relevant row by the registerId, and the column by position.
    // Also select the instruction and the block in which the position is in.
    const select = [registerId, "position-" + position];
    if (intervalNodeId) select.push(intervalNodeId);
    const rowGroupIndex = (Math.floor(row / C.ROW_GROUP_SIZE) * 2) + 1;
    element.onclick = (e: MouseEvent) => {
      e.stopPropagation();
      if (!this.canSelectRow(element, row, rowGroupIndex)) return;
      if (!e.shiftKey) {
        this.clear();
      }
      this.select(select, [this.sequenceView.getSubId(instrId)],
                          [this.sequenceView.getSubId(blockId)], true);
      this.rangeView.focusHandler.setFocus(element, row, column);
    };
    if (!isEmptyCell) {
      this.sequenceView.addHtmlElementForBlockId(blockId, element);
      this.sequenceView.addHtmlElementForInstructionId(instrId, element);
      this.sequenceView.addHtmlElementForNodeId("position-" + position, element);
    }
  }

  public addBackground(background: HTMLElement): void {
    background.onclick = (e: MouseEvent) => {
      if (this.rangeView.divs.grid === undefined) return;
      if (this.rangeView.divs.registers === undefined) return;
      if (this.rangeView.divs.positionHeaders === null) return;
      const gridBoundRect = this.rangeView.divs.grid.getBoundingClientRect();
      const clickLeft = e.clientX - gridBoundRect.left + this.rangeView.divs.grid.scrollLeft;
      const clickTop = e.clientY - gridBoundRect.top + this.rangeView.divs.grid.scrollTop;
      const rangePosCellSize = this.rangeView.divs.getRangePositionCellSize();
      const rangeRegCellSize = this.rangeView.divs.getRangeRegCellSize();
      const firstPosition = this.rangeView.instructionRangeHandler.getFirstPosition();
      let row = -1;
      let column = -1;
      if (this.rangeView.userSettings.get("flipped")) {
        row = Math.floor(clickLeft / rangeRegCellSize[0]);
        column = Math.floor(clickTop / rangePosCellSize[1]) + firstPosition;
      } else {
        row = Math.floor(clickTop / rangeRegCellSize[1]);
        column = Math.floor(clickLeft / rangePosCellSize[0]) + firstPosition;
      }
      if (!this.rowToRegisterId.has(row)) return;
      const registerId = this.rowToRegisterId.get(row);
      const positionId = "position-" + column;
      e.stopPropagation();
      if (!e.shiftKey) {
        this.clear();
      }
      this.rangeView.focusHandler.setFocus(undefined, row, column, true);
      const instrId = Math.floor(column / C.POSITIONS_PER_INSTRUCTION);
      const blockId = this.positionToBlockId.get(column);
      this.select([registerId, positionId], [this.sequenceView.getSubId(instrId)],
                                            [this.sequenceView.getSubId(blockId)], true);
    };
  }

  public updateBackground(): void {
    this.rangeView.gridAccessor.currentGrid().updateBackground(this.rangeView,
      this.rangeView.divs.grid.scrollTop, this.rangeView.divs.grid.scrollLeft);
  }

  private canSelectRow(element: HTMLElement, row: number, rowGroupIndex: number): boolean {
    // No need to repeat work if already in focus.
    if (this.rangeView.focusHandler.isInFocus(element)) return false;
    // Don't select anything if the row group which this row is included in is hidden.
    if (row >= 0
        && this.rangeView.divs.grid.children[rowGroupIndex].classList.contains("range-hidden")) {
      this.rangeView.scrollHandler.syncHidden();
      return false;
    }
    return true;
  }

  // Don't call multiple times in a row or the SelectionMapsHandlers will clear their previous
  // causing the HTMLElements to not be deselected by select.
  private clear(): void {
    this.rangeView.focusHandler.clear();
    this.sequenceView.blockSelections.clearCurrent();
    this.sequenceView.instructionSelections.clearCurrent();
    this.sequenceView.nodeSelections.clearCurrent();
    // Mark as cleared so that the HTMLElements are not updated on broadcastClear.
    // The HTMLElements will be updated when select is called.
    this.sequenceView.selectionCleared = true;
    // broadcastClear calls brokeredClear on all SelectionHandlers but that which is passed to it.
    this.sequenceView.broker.broadcastClear(this.sequenceView.nodeSelectionHandler);
    this.sequenceView.selectionCleared = false;
  }

  private select(nodeIds: Iterable<string>, instrIds: Iterable<number>,
                 blockIds: Iterable<number>, selected: boolean): void {
    // Add nodeIds and blockIds to selections.
    if (nodeIds) this.sequenceView.nodeSelections.current.select(nodeIds, selected);
    if (instrIds) this.sequenceView.instructionSelections.current.select(instrIds, selected);
    if (blockIds) this.sequenceView.blockSelections.current.select(blockIds, selected);
    // Update the HTMLElements.
    this.sequenceView.updateSelection(true);
    if (nodeIds) {
      // Broadcast selections to other SelectionHandlers.
      this.sequenceView.broker.broadcastNodeSelect(this.sequenceView.nodeSelectionHandler,
        this.sequenceView.nodeSelections.current.selectedKeys(), selected);
    }
    if (instrIds) {
      this.sequenceView.broker.broadcastInstructionSelect(
            this.sequenceView.registerAllocationSelectionHandler,
            Array.from(this.sequenceView.instructionSelections.current.selectedKeysAsAbsNumbers()),
            selected);
    }
  }
}

class DisplayResetter {
  view: RangeView;
  isFlipped: boolean;

  constructor(view: RangeView) {
    this.view = view;
  }

  // Allow much of the changes required to be done in the css.
  public updateClassesOnContainer(): void {
    this.isFlipped = this.view.userSettings.get("flipped");
    this.view.divs.container.classList.toggle("not_flipped", !this.isFlipped);
    this.view.divs.container.classList.toggle("flipped", this.isFlipped);
  }

  public resetLandscapeMode(isInLandscapeMode: boolean): void {
    // Used to communicate the setting to Resizer.
    this.view.divs.container.dataset.landscapeMode =
                                                  isInLandscapeMode.toString();

    window.dispatchEvent(new Event('resize'));
    // Required to adjust scrollbar spacing.
    setTimeout(() => {
      window.dispatchEvent(new Event('resize'));
    }, 100);
  }

  public resetFlipped(): void {
    this.updateClassesOnContainer();
    // Appending the HTMLElement removes it from its current position.
    this.view.divs.wholeHeader.appendChild(this.isFlipped ? this.view.divs.registerHeaders
                                                          : this.view.divs.positionHeaders);
    this.view.divs.yAxis.appendChild(this.isFlipped ? this.view.divs.positionHeaders
                                                    : this.view.divs.registerHeaders);
    this.resetLayout();
    // Set the label text appropriately.
    this.view.divs.xAxisLabel.innerText = this.isFlipped
                                          ? this.view.divs.xAxisLabel.dataset.flipped
                                          : this.view.divs.xAxisLabel.dataset.notFlipped;
    this.view.divs.yAxisLabel.innerText = this.isFlipped
                                          ? this.view.divs.yAxisLabel.dataset.flipped
                                          : this.view.divs.yAxisLabel.dataset.notFlipped;
    this.view.focusHandler.resetFocus();
  }

  private resetLayout(): void {
    this.resetRegisters();
    this.resetIntervals();
  }

  private resetRegisters(): void {
    // Reset register strings.
    for (const regNode of this.view.divs.registers.childNodes) {
      const regEl = regNode as HTMLElement;
      const registerName = regEl.getAttribute("title");
      this.view.stringConstructor.setRegisterString(registerName,
                                                    regEl.dataset.virtual == "true", regEl);
    }

    // registerTypeHeader is only displayed when the axes are switched.
    this.view.divs.registersTypeHeader.classList.toggle("range-hidden", !this.isFlipped);
    if (this.isFlipped) {
      for (const typeHeader of this.view.divs.registersTypeHeader.children) {
        const element = (typeHeader as HTMLElement);
        const regCount = parseInt(element.dataset.count, 10);
        const spaceAvailable = regCount * Math.floor(this.view.cssVariables.positionWidth);
        // The more space available the longer the header text can be.
        if (spaceAvailable > element.dataset.max.length) {
          element.innerText = element.dataset.max;
        } else if (spaceAvailable > element.dataset.med.length) {
          element.innerText = element.dataset.med;
        } else {
          element.innerText = element.dataset.min;
        }
      }
    }
  }

  private resetIntervals(): void {
    this.view.intervalsAccessor.forEachInterval((_, interval) => {
      const intervalEl = interval as HTMLElement;
      const spanEl = intervalEl.children[0] as HTMLElement;
      const spanElBehind = intervalEl.children[1] as HTMLElement;
      const intervalLength = this.view.getInnerWrapperFromInterval(interval).children.length;
      this.view.stringConstructor.setIntervalString(spanEl, spanElBehind,
                                                    intervalEl.dataset.tooltip, intervalLength);
      const intervalInnerWrapper = intervalEl.children[2] as HTMLElement;
      intervalInnerWrapper.style.gridTemplateColumns =
        this.view.rowConstructor.getGridTemplateColumnsValueForInterval(intervalLength);
    });
  }
}

enum ToSync {
  LEFT,
  TOP
}

// Handles saving and syncing the scroll positions of the grid.
class ScrollHandler {
  view: RangeView;
  scrollTop: number;
  scrollLeft: number;
  scrollTopTimeout: NodeJS.Timeout;
  scrollLeftTimeout: NodeJS.Timeout;
  scrollTopFunc: (this: GlobalEventHandlers, ev: Event) => any;
  scrollLeftFunc: (this: GlobalEventHandlers, ev: Event) => any;

  constructor(view: RangeView) {
    this.view = view;
    this.scrollTop = 0;
    this.scrollLeft = 0;
  }

  // This function is used to hide the rows which are not currently in view and
  // so reduce the performance cost of things like hit tests and scrolling.
  public syncHidden(): void {
    const isFlipped = this.view.userSettings.get("flipped");
    const toHide = new Array<[HTMLElement, HTMLElement]>();

    const sampleCell = this.view.divs.registers.children[1] as HTMLElement;
    const buffer = 2 * (isFlipped ? sampleCell.clientWidth : sampleCell.clientHeight);
    const min = (isFlipped ? this.view.divs.grid.offsetLeft + this.view.divs.grid.scrollLeft
                           : this.view.divs.grid.offsetTop + this.view.divs.grid.scrollTop)
                - buffer;
    const max = (isFlipped ? min + this.view.divs.grid.clientWidth
                           : min + this.view.divs.grid.clientHeight)
                + buffer;

    // The rows are grouped by being contained within a group div. This is so as to allow
    // groups of rows to easily be displayed and hidden with less of a performance cost.
    // Each row in the mainGroup div is matched with an equivalent placeholder row in
    // the placeholderGroup div that will be shown when mainGroup is hidden so as to maintain
    // the dimensions and scroll positions of the grid.

    const rangeGroups = this.view.divs.grid.children;
    for (let i = 1; i < rangeGroups.length; i += 2) {
      const mainGroup = rangeGroups[i] as HTMLElement;
      const placeholderGroup = rangeGroups[i - 1] as HTMLElement;
      const isHidden = mainGroup.classList.contains("range-hidden");
      // The offsets are used to calculate whether the group is in view.
      const offsetMin = this.getOffset(mainGroup.firstChild as HTMLElement,
        placeholderGroup.firstChild as HTMLElement, isHidden, isFlipped);
      const offsetMax = this.getOffset(mainGroup.lastChild as HTMLElement,
        placeholderGroup.lastChild as HTMLElement, isHidden, isFlipped);

      if (offsetMax > min && offsetMin < max) {
        if (isHidden) {
          // Show the rows, hide the placeholders.
          mainGroup.classList.toggle("range-hidden", false);
          placeholderGroup.classList.toggle("range-hidden", true);
        }
      } else if (!isHidden) {
        // Only hide the rows once the new rows are shown so that scrollLeft is not lost.
        toHide.push([mainGroup, placeholderGroup]);
      }
    }

    for (const [mainGroup, placeholderGroup] of toHide) {
      // Hide the rows, show the placeholders.
      mainGroup.classList.toggle("range-hidden", true);
      placeholderGroup.classList.toggle("range-hidden", false);
    }
  }

  // This function is required to keep the axes labels in line with the grid
  // content when scrolling.
  public syncScroll(toSync: ToSync, source: HTMLElement, target: HTMLElement): void {
    // Continually delay timeout until scrolling has stopped.
    toSync == ToSync.TOP
      ? clearTimeout(this.scrollTopTimeout)
      : clearTimeout(this.scrollLeftTimeout);

    if (target.onscroll) {
      if (toSync == ToSync.TOP) {
        this.scrollTopFunc = target.onscroll;
      } else {
        this.scrollLeftFunc = target.onscroll;
      }
    }
    // Clear onscroll to prevent the target syncing back with the source.
    target.onscroll = null;

    if (toSync == ToSync.TOP) {
      target.scrollTop = source.scrollTop;
    } else {
      target.scrollLeft = source.scrollLeft;
    }

    // Only show / hide the grid content once scrolling has stopped.
    if (toSync == ToSync.TOP) {
      this.scrollTopTimeout = setTimeout(() => {
        target.onscroll = this.scrollTopFunc;
        this.syncHidden();
      }, 100);
    } else {
      this.scrollLeftTimeout = setTimeout(() => {
        target.onscroll = this.scrollLeftFunc;
        this.syncHidden();
      }, 100);
    }

    // Update background cell.
    const currentGrid = this.view.gridAccessor.currentGrid();
    const gridScrollTop = this.view.divs.grid.scrollTop;
    const gridScrollLeft = this.view.divs.grid.scrollLeft;
    if (currentGrid !== undefined && (currentGrid.backgroundScrollLeft != gridScrollLeft ||
                                     currentGrid.backgroundScrollTop != gridScrollTop)) {
      currentGrid.updateBackground(this.view, gridScrollTop, gridScrollLeft);
    }
  }

  public saveScroll(): void {
    this.scrollLeft = this.view.divs.grid.scrollLeft;
    this.scrollTop = this.view.divs.grid.scrollTop;
  }

  public restoreScroll(): void {
    if (this.scrollLeft) {
      this.view.divs.grid.scrollLeft = this.scrollLeft;
      this.view.divs.grid.scrollTop = this.scrollTop;
    }
  }

  private getOffset(rowEl: HTMLElement, placeholderRowEl: HTMLElement,
                    isHidden: boolean, isFlipped: boolean): number {
    const element = isHidden ? placeholderRowEl : rowEl;
    return isFlipped ? element.offsetLeft : element.offsetTop;
  }
}

// RangeView displays the live range data as passed in by SequenceView.
// The data is displayed in a grid format, with the fixed and virtual registers
// along one axis, and the LifeTimePositions along the other. Each LifeTimePosition
// is part of an Instruction in SequenceView, which itself is part of an Instruction
// Block. The live ranges are displayed as intervals, each belonging to a register,
// and spanning across a certain range of LifeTimePositions.
// When the phase being displayed changes between before register allocation and
// after register allocation, only the intervals need to be changed.
export class RangeView {
  sequenceView: SequenceView;
  gridAccessor: GridAccessor;
  intervalsAccessor: IntervalElementsAccessor;
  cssVariables: CSSVariables;
  userSettings: UserSettings;
  blocksData: BlocksData;
  divs: Divs;
  scrollHandler: ScrollHandler;
  phaseChangeHandler: PhaseChangeHandler;
  instructionRangeHandler: InstructionRangeHandler;
  selectionHandler: RangeViewSelectionHandler;
  focusHandler: FocusHandler;
  displayResetter: DisplayResetter;
  rowConstructor: RowConstructor;
  stringConstructor: StringConstructor;
  initialized: boolean;
  isShown: boolean;
  maxLengthVirtualRegisterNumber: number;

  constructor(sequence: SequenceView, firstInstr: number, lastInstr: number) {
    this.sequenceView = sequence;
    this.initialized = false;
    this.isShown = false;
    this.instructionRangeHandler = new InstructionRangeHandler(this, firstInstr, lastInstr);
  }

  public initializeContent(blocks: Array<SequenceBlock>, broker: SelectionBroker): void {
    if (!this.initialized) {
      this.gridAccessor = new GridAccessor(this.sequenceView);
      this.intervalsAccessor = new IntervalElementsAccessor(this.sequenceView);
      this.cssVariables = new CSSVariables();
      this.userSettings = new UserSettings();
      this.displayResetter = new DisplayResetter(this);
      // Indicates whether the RangeView is displayed beside or below the SequenceView.
      this.userSettings.addSetting("landscapeMode", false,
      this.displayResetter.resetLandscapeMode.bind(this.displayResetter));
      // Indicates whether the grid axes are switched.
      this.userSettings.addSetting("flipped", false,
      this.displayResetter.resetFlipped.bind(this.displayResetter));
      this.blocksData = new BlocksData(this, blocks);
      this.divs = new Divs(this.userSettings,
        this.instructionRangeHandler.getInstructionRangeString());
      this.displayResetter.updateClassesOnContainer();
      this.scrollHandler = new ScrollHandler(this);
      this.rowConstructor = new RowConstructor(this);
      this.stringConstructor = new StringConstructor(this);
      this.selectionHandler = new RangeViewSelectionHandler(this);
      this.sequenceView.setRangeViewSelectionHandler(this.selectionHandler);
      this.focusHandler = new FocusHandler(this);
      broker.addBlockHandler(this.focusHandler);
      broker.addRegisterAllocatorHandler(this.focusHandler);
      const constructor = new RangeViewConstructor(this);
      constructor.construct();
      this.cssVariables.setVariables(this.instructionRangeHandler.numPositions,
                                     this.divs.registers.children.length);
      this.phaseChangeHandler = new PhaseChangeHandler(this);
      let maxVirtualRegisterNumber = 0;
      for (const register of this.focusHandler.virtualRegisterToRowMap.keys()) {
        maxVirtualRegisterNumber = Math.max(maxVirtualRegisterNumber, register);
      }
      this.maxLengthVirtualRegisterNumber = Math.floor(Math.log10(maxVirtualRegisterNumber)) + 1;
      this.initialized = true;
    } else {
      // If the RangeView has already been initialized then the phase must have
      // been changed.
      this.phaseChangeHandler.phaseChange();
    }
  }

  public show(): void {
    if (!this.isShown) {
      this.isShown = true;
      this.divs.container.appendChild(this.divs.content);
      this.divs.resizerBar.style.visibility = "visible";
      this.divs.container.style.visibility = "visible";
      this.divs.snapper.style.visibility = "visible";
      // Dispatch a resize event to ensure that the
      // panel is shown.
      window.dispatchEvent(new Event("resize"));

      if (this.divs.registers.children.length) {
        setTimeout(() => {
          this.userSettings.resetFromSessionStorage();
          this.focusHandler.resetFocus();
          this.scrollHandler.restoreScroll();
          this.scrollHandler.syncHidden();
          this.selectionHandler.updateBackground();
          this.divs.showOnLoad.style.visibility = "visible";
        }, 100);
      }
    }
  }

  public hide(): void {
    if (this.initialized) {
      this.isShown = false;
      this.divs.container.removeChild(this.divs.content);
      this.divs.resizerBar.style.visibility = "hidden";
      this.divs.container.style.visibility = "hidden";
      this.divs.snapper.style.visibility = "hidden";
      this.divs.showOnLoad.style.visibility = "hidden";
    } else {
      window.document.getElementById(C.RANGES_PANE_ID).style.visibility = "hidden";
    }
    // Dispatch a resize event to ensure that the
    // panel is hidden.
    window.dispatchEvent(new Event("resize"));
  }

  public onresize(): void {
    if (this.divs.registers.children.length && this.isShown) {
     this.scrollHandler.syncHidden();
     this.selectionHandler.updateBackground();
    }
  }

  public getInnerWrapperFromInterval(interval: HTMLElement) {
    return interval.children[2] as HTMLElement;
  }
}
