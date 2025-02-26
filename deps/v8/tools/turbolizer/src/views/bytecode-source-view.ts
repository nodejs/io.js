// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { createElement } from "../common/util";
import { CodeMode, View } from "./view";
import { SelectionBroker } from "../selection/selection-broker";
import { BytecodeSource } from "../source";
import { SourceResolver } from "../source-resolver";
import { SelectionMap } from "../selection/selection-map";
import { ViewElements } from "../common/view-elements";
import { BytecodeOffsetSelectionHandler, ClearableHandler } from "../selection/selection-handler";
import { BytecodePosition } from "../position";

export class BytecodeSourceView extends View {
  broker: SelectionBroker;
  source: BytecodeSource;
  sourceResolver: SourceResolver;
  codeMode: CodeMode;
  bytecodeOffsetToHtmlElement: Map<number, HTMLElement>;
  bytecodeOffsetSelection: SelectionMap;
  bytecodeOffsetSelectionHandler: BytecodeOffsetSelectionHandler & ClearableHandler;

  constructor(parent: HTMLElement, broker: SelectionBroker,  sourceFunction: BytecodeSource,
              sourceResolver: SourceResolver, codeMode: CodeMode) {
    super(parent);
    this.broker = broker;
    this.source = sourceFunction;
    this.sourceResolver = sourceResolver;
    this.codeMode = codeMode;
    this.bytecodeOffsetToHtmlElement = new Map<number, HTMLElement>();
    this.bytecodeOffsetSelection = new SelectionMap((offset: number) => String(offset));
    this.bytecodeOffsetSelectionHandler = this.initializeBytecodeOffsetSelectionHandler();
    this.broker.addBytecodeOffsetHandler(this.bytecodeOffsetSelectionHandler);

    this.initializeCode();
  }

  protected createViewElement(): HTMLElement {
    return createElement("div", "bytecode-source-container");
  }

  private initializeCode(): void {
    const view = this;
    const source = this.source;
    const bytecodeContainer = this.divNode;
    bytecodeContainer.classList.add(view.getSourceClass());

    const bytecodeHeader = createElement("div", "code-header");
    bytecodeHeader.setAttribute("id", view.getBytecodeHeaderHtmlElementName());

    const codeFileFunction = createElement("div", "code-file-function", source.functionName);
    bytecodeHeader.appendChild(codeFileFunction);

    const codeMode = createElement("div", "code-mode", view.codeMode);
    bytecodeHeader.appendChild(codeMode);

    const clearElement = document.createElement("div");
    clearElement.style.clear = "both";
    bytecodeHeader.appendChild(clearElement);
    bytecodeContainer.appendChild(bytecodeHeader);

    const codePre = createElement("pre", "prettyprint linenums");
    codePre.setAttribute("id", view.getBytecodeHtmlElementName());
    bytecodeContainer.appendChild(codePre);

    bytecodeHeader.onclick = () => {
      codePre.style.display = codePre.style.display === "none" ? "block" : "none";
    };

    const sourceList = createElement("ol", "linenums");
    for (const bytecodeSource of view.source.data) {
      const currentLine = createElement("li", `L${bytecodeSource.offset}`);
      currentLine.setAttribute("id", `li${bytecodeSource.offset}`);
      view.insertLineContent(currentLine, bytecodeSource.disassembly);
      view.insertLineNumber(currentLine, bytecodeSource.offset);
      view.bytecodeOffsetToHtmlElement.set(bytecodeSource.offset, currentLine);
      sourceList.appendChild(currentLine);
    }
    codePre.appendChild(sourceList);

    if (view.source.constantPool) {
      const constantList = createElement("ol", "linenums constants");
      const constantListHeader = createElement("li", "");
      view.insertLineContent(constantListHeader,
        `Constant pool (size = ${view.source.constantPool.length})`);
      constantList.appendChild(constantListHeader);

      for (const [idx, constant] of view.source.constantPool.entries()) {
        const currentLine = createElement("li", `C${idx}`);
        view.insertLineContent(currentLine, `${idx}: ${constant}`);
        constantList.appendChild(currentLine);
      }
      codePre.appendChild(constantList);
    }

    if(view.source.feedbackVector) {
      // TODO(nicohartmann): We could use a different element here, but it's
      // the simplest way to display that with a consistent style by just
      // reusing the existing structure and css classes.
      const feedbackList = createElement("ol", "linenums feedback");
      const feedbackListHeader = createElement("li", "");
      view.insertLineContent(feedbackListHeader, "Feedback Vector");
      feedbackList.appendChild(feedbackListHeader);
      const currentLine = createElement("li", "C0");
      view.insertLineContent(currentLine, view.source.feedbackVector);
      feedbackList.appendChild(currentLine);
      codePre.appendChild(feedbackList);
    }
  }

  private initializeBytecodeOffsetSelectionHandler(): BytecodeOffsetSelectionHandler
    & ClearableHandler {
    const view = this;
    const broker = this.broker;
    return {
      select: function (offsets: Array<number>, selected: boolean) {
        const bytecodePositions = new Array<BytecodePosition>();
        for (const offset of offsets) {
          view.source.inliningIds.forEach(inliningId =>
                                  bytecodePositions.push(new BytecodePosition(offset, inliningId)));
        }
        view.bytecodeOffsetSelection.select(offsets, selected);
        view.updateSelection();
        broker.broadcastBytecodePositionsSelect(this, bytecodePositions, selected);
      },
      clear: function () {
        view.bytecodeOffsetSelection.clear();
        view.updateSelection();
        broker.broadcastClear(this);
      },
      brokeredBytecodeOffsetSelect: function (positions: Array<BytecodePosition>,
                                              selected: boolean) {
        const offsets = new Array<number>();
        const firstSelect = view.bytecodeOffsetSelection.isEmpty();
        for (const position of positions) {
          if (view.source.inliningIds.includes(position.inliningId)) {
            offsets.push(position.bytecodePosition);
          }
        }
        view.bytecodeOffsetSelection.select(offsets, selected);
        view.updateSelection(firstSelect);
      },
      brokeredClear: function () {
        view.bytecodeOffsetSelection.clear();
        view.updateSelection();
      },
    };
  }

  private getBytecodeHeaderHtmlElementName(): string {
    return `source-pre-${this.source.sourceId}-header`;
  }

  private getBytecodeHtmlElementName(): string {
    return `source-pre-${this.source.sourceId}`;
  }

  private getSourceClass(): string {
    return this.codeMode == CodeMode.MainSource ? "main-source" : "inlined-source";
  }

  private updateSelection(scrollIntoView: boolean = false): void {
    const mkVisible = new ViewElements(this.divNode.parentNode as HTMLElement);
    for (const [offset, element] of this.bytecodeOffsetToHtmlElement.entries()) {
      const key = this.bytecodeOffsetSelection.stringKey(offset);
      const isSelected = this.bytecodeOffsetSelection.isKeySelected(key);
      mkVisible.consider(element, isSelected);
      element.classList.toggle("selected", isSelected);
    }
    mkVisible.apply(scrollIntoView);
  }

  private onSelectBytecodeOffset(offset: number, doClear: boolean) {
    if (doClear) {
      this.bytecodeOffsetSelectionHandler.clear();
    }
    this.bytecodeOffsetSelectionHandler.select([offset], undefined);
  }

  private insertLineContent(lineElement: HTMLElement, content: string): void {
    const lineContentElement = createElement("span", "", content);
    lineElement.appendChild(lineContentElement);
  }

  private insertLineNumber(lineElement: HTMLElement, lineNumber: number): void {
    const view = this;
    const lineNumberElement = createElement("div", "line-number", String(lineNumber));
    lineNumberElement.onclick = function (e: MouseEvent) {
      e.stopPropagation();
      view.onSelectBytecodeOffset(lineNumber, !e.shiftKey);
    };
    lineElement.insertBefore(lineNumberElement, lineElement.firstChild);
  }
}
