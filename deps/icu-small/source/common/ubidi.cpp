// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*
*   Copyright (C) 1999-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*   file name:  ubidi.c
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 1999jul27
*   created by: Markus W. Scherer, updated by Matitiahu Allouche
*
*/

#include "cmemory.h"
#include "unicode/utypes.h"
#include "unicode/ustring.h"
#include "unicode/uchar.h"
#include "unicode/ubidi.h"
#include "unicode/utf16.h"
#include "ubidi_props.h"
#include "ubidiimp.h"
#include "uassert.h"

/*
 * General implementation notes:
 *
 * Throughout the implementation, there are comments like (W2) that refer to
 * rules of the BiDi algorithm, in this example to the second rule of the
 * resolution of weak types.
 *
 * For handling surrogate pairs, where two char16_t's form one "abstract" (or UTF-32)
 * character according to UTF-16, the second char16_t gets the directional property of
 * the entire character assigned, while the first one gets a BN, a boundary
 * neutral, type, which is ignored by most of the algorithm according to
 * rule (X9) and the implementation suggestions of the BiDi algorithm.
 *
 * Later, adjustWSLevels() will set the level for each BN to that of the
 * following character (char16_t), which results in surrogate pairs getting the
 * same level on each of their surrogates.
 *
 * In a UTF-8 implementation, the same thing could be done: the last byte of
 * a multi-byte sequence would get the "real" property, while all previous
 * bytes of that sequence would get BN.
 *
 * It is not possible to assign all those parts of a character the same real
 * property because this would fail in the resolution of weak types with rules
 * that look at immediately surrounding types.
 *
 * As a related topic, this implementation does not remove Boundary Neutral
 * types from the input, but ignores them wherever this is relevant.
 * For example, the loop for the resolution of the weak types reads
 * types until it finds a non-BN.
 * Also, explicit embedding codes are neither changed into BN nor removed.
 * They are only treated the same way real BNs are.
 * As stated before, adjustWSLevels() takes care of them at the end.
 * For the purpose of conformance, the levels of all these codes
 * do not matter.
 *
 * Note that this implementation modifies the dirProps
 * after the initial setup, when applying X5c (replace FSI by LRI or RLI),
 * X6, N0 (replace paired brackets by L or R).
 *
 * In this implementation, the resolution of weak types (W1 to W6),
 * neutrals (N1 and N2), and the assignment of the resolved level (In)
 * are all done in one single loop, in resolveImplicitLevels().
 * Changes of dirProp values are done on the fly, without writing
 * them back to the dirProps array.
 *
 *
 * This implementation contains code that allows to bypass steps of the
 * algorithm that are not needed on the specific paragraph
 * in order to speed up the most common cases considerably,
 * like text that is entirely LTR, or RTL text without numbers.
 *
 * Most of this is done by setting a bit for each directional property
 * in a flags variable and later checking for whether there are
 * any LTR characters or any RTL characters, or both, whether
 * there are any explicit embedding codes, etc.
 *
 * If the (Xn) steps are performed, then the flags are re-evaluated,
 * because they will then not contain the embedding codes any more
 * and will be adjusted for override codes, so that subsequently
 * more bypassing may be possible than what the initial flags suggested.
 *
 * If the text is not mixed-directional, then the
 * algorithm steps for the weak type resolution are not performed,
 * and all levels are set to the paragraph level.
 *
 * If there are no explicit embedding codes, then the (Xn) steps
 * are not performed.
 *
 * If embedding levels are supplied as a parameter, then all
 * explicit embedding codes are ignored, and the (Xn) steps
 * are not performed.
 *
 * White Space types could get the level of the run they belong to,
 * and are checked with a test of (flags&MASK_EMBEDDING) to
 * consider if the paragraph direction should be considered in
 * the flags variable.
 *
 * If there are no White Space types in the paragraph, then
 * (L1) is not necessary in adjustWSLevels().
 */

/* to avoid some conditional statements, use tiny constant arrays */
static const Flags flagLR[2]={ DIRPROP_FLAG(L), DIRPROP_FLAG(R) };
static const Flags flagE[2]={ DIRPROP_FLAG(LRE), DIRPROP_FLAG(RLE) };
static const Flags flagO[2]={ DIRPROP_FLAG(LRO), DIRPROP_FLAG(RLO) };

#define DIRPROP_FLAG_LR(level) flagLR[(level)&1]
#define DIRPROP_FLAG_E(level)  flagE[(level)&1]
#define DIRPROP_FLAG_O(level)  flagO[(level)&1]

#define DIR_FROM_STRONG(strong) ((strong)==L ? L : R)

#define NO_OVERRIDE(level)  ((level)&~UBIDI_LEVEL_OVERRIDE)

/* UBiDi object management -------------------------------------------------- */

U_CAPI UBiDi * U_EXPORT2
ubidi_open()
{
    UErrorCode errorCode=U_ZERO_ERROR;
    return ubidi_openSized(0, 0, &errorCode);
}

U_CAPI UBiDi * U_EXPORT2
ubidi_openSized(int32_t maxLength, int32_t maxRunCount, UErrorCode *pErrorCode) {
    UBiDi *pBiDi;

    /* check the argument values */
    if(pErrorCode==nullptr || U_FAILURE(*pErrorCode)) {
        return nullptr;
    } else if(maxLength<0 || maxRunCount<0) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;    /* invalid arguments */
    }

    /* allocate memory for the object */
    pBiDi=(UBiDi *)uprv_malloc(sizeof(UBiDi));
    if(pBiDi==nullptr) {
        *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    /* reset the object, all pointers nullptr, all flags false, all sizes 0 */
    uprv_memset(pBiDi, 0, sizeof(UBiDi));

    /* allocate memory for arrays as requested */
    if(maxLength>0) {
        if( !getInitialDirPropsMemory(pBiDi, maxLength) ||
            !getInitialLevelsMemory(pBiDi, maxLength)
        ) {
            *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
        }
    } else {
        pBiDi->mayAllocateText=true;
    }

    if(maxRunCount>0) {
        if(maxRunCount==1) {
            /* use simpleRuns[] */
            pBiDi->runsSize=sizeof(Run);
        } else if(!getInitialRunsMemory(pBiDi, maxRunCount)) {
            *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
        }
    } else {
        pBiDi->mayAllocateRuns=true;
    }

    if(U_SUCCESS(*pErrorCode)) {
        return pBiDi;
    } else {
        ubidi_close(pBiDi);
        return nullptr;
    }
}

/*
 * We are allowed to allocate memory if memory==nullptr or
 * mayAllocate==true for each array that we need.
 * We also try to grow memory as needed if we
 * allocate it.
 *
 * Assume sizeNeeded>0.
 * If *pMemory!=nullptr, then assume *pSize>0.
 *
 * ### this realloc() may unnecessarily copy the old data,
 * which we know we don't need any more;
 * is this the best way to do this??
 */
U_CFUNC UBool
ubidi_getMemory(BidiMemoryForAllocation *bidiMem, int32_t *pSize, UBool mayAllocate, int32_t sizeNeeded) {
    void **pMemory = (void **)bidiMem;
    /* check for existing memory */
    if(*pMemory==nullptr) {
        /* we need to allocate memory */
        if(mayAllocate && (*pMemory=uprv_malloc(sizeNeeded))!=nullptr) {
            *pSize=sizeNeeded;
            return true;
        } else {
            return false;
        }
    } else {
        if(sizeNeeded<=*pSize) {
            /* there is already enough memory */
            return true;
        }
        else if(!mayAllocate) {
            /* not enough memory, and we must not allocate */
            return false;
        } else {
            /* we try to grow */
            void *memory;
            /* in most cases, we do not need the copy-old-data part of
             * realloc, but it is needed when adding runs using getRunsMemory()
             * in setParaRunsOnly()
             */
            if((memory=uprv_realloc(*pMemory, sizeNeeded))!=nullptr) {
                *pMemory=memory;
                *pSize=sizeNeeded;
                return true;
            } else {
                /* we failed to grow */
                return false;
            }
        }
    }
}

U_CAPI void U_EXPORT2
ubidi_close(UBiDi *pBiDi) {
    if(pBiDi!=nullptr) {
        pBiDi->pParaBiDi=nullptr;          /* in case one tries to reuse this block */
        if(pBiDi->dirPropsMemory!=nullptr) {
            uprv_free(pBiDi->dirPropsMemory);
        }
        if(pBiDi->levelsMemory!=nullptr) {
            uprv_free(pBiDi->levelsMemory);
        }
        if(pBiDi->openingsMemory!=nullptr) {
            uprv_free(pBiDi->openingsMemory);
        }
        if(pBiDi->parasMemory!=nullptr) {
            uprv_free(pBiDi->parasMemory);
        }
        if(pBiDi->runsMemory!=nullptr) {
            uprv_free(pBiDi->runsMemory);
        }
        if(pBiDi->isolatesMemory!=nullptr) {
            uprv_free(pBiDi->isolatesMemory);
        }
        if(pBiDi->insertPoints.points!=nullptr) {
            uprv_free(pBiDi->insertPoints.points);
        }

        uprv_free(pBiDi);
    }
}

/* set to approximate "inverse BiDi" ---------------------------------------- */

U_CAPI void U_EXPORT2
ubidi_setInverse(UBiDi *pBiDi, UBool isInverse) {
    if(pBiDi!=nullptr) {
        pBiDi->isInverse=isInverse;
        pBiDi->reorderingMode = isInverse ? UBIDI_REORDER_INVERSE_NUMBERS_AS_L
                                          : UBIDI_REORDER_DEFAULT;
    }
}

U_CAPI UBool U_EXPORT2
ubidi_isInverse(UBiDi *pBiDi) {
    if(pBiDi!=nullptr) {
        return pBiDi->isInverse;
    } else {
        return false;
    }
}

/* FOOD FOR THOUGHT: currently the reordering modes are a mixture of
 * algorithm for direct BiDi, algorithm for inverse BiDi and the bizarre
 * concept of RUNS_ONLY which is a double operation.
 * It could be advantageous to divide this into 3 concepts:
 * a) Operation: direct / inverse / RUNS_ONLY
 * b) Direct algorithm: default / NUMBERS_SPECIAL / GROUP_NUMBERS_WITH_R
 * c) Inverse algorithm: default / INVERSE_LIKE_DIRECT / NUMBERS_SPECIAL
 * This would allow combinations not possible today like RUNS_ONLY with
 * NUMBERS_SPECIAL.
 * Also allow to set INSERT_MARKS for the direct step of RUNS_ONLY and
 * REMOVE_CONTROLS for the inverse step.
 * Not all combinations would be supported, and probably not all do make sense.
 * This would need to document which ones are supported and what are the
 * fallbacks for unsupported combinations.
 */
U_CAPI void U_EXPORT2
ubidi_setReorderingMode(UBiDi *pBiDi, UBiDiReorderingMode reorderingMode) UPRV_NO_SANITIZE_UNDEFINED {
    if ((pBiDi!=nullptr) && (reorderingMode >= UBIDI_REORDER_DEFAULT)
                        && (reorderingMode < UBIDI_REORDER_COUNT)) {
        pBiDi->reorderingMode = reorderingMode;
        pBiDi->isInverse = reorderingMode == UBIDI_REORDER_INVERSE_NUMBERS_AS_L;
    }
}

U_CAPI UBiDiReorderingMode U_EXPORT2
ubidi_getReorderingMode(UBiDi *pBiDi) {
    if (pBiDi!=nullptr) {
        return pBiDi->reorderingMode;
    } else {
        return UBIDI_REORDER_DEFAULT;
    }
}

U_CAPI void U_EXPORT2
ubidi_setReorderingOptions(UBiDi *pBiDi, uint32_t reorderingOptions) {
    if (reorderingOptions & UBIDI_OPTION_REMOVE_CONTROLS) {
        reorderingOptions&=~UBIDI_OPTION_INSERT_MARKS;
    }
    if (pBiDi!=nullptr) {
        pBiDi->reorderingOptions=reorderingOptions;
    }
}

U_CAPI uint32_t U_EXPORT2
ubidi_getReorderingOptions(UBiDi *pBiDi) {
    if (pBiDi!=nullptr) {
        return pBiDi->reorderingOptions;
    } else {
        return 0;
    }
}

U_CAPI UBiDiDirection U_EXPORT2
ubidi_getBaseDirection(const char16_t *text,
int32_t length){

    int32_t i;
    UChar32 uchar;
    UCharDirection dir;

    if( text==nullptr || length<-1 ){
        return UBIDI_NEUTRAL;
    }

    if(length==-1) {
        length=u_strlen(text);
    }

    for( i = 0 ; i < length; ) {
        /* i is incremented by U16_NEXT */
        U16_NEXT(text, i, length, uchar);
        dir = u_charDirection(uchar);
        if( dir == U_LEFT_TO_RIGHT )
                return UBIDI_LTR;
        if( dir == U_RIGHT_TO_LEFT || dir ==U_RIGHT_TO_LEFT_ARABIC )
                return UBIDI_RTL;
    }
    return UBIDI_NEUTRAL;
}

/* perform (P2)..(P3) ------------------------------------------------------- */

/**
 * Returns the directionality of the first strong character
 * after the last B in prologue, if any.
 * Requires prologue!=null.
 */
static DirProp
firstL_R_AL(UBiDi *pBiDi) {
    const char16_t *text=pBiDi->prologue;
    int32_t length=pBiDi->proLength;
    int32_t i;
    UChar32 uchar;
    DirProp dirProp, result=ON;
    for(i=0; i<length; ) {
        /* i is incremented by U16_NEXT */
        U16_NEXT(text, i, length, uchar);
        dirProp = static_cast<DirProp>(ubidi_getCustomizedClass(pBiDi, uchar));
        if(result==ON) {
            if(dirProp==L || dirProp==R || dirProp==AL) {
                result=dirProp;
            }
        } else {
            if(dirProp==B) {
                result=ON;
            }
        }
    }
    return result;
}

/*
 * Check that there are enough entries in the array pointed to by pBiDi->paras
 */
static UBool
checkParaCount(UBiDi *pBiDi) {
    int32_t count=pBiDi->paraCount;
    if(pBiDi->paras==pBiDi->simpleParas) {
        if(count<=SIMPLE_PARAS_COUNT)
            return true;
        if(!getInitialParasMemory(pBiDi, SIMPLE_PARAS_COUNT * 2))
            return false;
        pBiDi->paras=pBiDi->parasMemory;
        uprv_memcpy(pBiDi->parasMemory, pBiDi->simpleParas, SIMPLE_PARAS_COUNT * sizeof(Para));
        return true;
    }
    if(!getInitialParasMemory(pBiDi, count * 2))
        return false;
    pBiDi->paras=pBiDi->parasMemory;
    return true;
}

/*
 * Get the directional properties for the text, calculate the flags bit-set, and
 * determine the paragraph level if necessary (in pBiDi->paras[i].level).
 * FSI initiators are also resolved and their dirProp replaced with LRI or RLI.
 * When encountering an FSI, it is initially replaced with an LRI, which is the
 * default. Only if a strong R or AL is found within its scope will the LRI be
 * replaced by an RLI.
 */
static UBool
getDirProps(UBiDi *pBiDi) {
    const char16_t *text=pBiDi->text;
    DirProp *dirProps=pBiDi->dirPropsMemory;    /* pBiDi->dirProps is const */

    int32_t i=0, originalLength=pBiDi->originalLength;
    Flags flags=0;      /* collect all directionalities in the text */
    UChar32 uchar;
    DirProp dirProp=0, defaultParaLevel=0;  /* initialize to avoid compiler warnings */
    UBool isDefaultLevel=IS_DEFAULT_LEVEL(pBiDi->paraLevel);
    /* for inverse BiDi, the default para level is set to RTL if there is a
       strong R or AL character at either end of the text                            */
    UBool isDefaultLevelInverse = isDefaultLevel && static_cast<UBool>(
            pBiDi->reorderingMode == UBIDI_REORDER_INVERSE_LIKE_DIRECT ||
            pBiDi->reorderingMode == UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL);
    int32_t lastArabicPos=-1;
    int32_t controlCount=0;
    UBool removeBiDiControls =
        static_cast<UBool>(pBiDi->reorderingOptions & UBIDI_OPTION_REMOVE_CONTROLS);

    enum State {
         NOT_SEEKING_STRONG,            /* 0: not contextual paraLevel, not after FSI */
         SEEKING_STRONG_FOR_PARA,       /* 1: looking for first strong char in para */
         SEEKING_STRONG_FOR_FSI,        /* 2: looking for first strong after FSI */
         LOOKING_FOR_PDI                /* 3: found strong after FSI, looking for PDI */
    };
    State state;
    DirProp lastStrong=ON;              /* for default level & inverse BiDi */
    /* The following stacks are used to manage isolate sequences. Those
       sequences may be nested, but obviously never more deeply than the
       maximum explicit embedding level.
       lastStack is the index of the last used entry in the stack. A value of -1
       means that there is no open isolate sequence.
       lastStack is reset to -1 on paragraph boundaries. */
    /* The following stack contains the position of the initiator of
       each open isolate sequence */
    int32_t isolateStartStack[UBIDI_MAX_EXPLICIT_LEVEL+1];
    /* The following stack contains the last known state before
       encountering the initiator of an isolate sequence */
    State  previousStateStack[UBIDI_MAX_EXPLICIT_LEVEL+1];
    int32_t stackLast=-1;

    if(pBiDi->reorderingOptions & UBIDI_OPTION_STREAMING)
        pBiDi->length=0;
    defaultParaLevel=pBiDi->paraLevel&1;
    if(isDefaultLevel) {
        pBiDi->paras[0].level=defaultParaLevel;
        lastStrong=defaultParaLevel;
        if(pBiDi->proLength>0 &&                    /* there is a prologue */
           (dirProp=firstL_R_AL(pBiDi))!=ON) {  /* with a strong character */
            if(dirProp==L)
                pBiDi->paras[0].level=0;    /* set the default para level */
            else
                pBiDi->paras[0].level=1;    /* set the default para level */
            state=NOT_SEEKING_STRONG;
        } else {
            state=SEEKING_STRONG_FOR_PARA;
        }
    } else {
        pBiDi->paras[0].level=pBiDi->paraLevel;
        state=NOT_SEEKING_STRONG;
    }
    /* count paragraphs and determine the paragraph level (P2..P3) */
    /*
     * see comment in ubidi.h:
     * the UBIDI_DEFAULT_XXX values are designed so that
     * their bit 0 alone yields the intended default
     */
    for( /* i=0 above */ ; i<originalLength; ) {
        /* i is incremented by U16_NEXT */
        U16_NEXT(text, i, originalLength, uchar);
        flags|=DIRPROP_FLAG(dirProp=(DirProp)ubidi_getCustomizedClass(pBiDi, uchar));
        dirProps[i-1]=dirProp;
        if(uchar>0xffff) {  /* set the lead surrogate's property to BN */
            flags|=DIRPROP_FLAG(BN);
            dirProps[i-2]=BN;
        }
        if(removeBiDiControls && IS_BIDI_CONTROL_CHAR(uchar))
            controlCount++;
        if(dirProp==L) {
            if(state==SEEKING_STRONG_FOR_PARA) {
                pBiDi->paras[pBiDi->paraCount-1].level=0;
                state=NOT_SEEKING_STRONG;
            }
            else if(state==SEEKING_STRONG_FOR_FSI) {
                if(stackLast<=UBIDI_MAX_EXPLICIT_LEVEL) {
                    /* no need for next statement, already set by default */
                    /* dirProps[isolateStartStack[stackLast]]=LRI; */
                    flags|=DIRPROP_FLAG(LRI);
                }
                state=LOOKING_FOR_PDI;
            }
            lastStrong=L;
            continue;
        }
        if(dirProp==R || dirProp==AL) {
            if(state==SEEKING_STRONG_FOR_PARA) {
                pBiDi->paras[pBiDi->paraCount-1].level=1;
                state=NOT_SEEKING_STRONG;
            }
            else if(state==SEEKING_STRONG_FOR_FSI) {
                if(stackLast<=UBIDI_MAX_EXPLICIT_LEVEL) {
                    dirProps[isolateStartStack[stackLast]]=RLI;
                    flags|=DIRPROP_FLAG(RLI);
                }
                state=LOOKING_FOR_PDI;
            }
            lastStrong=R;
            if(dirProp==AL)
                lastArabicPos=i-1;
            continue;
        }
        if(dirProp>=FSI && dirProp<=RLI) {  /* FSI, LRI or RLI */
            stackLast++;
            if(stackLast<=UBIDI_MAX_EXPLICIT_LEVEL) {
                isolateStartStack[stackLast]=i-1;
                previousStateStack[stackLast]=state;
            }
            if(dirProp==FSI) {
                dirProps[i-1]=LRI;      /* default if no strong char */
                state=SEEKING_STRONG_FOR_FSI;
            }
            else
                state=LOOKING_FOR_PDI;
            continue;
        }
        if(dirProp==PDI) {
            if(state==SEEKING_STRONG_FOR_FSI) {
                if(stackLast<=UBIDI_MAX_EXPLICIT_LEVEL) {
                    /* no need for next statement, already set by default */
                    /* dirProps[isolateStartStack[stackLast]]=LRI; */
                    flags|=DIRPROP_FLAG(LRI);
                }
            }
            if(stackLast>=0) {
                if(stackLast<=UBIDI_MAX_EXPLICIT_LEVEL)
                    state=previousStateStack[stackLast];
                stackLast--;
            }
            continue;
        }
        if(dirProp==B) {
            if(i<originalLength && uchar==CR && text[i]==LF) /* do nothing on the CR */
                continue;
            pBiDi->paras[pBiDi->paraCount-1].limit=i;
            if(isDefaultLevelInverse && lastStrong==R)
                pBiDi->paras[pBiDi->paraCount-1].level=1;
            if(pBiDi->reorderingOptions & UBIDI_OPTION_STREAMING) {
                /* When streaming, we only process whole paragraphs
                   thus some updates are only done on paragraph boundaries */
                pBiDi->length=i;        /* i is index to next character */
                pBiDi->controlCount=controlCount;
            }
            if(i<originalLength) {              /* B not last char in text */
                pBiDi->paraCount++;
                if(checkParaCount(pBiDi)==false)    /* not enough memory for a new para entry */
                    return false;
                if(isDefaultLevel) {
                    pBiDi->paras[pBiDi->paraCount-1].level=defaultParaLevel;
                    state=SEEKING_STRONG_FOR_PARA;
                    lastStrong=defaultParaLevel;
                } else {
                    pBiDi->paras[pBiDi->paraCount-1].level=pBiDi->paraLevel;
                    state=NOT_SEEKING_STRONG;
                }
                stackLast=-1;
            }
            continue;
        }
    }
    /* Ignore still open isolate sequences with overflow */
    if(stackLast>UBIDI_MAX_EXPLICIT_LEVEL) {
        stackLast=UBIDI_MAX_EXPLICIT_LEVEL;
        state=SEEKING_STRONG_FOR_FSI;   /* to be on the safe side */
    }
    /* Resolve direction of still unresolved open FSI sequences */
    while(stackLast>=0) {
        if(state==SEEKING_STRONG_FOR_FSI) {
            /* no need for next statement, already set by default */
            /* dirProps[isolateStartStack[stackLast]]=LRI; */
            flags|=DIRPROP_FLAG(LRI);
            break;
        }
        state=previousStateStack[stackLast];
        stackLast--;
    }
    /* When streaming, ignore text after the last paragraph separator */
    if(pBiDi->reorderingOptions & UBIDI_OPTION_STREAMING) {
        if(pBiDi->length<originalLength)
            pBiDi->paraCount--;
    } else {
        pBiDi->paras[pBiDi->paraCount-1].limit=originalLength;
        pBiDi->controlCount=controlCount;
    }
    /* For inverse bidi, default para direction is RTL if there is
       a strong R or AL at either end of the paragraph */
    if(isDefaultLevelInverse && lastStrong==R) {
        pBiDi->paras[pBiDi->paraCount-1].level=1;
    }
    if(isDefaultLevel) {
        pBiDi->paraLevel=static_cast<UBiDiLevel>(pBiDi->paras[0].level);
    }
    /* The following is needed to resolve the text direction for default level
       paragraphs containing no strong character */
    for(i=0; i<pBiDi->paraCount; i++)
        flags|=DIRPROP_FLAG_LR(pBiDi->paras[i].level);

    if(pBiDi->orderParagraphsLTR && (flags&DIRPROP_FLAG(B))) {
        flags|=DIRPROP_FLAG(L);
    }
    pBiDi->flags=flags;
    pBiDi->lastArabicPos=lastArabicPos;
    return true;
}

/* determine the paragraph level at position index */
U_CFUNC UBiDiLevel
ubidi_getParaLevelAtIndex(const UBiDi *pBiDi, int32_t pindex) {
    int32_t i;
    for(i=0; i<pBiDi->paraCount; i++)
        if(pindex<pBiDi->paras[i].limit)
            break;
    if(i>=pBiDi->paraCount)
        i=pBiDi->paraCount-1;
    return (UBiDiLevel)(pBiDi->paras[i].level);
}

/* Functions for handling paired brackets ----------------------------------- */

/* In the isoRuns array, the first entry is used for text outside of any
   isolate sequence.  Higher entries are used for each more deeply nested
   isolate sequence. isoRunLast is the index of the last used entry.  The
   openings array is used to note the data of opening brackets not yet
   matched by a closing bracket, or matched but still susceptible to change
   level.
   Each isoRun entry contains the index of the first and
   one-after-last openings entries for pending opening brackets it
   contains.  The next openings entry to use is the one-after-last of the
   most deeply nested isoRun entry.
   isoRun entries also contain their current embedding level and the last
   encountered strong character, since these will be needed to resolve
   the level of paired brackets.  */

static void
bracketInit(UBiDi *pBiDi, BracketData *bd) {
    bd->pBiDi=pBiDi;
    bd->isoRunLast=0;
    bd->isoRuns[0].start=0;
    bd->isoRuns[0].limit=0;
    bd->isoRuns[0].level=GET_PARALEVEL(pBiDi, 0);
    UBiDiLevel t = GET_PARALEVEL(pBiDi, 0) & 1;
    bd->isoRuns[0].lastStrong = bd->isoRuns[0].lastBase = t;
    bd->isoRuns[0].contextDir = static_cast<UBiDiDirection>(t);
    bd->isoRuns[0].contextPos=0;
    if(pBiDi->openingsMemory) {
        bd->openings=pBiDi->openingsMemory;
        bd->openingsCount=pBiDi->openingsSize / sizeof(Opening);
    } else {
        bd->openings=bd->simpleOpenings;
        bd->openingsCount=SIMPLE_OPENINGS_COUNT;
    }
    bd->isNumbersSpecial=bd->pBiDi->reorderingMode==UBIDI_REORDER_NUMBERS_SPECIAL ||
                         bd->pBiDi->reorderingMode==UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL;
}

/* paragraph boundary */
static void
bracketProcessB(BracketData *bd, UBiDiLevel level) {
    bd->isoRunLast=0;
    bd->isoRuns[0].limit=0;
    bd->isoRuns[0].level=level;
    bd->isoRuns[0].lastStrong=bd->isoRuns[0].lastBase=level&1;
    bd->isoRuns[0].contextDir = static_cast<UBiDiDirection>(level & 1);
    bd->isoRuns[0].contextPos=0;
}

/* LRE, LRO, RLE, RLO, PDF */
static void
bracketProcessBoundary(BracketData *bd, int32_t lastCcPos,
                       UBiDiLevel contextLevel, UBiDiLevel embeddingLevel) {
    IsoRun *pLastIsoRun=&bd->isoRuns[bd->isoRunLast];
    DirProp *dirProps=bd->pBiDi->dirProps;
    if(DIRPROP_FLAG(dirProps[lastCcPos])&MASK_ISO)  /* after an isolate */
        return;
    if(NO_OVERRIDE(embeddingLevel)>NO_OVERRIDE(contextLevel))   /* not a PDF */
        contextLevel=embeddingLevel;
    pLastIsoRun->limit=pLastIsoRun->start;
    pLastIsoRun->level=embeddingLevel;
    pLastIsoRun->lastStrong=pLastIsoRun->lastBase=contextLevel&1;
    pLastIsoRun->contextDir = static_cast<UBiDiDirection>(contextLevel & 1);
    pLastIsoRun->contextPos = static_cast<UBiDiDirection>(lastCcPos);
}

/* LRI or RLI */
static void
bracketProcessLRI_RLI(BracketData *bd, UBiDiLevel level) {
    IsoRun *pLastIsoRun=&bd->isoRuns[bd->isoRunLast];
    int16_t lastLimit;
    pLastIsoRun->lastBase=ON;
    lastLimit=pLastIsoRun->limit;
    bd->isoRunLast++;
    pLastIsoRun++;
    pLastIsoRun->start=pLastIsoRun->limit=lastLimit;
    pLastIsoRun->level=level;
    pLastIsoRun->lastStrong=pLastIsoRun->lastBase=level&1;
    pLastIsoRun->contextDir = static_cast<UBiDiDirection>(level & 1);
    pLastIsoRun->contextPos=0;
}

/* PDI */
static void
bracketProcessPDI(BracketData *bd) {
    IsoRun *pLastIsoRun;
    bd->isoRunLast--;
    pLastIsoRun=&bd->isoRuns[bd->isoRunLast];
    pLastIsoRun->lastBase=ON;
}

/* newly found opening bracket: create an openings entry */
static UBool                            /* return true if success */
bracketAddOpening(BracketData *bd, char16_t match, int32_t position) {
    IsoRun *pLastIsoRun=&bd->isoRuns[bd->isoRunLast];
    Opening *pOpening;
    if(pLastIsoRun->limit>=bd->openingsCount) {  /* no available new entry */
        UBiDi *pBiDi=bd->pBiDi;
        if(!getInitialOpeningsMemory(pBiDi, pLastIsoRun->limit * 2))
            return false;
        if(bd->openings==bd->simpleOpenings)
            uprv_memcpy(pBiDi->openingsMemory, bd->simpleOpenings,
                        SIMPLE_OPENINGS_COUNT * sizeof(Opening));
        bd->openings=pBiDi->openingsMemory;     /* may have changed */
        bd->openingsCount=pBiDi->openingsSize / sizeof(Opening);
    }
    pOpening=&bd->openings[pLastIsoRun->limit];
    pOpening->position=position;
    pOpening->match=match;
    pOpening->contextDir=pLastIsoRun->contextDir;
    pOpening->contextPos=pLastIsoRun->contextPos;
    pOpening->flags=0;
    pLastIsoRun->limit++;
    return true;
}

/* change N0c1 to N0c2 when a preceding bracket is assigned the embedding level */
static void
fixN0c(BracketData *bd, int32_t openingIndex, int32_t newPropPosition, DirProp newProp) {
    /* This function calls itself recursively */
    IsoRun *pLastIsoRun=&bd->isoRuns[bd->isoRunLast];
    Opening *qOpening;
    DirProp *dirProps=bd->pBiDi->dirProps;
    int32_t k, openingPosition, closingPosition;
    for(k=openingIndex+1, qOpening=&bd->openings[k]; k<pLastIsoRun->limit; k++, qOpening++) {
        if(qOpening->match>=0)      /* not an N0c match */
            continue;
        if(newPropPosition<qOpening->contextPos)
            break;
        if(newPropPosition>=qOpening->position)
            continue;
        if(newProp==qOpening->contextDir)
            break;
        openingPosition=qOpening->position;
        dirProps[openingPosition]=newProp;
        closingPosition=-(qOpening->match);
        dirProps[closingPosition]=newProp;
        qOpening->match=0;                      /* prevent further changes */
        fixN0c(bd, k, openingPosition, newProp);
        fixN0c(bd, k, closingPosition, newProp);
    }
}

/* process closing bracket */
static DirProp              /* return L or R if N0b or N0c, ON if N0d */
bracketProcessClosing(BracketData *bd, int32_t openIdx, int32_t position) {
    IsoRun *pLastIsoRun=&bd->isoRuns[bd->isoRunLast];
    Opening *pOpening, *qOpening;
    UBiDiDirection direction;
    UBool stable;
    DirProp newProp;
    pOpening=&bd->openings[openIdx];
    direction = static_cast<UBiDiDirection>(pLastIsoRun->level & 1);
    stable=true;            /* assume stable until proved otherwise */

    /* The stable flag is set when brackets are paired and their
       level is resolved and cannot be changed by what will be
       found later in the source string.
       An unstable match can occur only when applying N0c, where
       the resolved level depends on the preceding context, and
       this context may be affected by text occurring later.
       Example: RTL paragraph containing:  abc[(latin) HEBREW]
       When the closing parenthesis is encountered, it appears
       that N0c1 must be applied since 'abc' sets an opposite
       direction context and both parentheses receive level 2.
       However, when the closing square bracket is processed,
       N0b applies because of 'HEBREW' being included within the
       brackets, thus the square brackets are treated like R and
       receive level 1. However, this changes the preceding
       context of the opening parenthesis, and it now appears
       that N0c2 must be applied to the parentheses rather than
       N0c1. */

    if((direction==0 && pOpening->flags&FOUND_L) ||
       (direction==1 && pOpening->flags&FOUND_R)) {                         /* N0b */
        newProp=static_cast<DirProp>(direction);
    }
    else if(pOpening->flags&(FOUND_L|FOUND_R)) {                            /* N0c */
        /* it is stable if there is no containing pair or in
           conditions too complicated and not worth checking */
        stable=(openIdx==pLastIsoRun->start);
        if(direction!=pOpening->contextDir)
            newProp= static_cast<DirProp>(pOpening->contextDir);           /* N0c1 */
        else
            newProp= static_cast<DirProp>(direction);                      /* N0c2 */
    } else {
        /* forget this and any brackets nested within this pair */
        pLastIsoRun->limit= static_cast<uint16_t>(openIdx);
        return ON;                                                          /* N0d */
    }
    bd->pBiDi->dirProps[pOpening->position]=newProp;
    bd->pBiDi->dirProps[position]=newProp;
    /* Update nested N0c pairs that may be affected */
    fixN0c(bd, openIdx, pOpening->position, newProp);
    if(stable) {
        pLastIsoRun->limit= static_cast<uint16_t>(openIdx); /* forget any brackets nested within this pair */
        /* remove lower located synonyms if any */
        while(pLastIsoRun->limit>pLastIsoRun->start &&
              bd->openings[pLastIsoRun->limit-1].position==pOpening->position)
            pLastIsoRun->limit--;
    } else {
        int32_t k;
        pOpening->match=-position;
        /* neutralize lower located synonyms if any */
        k=openIdx-1;
        while(k>=pLastIsoRun->start &&
              bd->openings[k].position==pOpening->position)
            bd->openings[k--].match=0;
        /* neutralize any unmatched opening between the current pair;
           this will also neutralize higher located synonyms if any */
        for(k=openIdx+1; k<pLastIsoRun->limit; k++) {
            qOpening=&bd->openings[k];
            if(qOpening->position>=position)
                break;
            if(qOpening->match>0)
                qOpening->match=0;
        }
    }
    return newProp;
}

/* handle strong characters, digits and candidates for closing brackets */
static UBool                            /* return true if success */
bracketProcessChar(BracketData *bd, int32_t position) {
    IsoRun *pLastIsoRun=&bd->isoRuns[bd->isoRunLast];
    DirProp *dirProps, dirProp, newProp;
    UBiDiLevel level;
    dirProps=bd->pBiDi->dirProps;
    dirProp=dirProps[position];
    if(dirProp==ON) {
        char16_t c, match;
        int32_t idx;
        /* First see if it is a matching closing bracket. Hopefully, this is
           more efficient than checking if it is a closing bracket at all */
        c=bd->pBiDi->text[position];
        for(idx=pLastIsoRun->limit-1; idx>=pLastIsoRun->start; idx--) {
            if(bd->openings[idx].match!=c)
                continue;
            /* We have a match */
            newProp=bracketProcessClosing(bd, idx, position);
            if(newProp==ON) {           /* N0d */
                c=0;        /* prevent handling as an opening */
                break;
            }
            pLastIsoRun->lastBase=ON;
            pLastIsoRun->contextDir = static_cast<UBiDiDirection>(newProp);
            pLastIsoRun->contextPos=position;
            level=bd->pBiDi->levels[position];
            if(level&UBIDI_LEVEL_OVERRIDE) {    /* X4, X5 */
                uint16_t flag;
                int32_t i;
                newProp=level&1;
                pLastIsoRun->lastStrong=newProp;
                flag=DIRPROP_FLAG(newProp);
                for(i=pLastIsoRun->start; i<idx; i++)
                    bd->openings[i].flags|=flag;
                /* matching brackets are not overridden by LRO/RLO */
                bd->pBiDi->levels[position]&=~UBIDI_LEVEL_OVERRIDE;
            }
            /* matching brackets are not overridden by LRO/RLO */
            bd->pBiDi->levels[bd->openings[idx].position]&=~UBIDI_LEVEL_OVERRIDE;
            return true;
        }
        /* We get here only if the ON character is not a matching closing
           bracket or it is a case of N0d */
        /* Now see if it is an opening bracket */
        if(c)
            match= static_cast<char16_t>(u_getBidiPairedBracket(c));    /* get the matching char */
        else
            match=0;
        if(match!=c &&                  /* has a matching char */
           ubidi_getPairedBracketType(c)==U_BPT_OPEN) { /* opening bracket */
            /* special case: process synonyms
               create an opening entry for each synonym */
            if(match==0x232A) {     /* RIGHT-POINTING ANGLE BRACKET */
                if(!bracketAddOpening(bd, 0x3009, position))
                    return false;
            }
            else if(match==0x3009) {         /* RIGHT ANGLE BRACKET */
                if(!bracketAddOpening(bd, 0x232A, position))
                    return false;
            }
            if(!bracketAddOpening(bd, match, position))
                return false;
        }
    }
    level=bd->pBiDi->levels[position];
    if(level&UBIDI_LEVEL_OVERRIDE) {    /* X4, X5 */
        newProp=level&1;
        if(dirProp!=S && dirProp!=WS && dirProp!=ON)
            dirProps[position]=newProp;
        pLastIsoRun->lastBase=newProp;
        pLastIsoRun->lastStrong=newProp;
        pLastIsoRun->contextDir = static_cast<UBiDiDirection>(newProp);
        pLastIsoRun->contextPos=position;
    }
    else if(dirProp<=R || dirProp==AL) {
        newProp= static_cast<DirProp>(DIR_FROM_STRONG(dirProp));
        pLastIsoRun->lastBase=dirProp;
        pLastIsoRun->lastStrong=dirProp;
        pLastIsoRun->contextDir = static_cast<UBiDiDirection>(newProp);
        pLastIsoRun->contextPos=position;
    }
    else if(dirProp==EN) {
        pLastIsoRun->lastBase=EN;
        if(pLastIsoRun->lastStrong==L) {
            newProp=L;                  /* W7 */
            if(!bd->isNumbersSpecial)
                dirProps[position]=ENL;
            pLastIsoRun->contextDir = static_cast<UBiDiDirection>(L);
            pLastIsoRun->contextPos=position;
        }
        else {
            newProp=R;                  /* N0 */
            if(pLastIsoRun->lastStrong==AL)
                dirProps[position]=AN;  /* W2 */
            else
                dirProps[position]=ENR;
            pLastIsoRun->contextDir = static_cast<UBiDiDirection>(R);
            pLastIsoRun->contextPos=position;
        }
    }
    else if(dirProp==AN) {
        newProp=R;                      /* N0 */
        pLastIsoRun->lastBase=AN;
        pLastIsoRun->contextDir = static_cast<UBiDiDirection>(R);
        pLastIsoRun->contextPos=position;
    }
    else if(dirProp==NSM) {
        /* if the last real char was ON, change NSM to ON so that it
           will stay ON even if the last real char is a bracket which
           may be changed to L or R */
        newProp=pLastIsoRun->lastBase;
        if(newProp==ON)
            dirProps[position]=newProp;
    }
    else {
        newProp=dirProp;
        pLastIsoRun->lastBase=dirProp;
    }
    if(newProp<=R || newProp==AL) {
        int32_t i;
        uint16_t flag=DIRPROP_FLAG(DIR_FROM_STRONG(newProp));
        for(i=pLastIsoRun->start; i<pLastIsoRun->limit; i++)
            if(position>bd->openings[i].position)
                bd->openings[i].flags|=flag;
    }
    return true;
}

/* perform (X1)..(X9) ------------------------------------------------------- */

/* determine if the text is mixed-directional or single-directional */
static UBiDiDirection
directionFromFlags(UBiDi *pBiDi) {
    Flags flags=pBiDi->flags;
    /* if the text contains AN and neutrals, then some neutrals may become RTL */
    if(!(flags&MASK_RTL || ((flags&DIRPROP_FLAG(AN)) && (flags&MASK_POSSIBLE_N)))) {
        return UBIDI_LTR;
    } else if(!(flags&MASK_LTR)) {
        return UBIDI_RTL;
    } else {
        return UBIDI_MIXED;
    }
}

/*
 * Resolve the explicit levels as specified by explicit embedding codes.
 * Recalculate the flags to have them reflect the real properties
 * after taking the explicit embeddings into account.
 *
 * The BiDi algorithm is designed to result in the same behavior whether embedding
 * levels are externally specified (from "styled text", supposedly the preferred
 * method) or set by explicit embedding codes (LRx, RLx, PDF, FSI, PDI) in the plain text.
 * That is why (X9) instructs to remove all not-isolate explicit codes (and BN).
 * However, in a real implementation, the removal of these codes and their index
 * positions in the plain text is undesirable since it would result in
 * reallocated, reindexed text.
 * Instead, this implementation leaves the codes in there and just ignores them
 * in the subsequent processing.
 * In order to get the same reordering behavior, positions with a BN or a not-isolate
 * explicit embedding code just get the same level assigned as the last "real"
 * character.
 *
 * Some implementations, not this one, then overwrite some of these
 * directionality properties at "real" same-level-run boundaries by
 * L or R codes so that the resolution of weak types can be performed on the
 * entire paragraph at once instead of having to parse it once more and
 * perform that resolution on same-level-runs.
 * This limits the scope of the implicit rules in effectively
 * the same way as the run limits.
 *
 * Instead, this implementation does not modify these codes, except for
 * paired brackets whose properties (ON) may be replaced by L or R.
 * On one hand, the paragraph has to be scanned for same-level-runs, but
 * on the other hand, this saves another loop to reset these codes,
 * or saves making and modifying a copy of dirProps[].
 *
 *
 * Note that (Pn) and (Xn) changed significantly from version 4 of the BiDi algorithm.
 *
 *
 * Handling the stack of explicit levels (Xn):
 *
 * With the BiDi stack of explicit levels, as pushed with each
 * LRE, RLE, LRO, RLO, LRI, RLI and FSI and popped with each PDF and PDI,
 * the explicit level must never exceed UBIDI_MAX_EXPLICIT_LEVEL.
 *
 * In order to have a correct push-pop semantics even in the case of overflows,
 * overflow counters and a valid isolate counter are used as described in UAX#9
 * section 3.3.2 "Explicit Levels and Directions".
 *
 * This implementation assumes that UBIDI_MAX_EXPLICIT_LEVEL is odd.
 *
 * Returns normally the direction; -1 if there was a memory shortage
 *
 */
static UBiDiDirection
resolveExplicitLevels(UBiDi *pBiDi, UErrorCode *pErrorCode) {
    DirProp *dirProps=pBiDi->dirProps;
    UBiDiLevel *levels=pBiDi->levels;
    const char16_t *text=pBiDi->text;

    int32_t i=0, length=pBiDi->length;
    Flags flags=pBiDi->flags;       /* collect all directionalities in the text */
    DirProp dirProp;
    UBiDiLevel level=GET_PARALEVEL(pBiDi, 0);
    UBiDiDirection direction;
    pBiDi->isolateCount=0;

    if(U_FAILURE(*pErrorCode)) { return UBIDI_LTR; }

    /* determine if the text is mixed-directional or single-directional */
    direction=directionFromFlags(pBiDi);

    /* we may not need to resolve any explicit levels */
    if((direction!=UBIDI_MIXED)) {
        /* not mixed directionality: levels don't matter - trailingWSStart will be 0 */
        return direction;
    }
    if(pBiDi->reorderingMode > UBIDI_REORDER_LAST_LOGICAL_TO_VISUAL) {
        /* inverse BiDi: mixed, but all characters are at the same embedding level */
        /* set all levels to the paragraph level */
        int32_t paraIndex, start, limit;
        for(paraIndex=0; paraIndex<pBiDi->paraCount; paraIndex++) {
            if(paraIndex==0)
                start=0;
            else
                start=pBiDi->paras[paraIndex-1].limit;
            limit=pBiDi->paras[paraIndex].limit;
            level= static_cast<UBiDiLevel>(pBiDi->paras[paraIndex].level);
            for(i=start; i<limit; i++)
                levels[i]=level;
        }
        return direction;   /* no bracket matching for inverse BiDi */
    }
    if(!(flags&(MASK_EXPLICIT|MASK_ISO))) {
        /* no embeddings, set all levels to the paragraph level */
        /* we still have to perform bracket matching */
        int32_t paraIndex, start, limit;
        BracketData bracketData;
        bracketInit(pBiDi, &bracketData);
        for(paraIndex=0; paraIndex<pBiDi->paraCount; paraIndex++) {
            if(paraIndex==0)
                start=0;
            else
                start=pBiDi->paras[paraIndex-1].limit;
            limit=pBiDi->paras[paraIndex].limit;
            level= static_cast<UBiDiLevel>(pBiDi->paras[paraIndex].level);
            for(i=start; i<limit; i++) {
                levels[i]=level;
                dirProp=dirProps[i];
                if(dirProp==BN)
                    continue;
                if(dirProp==B) {
                    if((i+1)<length) {
                        if(text[i]==CR && text[i+1]==LF)
                            continue;   /* skip CR when followed by LF */
                        bracketProcessB(&bracketData, level);
                    }
                    continue;
                }
                if(!bracketProcessChar(&bracketData, i)) {
                    *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
                    return UBIDI_LTR;
                }
            }
        }
        return direction;
    }
    {
        /* continue to perform (Xn) */

        /* (X1) level is set for all codes, embeddingLevel keeps track of the push/pop operations */
        /* both variables may carry the UBIDI_LEVEL_OVERRIDE flag to indicate the override status */
        UBiDiLevel embeddingLevel=level, newLevel;
        UBiDiLevel previousLevel=level;     /* previous level for regular (not CC) characters */
        int32_t lastCcPos=0;                /* index of last effective LRx,RLx, PDx */

        /* The following stack remembers the embedding level and the ISOLATE flag of level runs.
           stackLast points to its current entry. */
        uint16_t stack[UBIDI_MAX_EXPLICIT_LEVEL+2];   /* we never push anything >=UBIDI_MAX_EXPLICIT_LEVEL
                                                        but we need one more entry as base */
        uint32_t stackLast=0;
        int32_t overflowIsolateCount=0;
        int32_t overflowEmbeddingCount=0;
        int32_t validIsolateCount=0;
        BracketData bracketData;
        bracketInit(pBiDi, &bracketData);
        stack[0]=level;     /* initialize base entry to para level, no override, no isolate */

        /* recalculate the flags */
        flags=0;

        for(i=0; i<length; ++i) {
            dirProp=dirProps[i];
            switch(dirProp) {
            case LRE:
            case RLE:
            case LRO:
            case RLO:
                /* (X2, X3, X4, X5) */
                flags|=DIRPROP_FLAG(BN);
                levels[i]=previousLevel;
                if (dirProp==LRE || dirProp==LRO)
                    /* least greater even level */
                    newLevel = static_cast<UBiDiLevel>((embeddingLevel + 2) & ~(UBIDI_LEVEL_OVERRIDE | 1));
                else
                    /* least greater odd level */
                    newLevel = static_cast<UBiDiLevel>((NO_OVERRIDE(embeddingLevel) + 1) | 1);
                if(newLevel<=UBIDI_MAX_EXPLICIT_LEVEL && overflowIsolateCount==0 &&
                                                         overflowEmbeddingCount==0) {
                    lastCcPos=i;
                    embeddingLevel=newLevel;
                    if(dirProp==LRO || dirProp==RLO)
                        embeddingLevel|=UBIDI_LEVEL_OVERRIDE;
                    stackLast++;
                    stack[stackLast]=embeddingLevel;
                    /* we don't need to set UBIDI_LEVEL_OVERRIDE off for LRE and RLE
                       since this has already been done for newLevel which is
                       the source for embeddingLevel.
                     */
                } else {
                    if(overflowIsolateCount==0)
                        overflowEmbeddingCount++;
                }
                break;
            case PDF:
                /* (X7) */
                flags|=DIRPROP_FLAG(BN);
                levels[i]=previousLevel;
                /* handle all the overflow cases first */
                if(overflowIsolateCount) {
                    break;
                }
                if(overflowEmbeddingCount) {
                    overflowEmbeddingCount--;
                    break;
                }
                if(stackLast>0 && stack[stackLast]<ISOLATE) {   /* not an isolate entry */
                    lastCcPos=i;
                    stackLast--;
                    embeddingLevel = static_cast<UBiDiLevel>(stack[stackLast]);
                }
                break;
            case LRI:
            case RLI:
                flags|=(DIRPROP_FLAG(ON)|DIRPROP_FLAG_LR(embeddingLevel));
                levels[i]=NO_OVERRIDE(embeddingLevel);
                if(NO_OVERRIDE(embeddingLevel)!=NO_OVERRIDE(previousLevel)) {
                    bracketProcessBoundary(&bracketData, lastCcPos,
                                           previousLevel, embeddingLevel);
                    flags|=DIRPROP_FLAG_MULTI_RUNS;
                }
                previousLevel=embeddingLevel;
                /* (X5a, X5b) */
                if(dirProp==LRI)
                    /* least greater even level */
                    newLevel = static_cast<UBiDiLevel>((embeddingLevel + 2) & ~(UBIDI_LEVEL_OVERRIDE | 1));
                else
                    /* least greater odd level */
                    newLevel = static_cast<UBiDiLevel>((NO_OVERRIDE(embeddingLevel) + 1) | 1);
                if(newLevel<=UBIDI_MAX_EXPLICIT_LEVEL && overflowIsolateCount==0 &&
                                                         overflowEmbeddingCount==0) {
                    flags|=DIRPROP_FLAG(dirProp);
                    lastCcPos=i;
                    validIsolateCount++;
                    if(validIsolateCount>pBiDi->isolateCount)
                        pBiDi->isolateCount=validIsolateCount;
                    embeddingLevel=newLevel;
                    /* we can increment stackLast without checking because newLevel
                       will exceed UBIDI_MAX_EXPLICIT_LEVEL before stackLast overflows */
                    stackLast++;
                    stack[stackLast]=embeddingLevel+ISOLATE;
                    bracketProcessLRI_RLI(&bracketData, embeddingLevel);
                } else {
                    /* make it WS so that it is handled by adjustWSLevels() */
                    dirProps[i]=WS;
                    overflowIsolateCount++;
                }
                break;
            case PDI:
                if(NO_OVERRIDE(embeddingLevel)!=NO_OVERRIDE(previousLevel)) {
                    bracketProcessBoundary(&bracketData, lastCcPos,
                                           previousLevel, embeddingLevel);
                    flags|=DIRPROP_FLAG_MULTI_RUNS;
                }
                /* (X6a) */
                if(overflowIsolateCount) {
                    overflowIsolateCount--;
                    /* make it WS so that it is handled by adjustWSLevels() */
                    dirProps[i]=WS;
                }
                else if(validIsolateCount) {
                    flags|=DIRPROP_FLAG(PDI);
                    lastCcPos=i;
                    overflowEmbeddingCount=0;
                    while(stack[stackLast]<ISOLATE) /* pop embedding entries */
                        stackLast--;                /* until the last isolate entry */
                    stackLast--;                    /* pop also the last isolate entry */
                    validIsolateCount--;
                    bracketProcessPDI(&bracketData);
                } else
                    /* make it WS so that it is handled by adjustWSLevels() */
                    dirProps[i]=WS;
                embeddingLevel = static_cast<UBiDiLevel>(stack[stackLast]) & ~ISOLATE;
                flags|=(DIRPROP_FLAG(ON)|DIRPROP_FLAG_LR(embeddingLevel));
                previousLevel=embeddingLevel;
                levels[i]=NO_OVERRIDE(embeddingLevel);
                break;
            case B:
                flags|=DIRPROP_FLAG(B);
                levels[i]=GET_PARALEVEL(pBiDi, i);
                if((i+1)<length) {
                    if(text[i]==CR && text[i+1]==LF)
                        break;          /* skip CR when followed by LF */
                    overflowEmbeddingCount=overflowIsolateCount=0;
                    validIsolateCount=0;
                    stackLast=0;
                    previousLevel=embeddingLevel=GET_PARALEVEL(pBiDi, i+1);
                    stack[0]=embeddingLevel; /* initialize base entry to para level, no override, no isolate */
                    bracketProcessB(&bracketData, embeddingLevel);
                }
                break;
            case BN:
                /* BN, LRE, RLE, and PDF are supposed to be removed (X9) */
                /* they will get their levels set correctly in adjustWSLevels() */
                levels[i]=previousLevel;
                flags|=DIRPROP_FLAG(BN);
                break;
            default:
                /* all other types are normal characters and get the "real" level */
                if(NO_OVERRIDE(embeddingLevel)!=NO_OVERRIDE(previousLevel)) {
                    bracketProcessBoundary(&bracketData, lastCcPos,
                                           previousLevel, embeddingLevel);
                    flags|=DIRPROP_FLAG_MULTI_RUNS;
                    if(embeddingLevel&UBIDI_LEVEL_OVERRIDE)
                        flags|=DIRPROP_FLAG_O(embeddingLevel);
                    else
                        flags|=DIRPROP_FLAG_E(embeddingLevel);
                }
                previousLevel=embeddingLevel;
                levels[i]=embeddingLevel;
                if(!bracketProcessChar(&bracketData, i))
                    return static_cast<UBiDiDirection>(-1);
                /* the dirProp may have been changed in bracketProcessChar() */
                flags|=DIRPROP_FLAG(dirProps[i]);
                break;
            }
        }
        if(flags&MASK_EMBEDDING)
            flags|=DIRPROP_FLAG_LR(pBiDi->paraLevel);
        if(pBiDi->orderParagraphsLTR && (flags&DIRPROP_FLAG(B)))
            flags|=DIRPROP_FLAG(L);
        /* again, determine if the text is mixed-directional or single-directional */
        pBiDi->flags=flags;
        direction=directionFromFlags(pBiDi);
    }
    return direction;
}

/*
 * Use a pre-specified embedding levels array:
 *
 * Adjust the directional properties for overrides (->LEVEL_OVERRIDE),
 * ignore all explicit codes (X9),
 * and check all the preset levels.
 *
 * Recalculate the flags to have them reflect the real properties
 * after taking the explicit embeddings into account.
 */
static UBiDiDirection
checkExplicitLevels(UBiDi *pBiDi, UErrorCode *pErrorCode) {
    DirProp *dirProps=pBiDi->dirProps;
    UBiDiLevel *levels=pBiDi->levels;
    int32_t isolateCount=0;

    int32_t length=pBiDi->length;
    Flags flags=0;  /* collect all directionalities in the text */
    pBiDi->isolateCount=0;

    int32_t currentParaIndex = 0;
    int32_t currentParaLimit = pBiDi->paras[0].limit;
    int32_t currentParaLevel = pBiDi->paraLevel;

    for(int32_t i=0; i<length; ++i) {
        UBiDiLevel level=levels[i];
        DirProp dirProp=dirProps[i];
        if(dirProp==LRI || dirProp==RLI) {
            isolateCount++;
            if(isolateCount>pBiDi->isolateCount)
                pBiDi->isolateCount=isolateCount;
        }
        else if(dirProp==PDI)
            isolateCount--;
        else if(dirProp==B)
            isolateCount=0;

        // optimized version of  int32_t currentParaLevel = GET_PARALEVEL(pBiDi, i);
        if (pBiDi->defaultParaLevel != 0 &&
                i == currentParaLimit && (currentParaIndex + 1) < pBiDi->paraCount) {
            currentParaLevel = pBiDi->paras[++currentParaIndex].level;
            currentParaLimit = pBiDi->paras[currentParaIndex].limit;
        }

        UBiDiLevel overrideFlag = level & UBIDI_LEVEL_OVERRIDE;
        level &= ~UBIDI_LEVEL_OVERRIDE;
        if (level < currentParaLevel || UBIDI_MAX_EXPLICIT_LEVEL < level) {
            if (level == 0) {
                if (dirProp == B) {
                    // Paragraph separators are ok with explicit level 0.
                    // Prevents reordering of paragraphs.
                } else {
                    // Treat explicit level 0 as a wildcard for the paragraph level.
                    // Avoid making the caller guess what the paragraph level would be.
                    level = static_cast<UBiDiLevel>(currentParaLevel);
                    levels[i] = level | overrideFlag;
                }
            } else {
                // 1 <= level < currentParaLevel or UBIDI_MAX_EXPLICIT_LEVEL < level
                /* level out of bounds */
                *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
                return UBIDI_LTR;
            }
        }
        if (overrideFlag != 0) {
            /* keep the override flag in levels[i] but adjust the flags */
            flags|=DIRPROP_FLAG_O(level);
        } else {
            /* set the flags */
            flags|=DIRPROP_FLAG_E(level)|DIRPROP_FLAG(dirProp);
        }
    }
    if(flags&MASK_EMBEDDING)
        flags|=DIRPROP_FLAG_LR(pBiDi->paraLevel);
    /* determine if the text is mixed-directional or single-directional */
    pBiDi->flags=flags;
    return directionFromFlags(pBiDi);
}

/******************************************************************
 The Properties state machine table
*******************************************************************

 All table cells are 8 bits:
      bits 0..4:  next state
      bits 5..7:  action to perform (if > 0)

 Cells may be of format "n" where n represents the next state
 (except for the rightmost column).
 Cells may also be of format "s(x,y)" where x represents an action
 to perform and y represents the next state.

*******************************************************************
 Definitions and type for properties state table
*******************************************************************
*/
#define IMPTABPROPS_COLUMNS 16
#define IMPTABPROPS_RES (IMPTABPROPS_COLUMNS - 1)
#define GET_STATEPROPS(cell) ((cell)&0x1f)
#define GET_ACTIONPROPS(cell) ((cell)>>5)
#define s(action, newState) ((uint8_t)(newState+(action<<5)))

static const uint8_t groupProp[] =          /* dirProp regrouped */
{
/*  L   R   EN  ES  ET  AN  CS  B   S   WS  ON  LRE LRO AL  RLE RLO PDF NSM BN  FSI LRI RLI PDI ENL ENR */
    0,  1,  2,  7,  8,  3,  9,  6,  5,  4,  4,  10, 10, 12, 10, 10, 10, 11, 10, 4,  4,  4,  4,  13, 14
};
enum { DirProp_L=0, DirProp_R=1, DirProp_EN=2, DirProp_AN=3, DirProp_ON=4, DirProp_S=5, DirProp_B=6 }; /* reduced dirProp */

/******************************************************************

      PROPERTIES  STATE  TABLE

 In table impTabProps,
      - the ON column regroups ON and WS, FSI, RLI, LRI and PDI
      - the BN column regroups BN, LRE, RLE, LRO, RLO, PDF
      - the Res column is the reduced property assigned to a run

 Action 1: process current run1, init new run1
        2: init new run2
        3: process run1, process run2, init new run1
        4: process run1, set run1=run2, init new run2

 Notes:
  1) This table is used in resolveImplicitLevels().
  2) This table triggers actions when there is a change in the Bidi
     property of incoming characters (action 1).
  3) Most such property sequences are processed immediately (in
     fact, passed to processPropertySeq().
  4) However, numbers are assembled as one sequence. This means
     that undefined situations (like CS following digits, until
     it is known if the next char will be a digit) are held until
     following chars define them.
     Example: digits followed by CS, then comes another CS or ON;
              the digits will be processed, then the CS assigned
              as the start of an ON sequence (action 3).
  5) There are cases where more than one sequence must be
     processed, for instance digits followed by CS followed by L:
     the digits must be processed as one sequence, and the CS
     must be processed as an ON sequence, all this before starting
     assembling chars for the opening L sequence.


*/
static const uint8_t impTabProps[][IMPTABPROPS_COLUMNS] =
{
/*                        L ,     R ,    EN ,    AN ,    ON ,     S ,     B ,    ES ,    ET ,    CS ,    BN ,   NSM ,    AL ,   ENL ,   ENR , Res */
/* 0 Init        */ {     1 ,     2 ,     4 ,     5 ,     7 ,    15 ,    17 ,     7 ,     9 ,     7 ,     0 ,     7 ,     3 ,    18 ,    21 , DirProp_ON },
/* 1 L           */ {     1 , s(1,2), s(1,4), s(1,5), s(1,7),s(1,15),s(1,17), s(1,7), s(1,9), s(1,7),     1 ,     1 , s(1,3),s(1,18),s(1,21),  DirProp_L },
/* 2 R           */ { s(1,1),     2 , s(1,4), s(1,5), s(1,7),s(1,15),s(1,17), s(1,7), s(1,9), s(1,7),     2 ,     2 , s(1,3),s(1,18),s(1,21),  DirProp_R },
/* 3 AL          */ { s(1,1), s(1,2), s(1,6), s(1,6), s(1,8),s(1,16),s(1,17), s(1,8), s(1,8), s(1,8),     3 ,     3 ,     3 ,s(1,18),s(1,21),  DirProp_R },
/* 4 EN          */ { s(1,1), s(1,2),     4 , s(1,5), s(1,7),s(1,15),s(1,17),s(2,10),    11 ,s(2,10),     4 ,     4 , s(1,3),    18 ,    21 , DirProp_EN },
/* 5 AN          */ { s(1,1), s(1,2), s(1,4),     5 , s(1,7),s(1,15),s(1,17), s(1,7), s(1,9),s(2,12),     5 ,     5 , s(1,3),s(1,18),s(1,21), DirProp_AN },
/* 6 AL:EN/AN    */ { s(1,1), s(1,2),     6 ,     6 , s(1,8),s(1,16),s(1,17), s(1,8), s(1,8),s(2,13),     6 ,     6 , s(1,3),    18 ,    21 , DirProp_AN },
/* 7 ON          */ { s(1,1), s(1,2), s(1,4), s(1,5),     7 ,s(1,15),s(1,17),     7 ,s(2,14),     7 ,     7 ,     7 , s(1,3),s(1,18),s(1,21), DirProp_ON },
/* 8 AL:ON       */ { s(1,1), s(1,2), s(1,6), s(1,6),     8 ,s(1,16),s(1,17),     8 ,     8 ,     8 ,     8 ,     8 , s(1,3),s(1,18),s(1,21), DirProp_ON },
/* 9 ET          */ { s(1,1), s(1,2),     4 , s(1,5),     7 ,s(1,15),s(1,17),     7 ,     9 ,     7 ,     9 ,     9 , s(1,3),    18 ,    21 , DirProp_ON },
/*10 EN+ES/CS    */ { s(3,1), s(3,2),     4 , s(3,5), s(4,7),s(3,15),s(3,17), s(4,7),s(4,14), s(4,7),    10 , s(4,7), s(3,3),    18 ,    21 , DirProp_EN },
/*11 EN+ET       */ { s(1,1), s(1,2),     4 , s(1,5), s(1,7),s(1,15),s(1,17), s(1,7),    11 , s(1,7),    11 ,    11 , s(1,3),    18 ,    21 , DirProp_EN },
/*12 AN+CS       */ { s(3,1), s(3,2), s(3,4),     5 , s(4,7),s(3,15),s(3,17), s(4,7),s(4,14), s(4,7),    12 , s(4,7), s(3,3),s(3,18),s(3,21), DirProp_AN },
/*13 AL:EN/AN+CS */ { s(3,1), s(3,2),     6 ,     6 , s(4,8),s(3,16),s(3,17), s(4,8), s(4,8), s(4,8),    13 , s(4,8), s(3,3),    18 ,    21 , DirProp_AN },
/*14 ON+ET       */ { s(1,1), s(1,2), s(4,4), s(1,5),     7 ,s(1,15),s(1,17),     7 ,    14 ,     7 ,    14 ,    14 , s(1,3),s(4,18),s(4,21), DirProp_ON },
/*15 S           */ { s(1,1), s(1,2), s(1,4), s(1,5), s(1,7),    15 ,s(1,17), s(1,7), s(1,9), s(1,7),    15 , s(1,7), s(1,3),s(1,18),s(1,21),  DirProp_S },
/*16 AL:S        */ { s(1,1), s(1,2), s(1,6), s(1,6), s(1,8),    16 ,s(1,17), s(1,8), s(1,8), s(1,8),    16 , s(1,8), s(1,3),s(1,18),s(1,21),  DirProp_S },
/*17 B           */ { s(1,1), s(1,2), s(1,4), s(1,5), s(1,7),s(1,15),    17 , s(1,7), s(1,9), s(1,7),    17 , s(1,7), s(1,3),s(1,18),s(1,21),  DirProp_B },
/*18 ENL         */ { s(1,1), s(1,2),    18 , s(1,5), s(1,7),s(1,15),s(1,17),s(2,19),    20 ,s(2,19),    18 ,    18 , s(1,3),    18 ,    21 ,  DirProp_L },
/*19 ENL+ES/CS   */ { s(3,1), s(3,2),    18 , s(3,5), s(4,7),s(3,15),s(3,17), s(4,7),s(4,14), s(4,7),    19 , s(4,7), s(3,3),    18 ,    21 ,  DirProp_L },
/*20 ENL+ET      */ { s(1,1), s(1,2),    18 , s(1,5), s(1,7),s(1,15),s(1,17), s(1,7),    20 , s(1,7),    20 ,    20 , s(1,3),    18 ,    21 ,  DirProp_L },
/*21 ENR         */ { s(1,1), s(1,2),    21 , s(1,5), s(1,7),s(1,15),s(1,17),s(2,22),    23 ,s(2,22),    21 ,    21 , s(1,3),    18 ,    21 , DirProp_AN },
/*22 ENR+ES/CS   */ { s(3,1), s(3,2),    21 , s(3,5), s(4,7),s(3,15),s(3,17), s(4,7),s(4,14), s(4,7),    22 , s(4,7), s(3,3),    18 ,    21 , DirProp_AN },
/*23 ENR+ET      */ { s(1,1), s(1,2),    21 , s(1,5), s(1,7),s(1,15),s(1,17), s(1,7),    23 , s(1,7),    23 ,    23 , s(1,3),    18 ,    21 , DirProp_AN }
};

/*  we must undef macro s because the levels tables have a different
 *  structure (4 bits for action and 4 bits for next state.
 */
#undef s

/******************************************************************
 The levels state machine tables
*******************************************************************

 All table cells are 8 bits:
      bits 0..3:  next state
      bits 4..7:  action to perform (if > 0)

 Cells may be of format "n" where n represents the next state
 (except for the rightmost column).
 Cells may also be of format "s(x,y)" where x represents an action
 to perform and y represents the next state.

 This format limits each table to 16 states each and to 15 actions.

*******************************************************************
 Definitions and type for levels state tables
*******************************************************************
*/
#define IMPTABLEVELS_COLUMNS (DirProp_B + 2)
#define IMPTABLEVELS_RES (IMPTABLEVELS_COLUMNS - 1)
#define GET_STATE(cell) ((cell)&0x0f)
#define GET_ACTION(cell) ((cell)>>4)
#define s(action, newState) ((uint8_t)(newState+(action<<4)))

typedef uint8_t ImpTab[][IMPTABLEVELS_COLUMNS];
typedef uint8_t ImpAct[];

/* FOOD FOR THOUGHT: each ImpTab should have its associated ImpAct,
 * instead of having a pair of ImpTab and a pair of ImpAct.
 */
typedef struct ImpTabPair {
    const void * pImpTab[2];
    const void * pImpAct[2];
} ImpTabPair;

/******************************************************************

      LEVELS  STATE  TABLES

 In all levels state tables,
      - state 0 is the initial state
      - the Res column is the increment to add to the text level
        for this property sequence.

 The impAct arrays for each table of a pair map the local action
 numbers of the table to the total list of actions. For instance,
 action 2 in a given table corresponds to the action number which
 appears in entry [2] of the impAct array for that table.
 The first entry of all impAct arrays must be 0.

 Action 1: init conditional sequence
        2: prepend conditional sequence to current sequence
        3: set ON sequence to new level - 1
        4: init EN/AN/ON sequence
        5: fix EN/AN/ON sequence followed by R
        6: set previous level sequence to level 2

 Notes:
  1) These tables are used in processPropertySeq(). The input
     is property sequences as determined by resolveImplicitLevels.
  2) Most such property sequences are processed immediately
     (levels are assigned).
  3) However, some sequences cannot be assigned a final level till
     one or more following sequences are received. For instance,
     ON following an R sequence within an even-level paragraph.
     If the following sequence is R, the ON sequence will be
     assigned basic run level+1, and so will the R sequence.
  4) S is generally handled like ON, since its level will be fixed
     to paragraph level in adjustWSLevels().

*/

static const ImpTab impTabL_DEFAULT =   /* Even paragraph level */
/*  In this table, conditional sequences receive the lower possible level
    until proven otherwise.
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ {     0 ,     1 ,     0 ,     2 ,     0 ,     0 ,     0 ,  0 },
/* 1 : R          */ {     0 ,     1 ,     3 ,     3 , s(1,4), s(1,4),     0 ,  1 },
/* 2 : AN         */ {     0 ,     1 ,     0 ,     2 , s(1,5), s(1,5),     0 ,  2 },
/* 3 : R+EN/AN    */ {     0 ,     1 ,     3 ,     3 , s(1,4), s(1,4),     0 ,  2 },
/* 4 : R+ON       */ {     0 , s(2,1), s(3,3), s(3,3),     4 ,     4 ,     0 ,  0 },
/* 5 : AN+ON      */ {     0 , s(2,1),     0 , s(3,2),     5 ,     5 ,     0 ,  0 }
};
static const ImpTab impTabR_DEFAULT =   /* Odd  paragraph level */
/*  In this table, conditional sequences receive the lower possible level
    until proven otherwise.
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ {     1 ,     0 ,     2 ,     2 ,     0 ,     0 ,     0 ,  0 },
/* 1 : L          */ {     1 ,     0 ,     1 ,     3 , s(1,4), s(1,4),     0 ,  1 },
/* 2 : EN/AN      */ {     1 ,     0 ,     2 ,     2 ,     0 ,     0 ,     0 ,  1 },
/* 3 : L+AN       */ {     1 ,     0 ,     1 ,     3 ,     5 ,     5 ,     0 ,  1 },
/* 4 : L+ON       */ { s(2,1),     0 , s(2,1),     3 ,     4 ,     4 ,     0 ,  0 },
/* 5 : L+AN+ON    */ {     1 ,     0 ,     1 ,     3 ,     5 ,     5 ,     0 ,  0 }
};
static const ImpAct impAct0 = {0,1,2,3,4};
static const ImpTabPair impTab_DEFAULT = {{&impTabL_DEFAULT,
                                           &impTabR_DEFAULT},
                                          {&impAct0, &impAct0}};

static const ImpTab impTabL_NUMBERS_SPECIAL =   /* Even paragraph level */
/*  In this table, conditional sequences receive the lower possible level
    until proven otherwise.
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ {     0 ,     2 , s(1,1), s(1,1),     0 ,     0 ,     0 ,  0 },
/* 1 : L+EN/AN    */ {     0 , s(4,2),     1 ,     1 ,     0 ,     0 ,     0 ,  0 },
/* 2 : R          */ {     0 ,     2 ,     4 ,     4 , s(1,3), s(1,3),     0 ,  1 },
/* 3 : R+ON       */ {     0 , s(2,2), s(3,4), s(3,4),     3 ,     3 ,     0 ,  0 },
/* 4 : R+EN/AN    */ {     0 ,     2 ,     4 ,     4 , s(1,3), s(1,3),     0 ,  2 }
};
static const ImpTabPair impTab_NUMBERS_SPECIAL = {{&impTabL_NUMBERS_SPECIAL,
                                                   &impTabR_DEFAULT},
                                                  {&impAct0, &impAct0}};

static const ImpTab impTabL_GROUP_NUMBERS_WITH_R =
/*  In this table, EN/AN+ON sequences receive levels as if associated with R
    until proven that there is L or sor/eor on both sides. AN is handled like EN.
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 init         */ {     0 ,     3 , s(1,1), s(1,1),     0 ,     0 ,     0 ,  0 },
/* 1 EN/AN        */ { s(2,0),     3 ,     1 ,     1 ,     2 , s(2,0), s(2,0),  2 },
/* 2 EN/AN+ON     */ { s(2,0),     3 ,     1 ,     1 ,     2 , s(2,0), s(2,0),  1 },
/* 3 R            */ {     0 ,     3 ,     5 ,     5 , s(1,4),     0 ,     0 ,  1 },
/* 4 R+ON         */ { s(2,0),     3 ,     5 ,     5 ,     4 , s(2,0), s(2,0),  1 },
/* 5 R+EN/AN      */ {     0 ,     3 ,     5 ,     5 , s(1,4),     0 ,     0 ,  2 }
};
static const ImpTab impTabR_GROUP_NUMBERS_WITH_R =
/*  In this table, EN/AN+ON sequences receive levels as if associated with R
    until proven that there is L on both sides. AN is handled like EN.
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 init         */ {     2 ,     0 ,     1 ,     1 ,     0 ,     0 ,     0 ,  0 },
/* 1 EN/AN        */ {     2 ,     0 ,     1 ,     1 ,     0 ,     0 ,     0 ,  1 },
/* 2 L            */ {     2 ,     0 , s(1,4), s(1,4), s(1,3),     0 ,     0 ,  1 },
/* 3 L+ON         */ { s(2,2),     0 ,     4 ,     4 ,     3 ,     0 ,     0 ,  0 },
/* 4 L+EN/AN      */ { s(2,2),     0 ,     4 ,     4 ,     3 ,     0 ,     0 ,  1 }
};
static const ImpTabPair impTab_GROUP_NUMBERS_WITH_R = {
                        {&impTabL_GROUP_NUMBERS_WITH_R,
                         &impTabR_GROUP_NUMBERS_WITH_R},
                        {&impAct0, &impAct0}};


static const ImpTab impTabL_INVERSE_NUMBERS_AS_L =
/*  This table is identical to the Default LTR table except that EN and AN are
    handled like L.
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ {     0 ,     1 ,     0 ,     0 ,     0 ,     0 ,     0 ,  0 },
/* 1 : R          */ {     0 ,     1 ,     0 ,     0 , s(1,4), s(1,4),     0 ,  1 },
/* 2 : AN         */ {     0 ,     1 ,     0 ,     0 , s(1,5), s(1,5),     0 ,  2 },
/* 3 : R+EN/AN    */ {     0 ,     1 ,     0 ,     0 , s(1,4), s(1,4),     0 ,  2 },
/* 4 : R+ON       */ { s(2,0),     1 , s(2,0), s(2,0),     4 ,     4 , s(2,0),  1 },
/* 5 : AN+ON      */ { s(2,0),     1 , s(2,0), s(2,0),     5 ,     5 , s(2,0),  1 }
};
static const ImpTab impTabR_INVERSE_NUMBERS_AS_L =
/*  This table is identical to the Default RTL table except that EN and AN are
    handled like L.
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ {     1 ,     0 ,     1 ,     1 ,     0 ,     0 ,     0 ,  0 },
/* 1 : L          */ {     1 ,     0 ,     1 ,     1 , s(1,4), s(1,4),     0 ,  1 },
/* 2 : EN/AN      */ {     1 ,     0 ,     1 ,     1 ,     0 ,     0 ,     0 ,  1 },
/* 3 : L+AN       */ {     1 ,     0 ,     1 ,     1 ,     5 ,     5 ,     0 ,  1 },
/* 4 : L+ON       */ { s(2,1),     0 , s(2,1), s(2,1),     4 ,     4 ,     0 ,  0 },
/* 5 : L+AN+ON    */ {     1 ,     0 ,     1 ,     1 ,     5 ,     5 ,     0 ,  0 }
};
static const ImpTabPair impTab_INVERSE_NUMBERS_AS_L = {
                        {&impTabL_INVERSE_NUMBERS_AS_L,
                         &impTabR_INVERSE_NUMBERS_AS_L},
                        {&impAct0, &impAct0}};

static const ImpTab impTabR_INVERSE_LIKE_DIRECT =   /* Odd  paragraph level */
/*  In this table, conditional sequences receive the lower possible level
    until proven otherwise.
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ {     1 ,     0 ,     2 ,     2 ,     0 ,     0 ,     0 ,  0 },
/* 1 : L          */ {     1 ,     0 ,     1 ,     2 , s(1,3), s(1,3),     0 ,  1 },
/* 2 : EN/AN      */ {     1 ,     0 ,     2 ,     2 ,     0 ,     0 ,     0 ,  1 },
/* 3 : L+ON       */ { s(2,1), s(3,0),     6 ,     4 ,     3 ,     3 , s(3,0),  0 },
/* 4 : L+ON+AN    */ { s(2,1), s(3,0),     6 ,     4 ,     5 ,     5 , s(3,0),  3 },
/* 5 : L+AN+ON    */ { s(2,1), s(3,0),     6 ,     4 ,     5 ,     5 , s(3,0),  2 },
/* 6 : L+ON+EN    */ { s(2,1), s(3,0),     6 ,     4 ,     3 ,     3 , s(3,0),  1 }
};
static const ImpAct impAct1 = {0,1,13,14};
/* FOOD FOR THOUGHT: in LTR table below, check case "JKL 123abc"
 */
static const ImpTabPair impTab_INVERSE_LIKE_DIRECT = {
                        {&impTabL_DEFAULT,
                         &impTabR_INVERSE_LIKE_DIRECT},
                        {&impAct0, &impAct1}};

static const ImpTab impTabL_INVERSE_LIKE_DIRECT_WITH_MARKS =
/*  The case handled in this table is (visually):  R EN L
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ {     0 , s(6,3),     0 ,     1 ,     0 ,     0 ,     0 ,  0 },
/* 1 : L+AN       */ {     0 , s(6,3),     0 ,     1 , s(1,2), s(3,0),     0 ,  4 },
/* 2 : L+AN+ON    */ { s(2,0), s(6,3), s(2,0),     1 ,     2 , s(3,0), s(2,0),  3 },
/* 3 : R          */ {     0 , s(6,3), s(5,5), s(5,6), s(1,4), s(3,0),     0 ,  3 },
/* 4 : R+ON       */ { s(3,0), s(4,3), s(5,5), s(5,6),     4 , s(3,0), s(3,0),  3 },
/* 5 : R+EN       */ { s(3,0), s(4,3),     5 , s(5,6), s(1,4), s(3,0), s(3,0),  4 },
/* 6 : R+AN       */ { s(3,0), s(4,3), s(5,5),     6 , s(1,4), s(3,0), s(3,0),  4 }
};
static const ImpTab impTabR_INVERSE_LIKE_DIRECT_WITH_MARKS =
/*  The cases handled in this table are (visually):  R EN L
                                                     R L AN L
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ { s(1,3),     0 ,     1 ,     1 ,     0 ,     0 ,     0 ,  0 },
/* 1 : R+EN/AN    */ { s(2,3),     0 ,     1 ,     1 ,     2 , s(4,0),     0 ,  1 },
/* 2 : R+EN/AN+ON */ { s(2,3),     0 ,     1 ,     1 ,     2 , s(4,0),     0 ,  0 },
/* 3 : L          */ {     3 ,     0 ,     3 , s(3,6), s(1,4), s(4,0),     0 ,  1 },
/* 4 : L+ON       */ { s(5,3), s(4,0),     5 , s(3,6),     4 , s(4,0), s(4,0),  0 },
/* 5 : L+ON+EN    */ { s(5,3), s(4,0),     5 , s(3,6),     4 , s(4,0), s(4,0),  1 },
/* 6 : L+AN       */ { s(5,3), s(4,0),     6 ,     6 ,     4 , s(4,0), s(4,0),  3 }
};
static const ImpAct impAct2 = {0,1,2,5,6,7,8};
static const ImpAct impAct3 = {0,1,9,10,11,12};
static const ImpTabPair impTab_INVERSE_LIKE_DIRECT_WITH_MARKS = {
                        {&impTabL_INVERSE_LIKE_DIRECT_WITH_MARKS,
                         &impTabR_INVERSE_LIKE_DIRECT_WITH_MARKS},
                        {&impAct2, &impAct3}};

static const ImpTabPair impTab_INVERSE_FOR_NUMBERS_SPECIAL = {
                        {&impTabL_NUMBERS_SPECIAL,
                         &impTabR_INVERSE_LIKE_DIRECT},
                        {&impAct0, &impAct1}};

static const ImpTab impTabL_INVERSE_FOR_NUMBERS_SPECIAL_WITH_MARKS =
/*  The case handled in this table is (visually):  R EN L
*/
{
/*                         L ,     R ,    EN ,    AN ,    ON ,     S ,     B , Res */
/* 0 : init       */ {     0 , s(6,2),     1 ,     1 ,     0 ,     0 ,     0 ,  0 },
/* 1 : L+EN/AN    */ {     0 , s(6,2),     1 ,     1 ,     0 , s(3,0),     0 ,  4 },
/* 2 : R          */ {     0 , s(6,2), s(5,4), s(5,4), s(1,3), s(3,0),     0 ,  3 },
/* 3 : R+ON       */ { s(3,0), s(4,2), s(5,4), s(5,4),     3 , s(3,0), s(3,0),  3 },
/* 4 : R+EN/AN    */ { s(3,0), s(4,2),     4 ,     4 , s(1,3), s(3,0), s(3,0),  4 }
};
static const ImpTabPair impTab_INVERSE_FOR_NUMBERS_SPECIAL_WITH_MARKS = {
                        {&impTabL_INVERSE_FOR_NUMBERS_SPECIAL_WITH_MARKS,
                         &impTabR_INVERSE_LIKE_DIRECT_WITH_MARKS},
                        {&impAct2, &impAct3}};

#undef s

typedef struct {
    const ImpTab * pImpTab;             /* level table pointer          */
    const ImpAct * pImpAct;             /* action map array             */
    int32_t startON;                    /* start of ON sequence         */
    int32_t startL2EN;                  /* start of level 2 sequence    */
    int32_t lastStrongRTL;              /* index of last found R or AL  */
    int32_t state;                      /* current state                */
    int32_t runStart;                   /* start position of the run    */
    UBiDiLevel runLevel;                /* run level before implicit solving */
} LevState;

/*------------------------------------------------------------------------*/

static void
addPoint(UBiDi *pBiDi, int32_t pos, int32_t flag)
  /* param pos:     position where to insert
     param flag:    one of LRM_BEFORE, LRM_AFTER, RLM_BEFORE, RLM_AFTER
  */
{
#define FIRSTALLOC  10
    Point point;
    InsertPoints * pInsertPoints=&(pBiDi->insertPoints);

    if (pInsertPoints->capacity == 0)
    {
        pInsertPoints->points=static_cast<Point *>(uprv_malloc(sizeof(Point)*FIRSTALLOC));
        if (pInsertPoints->points == nullptr)
        {
            pInsertPoints->errorCode=U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        pInsertPoints->capacity=FIRSTALLOC;
    }
    if (pInsertPoints->size >= pInsertPoints->capacity) /* no room for new point */
    {
        Point * savePoints=pInsertPoints->points;
        pInsertPoints->points=static_cast<Point *>(uprv_realloc(pInsertPoints->points,
                                           pInsertPoints->capacity*2*sizeof(Point)));
        if (pInsertPoints->points == nullptr)
        {
            pInsertPoints->points=savePoints;
            pInsertPoints->errorCode=U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        else  pInsertPoints->capacity*=2;
    }
    point.pos=pos;
    point.flag=flag;
    pInsertPoints->points[pInsertPoints->size]=point;
    pInsertPoints->size++;
#undef FIRSTALLOC
}

static void
setLevelsOutsideIsolates(UBiDi *pBiDi, int32_t start, int32_t limit, UBiDiLevel level)
{
    DirProp *dirProps=pBiDi->dirProps, dirProp;
    UBiDiLevel *levels=pBiDi->levels;
    int32_t isolateCount=0, k;
    for(k=start; k<limit; k++) {
        dirProp=dirProps[k];
        if(dirProp==PDI)
            isolateCount--;
        if(isolateCount==0)
            levels[k]=level;
        if(dirProp==LRI || dirProp==RLI)
            isolateCount++;
    }
}

/* perform rules (Wn), (Nn), and (In) on a run of the text ------------------ */

/*
 * This implementation of the (Wn) rules applies all rules in one pass.
 * In order to do so, it needs a look-ahead of typically 1 character
 * (except for W5: sequences of ET) and keeps track of changes
 * in a rule Wp that affect a later Wq (p<q).
 *
 * The (Nn) and (In) rules are also performed in that same single loop,
 * but effectively one iteration behind for white space.
 *
 * Since all implicit rules are performed in one step, it is not necessary
 * to actually store the intermediate directional properties in dirProps[].
 */

static void
processPropertySeq(UBiDi *pBiDi, LevState *pLevState, uint8_t _prop,
                   int32_t start, int32_t limit) {
    uint8_t cell, oldStateSeq, actionSeq;
    const ImpTab * pImpTab=pLevState->pImpTab;
    const ImpAct * pImpAct=pLevState->pImpAct;
    UBiDiLevel * levels=pBiDi->levels;
    UBiDiLevel level, addLevel;
    InsertPoints * pInsertPoints;
    int32_t start0, k;

    start0=start;                           /* save original start position */
    oldStateSeq = static_cast<uint8_t>(pLevState->state);
    cell=(*pImpTab)[oldStateSeq][_prop];
    pLevState->state=GET_STATE(cell);       /* isolate the new state */
    actionSeq=(*pImpAct)[GET_ACTION(cell)]; /* isolate the action */
    addLevel=(*pImpTab)[pLevState->state][IMPTABLEVELS_RES];

    if(actionSeq) {
        switch(actionSeq) {
        case 1:                         /* init ON seq */
            pLevState->startON=start0;
            break;

        case 2:                         /* prepend ON seq to current seq */
            start=pLevState->startON;
            break;

        case 3:                         /* EN/AN after R+ON */
            level=pLevState->runLevel+1;
            setLevelsOutsideIsolates(pBiDi, pLevState->startON, start0, level);
            break;

        case 4:                         /* EN/AN before R for NUMBERS_SPECIAL */
            level=pLevState->runLevel+2;
            setLevelsOutsideIsolates(pBiDi, pLevState->startON, start0, level);
            break;

        case 5:                         /* L or S after possible relevant EN/AN */
            /* check if we had EN after R/AL */
            if (pLevState->startL2EN >= 0) {
                addPoint(pBiDi, pLevState->startL2EN, LRM_BEFORE);
            }
            pLevState->startL2EN=-1;  /* not within previous if since could also be -2 */
            /* check if we had any relevant EN/AN after R/AL */
            pInsertPoints=&(pBiDi->insertPoints);
            if ((pInsertPoints->capacity == 0) ||
                (pInsertPoints->size <= pInsertPoints->confirmed))
            {
                /* nothing, just clean up */
                pLevState->lastStrongRTL=-1;
                /* check if we have a pending conditional segment */
                level=(*pImpTab)[oldStateSeq][IMPTABLEVELS_RES];
                if ((level & 1) && (pLevState->startON > 0)) {  /* after ON */
                    start=pLevState->startON;   /* reset to basic run level */
                }
                if (_prop == DirProp_S)                /* add LRM before S */
                {
                    addPoint(pBiDi, start0, LRM_BEFORE);
                    pInsertPoints->confirmed=pInsertPoints->size;
                }
                break;
            }
            /* reset previous RTL cont to level for LTR text */
            for (k=pLevState->lastStrongRTL+1; k<start0; k++)
            {
                /* reset odd level, leave runLevel+2 as is */
                levels[k]=(levels[k] - 2) & ~1;
            }
            /* mark insert points as confirmed */
            pInsertPoints->confirmed=pInsertPoints->size;
            pLevState->lastStrongRTL=-1;
            if (_prop == DirProp_S)            /* add LRM before S */
            {
                addPoint(pBiDi, start0, LRM_BEFORE);
                pInsertPoints->confirmed=pInsertPoints->size;
            }
            break;

        case 6:                         /* R/AL after possible relevant EN/AN */
            /* just clean up */
            pInsertPoints=&(pBiDi->insertPoints);
            if (pInsertPoints->capacity > 0)
                /* remove all non confirmed insert points */
                pInsertPoints->size=pInsertPoints->confirmed;
            pLevState->startON=-1;
            pLevState->startL2EN=-1;
            pLevState->lastStrongRTL=limit - 1;
            break;

        case 7:                         /* EN/AN after R/AL + possible cont */
            /* check for real AN */
            if ((_prop == DirProp_AN) && (pBiDi->dirProps[start0] == AN) &&
                (pBiDi->reorderingMode!=UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL))
            {
                /* real AN */
                if (pLevState->startL2EN == -1) /* if no relevant EN already found */
                {
                    /* just note the righmost digit as a strong RTL */
                    pLevState->lastStrongRTL=limit - 1;
                    break;
                }
                if (pLevState->startL2EN >= 0)  /* after EN, no AN */
                {
                    addPoint(pBiDi, pLevState->startL2EN, LRM_BEFORE);
                    pLevState->startL2EN=-2;
                }
                /* note AN */
                addPoint(pBiDi, start0, LRM_BEFORE);
                break;
            }
            /* if first EN/AN after R/AL */
            if (pLevState->startL2EN == -1) {
                pLevState->startL2EN=start0;
            }
            break;

        case 8:                         /* note location of latest R/AL */
            pLevState->lastStrongRTL=limit - 1;
            pLevState->startON=-1;
            break;

        case 9:                         /* L after R+ON/EN/AN */
            /* include possible adjacent number on the left */
            for (k=start0-1; k>=0 && !(levels[k]&1); k--);
            if(k>=0) {
                addPoint(pBiDi, k, RLM_BEFORE);             /* add RLM before */
                pInsertPoints=&(pBiDi->insertPoints);
                pInsertPoints->confirmed=pInsertPoints->size;   /* confirm it */
            }
            pLevState->startON=start0;
            break;

        case 10:                        /* AN after L */
            /* AN numbers between L text on both sides may be trouble. */
            /* tentatively bracket with LRMs; will be confirmed if followed by L */
            addPoint(pBiDi, start0, LRM_BEFORE);    /* add LRM before */
            addPoint(pBiDi, start0, LRM_AFTER);     /* add LRM after  */
            break;

        case 11:                        /* R after L+ON/EN/AN */
            /* false alert, infirm LRMs around previous AN */
            pInsertPoints=&(pBiDi->insertPoints);
            pInsertPoints->size=pInsertPoints->confirmed;
            if (_prop == DirProp_S)            /* add RLM before S */
            {
                addPoint(pBiDi, start0, RLM_BEFORE);
                pInsertPoints->confirmed=pInsertPoints->size;
            }
            break;

        case 12:                        /* L after L+ON/AN */
            level=pLevState->runLevel + addLevel;
            for(k=pLevState->startON; k<start0; k++) {
                if (levels[k]<level)
                    levels[k]=level;
            }
            pInsertPoints=&(pBiDi->insertPoints);
            pInsertPoints->confirmed=pInsertPoints->size;   /* confirm inserts */
            pLevState->startON=start0;
            break;

        case 13:                        /* L after L+ON+EN/AN/ON */
            level=pLevState->runLevel;
            for(k=start0-1; k>=pLevState->startON; k--) {
                if(levels[k]==level+3) {
                    while(levels[k]==level+3) {
                        levels[k--]-=2;
                    }
                    while(levels[k]==level) {
                        k--;
                    }
                }
                if(levels[k]==level+2) {
                    levels[k]=level;
                    continue;
                }
                levels[k]=level+1;
            }
            break;

        case 14:                        /* R after L+ON+EN/AN/ON */
            level=pLevState->runLevel+1;
            for(k=start0-1; k>=pLevState->startON; k--) {
                if(levels[k]>level) {
                    levels[k]-=2;
                }
            }
            break;

        default:                        /* we should never get here */
            UPRV_UNREACHABLE_EXIT;
        }
    }
    if((addLevel) || (start < start0)) {
        level=pLevState->runLevel + addLevel;
        if(start>=pLevState->runStart) {
            for(k=start; k<limit; k++) {
                levels[k]=level;
            }
        } else {
            setLevelsOutsideIsolates(pBiDi, start, limit, level);
        }
    }
}

/**
 * Returns the directionality of the last strong character at the end of the prologue, if any.
 * Requires prologue!=null.
 */
static DirProp
lastL_R_AL(UBiDi *pBiDi) {
    const char16_t *text=pBiDi->prologue;
    int32_t length=pBiDi->proLength;
    int32_t i;
    UChar32 uchar;
    DirProp dirProp;
    for(i=length; i>0; ) {
        /* i is decremented by U16_PREV */
        U16_PREV(text, 0, i, uchar);
        dirProp = static_cast<DirProp>(ubidi_getCustomizedClass(pBiDi, uchar));
        if(dirProp==L) {
            return DirProp_L;
        }
        if(dirProp==R || dirProp==AL) {
            return DirProp_R;
        }
        if(dirProp==B) {
            return DirProp_ON;
        }
    }
    return DirProp_ON;
}

/**
 * Returns the directionality of the first strong character, or digit, in the epilogue, if any.
 * Requires epilogue!=null.
 */
static DirProp
firstL_R_AL_EN_AN(UBiDi *pBiDi) {
    const char16_t *text=pBiDi->epilogue;
    int32_t length=pBiDi->epiLength;
    int32_t i;
    UChar32 uchar;
    DirProp dirProp;
    for(i=0; i<length; ) {
        /* i is incremented by U16_NEXT */
        U16_NEXT(text, i, length, uchar);
        dirProp = static_cast<DirProp>(ubidi_getCustomizedClass(pBiDi, uchar));
        if(dirProp==L) {
            return DirProp_L;
        }
        if(dirProp==R || dirProp==AL) {
            return DirProp_R;
        }
        if(dirProp==EN) {
            return DirProp_EN;
        }
        if(dirProp==AN) {
            return DirProp_AN;
        }
    }
    return DirProp_ON;
}

static void
resolveImplicitLevels(UBiDi *pBiDi,
                      int32_t start, int32_t limit,
                      DirProp sor, DirProp eor) {
    const DirProp *dirProps=pBiDi->dirProps;
    DirProp dirProp;
    LevState levState;
    int32_t i, start1, start2;
    uint16_t oldStateImp, stateImp, actionImp;
    uint8_t gprop, resProp, cell;
    UBool inverseRTL;
    DirProp nextStrongProp=R;
    int32_t nextStrongPos=-1;

    /* check for RTL inverse BiDi mode */
    /* FOOD FOR THOUGHT: in case of RTL inverse BiDi, it would make sense to
     * loop on the text characters from end to start.
     * This would need a different properties state table (at least different
     * actions) and different levels state tables (maybe very similar to the
     * LTR corresponding ones.
     */
    inverseRTL =
        static_cast<UBool>((start < pBiDi->lastArabicPos) && (GET_PARALEVEL(pBiDi, start) & 1) &&
                           (pBiDi->reorderingMode == UBIDI_REORDER_INVERSE_LIKE_DIRECT ||
                            pBiDi->reorderingMode == UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL));

    /* initialize for property and levels state tables */
    levState.startL2EN=-1;              /* used for INVERSE_LIKE_DIRECT_WITH_MARKS */
    levState.lastStrongRTL=-1;          /* used for INVERSE_LIKE_DIRECT_WITH_MARKS */
    levState.runStart=start;
    levState.runLevel=pBiDi->levels[start];
    levState.pImpTab = static_cast<const ImpTab*>(((pBiDi->pImpTabPair)->pImpTab)[levState.runLevel & 1]);
    levState.pImpAct = static_cast<const ImpAct*>(((pBiDi->pImpTabPair)->pImpAct)[levState.runLevel & 1]);
    if(start==0 && pBiDi->proLength>0) {
        DirProp lastStrong=lastL_R_AL(pBiDi);
        if(lastStrong!=DirProp_ON) {
            sor=lastStrong;
        }
    }
    /* The isolates[] entries contain enough information to
       resume the bidi algorithm in the same state as it was
       when it was interrupted by an isolate sequence. */
    if(dirProps[start]==PDI  && pBiDi->isolateCount >= 0) {
        levState.startON=pBiDi->isolates[pBiDi->isolateCount].startON;
        start1=pBiDi->isolates[pBiDi->isolateCount].start1;
        stateImp=pBiDi->isolates[pBiDi->isolateCount].stateImp;
        levState.state=pBiDi->isolates[pBiDi->isolateCount].state;
        pBiDi->isolateCount--;
    } else {
        levState.startON=-1;
        start1=start;
        if(dirProps[start]==NSM)
            stateImp = 1 + sor;
        else
            stateImp=0;
        levState.state=0;
        processPropertySeq(pBiDi, &levState, sor, start, start);
    }
    start2=start;                       /* to make Java compiler happy */

    for(i=start; i<=limit; i++) {
        if(i>=limit) {
            int32_t k;
            for(k=limit-1; k>start&&(DIRPROP_FLAG(dirProps[k])&MASK_BN_EXPLICIT); k--);
            dirProp=dirProps[k];
            if(dirProp==LRI || dirProp==RLI)
                break;      /* no forced closing for sequence ending with LRI/RLI */
            gprop=eor;
        } else {
            DirProp prop, prop1;
            prop=dirProps[i];
            if(prop==B) {
                pBiDi->isolateCount=-1; /* current isolates stack entry == none */
            }
            if(inverseRTL) {
                if(prop==AL) {
                    /* AL before EN does not make it AN */
                    prop=R;
                } else if(prop==EN) {
                    if(nextStrongPos<=i) {
                        /* look for next strong char (L/R/AL) */
                        int32_t j;
                        nextStrongProp=R;   /* set default */
                        nextStrongPos=limit;
                        for(j=i+1; j<limit; j++) {
                            prop1=dirProps[j];
                            if(prop1==L || prop1==R || prop1==AL) {
                                nextStrongProp=prop1;
                                nextStrongPos=j;
                                break;
                            }
                        }
                    }
                    if(nextStrongProp==AL) {
                        prop=AN;
                    }
                }
            }
            gprop=groupProp[prop];
        }
        oldStateImp=stateImp;
        cell=impTabProps[oldStateImp][gprop];
        stateImp=GET_STATEPROPS(cell);      /* isolate the new state */
        actionImp=GET_ACTIONPROPS(cell);    /* isolate the action */
        if((i==limit) && (actionImp==0)) {
            /* there is an unprocessed sequence if its property == eor   */
            actionImp=1;                    /* process the last sequence */
        }
        if(actionImp) {
            resProp=impTabProps[oldStateImp][IMPTABPROPS_RES];
            switch(actionImp) {
            case 1:             /* process current seq1, init new seq1 */
                processPropertySeq(pBiDi, &levState, resProp, start1, i);
                start1=i;
                break;
            case 2:             /* init new seq2 */
                start2=i;
                break;
            case 3:             /* process seq1, process seq2, init new seq1 */
                processPropertySeq(pBiDi, &levState, resProp, start1, start2);
                processPropertySeq(pBiDi, &levState, DirProp_ON, start2, i);
                start1=i;
                break;
            case 4:             /* process seq1, set seq1=seq2, init new seq2 */
                processPropertySeq(pBiDi, &levState, resProp, start1, start2);
                start1=start2;
                start2=i;
                break;
            default:            /* we should never get here */
                UPRV_UNREACHABLE_EXIT;
            }
        }
    }

    /* flush possible pending sequence, e.g. ON */
    if(limit==pBiDi->length && pBiDi->epiLength>0) {
        DirProp firstStrong=firstL_R_AL_EN_AN(pBiDi);
        if(firstStrong!=DirProp_ON) {
            eor=firstStrong;
        }
    }

    /* look for the last char not a BN or LRE/RLE/LRO/RLO/PDF */
    for(i=limit-1; i>start&&(DIRPROP_FLAG(dirProps[i])&MASK_BN_EXPLICIT); i--);
    dirProp=dirProps[i];
    if((dirProp==LRI || dirProp==RLI) && limit<pBiDi->length) {
        pBiDi->isolateCount++;
        pBiDi->isolates[pBiDi->isolateCount].stateImp=stateImp;
        pBiDi->isolates[pBiDi->isolateCount].state=levState.state;
        pBiDi->isolates[pBiDi->isolateCount].start1=start1;
        pBiDi->isolates[pBiDi->isolateCount].startON=levState.startON;
    }
    else
        processPropertySeq(pBiDi, &levState, eor, limit, limit);
}

/* perform (L1) and (X9) ---------------------------------------------------- */

/*
 * Reset the embedding levels for some non-graphic characters (L1).
 * This function also sets appropriate levels for BN, and
 * explicit embedding types that are supposed to have been removed
 * from the paragraph in (X9).
 */
static void
adjustWSLevels(UBiDi *pBiDi) {
    const DirProp *dirProps=pBiDi->dirProps;
    UBiDiLevel *levels=pBiDi->levels;
    int32_t i;

    if(pBiDi->flags&MASK_WS) {
        UBool orderParagraphsLTR=pBiDi->orderParagraphsLTR;
        Flags flag;

        i=pBiDi->trailingWSStart;
        while(i>0) {
            /* reset a sequence of WS/BN before eop and B/S to the paragraph paraLevel */
            while(i>0 && (flag=DIRPROP_FLAG(dirProps[--i]))&MASK_WS) {
                if(orderParagraphsLTR&&(flag&DIRPROP_FLAG(B))) {
                    levels[i]=0;
                } else {
                    levels[i]=GET_PARALEVEL(pBiDi, i);
                }
            }

            /* reset BN to the next character's paraLevel until B/S, which restarts above loop */
            /* here, i+1 is guaranteed to be <length */
            while(i>0) {
                flag=DIRPROP_FLAG(dirProps[--i]);
                if(flag&MASK_BN_EXPLICIT) {
                    levels[i]=levels[i+1];
                } else if(orderParagraphsLTR&&(flag&DIRPROP_FLAG(B))) {
                    levels[i]=0;
                    break;
                } else if(flag&MASK_B_S) {
                    levels[i]=GET_PARALEVEL(pBiDi, i);
                    break;
                }
            }
        }
    }
}

U_CAPI void U_EXPORT2
ubidi_setContext(UBiDi *pBiDi,
                 const char16_t *prologue, int32_t proLength,
                 const char16_t *epilogue, int32_t epiLength,
                 UErrorCode *pErrorCode) {
    /* check the argument values */
    RETURN_VOID_IF_NULL_OR_FAILING_ERRCODE(pErrorCode);
    if(pBiDi==nullptr || proLength<-1 || epiLength<-1 ||
       (prologue==nullptr && proLength!=0) || (epilogue==nullptr && epiLength!=0)) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if(proLength==-1) {
        pBiDi->proLength=u_strlen(prologue);
    } else {
        pBiDi->proLength=proLength;
    }
    if(epiLength==-1) {
        pBiDi->epiLength=u_strlen(epilogue);
    } else {
        pBiDi->epiLength=epiLength;
    }
    pBiDi->prologue=prologue;
    pBiDi->epilogue=epilogue;
}

static void
setParaSuccess(UBiDi *pBiDi) {
    pBiDi->proLength=0;                 /* forget the last context */
    pBiDi->epiLength=0;
    pBiDi->pParaBiDi=pBiDi;             /* mark successful setPara */
}

#define BIDI_MIN(x, y)   ((x)<(y) ? (x) : (y))
#define BIDI_ABS(x)      ((x)>=0  ? (x) : (-(x)))

static void
setParaRunsOnly(UBiDi *pBiDi, const char16_t *text, int32_t length,
                UBiDiLevel paraLevel, UErrorCode *pErrorCode) {
    int32_t *runsOnlyMemory = nullptr;
    int32_t *visualMap;
    char16_t *visualText;
    int32_t saveLength, saveTrailingWSStart;
    const UBiDiLevel *levels;
    UBiDiLevel *saveLevels;
    UBiDiDirection saveDirection;
    UBool saveMayAllocateText;
    Run *runs;
    int32_t visualLength, i, j, visualStart, logicalStart,
            runCount, runLength, addedRuns, insertRemove,
            start, limit, step, indexOddBit, logicalPos,
            index0, index1;
    uint32_t saveOptions;

    pBiDi->reorderingMode=UBIDI_REORDER_DEFAULT;
    if(length==0) {
        ubidi_setPara(pBiDi, text, length, paraLevel, nullptr, pErrorCode);
        goto cleanup3;
    }
    /* obtain memory for mapping table and visual text */
    runsOnlyMemory=static_cast<int32_t *>(uprv_malloc(length*(sizeof(int32_t)+sizeof(char16_t)+sizeof(UBiDiLevel))));
    if(runsOnlyMemory==nullptr) {
        *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
        goto cleanup3;
    }
    visualMap=runsOnlyMemory;
    visualText = reinterpret_cast<char16_t*>(&visualMap[length]);
    saveLevels = reinterpret_cast<UBiDiLevel*>(&visualText[length]);
    saveOptions=pBiDi->reorderingOptions;
    if(saveOptions & UBIDI_OPTION_INSERT_MARKS) {
        pBiDi->reorderingOptions&=~UBIDI_OPTION_INSERT_MARKS;
        pBiDi->reorderingOptions|=UBIDI_OPTION_REMOVE_CONTROLS;
    }
    paraLevel&=1;                       /* accept only 0 or 1 */
    ubidi_setPara(pBiDi, text, length, paraLevel, nullptr, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        goto cleanup3;
    }
    /* we cannot access directly pBiDi->levels since it is not yet set if
     * direction is not MIXED
     */
    levels=ubidi_getLevels(pBiDi, pErrorCode);
    uprv_memcpy(saveLevels, levels, (size_t)pBiDi->length*sizeof(UBiDiLevel));
    saveTrailingWSStart=pBiDi->trailingWSStart;
    saveLength=pBiDi->length;
    saveDirection=pBiDi->direction;

    /* FOOD FOR THOUGHT: instead of writing the visual text, we could use
     * the visual map and the dirProps array to drive the second call
     * to ubidi_setPara (but must make provision for possible removal of
     * BiDi controls.  Alternatively, only use the dirProps array via
     * customized classifier callback.
     */
    visualLength=ubidi_writeReordered(pBiDi, visualText, length,
                                      UBIDI_DO_MIRRORING, pErrorCode);
    ubidi_getVisualMap(pBiDi, visualMap, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        goto cleanup2;
    }
    pBiDi->reorderingOptions=saveOptions;

    pBiDi->reorderingMode=UBIDI_REORDER_INVERSE_LIKE_DIRECT;
    paraLevel^=1;
    /* Because what we did with reorderingOptions, visualText may be shorter
     * than the original text. But we don't want the levels memory to be
     * reallocated shorter than the original length, since we need to restore
     * the levels as after the first call to ubidi_setpara() before returning.
     * We will force mayAllocateText to false before the second call to
     * ubidi_setpara(), and will restore it afterwards.
     */
    saveMayAllocateText=pBiDi->mayAllocateText;
    pBiDi->mayAllocateText=false;
    ubidi_setPara(pBiDi, visualText, visualLength, paraLevel, nullptr, pErrorCode);
    pBiDi->mayAllocateText=saveMayAllocateText;
    ubidi_getRuns(pBiDi, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        goto cleanup1;
    }
    /* check if some runs must be split, count how many splits */
    addedRuns=0;
    runCount=pBiDi->runCount;
    runs=pBiDi->runs;
    visualStart=0;
    for(i=0; i<runCount; i++, visualStart+=runLength) {
        runLength=runs[i].visualLimit-visualStart;
        if(runLength<2) {
            continue;
        }
        logicalStart=GET_INDEX(runs[i].logicalStart);
        for(j=logicalStart+1; j<logicalStart+runLength; j++) {
            index0=visualMap[j];
            index1=visualMap[j-1];
            if((BIDI_ABS(index0-index1)!=1) || (saveLevels[index0]!=saveLevels[index1])) {
                addedRuns++;
            }
        }
    }
    if(addedRuns) {
        if(getRunsMemory(pBiDi, runCount+addedRuns)) {
            if(runCount==1) {
                /* because we switch from UBiDi.simpleRuns to UBiDi.runs */
                pBiDi->runsMemory[0]=runs[0];
            }
            runs=pBiDi->runs=pBiDi->runsMemory;
            pBiDi->runCount+=addedRuns;
        } else {
            goto cleanup1;
        }
    }
    /* split runs which are not consecutive in source text */
    for(i=runCount-1; i>=0; i--) {
        runLength= i==0 ? runs[0].visualLimit :
                          runs[i].visualLimit-runs[i-1].visualLimit;
        logicalStart=runs[i].logicalStart;
        indexOddBit=GET_ODD_BIT(logicalStart);
        logicalStart=GET_INDEX(logicalStart);
        if(runLength<2) {
            if(addedRuns) {
                runs[i+addedRuns]=runs[i];
            }
            logicalPos=visualMap[logicalStart];
            runs[i+addedRuns].logicalStart=MAKE_INDEX_ODD_PAIR(logicalPos,
                                            saveLevels[logicalPos]^indexOddBit);
            continue;
        }
        if(indexOddBit) {
            start=logicalStart;
            limit=logicalStart+runLength-1;
            step=1;
        } else {
            start=logicalStart+runLength-1;
            limit=logicalStart;
            step=-1;
        }
        for(j=start; j!=limit; j+=step) {
            index0=visualMap[j];
            index1=visualMap[j+step];
            if((BIDI_ABS(index0-index1)!=1) || (saveLevels[index0]!=saveLevels[index1])) {
                logicalPos=BIDI_MIN(visualMap[start], index0);
                runs[i+addedRuns].logicalStart=MAKE_INDEX_ODD_PAIR(logicalPos,
                                            saveLevels[logicalPos]^indexOddBit);
                runs[i+addedRuns].visualLimit=runs[i].visualLimit;
                runs[i].visualLimit-=BIDI_ABS(j-start)+1;
                insertRemove=runs[i].insertRemove&(LRM_AFTER|RLM_AFTER);
                runs[i+addedRuns].insertRemove=insertRemove;
                runs[i].insertRemove&=~insertRemove;
                start=j+step;
                addedRuns--;
            }
        }
        if(addedRuns) {
            runs[i+addedRuns]=runs[i];
        }
        logicalPos=BIDI_MIN(visualMap[start], visualMap[limit]);
        runs[i+addedRuns].logicalStart=MAKE_INDEX_ODD_PAIR(logicalPos,
                                            saveLevels[logicalPos]^indexOddBit);
    }

  cleanup1:
    /* restore initial paraLevel */
    pBiDi->paraLevel^=1;
  cleanup2:
    /* restore real text */
    pBiDi->text=text;
    pBiDi->length=saveLength;
    pBiDi->originalLength=length;
    pBiDi->direction=saveDirection;
    /* the saved levels should never excess levelsSize, but we check anyway */
    if(saveLength>pBiDi->levelsSize) {
        saveLength=pBiDi->levelsSize;
    }
    uprv_memcpy(pBiDi->levels, saveLevels, (size_t)saveLength*sizeof(UBiDiLevel));
    pBiDi->trailingWSStart=saveTrailingWSStart;
    if(pBiDi->runCount>1) {
        pBiDi->direction=UBIDI_MIXED;
    }
  cleanup3:
    /* free memory for mapping table and visual text */
    uprv_free(runsOnlyMemory);

    pBiDi->reorderingMode=UBIDI_REORDER_RUNS_ONLY;
}

/* ubidi_setPara ------------------------------------------------------------ */

U_CAPI void U_EXPORT2
ubidi_setPara(UBiDi *pBiDi, const char16_t *text, int32_t length,
              UBiDiLevel paraLevel, UBiDiLevel *embeddingLevels,
              UErrorCode *pErrorCode) {
    UBiDiDirection direction;
    DirProp *dirProps;

    /* check the argument values */
    RETURN_VOID_IF_NULL_OR_FAILING_ERRCODE(pErrorCode);
    if(pBiDi==nullptr || text==nullptr || length<-1 ||
       (paraLevel>UBIDI_MAX_EXPLICIT_LEVEL && paraLevel<UBIDI_DEFAULT_LTR)) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if(length==-1) {
        length=u_strlen(text);
    }

    /* special treatment for RUNS_ONLY mode */
    if(pBiDi->reorderingMode==UBIDI_REORDER_RUNS_ONLY) {
        setParaRunsOnly(pBiDi, text, length, paraLevel, pErrorCode);
        return;
    }

    /* initialize the UBiDi structure */
    pBiDi->pParaBiDi=nullptr;          /* mark unfinished setPara */
    pBiDi->text=text;
    pBiDi->length=pBiDi->originalLength=pBiDi->resultLength=length;
    pBiDi->paraLevel=paraLevel;
    pBiDi->direction=(UBiDiDirection)(paraLevel&1);
    pBiDi->paraCount=1;

    pBiDi->dirProps=nullptr;
    pBiDi->levels=nullptr;
    pBiDi->runs=nullptr;
    pBiDi->insertPoints.size=0;         /* clean up from last call */
    pBiDi->insertPoints.confirmed=0;    /* clean up from last call */

    /*
     * Save the original paraLevel if contextual; otherwise, set to 0.
     */
    pBiDi->defaultParaLevel=IS_DEFAULT_LEVEL(paraLevel);

    if(length==0) {
        /*
         * For an empty paragraph, create a UBiDi object with the paraLevel and
         * the flags and the direction set but without allocating zero-length arrays.
         * There is nothing more to do.
         */
        if(IS_DEFAULT_LEVEL(paraLevel)) {
            pBiDi->paraLevel&=1;
            pBiDi->defaultParaLevel=0;
        }
        pBiDi->flags=DIRPROP_FLAG_LR(paraLevel);
        pBiDi->runCount=0;
        pBiDi->paraCount=0;
        setParaSuccess(pBiDi);          /* mark successful setPara */
        return;
    }

    pBiDi->runCount=-1;

    /* allocate paras memory */
    if(pBiDi->parasMemory)
        pBiDi->paras=pBiDi->parasMemory;
    else
        pBiDi->paras=pBiDi->simpleParas;

    /*
     * Get the directional properties,
     * the flags bit-set, and
     * determine the paragraph level if necessary.
     */
    if(getDirPropsMemory(pBiDi, length)) {
        pBiDi->dirProps=pBiDi->dirPropsMemory;
        if(!getDirProps(pBiDi)) {
            *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    } else {
        *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    dirProps=pBiDi->dirProps;
    /* the processed length may have changed if UBIDI_OPTION_STREAMING */
    length= pBiDi->length;
    pBiDi->trailingWSStart=length;  /* the levels[] will reflect the WS run */

    /* are explicit levels specified? */
    if(embeddingLevels==nullptr) {
        /* no: determine explicit levels according to the (Xn) rules */\
        if(getLevelsMemory(pBiDi, length)) {
            pBiDi->levels=pBiDi->levelsMemory;
            direction=resolveExplicitLevels(pBiDi, pErrorCode);
            if(U_FAILURE(*pErrorCode)) {
                return;
            }
        } else {
            *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    } else {
        /* set BN for all explicit codes, check that all levels are 0 or paraLevel..UBIDI_MAX_EXPLICIT_LEVEL */
        pBiDi->levels=embeddingLevels;
        direction=checkExplicitLevels(pBiDi, pErrorCode);
        if(U_FAILURE(*pErrorCode)) {
            return;
        }
    }

    /* allocate isolate memory */
    if(pBiDi->isolateCount<=SIMPLE_ISOLATES_COUNT)
        pBiDi->isolates=pBiDi->simpleIsolates;
    else
        if((int32_t)(pBiDi->isolateCount*sizeof(Isolate))<=pBiDi->isolatesSize)
            pBiDi->isolates=pBiDi->isolatesMemory;
        else {
            if(getInitialIsolatesMemory(pBiDi, pBiDi->isolateCount)) {
                pBiDi->isolates=pBiDi->isolatesMemory;
            } else {
                *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
                return;
            }
        }
    pBiDi->isolateCount=-1;             /* current isolates stack entry == none */

    /*
     * The steps after (X9) in the UBiDi algorithm are performed only if
     * the paragraph text has mixed directionality!
     */
    pBiDi->direction=direction;
    switch(direction) {
    case UBIDI_LTR:
        /* all levels are implicitly at paraLevel (important for ubidi_getLevels()) */
        pBiDi->trailingWSStart=0;
        break;
    case UBIDI_RTL:
        /* all levels are implicitly at paraLevel (important for ubidi_getLevels()) */
        pBiDi->trailingWSStart=0;
        break;
    default:
        /*
         *  Choose the right implicit state table
         */
        switch(pBiDi->reorderingMode) {
        case UBIDI_REORDER_DEFAULT:
            pBiDi->pImpTabPair=&impTab_DEFAULT;
            break;
        case UBIDI_REORDER_NUMBERS_SPECIAL:
            pBiDi->pImpTabPair=&impTab_NUMBERS_SPECIAL;
            break;
        case UBIDI_REORDER_GROUP_NUMBERS_WITH_R:
            pBiDi->pImpTabPair=&impTab_GROUP_NUMBERS_WITH_R;
            break;
        case UBIDI_REORDER_INVERSE_NUMBERS_AS_L:
            pBiDi->pImpTabPair=&impTab_INVERSE_NUMBERS_AS_L;
            break;
        case UBIDI_REORDER_INVERSE_LIKE_DIRECT:
            if (pBiDi->reorderingOptions & UBIDI_OPTION_INSERT_MARKS) {
                pBiDi->pImpTabPair=&impTab_INVERSE_LIKE_DIRECT_WITH_MARKS;
            } else {
                pBiDi->pImpTabPair=&impTab_INVERSE_LIKE_DIRECT;
            }
            break;
        case UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL:
            if (pBiDi->reorderingOptions & UBIDI_OPTION_INSERT_MARKS) {
                pBiDi->pImpTabPair=&impTab_INVERSE_FOR_NUMBERS_SPECIAL_WITH_MARKS;
            } else {
                pBiDi->pImpTabPair=&impTab_INVERSE_FOR_NUMBERS_SPECIAL;
            }
            break;
        default:
            /* we should never get here */
            UPRV_UNREACHABLE_EXIT;
        }
        /*
         * If there are no external levels specified and there
         * are no significant explicit level codes in the text,
         * then we can treat the entire paragraph as one run.
         * Otherwise, we need to perform the following rules on runs of
         * the text with the same embedding levels. (X10)
         * "Significant" explicit level codes are ones that actually
         * affect non-BN characters.
         * Examples for "insignificant" ones are empty embeddings
         * LRE-PDF, LRE-RLE-PDF-PDF, etc.
         */
        if(embeddingLevels==nullptr && pBiDi->paraCount<=1 &&
                                   !(pBiDi->flags&DIRPROP_FLAG_MULTI_RUNS)) {
            resolveImplicitLevels(pBiDi, 0, length,
                                    GET_LR_FROM_LEVEL(GET_PARALEVEL(pBiDi, 0)),
                                    GET_LR_FROM_LEVEL(GET_PARALEVEL(pBiDi, length-1)));
        } else {
            /* sor, eor: start and end types of same-level-run */
            UBiDiLevel *levels=pBiDi->levels;
            int32_t start, limit=0;
            UBiDiLevel level, nextLevel;
            DirProp sor, eor;

            /* determine the first sor and set eor to it because of the loop body (sor=eor there) */
            level=GET_PARALEVEL(pBiDi, 0);
            nextLevel=levels[0];
            if(level<nextLevel) {
                eor=GET_LR_FROM_LEVEL(nextLevel);
            } else {
                eor=GET_LR_FROM_LEVEL(level);
            }

            do {
                /* determine start and limit of the run (end points just behind the run) */

                /* the values for this run's start are the same as for the previous run's end */
                start=limit;
                level=nextLevel;
                if((start>0) && (dirProps[start-1]==B)) {
                    /* except if this is a new paragraph, then set sor = para level */
                    sor=GET_LR_FROM_LEVEL(GET_PARALEVEL(pBiDi, start));
                } else {
                    sor=eor;
                }

                /* search for the limit of this run */
                while((++limit<length) &&
                      ((levels[limit]==level) ||
                       (DIRPROP_FLAG(dirProps[limit])&MASK_BN_EXPLICIT))) {}

                /* get the correct level of the next run */
                if(limit<length) {
                    nextLevel=levels[limit];
                } else {
                    nextLevel=GET_PARALEVEL(pBiDi, length-1);
                }

                /* determine eor from max(level, nextLevel); sor is last run's eor */
                if(NO_OVERRIDE(level)<NO_OVERRIDE(nextLevel)) {
                    eor=GET_LR_FROM_LEVEL(nextLevel);
                } else {
                    eor=GET_LR_FROM_LEVEL(level);
                }

                /* if the run consists of overridden directional types, then there
                   are no implicit types to be resolved */
                if(!(level&UBIDI_LEVEL_OVERRIDE)) {
                    resolveImplicitLevels(pBiDi, start, limit, sor, eor);
                } else {
                    /* remove the UBIDI_LEVEL_OVERRIDE flags */
                    do {
                        levels[start++]&=~UBIDI_LEVEL_OVERRIDE;
                    } while(start<limit);
                }
            } while(limit<length);
        }
        /* check if we got any memory shortage while adding insert points */
        if (U_FAILURE(pBiDi->insertPoints.errorCode))
        {
            *pErrorCode=pBiDi->insertPoints.errorCode;
            return;
        }
        /* reset the embedding levels for some non-graphic characters (L1), (X9) */
        adjustWSLevels(pBiDi);
        break;
    }
    /* add RLM for inverse Bidi with contextual orientation resolving
     * to RTL which would not round-trip otherwise
     */
    if((pBiDi->defaultParaLevel>0) &&
       (pBiDi->reorderingOptions & UBIDI_OPTION_INSERT_MARKS) &&
       ((pBiDi->reorderingMode==UBIDI_REORDER_INVERSE_LIKE_DIRECT) ||
        (pBiDi->reorderingMode==UBIDI_REORDER_INVERSE_FOR_NUMBERS_SPECIAL))) {
        int32_t i, j, start, last;
        UBiDiLevel level;
        DirProp dirProp;
        for(i=0; i<pBiDi->paraCount; i++) {
            last=(pBiDi->paras[i].limit)-1;
            level= static_cast<UBiDiLevel>(pBiDi->paras[i].level);
            if(level==0)
                continue;           /* LTR paragraph */
            start= i==0 ? 0 : pBiDi->paras[i-1].limit;
            for(j=last; j>=start; j--) {
                dirProp=dirProps[j];
                if(dirProp==L) {
                    if(j<last) {
                        while(dirProps[last]==B) {
                            last--;
                        }
                    }
                    addPoint(pBiDi, last, RLM_BEFORE);
                    break;
                }
                if(DIRPROP_FLAG(dirProp) & MASK_R_AL) {
                    break;
                }
            }
        }
    }

    if(pBiDi->reorderingOptions & UBIDI_OPTION_REMOVE_CONTROLS) {
        pBiDi->resultLength -= pBiDi->controlCount;
    } else {
        pBiDi->resultLength += pBiDi->insertPoints.size;
    }
    setParaSuccess(pBiDi);              /* mark successful setPara */
}

U_CAPI void U_EXPORT2
ubidi_orderParagraphsLTR(UBiDi *pBiDi, UBool orderParagraphsLTR) {
    if(pBiDi!=nullptr) {
        pBiDi->orderParagraphsLTR=orderParagraphsLTR;
    }
}

U_CAPI UBool U_EXPORT2
ubidi_isOrderParagraphsLTR(UBiDi *pBiDi) {
    if(pBiDi!=nullptr) {
        return pBiDi->orderParagraphsLTR;
    } else {
        return false;
    }
}

U_CAPI UBiDiDirection U_EXPORT2
ubidi_getDirection(const UBiDi *pBiDi) {
    if(IS_VALID_PARA_OR_LINE(pBiDi)) {
        return pBiDi->direction;
    } else {
        return UBIDI_LTR;
    }
}

U_CAPI const char16_t * U_EXPORT2
ubidi_getText(const UBiDi *pBiDi) {
    if(IS_VALID_PARA_OR_LINE(pBiDi)) {
        return pBiDi->text;
    } else {
        return nullptr;
    }
}

U_CAPI int32_t U_EXPORT2
ubidi_getLength(const UBiDi *pBiDi) {
    if(IS_VALID_PARA_OR_LINE(pBiDi)) {
        return pBiDi->originalLength;
    } else {
        return 0;
    }
}

U_CAPI int32_t U_EXPORT2
ubidi_getProcessedLength(const UBiDi *pBiDi) {
    if(IS_VALID_PARA_OR_LINE(pBiDi)) {
        return pBiDi->length;
    } else {
        return 0;
    }
}

U_CAPI int32_t U_EXPORT2
ubidi_getResultLength(const UBiDi *pBiDi) {
    if(IS_VALID_PARA_OR_LINE(pBiDi)) {
        return pBiDi->resultLength;
    } else {
        return 0;
    }
}

/* paragraphs API functions ------------------------------------------------- */

U_CAPI UBiDiLevel U_EXPORT2
ubidi_getParaLevel(const UBiDi *pBiDi) {
    if(IS_VALID_PARA_OR_LINE(pBiDi)) {
        return pBiDi->paraLevel;
    } else {
        return 0;
    }
}

U_CAPI int32_t U_EXPORT2
ubidi_countParagraphs(UBiDi *pBiDi) {
    if(!IS_VALID_PARA_OR_LINE(pBiDi)) {
        return 0;
    } else {
        return pBiDi->paraCount;
    }
}

U_CAPI void U_EXPORT2
ubidi_getParagraphByIndex(const UBiDi *pBiDi, int32_t paraIndex,
                          int32_t *pParaStart, int32_t *pParaLimit,
                          UBiDiLevel *pParaLevel, UErrorCode *pErrorCode) {
    int32_t paraStart;

    /* check the argument values */
    RETURN_VOID_IF_NULL_OR_FAILING_ERRCODE(pErrorCode);
    RETURN_VOID_IF_NOT_VALID_PARA_OR_LINE(pBiDi, *pErrorCode);
    RETURN_VOID_IF_BAD_RANGE(paraIndex, 0, pBiDi->paraCount, *pErrorCode);

    pBiDi=pBiDi->pParaBiDi;             /* get Para object if Line object */
    if(paraIndex) {
        paraStart=pBiDi->paras[paraIndex-1].limit;
    } else {
        paraStart=0;
    }
    if(pParaStart!=nullptr) {
        *pParaStart=paraStart;
    }
    if(pParaLimit!=nullptr) {
        *pParaLimit=pBiDi->paras[paraIndex].limit;
    }
    if(pParaLevel!=nullptr) {
        *pParaLevel=GET_PARALEVEL(pBiDi, paraStart);
    }
}

U_CAPI int32_t U_EXPORT2
ubidi_getParagraph(const UBiDi *pBiDi, int32_t charIndex,
                          int32_t *pParaStart, int32_t *pParaLimit,
                          UBiDiLevel *pParaLevel, UErrorCode *pErrorCode) {
    int32_t paraIndex;

    /* check the argument values */
    /* pErrorCode will be checked by the call to ubidi_getParagraphByIndex */
    RETURN_IF_NULL_OR_FAILING_ERRCODE(pErrorCode, -1);
    RETURN_IF_NOT_VALID_PARA_OR_LINE(pBiDi, *pErrorCode, -1);
    pBiDi=pBiDi->pParaBiDi;             /* get Para object if Line object */
    RETURN_IF_BAD_RANGE(charIndex, 0, pBiDi->length, *pErrorCode, -1);

    for(paraIndex=0; charIndex>=pBiDi->paras[paraIndex].limit; paraIndex++);
    ubidi_getParagraphByIndex(pBiDi, paraIndex, pParaStart, pParaLimit, pParaLevel, pErrorCode);
    return paraIndex;
}

U_CAPI void U_EXPORT2
ubidi_setClassCallback(UBiDi *pBiDi, UBiDiClassCallback *newFn,
                       const void *newContext, UBiDiClassCallback **oldFn,
                       const void **oldContext, UErrorCode *pErrorCode)
{
    RETURN_VOID_IF_NULL_OR_FAILING_ERRCODE(pErrorCode);
    if(pBiDi==nullptr) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if( oldFn )
    {
        *oldFn = pBiDi->fnClassCallback;
    }
    if( oldContext )
    {
        *oldContext = pBiDi->coClassCallback;
    }
    pBiDi->fnClassCallback = newFn;
    pBiDi->coClassCallback = newContext;
}

U_CAPI void U_EXPORT2
ubidi_getClassCallback(UBiDi *pBiDi, UBiDiClassCallback **fn, const void **context)
{
    if(pBiDi==nullptr) {
        return;
    }
    if( fn )
    {
        *fn = pBiDi->fnClassCallback;
    }
    if( context )
    {
        *context = pBiDi->coClassCallback;
    }
}

U_CAPI UCharDirection U_EXPORT2
ubidi_getCustomizedClass(UBiDi *pBiDi, UChar32 c)
{
    UCharDirection dir;

    if( pBiDi->fnClassCallback == nullptr ||
        (dir = (*pBiDi->fnClassCallback)(pBiDi->coClassCallback, c)) == U_BIDI_CLASS_DEFAULT )
    {
        dir = ubidi_getClass(c);
    }
    if(dir >= U_CHAR_DIRECTION_COUNT) {
        dir = (UCharDirection)ON;
    }
    return dir;
}
