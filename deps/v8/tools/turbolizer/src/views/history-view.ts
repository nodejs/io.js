// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../common/constants";
import * as d3 from "d3";
import { createElement, getNumericCssValue, measureText } from "../common/util";
import { View } from "./view";
import { SelectionBroker } from "../selection/selection-broker";
import { SourceResolver } from "../source-resolver";
import { GraphNode } from "../phases/graph-phase/graph-node";
import { HistoryHandler } from "../selection/selection-handler";
import { GraphPhase } from "../phases/graph-phase/graph-phase";
import { SelectionStorage } from "../selection/selection-storage";
import { TurboshaftGraphOperation } from "../phases/turboshaft-graph-phase/turboshaft-graph-operation";
import { TurboshaftGraphPhase } from "../phases/turboshaft-graph-phase/turboshaft-graph-phase";
import { PhaseType } from "../phases/phase";

type GNode = GraphNode | TurboshaftGraphOperation;
type GPhase = GraphPhase | TurboshaftGraphPhase;

export class HistoryView extends View {
  node: GNode;
  broker: SelectionBroker;
  sourceResolver: SourceResolver;
  historyHandler: HistoryHandler;
  phaseIdToHistory: Map<number, PhaseHistory>;
  svg: d3.Selection<any, any, any, any>;
  historyList: d3.Selection<any, any, any, any>;
  label: string;
  labelBox: { width: number, height: number };
  x: number;
  y: number;
  maxNodeWidth: number;
  maxPhaseNameWidth: number;
  showPhaseByName: (name: string, selection: SelectionStorage) => void;

  constructor(id: string, broker: SelectionBroker, sourceResolver: SourceResolver,
              showPhaseByName: (name: string, selection: SelectionStorage) => void) {
    super(id);
    this.broker = broker;
    this.sourceResolver = sourceResolver;
    this.showPhaseByName = showPhaseByName;
    this.historyHandler = this.initializeNodeSelectionHandler();
    this.broker.addHistoryHandler(this.historyHandler);
    this.phaseIdToHistory = new Map<number, PhaseHistory>();

    this.x = 0;
    this.y = 0;

    this.initializeSvgHistoryContainer();
  }

  public createViewElement(): HTMLElement {
    return createElement("div", "history-container");
  }

  public hide(): void {
    super.hide();
    this.broker.deleteHistoryHandler(this.historyHandler);
  }

  private initializeSvgHistoryContainer(): void {
    this.svg = d3.select(this.divNode)
      .append("svg")
      .classed("history-svg-container", true)
      .attr("version", "2.0")
      .attr("transform", _ => `translate(${this.x},${this.y})`)
      .style("visibility", "hidden");

    const dragHandler = d3.drag().on("drag", () => {
      const rect = document.body.getBoundingClientRect();
      const x = this.x + d3.event.dx;
      this.x = d3.event.dx > 0 ? Math.min(x, rect.width - this.getWidth()) : Math.max(x, 0);
      const y = this.y + d3.event.dy;
      this.y = d3.event.dy > 0 ? Math.min(y, rect.height - this.getHeight()) : Math.max(y, 0);
      this.svg.attr("transform", _ => `translate(${this.x},${this.y})`);
    });

    this.svg.call(dragHandler);
  }

  private initializeNodeSelectionHandler(): HistoryHandler {
    const view = this;
    return {
      showNodeHistory: function (node: GNode, phaseName: string) {
        view.clear();
        view.node = node;
        const phaseId = view.sourceResolver.getPhaseIdByName(phaseName);
        const historyChain = view.getHistoryChain(phaseId, node);
        view.getPhaseHistory(historyChain);
        view.render();
        if (C.TRACE_HISTORY) view.traceToConsole();
      }
    };
  }

  private render(): void {
    this.setLabel();

    this.svg
      .attr("width", this.getWidth())
      .attr("height", this.getHeight());

    this.svg
      .append("text")
      .classed("history-label", true)
      .attr("text-anchor", "middle")
      .attr("x", this.getWidth() / 2)
      .append("tspan")
      .text(this.label);

    this.svg
      .append("circle")
      .classed("close-button", true)
      .attr("r", this.labelBox.height / 4)
      .attr("cx", this.getWidth() - this.labelBox.height / 2)
      .attr("cy", this.labelBox.height / 2)
      .on("click", () => {
        d3.event.stopPropagation();
        this.clear();
        this.svg.style("visibility", "hidden");
      });

    this.historyList = this.svg
      .append("g")
      .attr("clip-path", "url(#history-clip-path)");

    this.renderHistoryContent();
    this.renderHistoryContentScroll();
    this.svg.style("visibility", "visible");
  }

  private renderHistoryContent(): void {
    const existCircles = new Set<string>();
    const defs = this.svg.append("svg:defs");
    let recordY = 0;

    for (const [phaseId, phaseHistory] of this.phaseIdToHistory.entries()) {
      if (!phaseHistory.hasChanges()) continue;
      const phaseName = this.sourceResolver.getPhaseNameById(phaseId);

      this.historyList
        .append("text")
        .classed("history-item", true)
        .attr("dy", recordY)
        .append("tspan")
        .text(phaseName);
      recordY += this.labelBox.height;

      for (const record of phaseHistory.nodeIdToRecord.values()) {
        const changes = Array.from(record.changes.values()).sort();
        const circleId = changes.map(i => HistoryChange[i]).join("-");

        if (!existCircles.has(circleId)) {
          const def = defs.append("linearGradient")
            .attr("id", circleId);

          const step = 100 / changes.length;
          for (let i = 0; i < changes.length; i++) {
            const start = i * step;
            const stop = (i + 1) * step;

            def.append("stop")
              .attr("offset", `${start}%`)
              .style("stop-color", this.getHistoryChangeColor(changes[i]));

            def.append("stop")
              .attr("offset", `${stop}%`)
              .style("stop-color", this.getHistoryChangeColor(changes[i]));
          }

          existCircles.add(circleId);
        }

        this.historyList
          .append("circle")
          .classed("history-item", true)
          .attr("r", this.labelBox.height / 3.5)
          .attr("cx", this.labelBox.height / 3)
          .attr("cy", this.labelBox.height / 2.5 + recordY)
          .attr("fill", `url(#${circleId})`)
          .append("title")
          .text(`[${record.toString()}]`);

        this.historyList
          .append("text")
          .classed("history-item history-item-record", true)
          .classed("current-history-item", record.changes.has(HistoryChange.Current))
          .attr("dy", recordY)
          .attr("dx", this.labelBox.height * 0.75)
          .append("tspan")
          .text(record.node.displayLabel)
          .on("click", () => {
            if (!record.changes.has(HistoryChange.Removed)) {
              const selectionStorage = new SelectionStorage();
              selectionStorage.adaptNode(record.node.identifier());
              this.showPhaseByName(phaseName, selectionStorage);
            }
          })
          .append("title")
          .text(record.node.getTitle());
        recordY += this.labelBox.height;
      }
    }
  }

  private renderHistoryContentScroll(): void {
    let scrollDistance = 0;
    const historyArea = {
      x: C.HISTORY_CONTENT_INDENT,
      y: this.labelBox.height + C.HISTORY_CONTENT_INDENT / 2,
      width: this.getWidth() - C.HISTORY_CONTENT_INDENT * 2,
      height: this.getHeight() - this.labelBox.height - C.HISTORY_CONTENT_INDENT * 1.5
    };

    const content = this.historyList
      .append("g")
      .attr("transform", `translate(${historyArea.x},${historyArea.y})`);

    this.historyList
      .selectAll(".history-item")
      .each(function () {
        content.node().appendChild(d3.select(this).node() as HTMLElement);
      });

    this.historyList
      .append("clipPath")
      .attr("id", "history-clip-path")
      .append("rect")
      .attr("width", historyArea.width)
      .attr("height", historyArea.height)
      .attr("transform", `translate(${historyArea.x},${historyArea.y})`);

    const scrollX = historyArea.x + historyArea.width - C.HISTORY_SCROLLBAR_WIDTH;
    const scrollBar = this.historyList
      .append("rect")
      .classed("history-content-scroll", true)
      .attr("width", C.HISTORY_SCROLLBAR_WIDTH)
      .attr("rx", C.HISTORY_SCROLLBAR_WIDTH / 2)
      .attr("ry", C.HISTORY_SCROLLBAR_WIDTH / 2)
      .attr("transform", `translate(${scrollX},${historyArea.y})`);

    // Calculate maximum scrollable amount
    const contentBBox = content.node().getBBox();
    const absoluteContentHeight = contentBBox.y + contentBBox.height;

    const scrollbarHeight = historyArea.height * historyArea.height / absoluteContentHeight;
    scrollBar.attr("height", Math.min(scrollbarHeight, historyArea.height));

    const maxScroll = Math.max(absoluteContentHeight - historyArea.height, 0);

    const updateScrollPosition = (diff: number) => {
      scrollDistance += diff;
      scrollDistance = Math.min(maxScroll, Math.max(0, scrollDistance));

      content.attr("transform", `translate(${historyArea.x},${historyArea.y - scrollDistance})`);
      const scrollBarPosition = scrollDistance / maxScroll * (historyArea.height - scrollbarHeight);
      if (!isNaN(scrollBarPosition)) scrollBar.attr("y", scrollBarPosition);
    };

    this.svg.on("wheel", () => {
      updateScrollPosition(d3.event.deltaY);
    });

    const dragBehaviour = d3.drag().on("drag", () => {
      updateScrollPosition(d3.event.dy * maxScroll / (historyArea.height - scrollbarHeight));
    });

    scrollBar.call(dragBehaviour);
  }

  private setLabel(): void {
    this.label = this.node.getHistoryLabel();
    const coefficient = this.getCoefficient("history-tspan-font-size");
    this.labelBox = measureText(this.label, coefficient);
  }

  private getCoefficient(varName: string): number {
    const tspanSize = getNumericCssValue("--tspan-font-size");
    const varSize = getNumericCssValue(`--${varName}`);
    return Math.min(tspanSize, varSize) / Math.max(tspanSize, varSize);
  }

  private getPhaseHistory(historyChain: Map<number, GNode>): void {
    const uniqueAncestors = new Set<string>();
    const coefficient = this.getCoefficient("history-item-tspan-font-size");
    let prevNode = null;
    let first = true;
    for (let i = 0; i < this.sourceResolver.phases.length; i++) {
      const phase = this.sourceResolver.getGraphPhase(i);
      if (!phase) continue;

      const phaseNameMeasure = measureText(phase.name, coefficient);
      this.maxPhaseNameWidth = Math.max(this.maxPhaseNameWidth, phaseNameMeasure.width);
      const node = historyChain.get(i);
      if (!node && prevNode) {
        this.addToHistory(i, prevNode, HistoryChange.Removed);
      }

      if (phase.type == PhaseType.Graph && node &&
        phase.originIdToNodesMap.has(node.identifier())) {
        this.addHistoryAncestors(node.identifier(), phase, uniqueAncestors);
      }

      if (phase.type == PhaseType.TurboshaftGraph && node?.getNodeOrigin().phase == phase.name) {
        this.addToHistory(i, node, HistoryChange.Lowered);
      }

      if (phase.type == PhaseType.Graph && prevNode && !prevNode.equals(node) &&
        phase.originIdToNodesMap.has(prevNode.identifier())) {
        const prevNodeCurrentState = phase.nodeIdToNodeMap[prevNode.identifier()];
        if (!prevNodeCurrentState) {
          this.addToHistory(i, prevNode, HistoryChange.Removed);
        } else if (!this.nodeEquals(prevNodeCurrentState, node) &&
          prevNodeCurrentState instanceof GraphNode &&
          prevNodeCurrentState.getInplaceUpdatePhase() == phase.name) {
          this.addToHistory(i, prevNodeCurrentState, HistoryChange.InplaceUpdated);
        } else if (node?.identifier() != prevNode.identifier()) {
          this.addToHistory(i, prevNodeCurrentState, HistoryChange.Survived);
        }
        this.addHistoryAncestors(prevNode.identifier(), phase, uniqueAncestors);
      }

      if (!node) {
        prevNode = null;
        continue;
      }

      if (node instanceof GraphNode && node.getInplaceUpdatePhase() == phase.name) {
        this.addToHistory(i, node, HistoryChange.InplaceUpdated);
      }

      if (first) {
        this.addToHistory(i, node, HistoryChange.Emerged);
        first = false;
      }

      this.addToHistory(i, node, HistoryChange.Current);
      prevNode = node;
    }
  }

  private addHistoryAncestors(key: string, phase: GPhase, uniqueAncestors: Set<string>):
    boolean {
    let changed = false;
    const phaseId = this.sourceResolver.getPhaseIdByName(phase.name);
    for (const ancestor of phase.originIdToNodesMap.get(key)) {
      const key = ancestor.identifier();
      if (!uniqueAncestors.has(key)) {
        this.addToHistory(phaseId, ancestor, HistoryChange.Lowered);
        uniqueAncestors.add(key);
        changed = true;
      }
    }
    return changed;
  }

  private getHistoryChain(phaseId: number, node: GNode): Map<number, GNode> {
    const leftChain = this.getLeftHistoryChain(phaseId, node);
    const rightChain = this.getRightHistoryChain(phaseId, node);
    return new Map([...leftChain, ...rightChain]);
  }

  private getLeftHistoryChain(phaseId: number, node: GNode): Map<number, GNode> {
    const leftChain = new Map<number, GNode>();

    for (let i = phaseId; i >= 0; i--) {
      const phase = this.sourceResolver.getGraphPhase(i);
      if (!phase) continue;
      const nodeKey = node instanceof GraphNode || i == phaseId || node.origin.phase == phase.name
        ? node.identifier()
        : node.origin.identifier();
      let currentNode = phase.nodeIdToNodeMap[nodeKey];
      if (!currentNode) {
        const nodeOrigin = node.getNodeOrigin();
        if (nodeOrigin) {
          currentNode = phase.nodeIdToNodeMap[nodeOrigin.identifier()];
        }
        if (!currentNode) return leftChain;
      }
      leftChain.set(i, currentNode);
      node = currentNode;
    }

    return leftChain;
  }

  private getRightHistoryChain(phaseId: number, node: GNode): Map<number, GNode> {
    const rightChain = new Map<number, GNode>();

    for (let i = phaseId + 1; i < this.sourceResolver.phases.length; i++) {
      const phase = this.sourceResolver.getGraphPhase(i);
      if (!phase) continue;
      let currentNode: GNode = null;
      if (phase.type == PhaseType.TurboshaftGraph) {
        const currentNodeState = phase.nodeIdToNodeMap[node.identifier()];
        if (node.equals(currentNodeState)) {
          currentNode = currentNodeState;
        } else {
          const nodes = phase.originIdToNodesMap.get(node.identifier());
          if (nodes?.length == 1 && nodes[0].getNodeOrigin().phase == phase.name) {
            currentNode = nodes[0];
          } else if (nodes?.length > 1) {
            return rightChain;
          }
        }
      } else {
        currentNode = phase.nodeIdToNodeMap[node.identifier()];
      }
      if (!currentNode) return rightChain;
      rightChain.set(i, currentNode);
      node = currentNode;
    }

    return rightChain;
  }

  private addToHistory(phaseId: number, node: GNode, change: HistoryChange): void {
    if (!this.phaseIdToHistory.has(phaseId)) {
      this.phaseIdToHistory.set(phaseId, new PhaseHistory(phaseId));
    }
    this.phaseIdToHistory.get(phaseId).addChange(node, change);
    if (change == HistoryChange.Current) {
      this.maxNodeWidth = Math.max(this.maxNodeWidth, node.labelBox.width * 1.07);
    } else {
      this.maxNodeWidth = Math.max(this.maxNodeWidth, node.labelBox.width);
    }
  }

  private nodeEquals(first: GNode, second: GNode): boolean {
    if (!first || !second) return false;
    if ((first instanceof GraphNode && second instanceof GraphNode)) {
      return first.equals(second);
    } else if (first instanceof TurboshaftGraphOperation && second instanceof TurboshaftGraphOperation) {
      return first.equals(second);
    }
    return first.getHistoryLabel() == second.getHistoryLabel();
  }

  private clear(): void {
    this.phaseIdToHistory.clear();
    this.maxNodeWidth = 0;
    this.maxPhaseNameWidth = 0;
    this.svg.selectAll("*").remove();
  }

  private getWidth(): number {
    const scrollWidth = C.HISTORY_SCROLLBAR_WIDTH / 2 + C.HISTORY_SCROLLBAR_WIDTH;
    const indentWidth = 2 * C.HISTORY_CONTENT_INDENT;

    const labelWidth = this.labelBox.width + 3 * this.labelBox.height;
    const phaseNameWidth = this.maxPhaseNameWidth + indentWidth + scrollWidth;
    const contentWidth = this.labelBox.height * 0.75 + indentWidth + scrollWidth
      + (this.maxNodeWidth * this.getCoefficient("history-item-tspan-font-size"));

    return Math.max(labelWidth, phaseNameWidth, contentWidth);
  }

  private getHeight(): number {
    return window.screen.availHeight * C.HISTORY_DEFAULT_HEIGHT_PERCENT;
  }

  private getHistoryChangeColor(historyChange: HistoryChange): string {
    switch (historyChange) {
      case HistoryChange.Current:
        return "rgb(255, 167, 0)";
      case HistoryChange.Emerged:
        return "rgb(160, 83, 236)";
      case HistoryChange.Lowered:
        return "rgb(0, 255, 0)";
      case HistoryChange.InplaceUpdated:
        return "rgb(57, 57, 208)";
      case HistoryChange.Removed:
        return "rgb(255, 0, 0)";
      case HistoryChange.Survived:
        return "rgb(7, 253, 232)";
    }
  }

  private traceToConsole(): void {
    const keys = Array.from(this.phaseIdToHistory.keys()).sort((a, b) => a - b);
    for (const key of keys) {
      console.log(`${key} ${this.sourceResolver.getPhaseNameById(key)}`);
      const phaseHistory = this.phaseIdToHistory.get(key);
      for (const record of phaseHistory.nodeIdToRecord.values()) {
        console.log(record.toString(), record.node);
      }
    }
  }
}

export class PhaseHistory {
  phaseId: number;
  nodeIdToRecord: Map<string, HistoryRecord>;

  constructor(phaseId: number) {
    this.phaseId = phaseId;
    this.nodeIdToRecord = new Map<string, HistoryRecord>();
  }

  public addChange(node: GNode, change: HistoryChange): void {
    const key = node.identifier();
    if (!this.nodeIdToRecord.has(key)) {
      this.nodeIdToRecord.set(key, new HistoryRecord(node));
    }
    this.nodeIdToRecord.get(key).addChange(change);
  }

  public hasChanges(): boolean {
    for (const record of this.nodeIdToRecord.values()) {
      if (record.hasChanges()) return true;
    }
    return false;
  }
}

export class HistoryRecord {
  node: GNode;
  changes: Set<HistoryChange>;

  constructor(node: GNode) {
    this.node = node;
    this.changes = new Set<HistoryChange>();
  }

  public addChange(change: HistoryChange): void {
    this.changes.add(change);
  }

  public hasChanges(): boolean {
    return this.changes.size > 1 ||
      (this.changes.size == 1 && !this.changes.has(HistoryChange.Current));
  }

  public toString(): string {
    return Array.from(this.changes.values()).sort().map(i => HistoryChange[i]).join(", ");
  }
}

export enum HistoryChange {
  Current,
  Emerged,
  Lowered,
  InplaceUpdated,
  Removed,
  Survived
}
