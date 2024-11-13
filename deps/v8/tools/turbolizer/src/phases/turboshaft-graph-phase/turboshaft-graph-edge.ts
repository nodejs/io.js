// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { TurboshaftGraphOperation } from "./turboshaft-graph-operation";
import { Edge } from "../../edge";
import { TurboshaftGraphBlock } from "./turboshaft-graph-block";

export class TurboshaftGraphEdge<Type extends TurboshaftGraphOperation | TurboshaftGraphBlock> extends
  Edge<Type> {

  constructor(target: Type, index: number, source: Type) {
    super(target, index, source);
    this.visible = target.visible && source.visible;
  }

  public isBackEdge(): boolean {
    if (this.target instanceof TurboshaftGraphBlock) {
      return this.target.hasBackEdges() && (this.target.rank < this.source.rank);
    }
    return this.target.rank < this.source.rank;
  }
}
