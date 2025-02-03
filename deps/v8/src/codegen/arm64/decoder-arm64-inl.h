// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_DECODER_ARM64_INL_H_
#define V8_CODEGEN_ARM64_DECODER_ARM64_INL_H_

#include "src/codegen/arm64/decoder-arm64.h"

namespace v8 {
namespace internal {

// Top-level instruction decode function.
template <typename V>
void Decoder<V>::Decode(Instruction* instr) {
  if (instr->Bits(28, 27) == 0) {
    V::VisitUnallocated(instr);
  } else {
    switch (instr->Bits(27, 24)) {
      // 0:   PC relative addressing.
      case 0x0:
        DecodePCRelAddressing(instr);
        break;

      // 1:   Add/sub immediate.
      case 0x1:
        DecodeAddSubImmediate(instr);
        break;

      // A:   Logical shifted register.
      //      Add/sub with carry.
      //      Conditional compare register.
      //      Conditional compare immediate.
      //      Conditional select.
      //      Data processing 1 source.
      //      Data processing 2 source.
      // B:   Add/sub shifted register.
      //      Add/sub extended register.
      //      Data processing 3 source.
      case 0xA:
      case 0xB:
        DecodeDataProcessing(instr);
        break;

      // 2:   Logical immediate.
      //      Move wide immediate.
      case 0x2:
        DecodeLogical(instr);
        break;

      // 3:   Bitfield.
      //      Extract.
      case 0x3:
        DecodeBitfieldExtract(instr);
        break;

      // 4:   Unconditional branch immediate.
      //      Exception generation.
      //      Compare and branch immediate.
      // 5:   Compare and branch immediate.
      //      Conditional branch.
      //      System.
      // 6,7: Unconditional branch.
      //      Test and branch immediate.
      case 0x4:
      case 0x5:
      case 0x6:
      case 0x7:
        DecodeBranchSystemException(instr);
        break;

      // 8,9: Load/store register pair post-index.
      //      Load register literal.
      //      Load/store register unscaled immediate.
      //      Load/store register immediate post-index.
      //      Load/store register immediate pre-index.
      //      Load/store register offset.
      //      Load/store exclusive.
      //      Load/store ordered.
      //      Compare and swap [Armv8.1].
      //      Compare and swap pair [Armv8.1].
      //      Atomic memory operations [Armv8.1].
      // C,D: Load/store register pair offset.
      //      Load/store register pair pre-index.
      //      Load/store register unsigned immediate.
      //      Advanced SIMD.
      case 0x8:
      case 0x9:
      case 0xC:
      case 0xD:
        DecodeLoadStore(instr);
        break;

      // E:   FP fixed point conversion.
      //      FP integer conversion.
      //      FP data processing 1 source.
      //      FP compare.
      //      FP immediate.
      //      FP data processing 2 source.
      //      FP conditional compare.
      //      FP conditional select.
      //      Advanced SIMD.
      // F:   FP data processing 3 source.
      //      Advanced SIMD.
      case 0xE:
      case 0xF:
        DecodeFP(instr);
        break;
    }
  }
}

template <typename V>
void Decoder<V>::DecodePCRelAddressing(Instruction* instr) {
  DCHECK_EQ(0x0, instr->Bits(27, 24));
  // We know bit 28 is set, as <b28:b27> = 0 is filtered out at the top level
  // decode.
  DCHECK_EQ(0x1, instr->Bit(28));
  V::VisitPCRelAddressing(instr);
}

template <typename V>
void Decoder<V>::DecodeBranchSystemException(Instruction* instr) {
  DCHECK_EQ(0x4, instr->Bits(27, 24) & 0xC);  // 0x4, 0x5, 0x6, 0x7

  switch (instr->Bits(31, 29)) {
    case 0:
    case 4: {
      V::VisitUnconditionalBranch(instr);
      break;
    }
    case 1:
    case 5: {
      if (instr->Bit(25) == 0) {
        V::VisitCompareBranch(instr);
      } else {
        V::VisitTestBranch(instr);
      }
      break;
    }
    case 2: {
      if (instr->Bit(25) == 0) {
        if ((instr->Bit(24) == 0x1) ||
            (instr->Mask(0x01000010) == 0x00000010)) {
          V::VisitUnallocated(instr);
        } else {
          V::VisitConditionalBranch(instr);
        }
      } else {
        V::VisitUnallocated(instr);
      }
      break;
    }
    case 6: {
      if (instr->Bit(25) == 0) {
        if (instr->Bit(24) == 0) {
          if ((instr->Bits(4, 2) != 0) ||
              (instr->Mask(0x00E0001D) == 0x00200001) ||
              (instr->Mask(0x00E0001D) == 0x00400001) ||
              (instr->Mask(0x00E0001E) == 0x00200002) ||
              (instr->Mask(0x00E0001E) == 0x00400002) ||
              (instr->Mask(0x00E0001C) == 0x00600000) ||
              (instr->Mask(0x00E0001C) == 0x00800000) ||
              (instr->Mask(0x00E0001F) == 0x00A00000) ||
              (instr->Mask(0x00C0001C) == 0x00C00000)) {
            V::VisitUnallocated(instr);
          } else {
            V::VisitException(instr);
          }
        } else {
          if (instr->Bits(23, 22) == 0) {
            const Instr masked_003FF0E0 = instr->Mask(0x003FF0E0);
            if ((instr->Bits(21, 19) == 0x4) ||
                (masked_003FF0E0 == 0x00033000) ||
                (masked_003FF0E0 == 0x003FF020) ||
                (masked_003FF0E0 == 0x003FF060) ||
                (masked_003FF0E0 == 0x003FF0E0) ||
                (instr->Mask(0x00388000) == 0x00008000) ||
                (instr->Mask(0x0038E000) == 0x00000000) ||
                (instr->Mask(0x0039E000) == 0x00002000) ||
                (instr->Mask(0x003AE000) == 0x00002000) ||
                (instr->Mask(0x003CE000) == 0x00042000) ||
                (instr->Mask(0x0038F000) == 0x00005000) ||
                (instr->Mask(0x0038E000) == 0x00006000)) {
              V::VisitUnallocated(instr);
            } else {
              V::VisitSystem(instr);
            }
          } else {
            V::VisitUnallocated(instr);
          }
        }
      } else {
        if ((instr->Bit(24) == 0x1) || (instr->Bits(20, 16) != 0x1F) ||
            (instr->Bits(15, 10) != 0) || (instr->Bits(4, 0) != 0) ||
            (instr->Bits(24, 21) == 0x3) || (instr->Bits(24, 22) == 0x3)) {
          V::VisitUnallocated(instr);
        } else {
          V::VisitUnconditionalBranchToRegister(instr);
        }
      }
      break;
    }
    case 3:
    case 7: {
      V::VisitUnallocated(instr);
      break;
    }
  }
}

template <typename V>
void Decoder<V>::DecodeLoadStore(Instruction* instr) {
  DCHECK_EQ(0x8, instr->Bits(27, 24) & 0xA);  // 0x8, 0x9, 0xC, 0xD

  if ((instr->Bit(28) == 0) && (instr->Bit(29) == 0) && (instr->Bit(26) == 1)) {
    DecodeNEONLoadStore(instr);
    return;
  }

  if (instr->Bit(24) == 0) {
    if (instr->Bit(28) == 0) {
      if (instr->Bit(29) == 0) {
        if (instr->Bit(26) == 0) {
          if (instr->Mask(0xA08000) == 0x800000) {
            V::VisitUnallocated(instr);
          } else if (instr->Mask(0xA08000) == 0) {
            // Load/Store exclusive without acquire/release are unimplemented.
            V::VisitUnimplemented(instr);
          } else {
            V::VisitLoadStoreAcquireRelease(instr);
          }
        } else {
          // This is handled by DecodeNEONLoadStore().
          UNREACHABLE();
        }
      } else {
        if ((instr->Bits(31, 30) == 0x3) ||
            (instr->Mask(0xC4400000) == 0x40000000)) {
          V::VisitUnallocated(instr);
        } else {
          if (instr->Bit(23) == 0) {
            if (instr->Mask(0xC4400000) == 0xC0400000) {
              V::VisitUnallocated(instr);
            } else {
              // Nontemporals are unimplemented.
              V::VisitUnimplemented(instr);
            }
          } else {
            V::VisitLoadStorePairPostIndex(instr);
          }
        }
      }
    } else {
      if (instr->Bit(29) == 0) {
        if (instr->Mask(0xC4000000) == 0xC4000000) {
          V::VisitUnallocated(instr);
        } else {
          V::VisitLoadLiteral(instr);
        }
      } else {
        if ((instr->Mask(0x44800000) == 0x44800000) ||
            (instr->Mask(0x84800000) == 0x84800000)) {
          V::VisitUnallocated(instr);
        } else {
          if (instr->Bit(21) == 0) {
            switch (instr->Bits(11, 10)) {
              case 0: {
                V::VisitLoadStoreUnscaledOffset(instr);
                break;
              }
              case 1: {
                if (instr->Mask(0xC4C00000) == 0xC0800000) {
                  V::VisitUnallocated(instr);
                } else {
                  V::VisitLoadStorePostIndex(instr);
                }
                break;
              }
              case 2: {
                // TODO(all): VisitLoadStoreRegisterOffsetUnpriv.
                V::VisitUnimplemented(instr);
                break;
              }
              case 3: {
                if (instr->Mask(0xC4C00000) == 0xC0800000) {
                  V::VisitUnallocated(instr);
                } else {
                  V::VisitLoadStorePreIndex(instr);
                }
                break;
              }
            }
          } else {
            if (instr->Bits(11, 10) == 0x2) {
              if (instr->Bit(14) == 0) {
                V::VisitUnallocated(instr);
              } else {
                V::VisitLoadStoreRegisterOffset(instr);
              }
            } else {
              if ((instr->Bits(11, 10) == 0x0) &&
                  (instr->Bits(26, 25) == 0x0)) {
                if ((instr->Bit(15) == 1) &&
                    ((instr->Bits(14, 12) == 0x1) || (instr->Bit(13) == 1) ||
                     (instr->Bits(14, 12) == 0x5) ||
                     ((instr->Bits(14, 12) == 0x4) &&
                      ((instr->Bit(23) == 0) ||
                       (instr->Bits(23, 22) == 0x3))))) {
                  V::VisitUnallocated(instr);
                } else {
                  V::VisitAtomicMemory(instr);
                }
              } else {
                V::VisitUnallocated(instr);
              }
            }
          }
        }
      }
    }
  } else {
    if (instr->Bit(28) == 0) {
      if (instr->Bit(29) == 0) {
        V::VisitUnallocated(instr);
      } else {
        if ((instr->Bits(31, 30) == 0x3) ||
            (instr->Mask(0xC4400000) == 0x40000000)) {
          V::VisitUnallocated(instr);
        } else {
          if (instr->Bit(23) == 0) {
            V::VisitLoadStorePairOffset(instr);
          } else {
            V::VisitLoadStorePairPreIndex(instr);
          }
        }
      }
    } else {
      if (instr->Bit(29) == 0) {
        V::VisitUnallocated(instr);
      } else {
        if ((instr->Mask(0x84C00000) == 0x80C00000) ||
            (instr->Mask(0x44800000) == 0x44800000) ||
            (instr->Mask(0x84800000) == 0x84800000)) {
          V::VisitUnallocated(instr);
        } else {
          V::VisitLoadStoreUnsignedOffset(instr);
        }
      }
    }
  }
}

template <typename V>
void Decoder<V>::DecodeLogical(Instruction* instr) {
  DCHECK_EQ(0x2, instr->Bits(27, 24));

  if (instr->Mask(0x80400000) == 0x00400000) {
    V::VisitUnallocated(instr);
  } else {
    if (instr->Bit(23) == 0) {
      V::VisitLogicalImmediate(instr);
    } else {
      if (instr->Bits(30, 29) == 0x1) {
        V::VisitUnallocated(instr);
      } else {
        V::VisitMoveWideImmediate(instr);
      }
    }
  }
}

template <typename V>
void Decoder<V>::DecodeBitfieldExtract(Instruction* instr) {
  DCHECK_EQ(0x3, instr->Bits(27, 24));

  if ((instr->Mask(0x80400000) == 0x80000000) ||
      (instr->Mask(0x80400000) == 0x00400000) ||
      (instr->Mask(0x80008000) == 0x00008000)) {
    V::VisitUnallocated(instr);
  } else if (instr->Bit(23) == 0) {
    if ((instr->Mask(0x80200000) == 0x00200000) ||
        (instr->Mask(0x60000000) == 0x60000000)) {
      V::VisitUnallocated(instr);
    } else {
      V::VisitBitfield(instr);
    }
  } else {
    if ((instr->Mask(0x60200000) == 0x00200000) ||
        (instr->Mask(0x60000000) != 0x00000000)) {
      V::VisitUnallocated(instr);
    } else {
      V::VisitExtract(instr);
    }
  }
}

template <typename V>
void Decoder<V>::DecodeAddSubImmediate(Instruction* instr) {
  DCHECK_EQ(0x1, instr->Bits(27, 24));
  if (instr->Bit(23) == 1) {
    V::VisitUnallocated(instr);
  } else {
    V::VisitAddSubImmediate(instr);
  }
}

template <typename V>
void Decoder<V>::DecodeDataProcessing(Instruction* instr) {
  DCHECK((instr->Bits(27, 24) == 0xA) || (instr->Bits(27, 24) == 0xB));

  if (instr->Bit(24) == 0) {
    if (instr->Bit(28) == 0) {
      if (instr->Mask(0x80008000) == 0x00008000) {
        V::VisitUnallocated(instr);
      } else {
        V::VisitLogicalShifted(instr);
      }
    } else {
      switch (instr->Bits(23, 21)) {
        case 0: {
          if (instr->Mask(0x0000FC00) != 0) {
            V::VisitUnallocated(instr);
          } else {
            V::VisitAddSubWithCarry(instr);
          }
          break;
        }
        case 2: {
          if ((instr->Bit(29) == 0) || (instr->Mask(0x00000410) != 0)) {
            V::VisitUnallocated(instr);
          } else {
            if (instr->Bit(11) == 0) {
              V::VisitConditionalCompareRegister(instr);
            } else {
              V::VisitConditionalCompareImmediate(instr);
            }
          }
          break;
        }
        case 4: {
          if (instr->Mask(0x20000800) != 0x00000000) {
            V::VisitUnallocated(instr);
          } else {
            V::VisitConditionalSelect(instr);
          }
          break;
        }
        case 6: {
          if (instr->Bit(29) == 0x1) {
            V::VisitUnallocated(instr);
          } else {
            if (instr->Bit(30) == 0) {
              if ((instr->Bit(15) == 0x1) || (instr->Bits(15, 11) == 0) ||
                  (instr->Bits(15, 12) == 0x1) ||
                  (instr->Bits(15, 12) == 0x3) ||
                  (instr->Bits(15, 13) == 0x3) ||
                  (instr->Mask(0x8000EC00) == 0x00004C00) ||
                  (instr->Mask(0x8000E800) == 0x80004000) ||
                  (instr->Mask(0x8000E400) == 0x80004000)) {
                V::VisitUnallocated(instr);
              } else {
                V::VisitDataProcessing2Source(instr);
              }
            } else {
              if ((instr->Bit(13) == 1) || (instr->Bits(20, 16) != 0) ||
                  (instr->Bits(15, 14) != 0) ||
                  (instr->Mask(0xA01FFC00) == 0x00000C00) ||
                  (instr->Mask(0x201FF800) == 0x00001800)) {
                V::VisitUnallocated(instr);
              } else {
                V::VisitDataProcessing1Source(instr);
              }
            }
            break;
          }
          [[fallthrough]];
        }
        case 1:
        case 3:
        case 5:
        case 7:
          V::VisitUnallocated(instr);
          break;
      }
    }
  } else {
    if (instr->Bit(28) == 0) {
      if (instr->Bit(21) == 0) {
        if ((instr->Bits(23, 22) == 0x3) ||
            (instr->Mask(0x80008000) == 0x00008000)) {
          V::VisitUnallocated(instr);
        } else {
          V::VisitAddSubShifted(instr);
        }
      } else {
        if ((instr->Mask(0x00C00000) != 0x00000000) ||
            (instr->Mask(0x00001400) == 0x00001400) ||
            (instr->Mask(0x00001800) == 0x00001800)) {
          V::VisitUnallocated(instr);
        } else {
          V::VisitAddSubExtended(instr);
        }
      }
    } else {
      if ((instr->Bit(30) == 0x1) || (instr->Bits(30, 29) == 0x1) ||
          (instr->Mask(0xE0600000) == 0x00200000) ||
          (instr->Mask(0xE0608000) == 0x00400000) ||
          (instr->Mask(0x60608000) == 0x00408000) ||
          (instr->Mask(0x60E00000) == 0x00E00000) ||
          (instr->Mask(0x60E00000) == 0x00800000) ||
          (instr->Mask(0x60E00000) == 0x00600000)) {
        V::VisitUnallocated(instr);
      } else {
        V::VisitDataProcessing3Source(instr);
      }
    }
  }
}

template <typename V>
void Decoder<V>::DecodeFP(Instruction* instr) {
  DCHECK((instr->Bits(27, 24) == 0xE) || (instr->Bits(27, 24) == 0xF));

  if (instr->Bit(28) == 0) {
    DecodeNEONVectorDataProcessing(instr);
  } else {
    if (instr->Bits(31, 30) == 0x3) {
      V::VisitUnallocated(instr);
    } else if (instr->Bits(31, 30) == 0x1) {
      DecodeNEONScalarDataProcessing(instr);
    } else {
      if (instr->Bit(29) == 0) {
        if (instr->Bit(24) == 0) {
          if (instr->Bit(21) == 0) {
            if ((instr->Bit(23) == 1) || (instr->Bit(18) == 1) ||
                (instr->Mask(0x80008000) == 0x00000000) ||
                (instr->Mask(0x000E0000) == 0x00000000) ||
                (instr->Mask(0x000E0000) == 0x000A0000) ||
                (instr->Mask(0x00160000) == 0x00000000) ||
                (instr->Mask(0x00160000) == 0x00120000)) {
              V::VisitUnallocated(instr);
            } else {
              V::VisitFPFixedPointConvert(instr);
            }
          } else {
            if (instr->Bits(15, 10) == 32) {
              V::VisitUnallocated(instr);
            } else if (instr->Bits(15, 10) == 0) {
              if ((instr->Bits(23, 22) == 0x3) ||
                  (instr->Mask(0x000E0000) == 0x000A0000) ||
                  (instr->Mask(0x000E0000) == 0x000C0000) ||
                  (instr->Mask(0x00160000) == 0x00120000) ||
                  (instr->Mask(0x00160000) == 0x00140000) ||
                  (instr->Mask(0x20C40000) == 0x00800000) ||
                  (instr->Mask(0x20C60000) == 0x00840000) ||
                  (instr->Mask(0xA0C60000) == 0x80060000) ||
                  (instr->Mask(0xA0C60000) == 0x00860000) ||
                  (instr->Mask(0xA0CE0000) == 0x80860000) ||
                  (instr->Mask(0xA0CE0000) == 0x804E0000) ||
                  (instr->Mask(0xA0CE0000) == 0x000E0000) ||
                  (instr->Mask(0xA0D60000) == 0x00160000) ||
                  (instr->Mask(0xA0D60000) == 0x80560000) ||
                  (instr->Mask(0xA0D60000) == 0x80960000)) {
                V::VisitUnallocated(instr);
              } else {
                V::VisitFPIntegerConvert(instr);
              }
            } else if (instr->Bits(14, 10) == 16) {
              const Instr masked_A0DF8000 = instr->Mask(0xA0DF8000);
              if ((instr->Mask(0x80180000) != 0) ||
                  (masked_A0DF8000 == 0x00020000) ||
                  (masked_A0DF8000 == 0x00030000) ||
                  (masked_A0DF8000 == 0x00068000) ||
                  (masked_A0DF8000 == 0x00428000) ||
                  (masked_A0DF8000 == 0x00430000) ||
                  (masked_A0DF8000 == 0x00468000) ||
                  (instr->Mask(0xA0D80000) == 0x00800000) ||
                  (instr->Mask(0xA0DE0000) == 0x00C00000) ||
                  (instr->Mask(0xA0DF0000) == 0x00C30000) ||
                  (instr->Mask(0xA0DC0000) == 0x00C40000)) {
                V::VisitUnallocated(instr);
              } else {
                V::VisitFPDataProcessing1Source(instr);
              }
            } else if (instr->Bits(13, 10) == 8) {
              if ((instr->Bits(15, 14) != 0) || (instr->Bits(2, 0) != 0) ||
                  (instr->Mask(0x80800000) != 0x00000000)) {
                V::VisitUnallocated(instr);
              } else {
                V::VisitFPCompare(instr);
              }
            } else if (instr->Bits(12, 10) == 4) {
              if ((instr->Bits(9, 5) != 0) ||
                  (instr->Mask(0x80800000) != 0x00000000)) {
                V::VisitUnallocated(instr);
              } else {
                V::VisitFPImmediate(instr);
              }
            } else {
              if (instr->Mask(0x80800000) != 0x00000000) {
                V::VisitUnallocated(instr);
              } else {
                switch (instr->Bits(11, 10)) {
                  case 1: {
                    V::VisitFPConditionalCompare(instr);
                    break;
                  }
                  case 2: {
                    if ((instr->Bits(15, 14) == 0x3) ||
                        (instr->Mask(0x00009000) == 0x00009000) ||
                        (instr->Mask(0x0000A000) == 0x0000A000)) {
                      V::VisitUnallocated(instr);
                    } else {
                      V::VisitFPDataProcessing2Source(instr);
                    }
                    break;
                  }
                  case 3: {
                    V::VisitFPConditionalSelect(instr);
                    break;
                  }
                  default:
                    UNREACHABLE();
                }
              }
            }
          }
        } else {
          // Bit 30 == 1 has been handled earlier.
          DCHECK_EQ(0, instr->Bit(30));
          if (instr->Mask(0xA0800000) != 0) {
            V::VisitUnallocated(instr);
          } else {
            V::VisitFPDataProcessing3Source(instr);
          }
        }
      } else {
        V::VisitUnallocated(instr);
      }
    }
  }
}

template <typename V>
void Decoder<V>::DecodeNEONLoadStore(Instruction* instr) {
  DCHECK_EQ(0x6, instr->Bits(29, 25));
  if (instr->Bit(31) == 0) {
    if ((instr->Bit(24) == 0) && (instr->Bit(21) == 1)) {
      V::VisitUnallocated(instr);
      return;
    }

    if (instr->Bit(23) == 0) {
      if (instr->Bits(20, 16) == 0) {
        if (instr->Bit(24) == 0) {
          V::VisitNEONLoadStoreMultiStruct(instr);
        } else {
          V::VisitNEONLoadStoreSingleStruct(instr);
        }
      } else {
        V::VisitUnallocated(instr);
      }
    } else {
      if (instr->Bit(24) == 0) {
        V::VisitNEONLoadStoreMultiStructPostIndex(instr);
      } else {
        V::VisitNEONLoadStoreSingleStructPostIndex(instr);
      }
    }
  } else {
    V::VisitUnallocated(instr);
  }
}

template <typename V>
void Decoder<V>::DecodeNEONVectorDataProcessing(Instruction* instr) {
  DCHECK_EQ(0x7, instr->Bits(28, 25));
  if (instr->Bit(31) == 0) {
    if (instr->Bit(24) == 0) {
      if (instr->Bit(21) == 0) {
        if (instr->Bit(15) == 0) {
          if (instr->Bit(10) == 0) {
            if (instr->Bit(29) == 0) {
              if (instr->Bit(11) == 0) {
                V::VisitNEONTable(instr);
              } else {
                V::VisitNEONPerm(instr);
              }
            } else {
              V::VisitNEONExtract(instr);
            }
          } else {
            if (instr->Bits(23, 22) == 0) {
              V::VisitNEONCopy(instr);
            } else {
              if (instr->Bit(14) == 0 && instr->Bit(22)) {
                V::VisitNEON3SameHP(instr);
              } else {
                V::VisitUnallocated(instr);
              }
            }
          }
        } else {
          if (instr->Bit(10) == 1) {
            V::VisitNEON3Extension(instr);
          } else {
            V::VisitUnallocated(instr);
          }
        }
      } else {
        if (instr->Bit(10) == 0) {
          if (instr->Bit(11) == 0) {
            V::VisitNEON3Different(instr);
          } else {
            if (instr->Bits(18, 17) == 0) {
              if (instr->Bit(20) == 0) {
                if (instr->Bit(19) == 0) {
                  V::VisitNEON2RegMisc(instr);
                } else {
                  if (instr->Bits(30, 29) == 0x2) {
                    V::VisitUnallocated(instr);
                  } else {
                    V::VisitUnallocated(instr);
                  }
                }
              } else {
                if (instr->Bit(19) == 0) {
                  V::VisitNEONAcrossLanes(instr);
                } else {
                  // Half-precision version.
                  V::VisitNEON2RegMisc(instr);
                }
              }
            } else {
              V::VisitUnallocated(instr);
            }
          }
        } else {
          V::VisitNEON3Same(instr);
        }
      }
    } else {
      if (instr->Bit(10) == 0) {
        V::VisitNEONByIndexedElement(instr);
      } else {
        if (instr->Bit(23) == 0) {
          if (instr->Bits(22, 19) == 0) {
            V::VisitNEONModifiedImmediate(instr);
          } else {
            V::VisitNEONShiftImmediate(instr);
          }
        } else {
          V::VisitUnallocated(instr);
        }
      }
    }
  } else {
    V::VisitUnallocated(instr);
  }
}

template <typename V>
void Decoder<V>::DecodeNEONScalarDataProcessing(Instruction* instr) {
  DCHECK_EQ(0xF, instr->Bits(28, 25));
  if (instr->Bit(24) == 0) {
    if (instr->Bit(21) == 0) {
      if (instr->Bit(15) == 0) {
        if (instr->Bit(10) == 0) {
          if (instr->Bit(29) == 0) {
            if (instr->Bit(11) == 0) {
              V::VisitUnallocated(instr);
            } else {
              V::VisitUnallocated(instr);
            }
          } else {
            V::VisitUnallocated(instr);
          }
        } else {
          if (instr->Bits(23, 22) == 0) {
            V::VisitNEONScalarCopy(instr);
          } else {
            V::VisitUnallocated(instr);
          }
        }
      } else {
        V::VisitUnallocated(instr);
      }
    } else {
      if (instr->Bit(10) == 0) {
        if (instr->Bit(11) == 0) {
          V::VisitNEONScalar3Diff(instr);
        } else {
          if (instr->Bits(18, 17) == 0) {
            if (instr->Bit(20) == 0) {
              if (instr->Bit(19) == 0) {
                V::VisitNEONScalar2RegMisc(instr);
              } else {
                if (instr->Bit(29) == 0) {
                  V::VisitUnallocated(instr);
                } else {
                  V::VisitUnallocated(instr);
                }
              }
            } else {
              if (instr->Bit(19) == 0) {
                V::VisitNEONScalarPairwise(instr);
              } else {
                V::VisitUnallocated(instr);
              }
            }
          } else {
            V::VisitUnallocated(instr);
          }
        }
      } else {
        V::VisitNEONScalar3Same(instr);
      }
    }
  } else {
    if (instr->Bit(10) == 0) {
      V::VisitNEONScalarByIndexedElement(instr);
    } else {
      if (instr->Bit(23) == 0) {
        V::VisitNEONScalarShiftImmediate(instr);
      } else {
        V::VisitUnallocated(instr);
      }
    }
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM64_DECODER_ARM64_INL_H_
