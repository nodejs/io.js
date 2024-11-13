// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../../common/constants";
import { measureText } from "../../common/util";
import { TurboshaftGraphEdge } from "./turboshaft-graph-edge";
import { TurboshaftGraphBlock } from "./turboshaft-graph-block";
import { Node } from "../../node";
import { BytecodePosition, SourcePosition } from "../../position";
import { NodeOrigin } from "../../origin";
import { TurboshaftCustomDataPhase } from "../turboshaft-custom-data-phase";

const SUBSCRIPT_DY: string = "50px";

enum Opcode {
  Constant = "Constant",
  WordBinop = "WordBinop",
  Comparison = "Comparison",
  Shift = "Shift",
  Load = "Load",
  Store = "Store",
}

enum RegisterRepresentation {
  Word32 = "Word32",
  Word64 = "Word64",
  Float32 = "Float32",
  Float64 = "Float64",
  Tagged = "Tagged",
  Compressed = "Compressed",
  Simd128 = "Simd128",
  Simd256 = "Simd256",
}

function rrString(rep: RegisterRepresentation): string {
  switch(rep) {
  case RegisterRepresentation.Word32: return "w32";
  case RegisterRepresentation.Word64: return "w64";
  case RegisterRepresentation.Float32: return "f32";
  case RegisterRepresentation.Float64: return "f64";
  case RegisterRepresentation.Tagged: return "t";
  case RegisterRepresentation.Compressed: return "c";
  case RegisterRepresentation.Simd128: return "s128";
  case RegisterRepresentation.Simd256: return "s256";
  }
}

enum WordRepresentation {
  Word32 = "Word32",
  Word64 = "Word64",
}

function wrString(rep: WordRepresentation): string {
  switch(rep) {
    case WordRepresentation.Word32: return "w32";
    case WordRepresentation.Word64: return "w64";
  }
}

enum MemoryRepresentation {
  Int8 = "Int8",
  Uint8 = "Uint8",
  Int16 = "Int16",
  Uint16 = "Uint16",
  Int32 = "Int32",
  Uint32 = "Uint32",
  Int64 = "Int64",
  Uint64 = "Uint64",
  Float16 = "Float16",
  Float32 = "Float32",
  Float64 = "Float64",
  AnyTagged = "AnyTagged",
  TaggedPointer = "TaggedPointer",
  TaggedSigned = "TaggedSigned",
  UncompressedTaggedPointer = "UncompressedTaggedPointer",
  ProtectedPointer = "ProtectedPointer",
  IndirectPointer = "IndirectPointer",
  SandboxedPointer = "SandboxedPointer",
  Simd128 = "Simd128",
  Simd256 = "Simd256",
}

function mrString(rep: MemoryRepresentation): string {
  switch(rep) {
  case MemoryRepresentation.Int8: return "i8";
  case MemoryRepresentation.Uint8: return "u8";
  case MemoryRepresentation.Int16: return "i16";
  case MemoryRepresentation.Uint16: return "u16";
  case MemoryRepresentation.Int32: return "i32";
  case MemoryRepresentation.Uint32: return "u32";
  case MemoryRepresentation.Int64: return "i64";
  case MemoryRepresentation.Uint64: return "u64";
  case MemoryRepresentation.Float16: return "f16";
  case MemoryRepresentation.Float32: return "f32";
  case MemoryRepresentation.Float64: return "f64";
  case MemoryRepresentation.AnyTagged: return "t";
  case MemoryRepresentation.TaggedPointer: return "tp";
  case MemoryRepresentation.TaggedSigned: return "ts";
  case MemoryRepresentation.UncompressedTaggedPointer: return "utp";
  case MemoryRepresentation.ProtectedPointer: return "pp";
  case MemoryRepresentation.IndirectPointer: return "ip";
  case MemoryRepresentation.SandboxedPointer: return "sp";
  case MemoryRepresentation.Simd128: return "s128";
  case MemoryRepresentation.Simd256: return "s256";
  }
}

function toEnum<T>(type: T, option: string): T[keyof T] {
  for(const key of Object.keys(type)) {
    if(option === type[key]) {
      const r: T[keyof T] = (type[key]) as T[keyof T];
      return r;
    }
  }
  throw new CompactOperationError(`Option "${option}" not recognized as ${type}`);
}

function chooseOption(option: string, otherwise: number | undefined,
                      ...candidates: string[]): number {
  for(var i = 0; i < candidates.length; ++i) {
    if(option === candidates[i]) return i;
  }
  if(otherwise !== undefined) return otherwise;
  throw new CompactOperationError(
    `Option "${option}" is unexpected. Expecing any of: ${candidates}`);
}
 
class CompactOperationError {
  message: string;

  constructor(message: string) {
    this.message = message;
  }
}

type InputPrinter = (n: number) => string;

abstract class CompactOperationPrinter {
  operation: TurboshaftGraphOperation;
  constructor(operation: TurboshaftGraphOperation) {
    this.operation = operation;
  }

  public IsFullyInlined(): boolean { return false; }
  public abstract Print(id: number, input: InputPrinter): string;
  public PrintInLine(): string { return ""; }

  protected GetInputCount(): number {
    return this.operation.inputs.length;
  }

  protected parseOptions(properties: string, expectedCount: number = -1): Array<string> {
    if(!properties.startsWith("[") || !properties.endsWith("]")) {
      throw new CompactOperationError(`Unexpected options format: "${properties}"`);
    }

    const options = properties.substring(1, properties.length - 1)
                              .split(",")
                              .map(o => o.trim());
    if(expectedCount > -1) {
      if(options.length != expectedCount) {
        throw new CompactOperationError(
          `Unexpected option count: ${options} (expected ${expectedCount})`);
      }
    }
    return options;
  }

  protected sub(text: string): string {
    return `<tspan class="subscript" dy="${SUBSCRIPT_DY}">${text}</tspan><tspan dy="-${SUBSCRIPT_DY}"> </tspan>`
  }
}

enum Constant_Kind {
  Word32 = "word32",
  Word64 = "word64",
}

class CompactOperationPrinter_Constant extends CompactOperationPrinter {
  kind: Constant_Kind;
  value: string;

  constructor(operation: TurboshaftGraphOperation, properties: string) {
    super(operation);

    const options = this.parseOptions(properties, 1);
    // Format of {options[0]} is "kind: value".
    let [key, value] = options[0].split(":").map(x => x.trim());
    this.kind = toEnum(Constant_Kind, key);
    this.value = value;
  }

  public IsFullyInlined(): boolean {
    return true;
  }
  public override Print(n: number, input: InputPrinter): string { return ""; }
  public PrintInLine(): string {
    switch(this.kind) {
      case Constant_Kind.Word32: return `${this.value}${this.sub("w32")}`;
      case Constant_Kind.Word64: return `${this.value}${this.sub("w64")}`;
    }
  }
}

enum Shift_Kind {
    ShiftRightArithmeticShiftOutZeros = "ShiftRightArithmeticShiftOutZeros",
    ShiftRightArithmetic = "ShiftRightArithmetic",
    ShiftRightLogical = "ShiftRightLogical",
    ShiftLeft = "ShiftLeft",
    RotateRight = "RotateRight",
    RotateLeft = "RotateLeft",
}

class CompactOperationPrinter_Shift extends CompactOperationPrinter {
  kind: Shift_Kind;
  rep: WordRepresentation;

  constructor(operation: TurboshaftGraphOperation, properties: string) {
    super(operation);

    const options = this.parseOptions(properties, 2);
    this.kind = toEnum(Shift_Kind, options[0]);
    this.rep = toEnum(WordRepresentation, options[1]);
  }

  public override Print(id: number, input: InputPrinter): string {
    let symbol: string;
    let subscript = "";
    switch(this.kind) {
      case Shift_Kind.ShiftRightArithmeticShiftOutZeros:
        symbol = ">>";
        subscript = "a0";
        break;
      case Shift_Kind.ShiftRightArithmetic:
        symbol = ">>";
        subscript = "a";
        break;
      case Shift_Kind.ShiftRightLogical:
        symbol = ">>";
        subscript = "l";
        break;
      case Shift_Kind.ShiftLeft:
        symbol = "<<";
        subscript = "l";
        break;
      case Shift_Kind.RotateRight:
        symbol = ">>";
        subscript = "ror";
        break;
      case Shift_Kind.RotateLeft:
        symbol = "<<";
        subscript = "rol";
        break;
    }
    if(subscript.length > 0) subscript += ",";
    switch(this.rep) {
      case WordRepresentation.Word32:
        subscript += "w32";
        break;
      case WordRepresentation.Word64:
        subscript += "w64";
        break;
    }
 
    return `v${id} = ${input(0)} ${symbol}${this.sub(subscript)} ${input(1)}`;
  }

  public override PrintInLine(): string { return ""; }
}

enum WordBinop_Kind {
  Add = "Add",
  Mul = "Mul",
  SignedMulOverflownBits = "SignedMulOverflownBits",
  UnsignedMulOverflownBits = "UnsignedMulOverflownBits",
  BitwiseAnd = "BitwiseAnd",
  BitwiseOr = "BitwiseOr",
  BitwiseXor = "BitwiseXor",
  Sub = "Sub",
  SignedDiv = "SignedDiv",
  UnsignedDiv = "UnsignedDiv",
  SignedMod = "SignedMod",
  UnsignedMod = "UnsignedMod",
}

class CompactOperationPrinter_WordBinop extends CompactOperationPrinter {
  kind: WordBinop_Kind;
  rep: WordRepresentation;

  constructor(operation: TurboshaftGraphOperation, properties: string) {
    super(operation);

    const options = this.parseOptions(properties, 2);
    this.kind = toEnum(WordBinop_Kind, options[0]);
    this.rep = toEnum(WordRepresentation, options[1]);
  }

   public override Print(id: number, input: InputPrinter): string {
    let symbol: string;
    let subscript = "";
    switch(this.kind) {
      case WordBinop_Kind.Add: symbol = "+"; break;
      case WordBinop_Kind.Mul: symbol = "*"; break;
      case WordBinop_Kind.SignedMulOverflownBits:
        symbol = "*";
        subscript = "s,of";
        break;
      case WordBinop_Kind.UnsignedMulOverflownBits:
        symbol = "*";
        subscript = "u,of";
        break;
      case WordBinop_Kind.BitwiseAnd: symbol = "&"; break;
      case WordBinop_Kind.BitwiseOr: symbol = "|"; break;
      case WordBinop_Kind.BitwiseXor: symbol = "^"; break;
      case WordBinop_Kind.Sub: symbol = "-"; break;
      case WordBinop_Kind.SignedDiv:
        symbol = "/";
        subscript = "s";
        break;
      case WordBinop_Kind.UnsignedDiv:
        symbol = "/";
        subscript = "u";
        break;
      case WordBinop_Kind.SignedMod:
        symbol = "%";
        subscript = "s";
        break;
      case WordBinop_Kind.UnsignedMod:
        symbol = "%";
        subscript = "u";
        break;
    }
    if(subscript.length > 0) subscript += ",";
    subscript += wrString(this.rep);
    return `v${id} = ${input(0)} ${symbol}${this.sub(subscript)} ${input(1)}`;
  }
}

enum Comparison_Kind {
  Equal = "Equal",
  SignedLessThan = "SignedLessThan",
  SignedLessThanOrEqual = "SignedLessThanOrEqual",
  UnsignedLessThan = "UnsignedLessThan",
  UnsignedLessThanOrEqual = "UnsignedLessThanOrEqual",
}

class CompactOperationPrinter_Comparison extends CompactOperationPrinter {
  kind: Comparison_Kind;
  rep: RegisterRepresentation;

  constructor(operation: TurboshaftGraphOperation, properties: string) {
    super(operation);

    const options = this.parseOptions(properties, 2);
    this.kind = toEnum(Comparison_Kind, options[0]);
    this.rep = toEnum(RegisterRepresentation, options[1]);
  }

   public override Print(id: number, input: InputPrinter): string {
    let symbol: string;
    let subscript = "";
    switch(this.kind) {
      case Comparison_Kind.Equal:
        symbol = "==";
        break;
      case Comparison_Kind.SignedLessThan:
        symbol = "<";
        subscript = "s";
        break;
      case Comparison_Kind.SignedLessThanOrEqual:
        symbol = "<=";
        subscript = "s";
        break;
       case Comparison_Kind.UnsignedLessThan:
        symbol = "<";
        subscript = "u";
        break;
      case Comparison_Kind.UnsignedLessThanOrEqual:
        symbol = "<=";
        subscript = "u";
        break;
    }
    if(subscript.length > 0) subscript += ",";
    subscript += rrString(this.rep);
    return `v${id} = ${input(0)} ${symbol}${this.sub(subscript)} ${input(1)}`;
  }
}

class CompactOperationPrinter_Load extends CompactOperationPrinter {
  taggedBase: boolean;
  maybeUnaligned: boolean = false;
  withTrapHandler: boolean = false;
  loadedRep: MemoryRepresentation;
  resultRep: RegisterRepresentation;
  elementSizeLog2: number = 0;
  offset: number = 0;

  constructor(operation: TurboshaftGraphOperation, properties: string) {
    super(operation);

    const options = this.parseOptions(properties);
    let idx = 0;
    this.taggedBase = chooseOption(options[idx++], undefined, "tagged base", "raw") === 0;
    if(chooseOption(options[idx], 1, "unaligned") === 0) {
      this.maybeUnaligned = true;
      ++idx;
    }
    if(chooseOption(options[idx], 1, "protected") === 0) {
      this.withTrapHandler = true;
      ++idx;
    }
    this.loadedRep = toEnum(MemoryRepresentation, options[idx++]);
    this.resultRep = toEnum(RegisterRepresentation, options[idx++]);
    if(idx < options.length && options[idx].startsWith("element size: 2^")) {
      this.elementSizeLog2 = parseInt(options[idx].substring("element size: 2^".length));
      ++idx;
    }
    if(idx < options.length) {
      if(!options[idx].startsWith("offset: ")) {
        throw new CompactOperationError(`Option "${options[idx]}" expected to start with "offset: "`);
      }
      this.offset = parseInt(options[idx].substring("offset: ".length));
    }
  }

  public override Print(id: number, input: InputPrinter): string {
    const prefix = `v${id} =${this.sub(rrString(this.resultRep))}`;
    let offsetStr = "";
    if(this.offset > 0) offsetStr = ` + ${this.offset}`;
    else if(this.offset < 0) offsetStr = ` - ${-this.offset}`;
    if(this.GetInputCount() === 1) {
      return prefix + ` [${input(0)}${this.sub(this.taggedBase ? "t" : "r")}${offsetStr}]${this.sub(mrString(this.loadedRep))}`;
    } else if(this.GetInputCount() === 2) {
      let indexStr = `+ ${input(1)}`;
      if(this.elementSizeLog2 > 0) indexStr += `*${2**this.elementSizeLog2}`;
      return prefix + ` [${input(0)}${this.sub(this.taggedBase ? "t" : "r")}${indexStr}${offsetStr}]${this.sub(mrString(this.loadedRep))}`;
    } else {
      throw new CompactOperationError("Unexpected input count in Load operation");
    }
  }
}

enum WriteBarrierKind {
  NoWriteBarrier = "NoWriteBarrier",
  AssertNoWriteBarrier = "AssertNoWriteBarrier",
  MapWriteBarrier = "MapWriteBarrier",
  PointerWriteBarrier = "PointerWriteBarrier",
  IndirectPointerWriteBarrier = "IndirectPointerWriteBarrier",
  EphemeronKeyWriteBarrier = "EphemeronKeyWriteBarrier",
  FullWriteBarrier = "FullWriteBarrier",
}

class CompactOperationPrinter_Store extends CompactOperationPrinter {
  taggedBase: boolean;
  maybeUnaligned: boolean = false;
  withTrapHandler: boolean = false;
  storedRep: MemoryRepresentation;
  writeBarrier: WriteBarrierKind;
  elementSizeLog2: number = 0;
  offset: number = 0;
  maybeInitializingOrTransitioning: boolean = false;

  constructor(operation: TurboshaftGraphOperation, properties: string) {
    super(operation);

    const options = this.parseOptions(properties);
    let idx = 0;
    this.taggedBase = chooseOption(options[idx++], undefined, "tagged base", "raw") === 0;
    if(chooseOption(options[idx], 1, "unaligned") === 0) {
      this.maybeUnaligned = true;
      ++idx;
    }
    if(chooseOption(options[idx], 1, "protected") === 0) {
      this.withTrapHandler = true;
      ++idx;
    }
    this.storedRep = toEnum(MemoryRepresentation, options[idx++]);
    this.writeBarrier = toEnum(WriteBarrierKind, options[idx++]);
    if(idx < options.length && options[idx].startsWith("element size: 2^")) {
      this.elementSizeLog2 = parseInt(options[idx].substring("element size: 2^".length));
      ++idx;
    }
    if(idx < options.length && options[idx].startsWith("offset: ")) {
      this.offset = parseInt(options[idx++].substring("offset: ".length));
    }
    if(idx < options.length) {
      if(options[idx] !== "initializing") {
        throw new CompactOperationError(`Option "${options[idx]}" expected to be "initializing`);
      }
      this.maybeInitializingOrTransitioning = true;
    }
  }

  public override Print(id: number, input: InputPrinter): string {
    let prefix = `${id}: [${input(0)}${this.sub(this.taggedBase ? "t" : "r")}`;
    let offsetStr = "";
    if(this.offset > 0) offsetStr = ` + ${this.offset}`;
    else if(this.offset < 0) offsetStr = ` - ${-this.offset}`;
    if(this.GetInputCount() === 2) {
      return prefix + `${offsetStr}]${this.sub(mrString(this.storedRep))} = ${input(1)}`;
    } else if(this.GetInputCount() === 3) {
      let indexStr = `+ ${input(1)}`;
      if(this.elementSizeLog2 > 0) indexStr += `*${2**this.elementSizeLog2}`;
      return prefix + `${indexStr}${offsetStr}]${this.sub(mrString(this.storedRep))}] = ${input(1)}`;
    } else {
      throw new CompactOperationError("Unexpected input count in Store operation");
    }
  }
}

export class TurboshaftGraphOperation extends Node<TurboshaftGraphEdge<TurboshaftGraphOperation>> {
  title: string;
  block: TurboshaftGraphBlock;
  sourcePosition: SourcePosition;
  bytecodePosition: BytecodePosition;
  origin: NodeOrigin;
  opEffects: String;

  compactPrinter: CompactOperationPrinter;

  constructor(id: number, title: string, block: TurboshaftGraphBlock,
              sourcePosition: SourcePosition, bytecodePosition: BytecodePosition,
              origin: NodeOrigin, opEffects: String) {
    super(id);
    this.title = title;
    this.block = block;
    this.sourcePosition = sourcePosition;
    this.bytecodePosition = bytecodePosition;
    this.origin = origin;
    this.opEffects = opEffects;
    this.visible = true;
    this.compactPrinter = null;
  }

  public propertiesChanged(customData: TurboshaftCustomDataPhase): void {
    // The properties have been parsed from the JSON, we need to update
    // operation printing.
    const properties = customData.data[this.id];
    this.compactPrinter = this.parseOperationForCompactRepresentation(properties);
  }

  public getHeight(showCustomData: boolean, compactView: boolean): number {
    if(compactView && this.compactPrinter?.IsFullyInlined()) return 0;
    return showCustomData ? this.labelBox.height * 2 : this.labelBox.height;
  }

  public getWidth(): number {
    return Math.max(this.inputs.length * C.NODE_INPUT_WIDTH, this.labelBox.width);
  }

  public initDisplayLabel(): void {
    this.displayLabel = this.getNonCompactedOperationText();
    this.labelBox = measureText(this.displayLabel);
  }

  public getTitle(): string {
    let title = `${this.id} ${this.title}`;
    title += `\nEffects: ${this.opEffects}`;
    if (this.origin) {
      title += `\nOrigin: ${this.origin.toString()}`;
    }
    if (this.inputs.length > 0) {
      title += `\nInputs: ${this.inputs.map(i => formatInput(i.source)).join(", ")}`;
    }
    if (this.outputs.length > 0) {
      title += `\nOutputs: ${this.outputs.map(i => i.target.id).join(", ")}`;
    }
    return title;

    function formatInput(input: TurboshaftGraphOperation) {
      return `[${input.block}] ${input.displayLabel}`;
    }
  }

  public getHistoryLabel(): string {
    return `${this.id} ${this.title}`;
  }

  public getNodeOrigin(): NodeOrigin {
    return this.origin;
  }

  public printInput(input: TurboshaftGraphOperation): string {
    if(input.compactPrinter && input.compactPrinter.IsFullyInlined()) {
      const s: string = input.compactPrinter.PrintInLine();
      return s;
    }
    return "v" + input.id.toString();
  }

  private getNonCompactedOperationText(): string {
    if(this.inputs.length == 0) {
      return `${this.id} ${this.title}`;
    } else {
      return `${this.id} ${this.title}(${this.inputs.map(i => i.source.id).join(", ")})`;
    }
  }

  public buildOperationText(compact: boolean): string {
    if(!compact) {
      return this.getNonCompactedOperationText();
    }
    const that = this;
    if(this.compactPrinter) {
      return this.compactPrinter.Print(this.id, n => that.printInput(this.inputs[n].source));
    } else if(this.inputs.length == 0) {
      return `v${this.id} ${this.title}`;
    } else {
      return `v${this.id} ${this.title}(${this.inputs.map(input => that.printInput(input.source)).join(", ")})`;
    }
  }

  public equals(that?: TurboshaftGraphOperation): boolean {
    if (!that) return false;
    if (this.id !== that.id) return false;
    return this.title === that.title;
  }

  private parseOperationForCompactRepresentation(properties: string):
      CompactOperationPrinter {
    try {
      switch(this.title) {
        case Opcode.Constant:
          return new CompactOperationPrinter_Constant(this, properties);
        case Opcode.WordBinop:
          return new CompactOperationPrinter_WordBinop(this, properties);
        case Opcode.Comparison:
          return new CompactOperationPrinter_Comparison(this, properties);
        case Opcode.Shift:
          return new CompactOperationPrinter_Shift(this, properties);
        case Opcode.Load:
          return new CompactOperationPrinter_Load(this, properties);
        case Opcode.Store:
          return new CompactOperationPrinter_Store(this, properties);
        default:
          return null;
      }
    }
    catch(e) {
      if(e instanceof CompactOperationError) {
        console.error(e.message);
        return null;
      }
      throw e;
    }
  }
}
