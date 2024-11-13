// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../common/constants";
import * as d3 from "d3";
import { storageGetItem, storageSetItem } from "../common/util";
import { PhaseView } from "./view";
import { SelectionBroker } from "../selection/selection-broker";
import { SelectionMap } from "../selection/selection-map";
import { ClearableHandler, NodeSelectionHandler } from "../selection/selection-handler";
import { GraphStateType } from "../phases/graph-phase/graph-phase";
import { Edge } from "../edge";
import { Node } from "../node";
import { TurboshaftGraph } from "../turboshaft-graph";
import { Graph } from "../graph";
import { TurboshaftGraphOperation } from "../phases/turboshaft-graph-phase/turboshaft-graph-operation";
import { GraphNode } from "../phases/graph-phase/graph-node";
import { SelectionStorage } from "../selection/selection-storage";

export abstract class MovableView<GraphType extends Graph | TurboshaftGraph> extends PhaseView {
  phaseName: string;
  graph: GraphType;
  broker: SelectionBroker;
  showPhaseByName: (name: string, selection: SelectionStorage) => void;
  toolbox: HTMLElement;
  state: MovableViewState;
  nodeSelectionHandler: NodeSelectionHandler & ClearableHandler;
  divElement: d3.Selection<any, any, any, any>;
  graphElement: d3.Selection<any, any, any, any>;
  svg: d3.Selection<any, any, any, any>;
  panZoom: d3.ZoomBehavior<SVGElement, any>;
  hoveredNodeIdentifier: string;

  public abstract updateGraphVisibility(): void;
  public abstract svgKeyDown(): void;

  constructor(idOrContainer: string | HTMLElement, broker: SelectionBroker,
              showPhaseByName: (name: string, selection: SelectionStorage) => void,
              toolbox: HTMLElement) {
    super(idOrContainer);
    this.broker = broker;
    this.showPhaseByName = showPhaseByName;
    this.toolbox = toolbox;
    this.state = new MovableViewState();
    this.divElement = d3.select(this.divNode);

    // Listen for key events. Note that the focus handler seems
    // to be important even if it does nothing.
    this.svg = this.divElement.append("svg")
      .attr("version", "2.0")
      .attr("width", "100%")
      .attr("height", "100%")
      .on("focus", () => { })
      .on("keydown", () => this.svgKeyDown());

    this.svg.append("svg:defs")
      .append("svg:marker")
      .attr("id", "end-arrow")
      .attr("viewBox", "0 -4 8 8")
      .attr("refX", 2)
      .attr("markerWidth", 2.5)
      .attr("markerHeight", 2.5)
      .attr("orient", "auto")
      .append("svg:path")
      .attr("d", "M0,-4L8,0L0,4");

    this.graphElement = this.svg.append("g");

    this.panZoom = d3.zoom<SVGElement, any>()
      .scaleExtent([0.2, 40])
      .on("zoom", () => {
        if (d3.event.shiftKey) return false;
        this.graphElement.attr("transform", d3.event.transform);
        return true;
      })
      .on("start", () => {
        if (d3.event.shiftKey) return;
        d3.select("body").style("cursor", "move");
      })
      .on("end", () => d3.select("body").style("cursor", "auto"));

    this.svg.call(this.panZoom).on("dblclick.zoom", null);
  }

  public createViewElement(): HTMLDivElement {
    const pane = document.createElement("div");
    pane.setAttribute("id", C.GRAPH_PANE_ID);
    return pane;
  }

  public onresize() {
    const trans = d3.zoomTransform(this.svg.node());
    const ctrans = this.panZoom.constrain()(trans, this.getSvgExtent(),
      this.panZoom.translateExtent());
    this.panZoom.transform(this.svg, ctrans);
  }

  public hide(): void {
    if (this.state.cacheLayout) {
      this.graph.graphPhase.transform = this.getTransformMatrix();
    } else {
      this.graph.graphPhase.transform = null;
    }
    this.broker.deleteNodeHandler(this.nodeSelectionHandler);
    super.hide();
    this.deleteContent();
  }

  protected getTransformMatrix(): { scale: number, x: number, y: number } {
    const matrix = this.graphElement.node().transform.baseVal.consolidate().matrix;
    return  { scale: matrix.a, x: matrix.e, y: matrix.f };
  }

  protected viewTransformMatrix(matrix: { scale: number, x: number, y: number }): void {
    this.svg.call(this.panZoom.transform, d3.zoomIdentity
      .translate(matrix.x, matrix.y)
      .scale(matrix.scale));
  }

  protected focusOnSvg(): void {
    const svg = document.getElementById(C.GRAPH_PANE_ID).childNodes[0] as HTMLElement;
    svg.focus();
  }

  protected updateGraphStateType(stateType: GraphStateType): void {
    this.graph.graphPhase.stateType = stateType;
  }

  protected viewGraphRegion(minX: number, minY: number,
                            maxX: number, maxY: number): void {
    const [width, height] = this.getSvgViewDimensions();
    const dx = maxX - minX;
    const dy = maxY - minY;
    const x = (minX + maxX) / 2;
    const y = (minY + maxY) / 2;
    const scale = Math.min(width / dx, height / dy) * 0.9;
    this.svg
      .transition().duration(120).call(this.panZoom.scaleTo, scale)
      .transition().duration(120).call(this.panZoom.translateTo, x, y);
  }

  protected addImgInput(id: string, title: string, onClick): void {
    const input = this.createImgInput(id, title, onClick);
    this.toolbox.appendChild(input);
  }

  protected addToggleImgInput(id: string, title: string, initState: boolean, onClick): void {
    const input = this.createImgToggleInput(id, title, initState, onClick);
    this.toolbox.appendChild(input);
  }

  protected minScale(): number {
    const [clientWith, clientHeight] = this.getSvgViewDimensions();
    const minXScale = clientWith / (2 * this.graph.width);
    const minYScale = clientHeight / (2 * this.graph.height);
    const minScale = Math.min(minXScale, minYScale);
    this.panZoom.scaleExtent([minScale, 40]);
    return minScale;
  }

  protected getNodeFrontier<NodeType extends Node<any>, EdgeType extends Edge<any>>(
    nodes: Iterable<NodeType>, inEdges: boolean,
    edgeFilter: (edge: EdgeType, idx: number) => boolean): Set<NodeType> {
    const frontier = new Set<NodeType>();
    let newState = true;
    const edgeFrontier = this.getEdgeFrontier<EdgeType>(nodes, inEdges, edgeFilter);
    // Control key toggles edges rather than just turning them on
    if (d3.event.ctrlKey) {
      for (const edge of edgeFrontier) {
        if (edge.visible) newState = false;
      }
    }
    for (const edge of edgeFrontier) {
      edge.visible = newState;
      if (newState) {
        const node = inEdges ? edge.source : edge.target;
        node.visible = true;
        frontier.add(node);
      }
    }
    this.updateGraphVisibility();
    return newState ? frontier : undefined;
  }

  protected showSelectionFrontierNodes<EdgeType extends Edge<any>>(
    inEdges: boolean, filter: (edge: EdgeType, idx: number) => boolean,
    select: boolean): void {
    const frontier = this.getNodeFrontier(this.state.selection, inEdges, filter);
    if (frontier !== undefined && frontier.size) {
      if (select) {
        if (!d3.event.shiftKey) this.state.selection.clear();
        this.state.selection.select([...frontier], true);
      }
      this.updateGraphVisibility();
    }
  }

  protected getEdgeFrontier<EdgeType extends Edge<any>> (nodes: Iterable<Node<any>>,
    inEdges: boolean, edgeFilter: (edge: EdgeType, idx: number) => boolean): Set<EdgeType> {
    const frontier = new Set<EdgeType>();
    for (const node of nodes) {
      let edgeNumber = 0;
      const edges = inEdges ? node.inputs : node.outputs;
      for (const edge of edges) {
        if (edgeFilter === undefined || edgeFilter(edge, edgeNumber)) {
          frontier.add(edge);
        }
        ++edgeNumber;
      }
    }
    return frontier;
  }

  protected connectVisibleSelectedElements(selection: SelectionMap): void {
    for (const element of selection) {
      element.inputs.forEach((edge: Edge<any>) => {
        if (edge.source.visible && edge.target.visible) {
          edge.visible = true;
        }
      });
      element.outputs.forEach((edge: Edge<any>) => {
        if (edge.source.visible && edge.target.visible) {
          edge.visible = true;
        }
      });
    }
  }

  protected showVisible(): void {
    this.updateGraphVisibility();
    this.viewWholeGraph();
    this.focusOnSvg();
  }

  protected viewWholeGraph(): void {
    this.panZoom.scaleTo(this.svg, 0);
    this.panZoom.translateTo(this.svg,
      this.graph.minGraphX + this.graph.width / 2,
      this.graph.minGraphY + this.graph.height / 2);
  }

  protected searchNodes(filterFunction: (node: TurboshaftGraphOperation | GraphNode) =>
                          boolean | RegExpExecArray, e: KeyboardEvent, onlyVisible: boolean):
    Array<TurboshaftGraphOperation | GraphNode> {
    return [...this.graph.nodes(node => {
      if ((e.ctrlKey || node.visible || !onlyVisible) && filterFunction(node)) {
        if (e.ctrlKey || !onlyVisible) node.visible = true;
        return true;
      }
      return false;
    })];
  }

  protected showHoveredNodeHistory(): void {
    const node = this.graph.nodeMap[this.hoveredNodeIdentifier];
    if (!node) return;
    this.broker.broadcastHistoryShow(null, node, this.phaseName);
  }

  protected createImgToggleInput(id: string, title: string, initState: boolean, onClick):
    HTMLElement {
    const input = this.createImgInput(id, title, onClick);
    input.classList.toggle("button-input-toggled", initState);
    return input;
  }

  private deleteContent(): void {
    for (const item of this.toolbox.querySelectorAll(".graph-toolbox-item")) {
      item.parentElement.removeChild(item);
    }

    if (!this.state.cacheLayout) {
      this.updateGraphStateType(GraphStateType.NeedToFullRebuild);
    }

    this.graph.graphPhase.rendered = false;
    this.updateGraphVisibility();
  }

  private getSvgViewDimensions(): [number, number] {
    return [this.container.clientWidth, this.container.clientHeight];
  }

  private getSvgExtent(): [[number, number], [number, number]] {
    return [[0, 0], [this.container.clientWidth, this.container.clientHeight]];
  }

  private createImgInput(id: string, title: string, onClick): HTMLElement {
    const input = document.createElement("input");
    input.setAttribute("id", id);
    input.setAttribute("type", "image");
    input.setAttribute("title", title);
    input.setAttribute("src", `img/toolbox/${id}-icon.png`);
    input.className = "button-input graph-toolbox-item";
    input.addEventListener("click", onClick);
    return input;
  }
}

export class MovableViewState {
  public selection: SelectionMap;
  public blocksSelection: SelectionMap;

  public get hideDead(): boolean {
    return storageGetItem("toggle-hide-dead", false);
  }

  public set hideDead(value: boolean) {
    storageSetItem("toggle-hide-dead", value);
  }

  public get showTypes(): boolean {
    return storageGetItem("toggle-types", false);
  }

  public set showTypes(value: boolean) {
    storageSetItem("toggle-types", value);
  }

  public get showCustomData(): boolean {
    return storageGetItem("toggle-custom-data", false);
  }

  public set showCustomData(value: boolean) {
    storageSetItem("toggle-custom-data", value);
  }

  public get cacheLayout(): boolean {
    return storageGetItem("toggle-cache-layout", true);
  }

  public set cacheLayout(value: boolean) {
    storageSetItem("toggle-cache-layout", value);
  }

  public get compactView(): boolean {
    return storageGetItem("compact-view", true);
  }

  public set compactView(value: boolean) {
    storageSetItem("compact-view", value);
  }
}
