// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../common/constants";
import * as d3 from "d3";
import {
  copyToClipboard,
  measureText,
  partial,
  storageGetItem,
  storageSetItem
} from "../common/util";
import { MovableView } from "./movable-view";
import { SelectionBroker } from "../selection/selection-broker";
import { SelectionMap } from "../selection/selection-map";
import { TurboshaftGraphOperation } from "../phases/turboshaft-graph-phase/turboshaft-graph-operation";
import { TurboshaftGraphEdge } from "../phases/turboshaft-graph-phase/turboshaft-graph-edge";
import { TurboshaftGraph } from "../turboshaft-graph";
import { TurboshaftGraphLayout } from "../turboshaft-graph-layout";
import { GraphStateType } from "../phases/graph-phase/graph-phase";
import { SelectionStorage } from "../selection/selection-storage";
import { DataTarget } from "../phases/turboshaft-custom-data-phase";
import { SourcePosition } from "../position";
import {
  TurboshaftCustomData,
  TurboshaftGraphPhase
} from "../phases/turboshaft-graph-phase/turboshaft-graph-phase";
import {
  BlockSelectionHandler,
  ClearableHandler,
  NodeSelectionHandler
} from "../selection/selection-handler";
import {
  TurboshaftGraphBlock,
  TurboshaftGraphBlockType
} from "../phases/turboshaft-graph-phase/turboshaft-graph-block";

export class TurboshaftGraphView extends MovableView<TurboshaftGraph> {
  graphLayout: TurboshaftGraphLayout;
  blockSelectionHandler: BlockSelectionHandler & ClearableHandler;
  visibleBlocks: d3.Selection<any, TurboshaftGraphBlock, any, any>;
  visibleNodes: d3.Selection<any, TurboshaftGraphOperation, any, any>;
  visibleEdges: d3.Selection<any, TurboshaftGraphEdge<TurboshaftGraphBlock>, any, any>;
  visibleBubbles: d3.Selection<any, any, any, any>;
  blockDrag: d3.DragBehavior<any, TurboshaftGraphBlock, TurboshaftGraphBlock>;

  constructor(idOrContainer: string | HTMLElement, broker: SelectionBroker,
              showPhaseByName: (name: string, selection: SelectionStorage) => void,
              toolbox: HTMLElement) {
    super(idOrContainer, broker, showPhaseByName, toolbox);

    this.state.selection = new SelectionMap(node => node.identifier());
    this.state.blocksSelection = new SelectionMap(block => {
      if (block instanceof TurboshaftGraphBlock) return block.identifier();
      return String(block);
    });

    this.nodeSelectionHandler = this.initializeNodeSelectionHandler();
    this.blockSelectionHandler = this.initializeBlockSelectionHandler();

    this.svg.on("click", () => {
      this.nodeSelectionHandler.clear();
      this.blockSelectionHandler.clear();
    });

    this.visibleEdges = this.graphElement.append("g");
    this.visibleBlocks = this.graphElement.append("g");

    this.blockDrag = d3.drag<any, TurboshaftGraphBlock, TurboshaftGraphBlock>()
      .on("drag",  (block: TurboshaftGraphBlock) => {
        block.x += d3.event.dx;
        block.y += d3.event.dy;
        this.updateBlockLocation(block);
      });
  }

  public initializeContent(data: TurboshaftGraphPhase, rememberedSelection: SelectionStorage):
    void {
    this.show();
    this.addImgInput("layout", "layout graph",
      partial(this.layoutAction, this));
    this.addImgInput("show-all", "uncollapse all blocks",
      partial(this.uncollapseAllBlocksAction, this));
    this.addImgInput("compress-layout", "compress layout",
      partial(this.compressLayoutAction, this));
    this.addImgInput("collapse-selected", "collapse selected blocks",
      partial(this.changeSelectedCollapsingAction, this, true));
    this.addImgInput("uncollapse-selected", "uncollapse selected blocks",
      partial(this.changeSelectedCollapsingAction, this, false));
    this.addImgInput("zoom-selection", "zoom selection",
      partial(this.zoomSelectionAction, this));
    this.addToggleImgInput("toggle-cache-layout", "toggle saving graph layout",
      this.state.cacheLayout, partial(this.toggleLayoutCachingAction, this));

    this.phaseName = data.name;
    this.addCustomDataSelect(data.customData);

    this.addToggleImgInput("toggle-compact-view",  "compact view",
      this.state.compactView, partial(this.toggleCompactViewAction, this));

    const adaptedSelection = this.createGraph(data, rememberedSelection);
    this.broker.addNodeHandler(this.nodeSelectionHandler);
    this.broker.addBlockHandler(this.blockSelectionHandler);

    const countOfSelectedItems = adaptedSelection.isAdapted()
      ? this.attachSelection(adaptedSelection)
      : 0;

    if (countOfSelectedItems > 0) {
      this.updateGraphVisibility();
      this.viewSelection();
    } else {
      if (this.state.cacheLayout && data.transform) {
        this.viewTransformMatrix(data.transform);
      } else {
        this.viewWholeGraph();
      }
    }

    const customDataShowed = this.graph.graphPhase.customDataShowed;
    if (customDataShowed != null && customDataShowed != this.nodesCustomDataShowed()) {
      this.compressLayoutAction(this);
    }
  }

  public updateGraphVisibility(): void {
    if (!this.graph) return;
    this.updateVisibleBlocksAndEdges();
    this.visibleNodes = this.visibleBlocks.selectAll(".turboshaft-node");
    this.visibleBubbles = d3.selectAll("circle");
    this.updateInputAndOutputBubbles();
    this.updateInlineNodes();
  }

  public svgKeyDown(): void {
    let eventHandled = true; // unless the below switch defaults
    switch (d3.event.keyCode) {
      case 38: // UP
      case 40: // DOWN
        this.showSelectionFrontierNodes(d3.event.keyCode == 38, undefined, true);
        break;
      case 65: // 'a'
        this.selectAllNodes();
        break;
      case 67: // 'c'
        if (!d3.event.ctrlKey && !d3.event.shiftKey) {
          this.copyToClipboardHoveredNodeInfo();
        } else {
          eventHandled = false;
        }
        break;
      case 72: // 'h'
        this.showHoveredNodeHistory();
        break;
      case 73: // 'i'
        this.selectNodesOfSelectedBlocks();
        break;
      case 80: // 'p'
        if (!d3.event.ctrlKey && !d3.event.shiftKey) {
          this.changeSelectedCollapsingAction(this, true);
        } else {
          eventHandled = false;
        }
        break;
      case 82: // 'r'
        if (!d3.event.ctrlKey && !d3.event.shiftKey) {
          this.layoutAction(this);
        } else {
          eventHandled = false;
        }
        break;
      case 83: // 's'
        if (!d3.event.ctrlKey && !d3.event.shiftKey) {
          this.changeSelectedCollapsingAction(this, false);
        } else {
          eventHandled = false;
        }
        break;
      case 85: // 'u'
        this.collapseUnusedBlocks(this.state.selection.selection.values());
        break;
      case 89: // 'y'
        const node = this.graph.nodeMap[this.hoveredNodeIdentifier];
        if (!node) return;
        this.collapseUnusedBlocks([node]);
        break;
      default:
        eventHandled = false;
        break;
    }
    if (eventHandled) d3.event.preventDefault();
  }

  public searchInputAction(searchInput: HTMLInputElement, e: KeyboardEvent, onlyVisible: boolean):
    void {
    if (e.keyCode == 13) {
      this.nodeSelectionHandler.clear();
      const query = searchInput.value;
      storageSetItem("lastSearch", query);
      if (query.length == 0) return;

      const reg = new RegExp(query);
      const filterFunction = (node: TurboshaftGraphOperation) => {
        if (!onlyVisible) node.block.collapsed = false;
        const customDataTitle = this.graph.customData.getTitle(node.id, DataTarget.Nodes);
        return (!onlyVisible || !node.block.collapsed) &&
          reg.exec(`${node.getTitle()}${customDataTitle}`);
      };

      const selection = this.searchNodes(filterFunction, e, onlyVisible);

      this.nodeSelectionHandler.select(selection, true, false);
      this.updateGraphVisibility();
      searchInput.blur();
      this.viewSelection();
      this.focusOnSvg();
    }
    e.stopPropagation();
  }

  public hide(): void {
    this.graph.graphPhase.customDataShowed = this.nodesCustomDataShowed();
    this.broker.deleteBlockHandler(this.blockSelectionHandler);
    super.hide();
  }

  public detachSelection(): SelectionStorage {
    return new SelectionStorage(this.state.selection.detachSelection(),
      this.state.blocksSelection.detachSelection());
  }

  public adaptSelection(rememberedSelection: SelectionStorage): SelectionStorage {
    if (!this.graph.nodeMap && !this.graph.blockMap ||
      !(rememberedSelection instanceof SelectionStorage)) {
      return new SelectionStorage();
    }

    for (const key of rememberedSelection.nodes.keys()) {
      if (this.graph.nodeMap[key]) {
        rememberedSelection.adaptNode(key);
      }
    }

    for (const key of rememberedSelection.blocks.keys()) {
      if (this.graph.blockMap[key]) {
        rememberedSelection.adaptBlock(key);
      }
    }

    return rememberedSelection;
  }

  private adaptiveUpdateGraphVisibility(): void {
    const graphElement = this.graphElement.node();
    const originalHeight = graphElement.getBBox().height;
    this.updateGraphVisibility();
    this.focusOnSvg();
    const newHeight = graphElement.getBBox().height;
    const transformMatrix = this.getTransformMatrix();
    transformMatrix.y *= (newHeight / originalHeight);
    this.viewTransformMatrix(transformMatrix);
  }

  private initializeNodeSelectionHandler(): NodeSelectionHandler & ClearableHandler {
    const view = this;
    return {
      select: function (selectedNodes: Array<TurboshaftGraphOperation>, selected: boolean,
                        scrollIntoView: boolean) {
        const sourcePositions = new Array<SourcePosition>();
        const nodes = new Set<string>();
        for (const node of selectedNodes) {
          if (!node) continue;
          if (node.sourcePosition) {
            sourcePositions.push(node.sourcePosition);
            nodes.add(node.identifier());
          }
        }
        view.state.selection.select(selectedNodes, selected);
        view.broker.broadcastSourcePositionSelect(this, sourcePositions, selected, nodes);
        view.updateGraphVisibility();
      },
      clear: function () {
        view.state.selection.clear();
        view.broker.broadcastClear(this);
        view.updateGraphVisibility();
      },
      brokeredNodeSelect: function (nodeIds: Set<string>, selected: boolean) {
        const selection = view.graph.nodes(node => nodeIds.has(node.identifier()));
        view.state.selection.select(selection, selected);
        view.updateGraphVisibility();
      },
      brokeredClear: function () {
        view.state.selection.clear();
        view.updateGraphVisibility();
      }
    };
  }

  private initializeBlockSelectionHandler(): BlockSelectionHandler & ClearableHandler {
    const view = this;
    return {
      select: function (selectedBlocks: Array<TurboshaftGraphBlock>, selected: boolean) {
        view.state.blocksSelection.select(selectedBlocks, selected);
        const selectedBlocksKeys = new Array<number>();
        for (const selectedBlock of selectedBlocks) {
          selectedBlocksKeys.push(Number(view.state.blocksSelection.stringKey(selectedBlock)));
        }
        view.broker.broadcastBlockSelect(this, selectedBlocksKeys, selected);
        view.updateGraphVisibility();
      },
      clear: function () {
        view.state.blocksSelection.clear();
        view.broker.broadcastClear(this);
        view.updateGraphVisibility();
      },
      brokeredBlockSelect: function (blockIds: Array<number>, selected: boolean) {
        view.state.blocksSelection.select(blockIds, selected);
        view.updateGraphVisibility();
      },
      brokeredClear: function () {
        view.state.blocksSelection.clear();
        view.updateGraphVisibility();
      },
    };
  }

  private createGraph(data: TurboshaftGraphPhase, rememberedSelection: SelectionStorage):
    SelectionStorage {
    this.graph = new TurboshaftGraph(data);
    this.graphLayout = new TurboshaftGraphLayout(this.graph);

    if (!this.state.cacheLayout ||
      this.graph.graphPhase.stateType == GraphStateType.NeedToFullRebuild) {
      this.updateGraphStateType(GraphStateType.NeedToFullRebuild);
    }

    this.showVisible();

    const adaptedSelection = this.adaptSelection(rememberedSelection);

    this.layoutGraph();
    this.updateGraphVisibility();
    return adaptedSelection;
  }

  private layoutGraph(): void {
    const layoutMessage = this.graph.graphPhase.stateType == GraphStateType.Cached
      ? "Layout turboshaft graph from cache"
      : "Layout turboshaft graph";

    console.time(layoutMessage);
    this.graphLayout.rebuild(this.nodesCustomDataShowed());
    const extent = this.graph.redetermineGraphBoundingBox(this.nodesCustomDataShowed());
    this.panZoom.translateExtent(extent);
    this.minScale();
    console.timeEnd(layoutMessage);
  }

  private addCustomDataSelect(customData: TurboshaftCustomData): void {
    const keys = Array.from(customData.nodes.keys());
    if (keys.length == 0) return;

    const select = document.createElement("select") as HTMLSelectElement;
    select.setAttribute("id", "custom-data-select");
    select.setAttribute("class", "graph-toolbox-item");
    select.setAttribute("title", "custom data");

    const checkBox = this.createImgToggleInput("toggle-custom-data",
      "toggle custom data visibility", this.state.showCustomData,
      partial(this.toggleCustomDataAction, this));

    for (const key of keys) {
      const option = document.createElement("option");
      option.text = key;
      select.add(option);
    }

    const storageKey = this.customDataStorageKey();
    const indexOfSelected = keys.indexOf(storageGetItem(storageKey, null, false));
    if (indexOfSelected != -1) {
      select.selectedIndex = indexOfSelected;
    } else {
      storageSetItem(storageKey, keys[0]);
    }

    const view = this;
    select.onchange = function (this: HTMLSelectElement) {
      const selectedCustomData = select.options[this.selectedIndex].text;
      storageSetItem(storageKey, selectedCustomData);
      view.updateGraphVisibility();
      view.updateInlineNodesCustomData();
    };

    this.toolbox.appendChild(select);
    this.toolbox.appendChild(checkBox);
  }

  private updateBlockLocation(block: TurboshaftGraphBlock): void {
    this.visibleBlocks
      .selectAll<SVGGElement, TurboshaftGraphBlock>(".turboshaft-block")
      .filter(b => b == block)
      .attr("transform", block => `translate(${block.x},${block.y})`);

    this.visibleEdges
      .selectAll<SVGPathElement, TurboshaftGraphEdge<TurboshaftGraphBlock>>("path")
      .filter(edge => edge.target === block || edge.source === block)
      .attr("d", edge => edge.generatePath(this.graph, this.nodesCustomDataShowed(), this.state.compactView));
  }

  private updateVisibleBlocksAndEdges(): void {
    const view = this;
    const iconsPath = "img/turboshaft/";

    // select existing edges
    const filteredEdges = [...view.graph.blocksEdges(_ => view.graph.isRendered())];

    const selEdges = view.visibleEdges
      .selectAll<SVGPathElement, TurboshaftGraphEdge<TurboshaftGraphBlock>>("path")
      .data(filteredEdges, edge => edge.toString());

    // remove old edges
    selEdges.exit().remove();

    // add new edges
    const newEdges = selEdges
      .enter()
      .append("path")
      .style("marker-end", "url(#end-arrow)")
      .attr("id", edge => `e,${edge.toString()}`)
      .on("click",  edge => {
        d3.event.stopPropagation();
        if (!d3.event.shiftKey) {
          view.blockSelectionHandler.clear();
        }
        view.blockSelectionHandler.select([edge.source, edge.target], true, false);
      })
      .attr("adjacentToHover", "false");

    const newAndOldEdges = newEdges.merge(selEdges);

    newAndOldEdges.classed("hidden", edge => !edge.isVisible());

    // select existing blocks
    const filteredBlocks = [...view.graph.blocks(_ => view.graph.isRendered())];
    const allBlocks = view.visibleBlocks
      .selectAll<SVGGElement, TurboshaftGraphBlock>(".turboshaft-block");
    const selBlocks = allBlocks.data(filteredBlocks, block => block.toString());

    // remove old blocks
    selBlocks.exit().remove();

    // add new blocks
    const newBlocks = selBlocks
      .enter()
      .append("g")
      .classed("turboshaft-block", true)
      .classed("block", b => b.type == TurboshaftGraphBlockType.Block)
      .classed("merge", b => b.type == TurboshaftGraphBlockType.Merge)
      .classed("loop", b => b.type == TurboshaftGraphBlockType.Loop)
      .on("mouseenter", (block: TurboshaftGraphBlock) => {
        const visibleEdges = view.visibleEdges
          .selectAll<SVGPathElement, TurboshaftGraphEdge<TurboshaftGraphBlock>>("path");
        const adjInputEdges = visibleEdges.filter(edge => edge.target === block);
        const adjOutputEdges = visibleEdges.filter(edge => edge.source === block);
        adjInputEdges.classed("input", true);
        adjOutputEdges.classed("output", true);
        view.updateGraphVisibility();
      })
      .on("mouseleave", (block: TurboshaftGraphBlock) => {
        const visibleEdges = view.visibleEdges
          .selectAll<SVGPathElement, TurboshaftGraphEdge<TurboshaftGraphBlock>>("path");
        const adjEdges = visibleEdges
          .filter(edge => edge.target === block || edge.source === block);
        adjEdges.classed("input output", false);
        view.updateGraphVisibility();
      })
      .on("click", (block: TurboshaftGraphBlock) => {
        if (!d3.event.shiftKey) view.blockSelectionHandler.clear();
        view.blockSelectionHandler.select([block], undefined, false);
        d3.event.stopPropagation();
      })
      .call(view.blockDrag);

    newBlocks
      .append("rect")
      .attr("rx", C.TURBOSHAFT_BLOCK_BORDER_RADIUS)
      .attr("ry", C.TURBOSHAFT_BLOCK_BORDER_RADIUS)
      .attr("width", block => block.getWidth())
      .attr("height", block => block.getHeight(view.nodesCustomDataShowed(), this.state?.compactView));

    newBlocks.each(function (block: TurboshaftGraphBlock) {
      const svg = d3.select<SVGGElement, TurboshaftGraphBlock>(this);
      svg
        .append("text")
        .classed("block-label", true)
        .attr("text-anchor", "middle")
        .attr("x", block.getWidth() / 2)
        .append("tspan")
        .text(block.displayLabel)
        .append("title")
        .text(view.graph.customData.getTitle(block.id, DataTarget.Blocks));

      svg
        .append("text")
        .classed("block-collapsed-label", true)
        .attr("text-anchor", "middle")
        .attr("x", block.getWidth() / 2)
        .attr("dy", block.labelBox.height)
        .attr("visibility", block.collapsed ? "visible" : "hidden")
        .append("tspan")
        .text(block.collapsedLabel);

      svg
        .append("image")
        .attr("xlink:href", `${iconsPath}collapse_${block.collapsed ? "down" : "up"}.svg`)
        .attr("height", block.labelBox.height)
        .attr("x", block.getWidth() - block.labelBox.height)
        .on("click", () => {
          d3.event.stopPropagation();
          block.collapsed = !block.collapsed;
          view.nodeSelectionHandler.select(block.nodes, false, false);
        });

      view.appendInputAndOutputBubbles(svg, block);
      view.appendInlineNodes(svg, block);
    });

    const newAndOldBlocks = newBlocks.merge(selBlocks);

    newAndOldBlocks
      .classed("selected", block => view.state.blocksSelection.isSelected(block))
      .attr("transform", block => `translate(${block.x},${block.y})`)
      .select("rect")
      .attr("height", block =>  block.getHeight(view.nodesCustomDataShowed(), this.state?.compactView));

    newAndOldBlocks.select("image")
      .attr("xlink:href", block => `${iconsPath}collapse_${block.collapsed ? "down" : "up"}.svg`);

    newAndOldBlocks.select(".block-collapsed-label")
      .attr("visibility", block => block.collapsed ? "visible" : "hidden");

    newAndOldEdges.attr("d", edge => edge.generatePath(view.graph, view.nodesCustomDataShowed(), view.state.compactView));
  }

  private appendInlineNodes(svg: d3.Selection<SVGGElement, TurboshaftGraphBlock, any, any>,
                            block: TurboshaftGraphBlock): void {
    const state = this.state;
    const graph = this.graph;
    const filteredNodes = [...block.nodes.filter(_ => graph.isRendered())];
    const allNodes = svg.selectAll<SVGGElement, TurboshaftGraphOperation>(".inline-node");
    const selNodes = allNodes.data(filteredNodes, node => node.toString());

    // remove old nodes
    selNodes.exit().remove();

    // add new nodes
    const newNodes = selNodes
      .enter()
      .append("g")
      .classed("turboshaft-node inline-node", true);

    let nodeY = block.labelBox.height;
    const blockWidth = block.getWidth();
    const view = this;
    const customData = this.graph.customData;
    const storageKey = this.customDataStorageKey();
    const selectedCustomData = storageGetItem(storageKey, null, false);
    newNodes.each(function (node: TurboshaftGraphOperation) {
      const nodeSvg = d3.select(this);
      nodeSvg
        .attr("id", node.id)
        .append("text")
        .attr("dx", C.TURBOSHAFT_NODE_X_INDENT)
        .classed("inline-node-label", true)
        .attr("dy", nodeY)
        .append("tspan")
        .html(node.buildOperationText(state.compactView))
        .append("title")
        .text(`${node.getTitle()}${customData.getTitle(node.id, DataTarget.Nodes)}`);

      nodeSvg
        .on("mouseenter", (node: TurboshaftGraphOperation) => {
          view.visibleNodes.data<TurboshaftGraphOperation>(
            node.inputs.map(edge => edge.source), source => source.toString())
            .classed("input", true);
          view.visibleNodes.data<TurboshaftGraphOperation>(
            node.outputs.map(edge => edge.target), target => target.toString())
            .classed("output", true);
          view.hoveredNodeIdentifier = node.identifier();
          view.updateGraphVisibility();
        })
        .on("mouseleave", (node: TurboshaftGraphOperation) => {
          const inOutNodes = node.inputs.map(edge => edge.source)
            .concat(node.outputs.map(edge => edge.target));
          view.visibleNodes.data<TurboshaftGraphOperation>(inOutNodes, inOut => inOut.toString())
            .classed("input output", false);
          view.hoveredNodeIdentifier = null;
          view.updateGraphVisibility();
        })
        .on("click", (node: TurboshaftGraphOperation) => {
          if (!d3.event.shiftKey) view.nodeSelectionHandler.clear();
          view.nodeSelectionHandler.select([node], undefined, false);
          d3.event.stopPropagation();
        });
      nodeY += node.getHeight(false, state.compactView);
      if (view.graph.customData.nodes.size > 0) {
        const customData = view.graph.getCustomData(selectedCustomData, node.id, DataTarget.Nodes);
        nodeSvg
          .append("text")
          .attr("dx", C.TURBOSHAFT_NODE_X_INDENT)
          .classed("inline-node-custom-data", true)
          .attr("dy", nodeY)
          .append("tspan")
          .text(view.getReadableString(customData, blockWidth))
          .append("title")
          .text(customData);
          nodeY += node.labelBox.height;
      }
    });

    newNodes.merge(selNodes)
      .classed("selected", node => state.selection.isSelected(node));
  }

  private updateInlineNodes(): void {
    const view = this;
    const state = this.state;
    const showCustomData = this.nodesCustomDataShowed();
    const customData = this.graph.customData;
 
    let totalHeight = 0;
    let blockId = 0;
    view.visibleNodes.each(function (node: TurboshaftGraphOperation) {
      const nodeSvg = d3.select(this);
      if (blockId != node.block.id) {
        blockId = node.block.id;
        totalHeight = 0;
      }
      totalHeight += node.getHeight(showCustomData, state?.compactView);
      const nodeY = showCustomData ? totalHeight - node.labelBox.height : totalHeight;

      nodeSvg
        .select(".inline-node-label")
        .classed("selected", node => state.selection.isSelected(node))
        .attr("dy", nodeY)
        .attr("visibility", !node.block.collapsed ? "visible" : "hidden")
        .select("tspan")
        .html(node.buildOperationText(state.compactView))
        .append("title")
        .text(`${node.getTitle()}${customData.getTitle(node.id, DataTarget.Nodes)}`);

      // node.getHeight() == 0 iff node is fully inlined in compact view, so we
      // don't want this to be visible.
      const isVisible = !node.block.collapsed && showCustomData &&
        node.getHeight(showCustomData, state.compactView) > 0;
      nodeSvg
        .select(".inline-node-custom-data")
        .attr("dy", nodeY + node.labelBox.height)
        .attr("visibility", isVisible ? "visible" : "hidden");
    });
  }

  private updateInlineNodesCustomData(): void {
    const view = this;
    const storageKey = this.customDataStorageKey();
    const selectedCustomData = storageGetItem(storageKey, null, false);
    if (!this.nodesCustomDataShowed()) return;
    view.visibleNodes.each(function (node: TurboshaftGraphOperation) {
      const customData = view.graph.getCustomData(selectedCustomData, node.id, DataTarget.Nodes);
      d3.select(this)
        .select(".inline-node-custom-data")
        .select("tspan")
        .text(view.getReadableString(customData, node.block.width))
        .append("title")
        .text(customData);
    });
  }

  private appendInputAndOutputBubbles(
    svg: d3.Selection<SVGGElement, TurboshaftGraphBlock, any, any>,
    block: TurboshaftGraphBlock): void {
    for (let i = 0; i < block.inputs.length; i++) {
      const x = block.getInputX(i);
      const y = -C.DEFAULT_NODE_BUBBLE_RADIUS;
      svg.append("circle")
        .classed("filledBubbleStyle", true)
        .attr("id", `ib,${block.inputs[i].toString()}`)
        .attr("r", C.DEFAULT_NODE_BUBBLE_RADIUS)
        .attr("transform", `translate(${x},${y})`);
    }
    if (block.outputs.length > 0) {
      const x = block.getOutputX();
      const y = block.getHeight(this.nodesCustomDataShowed(), this.state?.compactView) + C.DEFAULT_NODE_BUBBLE_RADIUS;
      svg.append("circle")
        .classed("filledBubbleStyle", true)
        .attr("id", `ob,${block.id}`)
        .attr("r", C.DEFAULT_NODE_BUBBLE_RADIUS)
        .attr("transform", `translate(${x},${y})`);
    }
  }

  private updateInputAndOutputBubbles(): void {
    const view = this;
    view.visibleBubbles.each(function () {
      const components = this.id.split(",");
      if (components[0] === "ob") {
        const from = view.graph.blockMap[components[1]];
        const x = from.getOutputX();
        const y = from.getHeight(view.nodesCustomDataShowed(), view.state?.compactView) + C.DEFAULT_NODE_BUBBLE_RADIUS;
        this.setAttribute("transform", `translate(${x},${y})`);
      }
    });
  }

  private viewSelection(): void {
    let minX;
    let maxX;
    let minY;
    let maxY;
    let hasSelection = false;
    this.visibleBlocks.selectAll<SVGGElement, TurboshaftGraphBlock>(".turboshaft-block")
      .each((block: TurboshaftGraphBlock) => {
      let blockHasSelection = false;
      for (const node of block.nodes) {
        if (this.state.selection.isSelected(node) || this.state.blocksSelection.isSelected(block)) {
          blockHasSelection = true;
          minX = minX ? Math.min(minX, block.x) : block.x;
          maxX = maxX ? Math.max(maxX, block.x + block.getWidth()) : block.x + block.getWidth();
          minY = minY ? Math.min(minY, block.y) : block.y;
          maxY = maxY
            ? Math.max(maxY, block.y + block.getHeight(this.nodesCustomDataShowed(), this.state?.compactView))
            : block.y + block.getHeight(this.nodesCustomDataShowed(), this.state?.compactView);
        }
        if (blockHasSelection) {
          hasSelection = true;
          break;
        }
      }
    });
    if (hasSelection) {
      this.viewGraphRegion(minX - C.NODE_INPUT_WIDTH, minY - 60,
        maxX + C.NODE_INPUT_WIDTH, maxY + 60);
    }
  }

  private attachSelection(selection: SelectionStorage): number {
    if (!(selection instanceof SelectionStorage)) return 0;
    this.nodeSelectionHandler.clear();
    this.blockSelectionHandler.clear();
    const selectedNodes = [
      ...this.graph.nodes(node =>
        selection.adaptedNodes.has(this.state.selection.stringKey(node)))
    ];
    this.nodeSelectionHandler.select(selectedNodes, true, false);
    const selectedBlocks = [
      ...this.graph.blocks(block =>
        selection.adaptedBocks.has(this.state.blocksSelection.stringKey(block)))
    ];
    this.blockSelectionHandler.select(selectedBlocks, true, false);
    return selectedNodes.length + selectedBlocks.length;
  }

  private nodesCustomDataShowed(): boolean {
    const storageKey = this.customDataStorageKey();
    const selectedCustomData = storageGetItem(storageKey, null, false);
    if (selectedCustomData == null) return false;

    return this.graph.hasCustomData(selectedCustomData, DataTarget.Nodes) &&
      this.state.showCustomData;
  }

  private customDataStorageKey(): string {
    return `${this.phaseName}-selected-custom-data`;
  }

  private getReadableString(str: string, maxWidth: number): string {
    if (!str) return "";
    const strBox = measureText(str);
    if (maxWidth > strBox.width) return str;
    const widthOfOneSymbol = Math.floor(strBox.width / str.length);
    const lengthOfReadableProperties = Math.floor(maxWidth / widthOfOneSymbol);
    return `${str.slice(0, lengthOfReadableProperties - 3)}..`;
  }

  // Actions (handlers of toolbox menu and hotkeys events)
  private layoutAction(view: TurboshaftGraphView): void {
    view.updateGraphStateType(GraphStateType.NeedToFullRebuild);
    view.layoutGraph();
    view.updateGraphVisibility();
    view.viewWholeGraph();
    view.focusOnSvg();
  }

  private uncollapseAllBlocksAction(view: TurboshaftGraphView): void {
    for (const block of view.graph.blocks()) {
      block.collapsed = false;
    }
    view.updateGraphVisibility();
    view.focusOnSvg();
  }

  private compressLayoutAction(view: TurboshaftGraphView): void {
    for (const block of view.graph.blocks()) {
      block.compressHeight();
    }

    const ranksMaxBlockHeight = view.graph.getRanksMaxBlockHeight(view.nodesCustomDataShowed());

    for (const block of view.graph.blocks()) {
      block.y = ranksMaxBlockHeight.slice(1, block.rank).reduce<number>((accumulator, current) => {
        return accumulator + current;
      }, block.getRankIndent());
    }

    view.adaptiveUpdateGraphVisibility();
  }

  private changeSelectedCollapsingAction(view: TurboshaftGraphView, collapsed: boolean): void {
    for (const key of view.state.blocksSelection.selectedKeys()) {
      const block = view.graph.blockMap[key];
      if (!block) continue;
      block.collapsed = collapsed;
    }
    view.updateGraphVisibility();
    view.focusOnSvg();
  }

  private zoomSelectionAction(view: TurboshaftGraphView): void {
    view.viewSelection();
    view.focusOnSvg();
  }

  private toggleCompactViewAction(view: TurboshaftGraphView): void {
    view.state.compactView = !view.state.compactView;

    const element = document.getElementById("toggle-compact-view");
    element.classList.toggle("button-input-toggled", view.state.compactView);
    const extent = view.graph.redetermineGraphBoundingBox(view.state.showCustomData);
    view.panZoom.translateExtent(extent);
    view.adaptiveUpdateGraphVisibility();
    view.updateInlineNodesCustomData();
  }

  private toggleCustomDataAction(view: TurboshaftGraphView): void {
    view.state.showCustomData = !view.state.showCustomData;
    const ranksMaxBlockHeight = new Array<number>();

    for (const block of view.graph.blocks()) {
      ranksMaxBlockHeight[block.rank] = Math.max(ranksMaxBlockHeight[block.rank] ?? 0,
        block.collapsed
          ? block.height
          : block.getHeight(view.nodesCustomDataShowed(), this.state?.compactView));
    }

    for (const block of view.graph.blocks()) {
      block.y = ranksMaxBlockHeight.slice(1, block.rank).reduce<number>((accumulator, current) => {
        return accumulator + current;
      }, block.getRankIndent());
    }

    const element = document.getElementById("toggle-custom-data");
    element.classList.toggle("button-input-toggled", view.state.showCustomData);
    const extent = view.graph.redetermineGraphBoundingBox(view.state.showCustomData);
    view.panZoom.translateExtent(extent);
    view.adaptiveUpdateGraphVisibility();
    view.updateInlineNodesCustomData();
  }

  private toggleLayoutCachingAction(view: TurboshaftGraphView): void {
    view.state.cacheLayout = !view.state.cacheLayout;
    const element = document.getElementById("toggle-cache-layout");
    element.classList.toggle("button-input-toggled", view.state.cacheLayout);
  }

  // Hotkeys handlers
  private selectAllNodes(): void {
    this.nodeSelectionHandler.select(this.graph.nodeMap, true, false);
    this.updateGraphVisibility();
  }

  private collapseUnusedBlocks(usedNodes: Iterable<TurboshaftGraphOperation>): void {
    const usedBlocks = new Set<TurboshaftGraphBlock>();
    for (const node of usedNodes) {
      usedBlocks.add(node.block);

      for (const input of node.inputs) {
        usedBlocks.add(input.source.block);
      }

      for (const output of node.outputs) {
        usedBlocks.add(output.target.block);
      }
    }

    if (usedBlocks.size == 0) return;

    for (const block of this.graph.blockMap) {
      block.collapsed = !usedBlocks.has(block);
    }

    this.updateGraphVisibility();
  }

  private copyToClipboardHoveredNodeInfo(): void {
    const node = this.graph.nodeMap[this.hoveredNodeIdentifier];
    if (!node) return;
    const customData = this.graph.customData;
    copyToClipboard(`${node.getTitle()}${customData.getTitle(node.id, DataTarget.Nodes)}`);
  }

  private selectNodesOfSelectedBlocks(): void {
    let selectedNodes = new Array<TurboshaftGraphOperation>();
    for (const key of this.state.blocksSelection.selectedKeys()) {
      const block = this.graph.blockMap[key];
      if (!block) continue;
      block.collapsed = false;
      selectedNodes = selectedNodes.concat(block.nodes);
    }
    this.nodeSelectionHandler.select(selectedNodes, true, false);
    this.updateGraphVisibility();
  }
}
