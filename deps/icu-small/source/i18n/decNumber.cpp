// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/* ------------------------------------------------------------------ */
/* Decimal Number arithmetic module                                   */
/* ------------------------------------------------------------------ */
/* Copyright (c) IBM Corporation, 2000-2014.  All rights reserved.    */
/*                                                                    */
/* This software is made available under the terms of the             */
/* ICU License -- ICU 1.8.1 and later.                                */
/*                                                                    */
/* The description and User's Guide ("The decNumber C Library") for   */
/* this software is called decNumber.pdf.  This document is           */
/* available, together with arithmetic and format specifications,     */
/* testcases, and Web links, on the General Decimal Arithmetic page.  */
/*                                                                    */
/* Please send comments, suggestions, and corrections to the author:  */
/*   mfc@uk.ibm.com                                                   */
/*   Mike Cowlishaw, IBM Fellow                                       */
/*   IBM UK, PO Box 31, Birmingham Road, Warwick CV34 5JL, UK         */
/* ------------------------------------------------------------------ */

/* Modified version, for use from within ICU.
 *    Renamed public functions, to avoid an unwanted export of the 
 *    standard names from the ICU library.
 *
 *    Use ICU's uprv_malloc() and uprv_free()
 *
 *    Revert comment syntax to plain C
 *
 *    Remove a few compiler warnings.
 */

/* This module comprises the routines for arbitrary-precision General */
/* Decimal Arithmetic as defined in the specification which may be    */
/* found on the General Decimal Arithmetic pages.  It implements both */
/* the full ('extended') arithmetic and the simpler ('subset')        */
/* arithmetic.                                                        */
/*                                                                    */
/* Usage notes:                                                       */
/*                                                                    */
/* 1. This code is ANSI C89 except:                                   */
/*                                                                    */
/*    a) C99 line comments (double forward slash) are used.  (Most C  */
/*       compilers accept these.  If yours does not, a simple script  */
/*       can be used to convert them to ANSI C comments.)             */
/*                                                                    */
/*    b) Types from C99 stdint.h are used.  If you do not have this   */
/*       header file, see the User's Guide section of the decNumber   */
/*       documentation; this lists the necessary definitions.         */
/*                                                                    */
/*    c) If DECDPUN>4 or DECUSE64=1, the C99 64-bit int64_t and       */
/*       uint64_t types may be used.  To avoid these, set DECUSE64=0  */
/*       and DECDPUN<=4 (see documentation).                          */
/*                                                                    */
/*    The code also conforms to C99 restrictions; in particular,      */
/*    strict aliasing rules are observed.                             */
/*                                                                    */
/* 2. The decNumber format which this library uses is optimized for   */
/*    efficient processing of relatively short numbers; in particular */
/*    it allows the use of fixed sized structures and minimizes copy  */
/*    and move operations.  It does, however, support arbitrary       */
/*    precision (up to 999,999,999 digits) and arbitrary exponent     */
/*    range (Emax in the range 0 through 999,999,999 and Emin in the  */
/*    range -999,999,999 through 0).  Mathematical functions (for     */
/*    example decNumberExp) as identified below are restricted more   */
/*    tightly: digits, emax, and -emin in the context must be <=      */
/*    DEC_MAX_MATH (999999), and their operand(s) must be within      */
/*    these bounds.                                                   */
/*                                                                    */
/* 3. Logical functions are further restricted; their operands must   */
/*    be finite, positive, have an exponent of zero, and all digits   */
/*    must be either 0 or 1.  The result will only contain digits     */
/*    which are 0 or 1 (and will have exponent=0 and a sign of 0).    */
/*                                                                    */
/* 4. Operands to operator functions are never modified unless they   */
/*    are also specified to be the result number (which is always     */
/*    permitted).  Other than that case, operands must not overlap.   */
/*                                                                    */
/* 5. Error handling: the type of the error is ORed into the status   */
/*    flags in the current context (decContext structure).  The       */
/*    SIGFPE signal is then raised if the corresponding trap-enabler  */
/*    flag in the decContext is set (is 1).                           */
/*                                                                    */
/*    It is the responsibility of the caller to clear the status      */
/*    flags as required.                                              */
/*                                                                    */
/*    The result of any routine which returns a number will always    */
/*    be a valid number (which may be a special value, such as an     */
/*    Infinity or NaN).                                               */
/*                                                                    */
/* 6. The decNumber format is not an exchangeable concrete            */
/*    representation as it comprises fields which may be machine-     */
/*    dependent (packed or unpacked, or special length, for example). */
/*    Canonical conversions to and from strings are provided; other   */
/*    conversions are available in separate modules.                  */
/*                                                                    */
/* 7. Normally, input operands are assumed to be valid.  Set DECCHECK */
/*    to 1 for extended operand checking (including nullptr operands).   */
/*    Results are undefined if a badly-formed structure (or a nullptr    */
/*    pointer to a structure) is provided, though with DECCHECK       */
/*    enabled the operator routines are protected against exceptions. */
/*    (Except if the result pointer is nullptr, which is unrecoverable.) */
/*                                                                    */
/*    However, the routines will never cause exceptions if they are   */
/*    given well-formed operands, even if the value of the operands   */
/*    is inappropriate for the operation and DECCHECK is not set.     */
/*    (Except for SIGFPE, as and where documented.)                   */
/*                                                                    */
/* 8. Subset arithmetic is available only if DECSUBSET is set to 1.   */
/* ------------------------------------------------------------------ */
/* Implementation notes for maintenance of this module:               */
/*                                                                    */
/* 1. Storage leak protection:  Routines which use malloc are not     */
/*    permitted to use return for fastpath or error exits (i.e.,      */
/*    they follow strict structured programming conventions).         */
/*    Instead they have a do{}while(0); construct surrounding the     */
/*    code which is protected -- break may be used to exit this.      */
/*    Other routines can safely use the return statement inline.      */
/*                                                                    */
/*    Storage leak accounting can be enabled using DECALLOC.          */
/*                                                                    */
/* 2. All loops use the for(;;) construct.  Any do construct does     */
/*    not loop; it is for allocation protection as just described.    */
/*                                                                    */
/* 3. Setting status in the context must always be the very last      */
/*    action in a routine, as non-0 status may raise a trap and hence */
/*    the call to set status may not return (if the handler uses long */
/*    jump).  Therefore all cleanup must be done first.  In general,  */
/*    to achieve this status is accumulated and is only applied just  */
/*    before return by calling decContextSetStatus (via decStatus).   */
/*                                                                    */
/*    Routines which allocate storage cannot, in general, use the     */
/*    'top level' routines which could cause a non-returning          */
/*    transfer of control.  The decXxxxOp routines are safe (do not   */
/*    call decStatus even if traps are set in the context) and should */
/*    be used instead (they are also a little faster).                */
/*                                                                    */
/* 4. Exponent checking is minimized by allowing the exponent to      */
/*    grow outside its limits during calculations, provided that      */
/*    the decFinalize function is called later.  Multiplication and   */
/*    division, and intermediate calculations in exponentiation,      */
/*    require more careful checks because of the risk of 31-bit       */
/*    overflow (the most negative valid exponent is -1999999997, for  */
/*    a 999999999-digit number with adjusted exponent of -999999999). */
/*                                                                    */
/* 5. Rounding is deferred until finalization of results, with any    */
/*    'off to the right' data being represented as a single digit     */
/*    residue (in the range -1 through 9).  This avoids any double-   */
/*    rounding when more than one shortening takes place (for         */
/*    example, when a result is subnormal).                           */
/*                                                                    */
/* 6. The digits count is allowed to rise to a multiple of DECDPUN    */
/*    during many operations, so whole Units are handled and exact    */
/*    accounting of digits is not needed.  The correct digits value   */
/*    is found by decGetDigits, which accounts for leading zeros.     */
/*    This must be called before any rounding if the number of digits */
/*    is not known exactly.                                           */
/*                                                                    */
/* 7. The multiply-by-reciprocal 'trick' is used for partitioning     */
/*    numbers up to four digits, using appropriate constants.  This   */
/*    is not useful for longer numbers because overflow of 32 bits    */
/*    would lead to 4 multiplies, which is almost as expensive as     */
/*    a divide (unless a floating-point or 64-bit multiply is         */
/*    assumed to be available).                                       */
/*                                                                    */
/* 8. Unusual abbreviations that may be used in the commentary:       */
/*      lhs -- left hand side (operand, of an operation)              */
/*      lsd -- least significant digit (of coefficient)               */
/*      lsu -- least significant Unit (of coefficient)                */
/*      msd -- most significant digit (of coefficient)                */
/*      msi -- most significant item (in an array)                    */
/*      msu -- most significant Unit (of coefficient)                 */
/*      rhs -- right hand side (operand, of an operation)             */
/*      +ve -- positive                                               */
/*      -ve -- negative                                               */
/*      **  -- raise to the power                                     */
/* ------------------------------------------------------------------ */

#include <stdlib.h>                /* for malloc, free, etc.  */
/*  #include <stdio.h>   */        /* for printf [if needed]  */
#include <string.h>                /* for strcpy  */
#include <ctype.h>                 /* for lower  */
#include "cmemory.h"               /* for uprv_malloc, etc., in ICU */
#include "decNumber.h"             /* base number library  */
#include "decNumberLocal.h"        /* decNumber local types, etc.  */
#include "uassert.h"

/* Constants */
/* Public lookup table used by the D2U macro  */
static const uByte d2utable[DECMAXD2U+1]=D2UTABLE;

#define DECVERB     1              /* set to 1 for verbose DECCHECK  */
#define powers      DECPOWERS      /* old internal name  */

/* Local constants  */
#define DIVIDE      0x80           /* Divide operators  */
#define REMAINDER   0x40           /* ..  */
#define DIVIDEINT   0x20           /* ..  */
#define REMNEAR     0x10           /* ..  */
#define COMPARE     0x01           /* Compare operators  */
#define COMPMAX     0x02           /* ..  */
#define COMPMIN     0x03           /* ..  */
#define COMPTOTAL   0x04           /* ..  */
#define COMPNAN     0x05           /* .. [NaN processing]  */
#define COMPSIG     0x06           /* .. [signaling COMPARE]  */
#define COMPMAXMAG  0x07           /* ..  */
#define COMPMINMAG  0x08           /* ..  */

#define DEC_sNaN     0x40000000    /* local status: sNaN signal  */
#define BADINT  (Int)0x80000000    /* most-negative Int; error indicator  */
/* Next two indicate an integer >= 10**6, and its parity (bottom bit)  */
#define BIGEVEN (Int)0x80000002
#define BIGODD  (Int)0x80000003

static const Unit uarrone[1]={1};   /* Unit array of 1, used for incrementing  */

/* ------------------------------------------------------------------ */
/* round-for-reround digits                                           */
/* ------------------------------------------------------------------ */
#if 0
static const uByte DECSTICKYTAB[10]={1,1,2,3,4,6,6,7,8,9}; /* used if sticky */
#endif

/* ------------------------------------------------------------------ */
/* Powers of ten (powers[n]==10**n, 0<=n<=9)                          */
/* ------------------------------------------------------------------ */
static const uInt DECPOWERS[10]={1, 10, 100, 1000, 10000, 100000, 1000000,
                          10000000, 100000000, 1000000000};


/* Granularity-dependent code */
#if DECDPUN<=4
  #define eInt  Int           /* extended integer  */
  #define ueInt uInt          /* unsigned extended integer  */
  /* Constant multipliers for divide-by-power-of five using reciprocal  */
  /* multiply, after removing powers of 2 by shifting, and final shift  */
  /* of 17 [we only need up to **4]  */
  static const uInt multies[]={131073, 26215, 5243, 1049, 210};
  /* QUOT10 -- macro to return the quotient of unit u divided by 10**n  */
  #define QUOT10(u, n) ((((uInt)(u)>>(n))*multies[n])>>17)
#else
  /* For DECDPUN>4 non-ANSI-89 64-bit types are needed.  */
  #if !DECUSE64
    #error decNumber.c: DECUSE64 must be 1 when DECDPUN>4
  #endif
  #define eInt  Long          /* extended integer  */
  #define ueInt uLong         /* unsigned extended integer  */
#endif

/* Local routines */
static decNumber * decAddOp(decNumber *, const decNumber *, const decNumber *,
                              decContext *, uByte, uInt *);
static Flag        decBiStr(const char *, const char *, const char *);
static uInt        decCheckMath(const decNumber *, decContext *, uInt *);
static void        decApplyRound(decNumber *, decContext *, Int, uInt *);
static Int         decCompare(const decNumber *lhs, const decNumber *rhs, Flag);
static decNumber * decCompareOp(decNumber *, const decNumber *,
                              const decNumber *, decContext *,
                              Flag, uInt *);
static void        decCopyFit(decNumber *, const decNumber *, decContext *,
                              Int *, uInt *);
static decNumber * decDecap(decNumber *, Int);
static decNumber * decDivideOp(decNumber *, const decNumber *,
                              const decNumber *, decContext *, Flag, uInt *);
static decNumber * decExpOp(decNumber *, const decNumber *,
                              decContext *, uInt *);
static void        decFinalize(decNumber *, decContext *, Int *, uInt *);
static Int         decGetDigits(Unit *, Int);
static Int         decGetInt(const decNumber *);
static decNumber * decLnOp(decNumber *, const decNumber *,
                              decContext *, uInt *);
static decNumber * decMultiplyOp(decNumber *, const decNumber *,
                              const decNumber *, decContext *,
                              uInt *);
static decNumber * decNaNs(decNumber *, const decNumber *,
                              const decNumber *, decContext *, uInt *);
static decNumber * decQuantizeOp(decNumber *, const decNumber *,
                              const decNumber *, decContext *, Flag,
                              uInt *);
static void        decReverse(Unit *, Unit *);
static void        decSetCoeff(decNumber *, decContext *, const Unit *,
                              Int, Int *, uInt *);
static void        decSetMaxValue(decNumber *, decContext *);
static void        decSetOverflow(decNumber *, decContext *, uInt *);
static void        decSetSubnormal(decNumber *, decContext *, Int *, uInt *);
static Int         decShiftToLeast(Unit *, Int, Int);
static Int         decShiftToMost(Unit *, Int, Int);
static void        decStatus(decNumber *, uInt, decContext *);
static void        decToString(const decNumber *, char[], Flag);
static decNumber * decTrim(decNumber *, decContext *, Flag, Flag, Int *);
static Int         decUnitAddSub(const Unit *, Int, const Unit *, Int, Int,
                              Unit *, Int);
static Int         decUnitCompare(const Unit *, Int, const Unit *, Int, Int);

#if !DECSUBSET
/* decFinish == decFinalize when no subset arithmetic needed */
#define decFinish(a,b,c,d) decFinalize(a,b,c,d)
#else
static void        decFinish(decNumber *, decContext *, Int *, uInt *);
static decNumber * decRoundOperand(const decNumber *, decContext *, uInt *);
#endif

/* Local macros */
/* masked special-values bits  */
#define SPECIALARG  (rhs->bits & DECSPECIAL)
#define SPECIALARGS ((lhs->bits | rhs->bits) & DECSPECIAL)

/* For use in ICU */
#define malloc(a) uprv_malloc(a)
#define free(a) uprv_free(a)

/* Diagnostic macros, etc. */
#if DECALLOC
/* Handle malloc/free accounting.  If enabled, our accountable routines  */
/* are used; otherwise the code just goes straight to the system malloc  */
/* and free routines.  */
#define malloc(a) decMalloc(a)
#define free(a) decFree(a)
#define DECFENCE 0x5a              /* corruption detector  */
/* 'Our' malloc and free:  */
static void *decMalloc(size_t);
static void  decFree(void *);
uInt decAllocBytes=0;              /* count of bytes allocated  */
/* Note that DECALLOC code only checks for storage buffer overflow.  */
/* To check for memory leaks, the decAllocBytes variable must be  */
/* checked to be 0 at appropriate times (e.g., after the test  */
/* harness completes a set of tests).  This checking may be unreliable  */
/* if the testing is done in a multi-thread environment.  */
#endif

#if DECCHECK
/* Optional checking routines.  Enabling these means that decNumber  */
/* and decContext operands to operator routines are checked for  */
/* correctness.  This roughly doubles the execution time of the  */
/* fastest routines (and adds 600+ bytes), so should not normally be  */
/* used in 'production'.  */
/* decCheckInexact is used to check that inexact results have a full  */
/* complement of digits (where appropriate -- this is not the case  */
/* for Quantize, for example)  */
#define DECUNRESU ((decNumber *)(void *)0xffffffff)
#define DECUNUSED ((const decNumber *)(void *)0xffffffff)
#define DECUNCONT ((decContext *)(void *)(0xffffffff))
static Flag decCheckOperands(decNumber *, const decNumber *,
                             const decNumber *, decContext *);
static Flag decCheckNumber(const decNumber *);
static void decCheckInexact(const decNumber *, decContext *);
#endif

#if DECTRACE || DECCHECK
/* Optional trace/debugging routines (may or may not be used)  */
void decNumberShow(const decNumber *);  /* displays the components of a number  */
static void decDumpAr(char, const Unit *, Int);
#endif

/* ================================================================== */
/* Conversions                                                        */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* from-int32 -- conversion from Int or uInt                          */
/*                                                                    */
/*  dn is the decNumber to receive the integer                        */
/*  in or uin is the integer to be converted                          */
/*  returns dn                                                        */
/*                                                                    */
/* No error is possible.                                              */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberFromInt32(decNumber *dn, Int in) {
  uInt unsig;
  if (in>=0) unsig=in;
   else {                               /* negative (possibly BADINT)  */
    if (in==BADINT) unsig=(uInt)1073741824*2; /* special case  */
     else unsig=-in;                    /* invert  */
    }
  /* in is now positive  */
  uprv_decNumberFromUInt32(dn, unsig);
  if (in<0) dn->bits=DECNEG;            /* sign needed  */
  return dn;
  } /* decNumberFromInt32  */

U_CAPI decNumber * U_EXPORT2 uprv_decNumberFromUInt32(decNumber *dn, uInt uin) {
  Unit *up;                             /* work pointer  */
  uprv_decNumberZero(dn);                    /* clean  */
  if (uin==0) return dn;                /* [or decGetDigits bad call]  */
  for (up=dn->lsu; uin>0; up++) {
    *up=(Unit)(uin%(DECDPUNMAX+1));
    uin=uin/(DECDPUNMAX+1);
    }
  dn->digits=decGetDigits(dn->lsu, static_cast<int32_t>(up - dn->lsu));
  return dn;
  } /* decNumberFromUInt32  */

/* ------------------------------------------------------------------ */
/* to-int32 -- conversion to Int or uInt                              */
/*                                                                    */
/*  dn is the decNumber to convert                                    */
/*  set is the context for reporting errors                           */
/*  returns the converted decNumber, or 0 if Invalid is set           */
/*                                                                    */
/* Invalid is set if the decNumber does not have exponent==0 or if    */
/* it is a NaN, Infinite, or out-of-range.                            */
/* ------------------------------------------------------------------ */
U_CAPI Int U_EXPORT2 uprv_decNumberToInt32(const decNumber *dn, decContext *set) {
  #if DECCHECK
  if (decCheckOperands(DECUNRESU, DECUNUSED, dn, set)) return 0;
  #endif

  /* special or too many digits, or bad exponent  */
  if (dn->bits&DECSPECIAL || dn->digits>10 || dn->exponent!=0) ; /* bad  */
   else { /* is a finite integer with 10 or fewer digits  */
    Int d;                         /* work  */
    const Unit *up;                /* ..  */
    uInt hi=0, lo;                 /* ..  */
    up=dn->lsu;                    /* -> lsu  */
    lo=*up;                        /* get 1 to 9 digits  */
    #if DECDPUN>1                  /* split to higher  */
      hi=lo/10;
      lo=lo%10;
    #endif
    up++;
    /* collect remaining Units, if any, into hi  */
    for (d=DECDPUN; d<dn->digits; up++, d+=DECDPUN) hi+=*up*powers[d-1];
    /* now low has the lsd, hi the remainder  */
    if (hi>214748364 || (hi==214748364 && lo>7)) { /* out of range?  */
      /* most-negative is a reprieve  */
      if (dn->bits&DECNEG && hi==214748364 && lo==8) return 0x80000000;
      /* bad -- drop through  */
      }
     else { /* in-range always  */
      Int i=X10(hi)+lo;
      if (dn->bits&DECNEG) return -i;
      return i;
      }
    } /* integer  */
  uprv_decContextSetStatus(set, DEC_Invalid_operation); /* [may not return]  */
  return 0;
  } /* decNumberToInt32  */

U_CAPI uInt U_EXPORT2 uprv_decNumberToUInt32(const decNumber *dn, decContext *set) {
  #if DECCHECK
  if (decCheckOperands(DECUNRESU, DECUNUSED, dn, set)) return 0;
  #endif
  /* special or too many digits, or bad exponent, or negative (<0)  */
  if (dn->bits&DECSPECIAL || dn->digits>10 || dn->exponent!=0
    || (dn->bits&DECNEG && !ISZERO(dn)));                   /* bad  */
   else { /* is a finite integer with 10 or fewer digits  */
    Int d;                         /* work  */
    const Unit *up;                /* ..  */
    uInt hi=0, lo;                 /* ..  */
    up=dn->lsu;                    /* -> lsu  */
    lo=*up;                        /* get 1 to 9 digits  */
    #if DECDPUN>1                  /* split to higher  */
      hi=lo/10;
      lo=lo%10;
    #endif
    up++;
    /* collect remaining Units, if any, into hi  */
    for (d=DECDPUN; d<dn->digits; up++, d+=DECDPUN) hi+=*up*powers[d-1];

    /* now low has the lsd, hi the remainder  */
    if (hi>429496729 || (hi==429496729 && lo>5)) ; /* no reprieve possible  */
     else return X10(hi)+lo;
    } /* integer  */
  uprv_decContextSetStatus(set, DEC_Invalid_operation); /* [may not return]  */
  return 0;
  } /* decNumberToUInt32  */

/* ------------------------------------------------------------------ */
/* to-scientific-string -- conversion to numeric string               */
/* to-engineering-string -- conversion to numeric string              */
/*                                                                    */
/*   decNumberToString(dn, string);                                   */
/*   decNumberToEngString(dn, string);                                */
/*                                                                    */
/*  dn is the decNumber to convert                                    */
/*  string is the string where the result will be laid out            */
/*                                                                    */
/*  string must be at least dn->digits+14 characters long             */
/*                                                                    */
/*  No error is possible, and no status can be set.                   */
/* ------------------------------------------------------------------ */
U_CAPI char * U_EXPORT2 uprv_decNumberToString(const decNumber *dn, char *string){
  decToString(dn, string, 0);
  return string;
  } /* DecNumberToString  */

U_CAPI char * U_EXPORT2 uprv_decNumberToEngString(const decNumber *dn, char *string){
  decToString(dn, string, 1);
  return string;
  } /* DecNumberToEngString  */

/* ------------------------------------------------------------------ */
/* to-number -- conversion from numeric string                        */
/*                                                                    */
/* decNumberFromString -- convert string to decNumber                 */
/*   dn        -- the number structure to fill                        */
/*   chars[]   -- the string to convert ('\0' terminated)             */
/*   set       -- the context used for processing any error,          */
/*                determining the maximum precision available         */
/*                (set.digits), determining the maximum and minimum   */
/*                exponent (set.emax and set.emin), determining if    */
/*                extended values are allowed, and checking the       */
/*                rounding mode if overflow occurs or rounding is     */
/*                needed.                                             */
/*                                                                    */
/* The length of the coefficient and the size of the exponent are     */
/* checked by this routine, so the correct error (Underflow or        */
/* Overflow) can be reported or rounding applied, as necessary.       */
/*                                                                    */
/* If bad syntax is detected, the result will be a quiet NaN.         */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberFromString(decNumber *dn, const char chars[],
                                decContext *set) {
  Int   exponent=0;                /* working exponent [assume 0]  */
  uByte bits=0;                    /* working flags [assume +ve]  */
  Unit  *res;                      /* where result will be built  */
  Unit  resbuff[SD2U(DECBUFFER+9)];/* local buffer in case need temporary  */
                                   /* [+9 allows for ln() constants]  */
  Unit  *allocres=nullptr;            /* -> allocated result, iff allocated  */
  Int   d=0;                       /* count of digits found in decimal part  */
  const char *dotchar=nullptr;        /* where dot was found  */
  const char *cfirst=chars;        /* -> first character of decimal part  */
  const char *last=nullptr;           /* -> last digit of decimal part  */
  const char *c;                   /* work  */
  Unit  *up;                       /* ..  */
  #if DECDPUN>1
  Int   cut, out;                  /* ..  */
  #endif
  Int   residue;                   /* rounding residue  */
  uInt  status=0;                  /* error code  */

  #if DECCHECK
  if (decCheckOperands(DECUNRESU, DECUNUSED, DECUNUSED, set))
    return uprv_decNumberZero(dn);
  #endif

  do {                             /* status & malloc protection  */
    for (c=chars;; c++) {          /* -> input character  */
      if (*c>='0' && *c<='9') {    /* test for Arabic digit  */
        last=c;
        d++;                       /* count of real digits  */
        continue;                  /* still in decimal part  */
        }
      if (*c=='.' && dotchar==nullptr) { /* first '.'  */
        dotchar=c;                 /* record offset into decimal part  */
        if (c==cfirst) cfirst++;   /* first digit must follow  */
        continue;}
      if (c==chars) {              /* first in string...  */
        if (*c=='-') {             /* valid - sign  */
          cfirst++;
          bits=DECNEG;
          continue;}
        if (*c=='+') {             /* valid + sign  */
          cfirst++;
          continue;}
        }
      /* *c is not a digit, or a valid +, -, or '.'  */
      break;
      } /* c  */

    if (last==nullptr) {              /* no digits yet  */
      status=DEC_Conversion_syntax;/* assume the worst  */
      if (*c=='\0') break;         /* and no more to come...  */
      #if DECSUBSET
      /* if subset then infinities and NaNs are not allowed  */
      if (!set->extended) break;   /* hopeless  */
      #endif
      /* Infinities and NaNs are possible, here  */
      if (dotchar!=nullptr) break;    /* .. unless had a dot  */
      uprv_decNumberZero(dn);           /* be optimistic  */
      if (decBiStr(c, "infinity", "INFINITY")
       || decBiStr(c, "inf", "INF")) {
        dn->bits=bits | DECINF;
        status=0;                  /* is OK  */
        break; /* all done  */
        }
      /* a NaN expected  */
      /* 2003.09.10 NaNs are now permitted to have a sign  */
      dn->bits=bits | DECNAN;      /* assume simple NaN  */
      if (*c=='s' || *c=='S') {    /* looks like an sNaN  */
        c++;
        dn->bits=bits | DECSNAN;
        }
      if (*c!='n' && *c!='N') break;    /* check caseless "NaN"  */
      c++;
      if (*c!='a' && *c!='A') break;    /* ..  */
      c++;
      if (*c!='n' && *c!='N') break;    /* ..  */
      c++;
      /* now either nothing, or nnnn payload, expected  */
      /* -> start of integer and skip leading 0s [including plain 0]  */
      for (cfirst=c; *cfirst=='0';) cfirst++;
      if (*cfirst=='\0') {         /* "NaN" or "sNaN", maybe with all 0s  */
        status=0;                  /* it's good  */
        break;                     /* ..  */
        }
      /* something other than 0s; setup last and d as usual [no dots]  */
      for (c=cfirst;; c++, d++) {
        if (*c<'0' || *c>'9') break; /* test for Arabic digit  */
        last=c;
        }
      if (*c!='\0') break;         /* not all digits  */
      if (d>set->digits-1) {
        /* [NB: payload in a decNumber can be full length unless  */
        /* clamped, in which case can only be digits-1]  */
        if (set->clamp) break;
        if (d>set->digits) break;
        } /* too many digits?  */
      /* good; drop through to convert the integer to coefficient  */
      status=0;                    /* syntax is OK  */
      bits=dn->bits;               /* for copy-back  */
      } /* last==nullptr  */

     else if (*c!='\0') {          /* more to process...  */
      /* had some digits; exponent is only valid sequence now  */
      Flag nege;                   /* 1=negative exponent  */
      const char *firstexp;        /* -> first significant exponent digit  */
      status=DEC_Conversion_syntax;/* assume the worst  */
      if (*c!='e' && *c!='E') break;
      /* Found 'e' or 'E' -- now process explicit exponent */
      /* 1998.07.11: sign no longer required  */
      nege=0;
      c++;                         /* to (possible) sign  */
      if (*c=='-') {nege=1; c++;}
       else if (*c=='+') c++;
      if (*c=='\0') break;

      for (; *c=='0' && *(c+1)!='\0';) c++;  /* strip insignificant zeros  */
      firstexp=c;                            /* save exponent digit place  */
      uInt uexponent = 0;   /* Avoid undefined behavior on signed int overflow */
      for (; ;c++) {
        if (*c<'0' || *c>'9') break;         /* not a digit  */
        uexponent=X10(uexponent)+(uInt)*c-(uInt)'0';
        } /* c  */
      exponent = (Int)uexponent;
      /* if not now on a '\0', *c must not be a digit  */
      if (*c!='\0') break;

      /* (this next test must be after the syntax checks)  */
      /* if it was too long the exponent may have wrapped, so check  */
      /* carefully and set it to a certain overflow if wrap possible  */
      if (c>=firstexp+9+1) {
        if (c>firstexp+9+1 || *firstexp>'1') exponent=DECNUMMAXE*2;
        /* [up to 1999999999 is OK, for example 1E-1000000998]  */
        }
      if (nege) exponent=-exponent;     /* was negative  */
      status=0;                         /* is OK  */
      } /* stuff after digits  */

    /* Here when whole string has been inspected; syntax is good  */
    /* cfirst->first digit (never dot), last->last digit (ditto)  */

    /* strip leading zeros/dot [leave final 0 if all 0's]  */
    if (*cfirst=='0') {                 /* [cfirst has stepped over .]  */
      for (c=cfirst; c<last; c++, cfirst++) {
        if (*c=='.') continue;          /* ignore dots  */
        if (*c!='0') break;             /* non-zero found  */
        d--;                            /* 0 stripped  */
        } /* c  */
      #if DECSUBSET
      /* make a rapid exit for easy zeros if !extended  */
      if (*cfirst=='0' && !set->extended) {
        uprv_decNumberZero(dn);              /* clean result  */
        break;                          /* [could be return]  */
        }
      #endif
      } /* at least one leading 0  */

    /* Handle decimal point...  */
    if (dotchar!=nullptr && dotchar<last)  /* non-trailing '.' found?  */
      exponent -= static_cast<int32_t>(last-dotchar);         /* adjust exponent  */
    /* [we can now ignore the .]  */

    /* OK, the digits string is good.  Assemble in the decNumber, or in  */
    /* a temporary units array if rounding is needed  */
    if (d<=set->digits) res=dn->lsu;    /* fits into supplied decNumber  */
     else {                             /* rounding needed  */
      Int needbytes=D2U(d)*sizeof(Unit);/* bytes needed  */
      res=resbuff;                      /* assume use local buffer  */
      if (needbytes>(Int)sizeof(resbuff)) { /* too big for local  */
        allocres=(Unit *)malloc(needbytes);
        if (allocres==nullptr) {status|=DEC_Insufficient_storage; break;}
        res=allocres;
        }
      }
    /* res now -> number lsu, buffer, or allocated storage for Unit array  */

    /* Place the coefficient into the selected Unit array  */
    /* [this is often 70% of the cost of this function when DECDPUN>1]  */
    #if DECDPUN>1
    out=0;                         /* accumulator  */
    up=res+D2U(d)-1;               /* -> msu  */
    cut=d-(up-res)*DECDPUN;        /* digits in top unit  */
    for (c=cfirst;; c++) {         /* along the digits  */
      if (*c=='.') continue;       /* ignore '.' [don't decrement cut]  */
      out=X10(out)+(Int)*c-(Int)'0';
      if (c==last) break;          /* done [never get to trailing '.']  */
      cut--;
      if (cut>0) continue;         /* more for this unit  */
      *up=(Unit)out;               /* write unit  */
      up--;                        /* prepare for unit below..  */
      cut=DECDPUN;                 /* ..  */
      out=0;                       /* ..  */
      } /* c  */
    *up=(Unit)out;                 /* write lsu  */

    #else
    /* DECDPUN==1  */
    up=res;                        /* -> lsu  */
    for (c=last; c>=cfirst; c--) { /* over each character, from least  */
      if (*c=='.') continue;       /* ignore . [don't step up]  */
      *up=(Unit)((Int)*c-(Int)'0');
      up++;
      } /* c  */
    #endif

    dn->bits=bits;
    dn->exponent=exponent;
    dn->digits=d;

    /* if not in number (too long) shorten into the number  */
    if (d>set->digits) {
      residue=0;
      decSetCoeff(dn, set, res, d, &residue, &status);
      /* always check for overflow or subnormal and round as needed  */
      decFinalize(dn, set, &residue, &status);
      }
     else { /* no rounding, but may still have overflow or subnormal  */
      /* [these tests are just for performance; finalize repeats them]  */
      if ((dn->exponent-1<set->emin-dn->digits)
       || (dn->exponent-1>set->emax-set->digits)) {
        residue=0;
        decFinalize(dn, set, &residue, &status);
        }
      }
    /* decNumberShow(dn);  */
    } while(0);                         /* [for break]  */

  if (allocres!=nullptr) free(allocres);   /* drop any storage used  */
  if (status!=0) decStatus(dn, status, set);
  return dn;
  } /* decNumberFromString */

/* ================================================================== */
/* Operators                                                          */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* decNumberAbs -- absolute value operator                            */
/*                                                                    */
/*   This computes C = abs(A)                                         */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* See also decNumberCopyAbs for a quiet bitwise version of this.     */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* This has the same effect as decNumberPlus unless A is negative,    */
/* in which case it has the same effect as decNumberMinus.            */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberAbs(decNumber *res, const decNumber *rhs,
                         decContext *set) {
  decNumber dzero;                      /* for 0  */
  uInt status=0;                        /* accumulator  */

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  uprv_decNumberZero(&dzero);                /* set 0  */
  dzero.exponent=rhs->exponent;         /* [no coefficient expansion]  */
  decAddOp(res, &dzero, rhs, set, (uByte)(rhs->bits & DECNEG), &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberAbs  */

/* ------------------------------------------------------------------ */
/* decNumberAdd -- add two Numbers                                    */
/*                                                                    */
/*   This computes C = A + B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X+X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* This just calls the routine shared with Subtract                   */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberAdd(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decAddOp(res, lhs, rhs, set, 0, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberAdd  */

/* ------------------------------------------------------------------ */
/* decNumberAnd -- AND two Numbers, digitwise                         */
/*                                                                    */
/*   This computes C = A & B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X&X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context (used for result length and error report)     */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Logical function restrictions apply (see above); a NaN is          */
/* returned with Invalid_operation if a restriction is violated.      */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberAnd(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, decContext *set) {
  const Unit *ua, *ub;                  /* -> operands  */
  const Unit *msua, *msub;              /* -> operand msus  */
  Unit *uc,  *msuc;                     /* -> result and its msu  */
  Int   msudigs;                        /* digits in res msu  */
  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  if (lhs->exponent!=0 || decNumberIsSpecial(lhs) || decNumberIsNegative(lhs)
   || rhs->exponent!=0 || decNumberIsSpecial(rhs) || decNumberIsNegative(rhs)) {
    decStatus(res, DEC_Invalid_operation, set);
    return res;
    }

  /* operands are valid  */
  ua=lhs->lsu;                          /* bottom-up  */
  ub=rhs->lsu;                          /* ..  */
  uc=res->lsu;                          /* ..  */
  msua=ua+D2U(lhs->digits)-1;           /* -> msu of lhs  */
  msub=ub+D2U(rhs->digits)-1;           /* -> msu of rhs  */
  msuc=uc+D2U(set->digits)-1;           /* -> msu of result  */
  msudigs=MSUDIGITS(set->digits);       /* [faster than remainder]  */
  for (; uc<=msuc; ua++, ub++, uc++) {  /* Unit loop  */
    Unit a, b;                          /* extract units  */
    if (ua>msua) a=0;
     else a=*ua;
    if (ub>msub) b=0;
     else b=*ub;
    *uc=0;                              /* can now write back  */
    if (a|b) {                          /* maybe 1 bits to examine  */
      Int i, j;
      *uc=0;                            /* can now write back  */
      /* This loop could be unrolled and/or use BIN2BCD tables  */
      for (i=0; i<DECDPUN; i++) {
        if (a&b&1) *uc=*uc+(Unit)powers[i];  /* effect AND  */
        j=a%10;
        a=a/10;
        j|=b%10;
        b=b/10;
        if (j>1) {
          decStatus(res, DEC_Invalid_operation, set);
          return res;
          }
        if (uc==msuc && i==msudigs-1) break; /* just did final digit  */
        } /* each digit  */
      } /* both OK  */
    } /* each unit  */
  /* [here uc-1 is the msu of the result]  */
  res->digits=decGetDigits(res->lsu, static_cast<int32_t>(uc - res->lsu));
  res->exponent=0;                      /* integer  */
  res->bits=0;                          /* sign=0  */
  return res;  /* [no status to set]  */
  } /* decNumberAnd  */

/* ------------------------------------------------------------------ */
/* decNumberCompare -- compare two Numbers                            */
/*                                                                    */
/*   This computes C = A ? B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for one digit (or NaN).                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberCompare(decNumber *res, const decNumber *lhs,
                             const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decCompareOp(res, lhs, rhs, set, COMPARE, &status);
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberCompare  */

/* ------------------------------------------------------------------ */
/* decNumberCompareSignal -- compare, signalling on all NaNs          */
/*                                                                    */
/*   This computes C = A ? B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for one digit (or NaN).                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberCompareSignal(decNumber *res, const decNumber *lhs,
                                   const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decCompareOp(res, lhs, rhs, set, COMPSIG, &status);
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberCompareSignal  */

/* ------------------------------------------------------------------ */
/* decNumberCompareTotal -- compare two Numbers, using total ordering */
/*                                                                    */
/*   This computes C = A ? B, under total ordering                    */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for one digit; the result will always be one of  */
/* -1, 0, or 1.                                                       */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberCompareTotal(decNumber *res, const decNumber *lhs,
                                  const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decCompareOp(res, lhs, rhs, set, COMPTOTAL, &status);
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberCompareTotal  */

/* ------------------------------------------------------------------ */
/* decNumberCompareTotalMag -- compare, total ordering of magnitudes  */
/*                                                                    */
/*   This computes C = |A| ? |B|, under total ordering                */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for one digit; the result will always be one of  */
/* -1, 0, or 1.                                                       */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberCompareTotalMag(decNumber *res, const decNumber *lhs,
                                     const decNumber *rhs, decContext *set) {
  uInt status=0;                   /* accumulator  */
  uInt needbytes;                  /* for space calculations  */
  decNumber bufa[D2N(DECBUFFER+1)];/* +1 in case DECBUFFER=0  */
  decNumber *allocbufa=nullptr;       /* -> allocated bufa, iff allocated  */
  decNumber bufb[D2N(DECBUFFER+1)];
  decNumber *allocbufb=nullptr;       /* -> allocated bufb, iff allocated  */
  decNumber *a, *b;                /* temporary pointers  */

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  do {                                  /* protect allocated storage  */
    /* if either is negative, take a copy and absolute  */
    if (decNumberIsNegative(lhs)) {     /* lhs<0  */
      a=bufa;
      needbytes=sizeof(decNumber)+(D2U(lhs->digits)-1)*sizeof(Unit);
      if (needbytes>sizeof(bufa)) {     /* need malloc space  */
        allocbufa=(decNumber *)malloc(needbytes);
        if (allocbufa==nullptr) {          /* hopeless -- abandon  */
          status|=DEC_Insufficient_storage;
          break;}
        a=allocbufa;                    /* use the allocated space  */
        }
      uprv_decNumberCopy(a, lhs);            /* copy content  */
      a->bits&=~DECNEG;                 /* .. and clear the sign  */
      lhs=a;                            /* use copy from here on  */
      }
    if (decNumberIsNegative(rhs)) {     /* rhs<0  */
      b=bufb;
      needbytes=sizeof(decNumber)+(D2U(rhs->digits)-1)*sizeof(Unit);
      if (needbytes>sizeof(bufb)) {     /* need malloc space  */
        allocbufb=(decNumber *)malloc(needbytes);
        if (allocbufb==nullptr) {          /* hopeless -- abandon  */
          status|=DEC_Insufficient_storage;
          break;}
        b=allocbufb;                    /* use the allocated space  */
        }
      uprv_decNumberCopy(b, rhs);            /* copy content  */
      b->bits&=~DECNEG;                 /* .. and clear the sign  */
      rhs=b;                            /* use copy from here on  */
      }
    decCompareOp(res, lhs, rhs, set, COMPTOTAL, &status);
    } while(0);                         /* end protected  */

  if (allocbufa!=nullptr) free(allocbufa); /* drop any storage used  */
  if (allocbufb!=nullptr) free(allocbufb); /* ..  */
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberCompareTotalMag  */

/* ------------------------------------------------------------------ */
/* decNumberDivide -- divide one number by another                    */
/*                                                                    */
/*   This computes C = A / B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X/X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberDivide(decNumber *res, const decNumber *lhs,
                            const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decDivideOp(res, lhs, rhs, set, DIVIDE, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberDivide  */

/* ------------------------------------------------------------------ */
/* decNumberDivideInteger -- divide and return integer quotient       */
/*                                                                    */
/*   This computes C = A # B, where # is the integer divide operator  */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X#X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberDivideInteger(decNumber *res, const decNumber *lhs,
                                   const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decDivideOp(res, lhs, rhs, set, DIVIDEINT, &status);
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberDivideInteger  */

/* ------------------------------------------------------------------ */
/* decNumberExp -- exponentiation                                     */
/*                                                                    */
/*   This computes C = exp(A)                                         */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context; note that rounding mode has no effect        */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Mathematical function restrictions apply (see above); a NaN is     */
/* returned with Invalid_operation if a restriction is violated.      */
/*                                                                    */
/* Finite results will always be full precision and Inexact, except   */
/* when A is a zero or -Infinity (giving 1 or 0 respectively).        */
/*                                                                    */
/* An Inexact result is rounded using DEC_ROUND_HALF_EVEN; it will    */
/* almost always be correctly rounded, but may be up to 1 ulp in      */
/* error in rare cases.                                               */
/* ------------------------------------------------------------------ */
/* This is a wrapper for decExpOp which can handle the slightly wider */
/* (double) range needed by Ln (which has to be able to calculate     */
/* exp(-a) where a can be the tiniest number (Ntiny).                 */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberExp(decNumber *res, const decNumber *rhs,
                         decContext *set) {
  uInt status=0;                        /* accumulator  */
  #if DECSUBSET
  decNumber *allocrhs=nullptr;        /* non-nullptr if rounded rhs allocated  */
  #endif

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  /* Check restrictions; these restrictions ensure that if h=8 (see  */
  /* decExpOp) then the result will either overflow or underflow to 0.  */
  /* Other math functions restrict the input range, too, for inverses.  */
  /* If not violated then carry out the operation.  */
  if (!decCheckMath(rhs, set, &status)) do { /* protect allocation  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operand and set lostDigits status, as needed  */
      if (rhs->digits>set->digits) {
        allocrhs=decRoundOperand(rhs, set, &status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      }
    #endif
    decExpOp(res, rhs, set, &status);
    } while(0);                         /* end protected  */

  #if DECSUBSET
  if (allocrhs !=nullptr) free(allocrhs);  /* drop any storage used  */
  #endif
  /* apply significant status  */
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberExp  */

/* ------------------------------------------------------------------ */
/* decNumberFMA -- fused multiply add                                 */
/*                                                                    */
/*   This computes D = (A * B) + C with only one rounding             */
/*                                                                    */
/*   res is D, the result.  D may be A or B or C (e.g., X=FMA(X,X,X)) */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   fhs is C [far hand side]                                         */
/*   set is the context                                               */
/*                                                                    */
/* Mathematical function restrictions apply (see above); a NaN is     */
/* returned with Invalid_operation if a restriction is violated.      */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberFMA(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, const decNumber *fhs,
                         decContext *set) {
  uInt status=0;                   /* accumulator  */
  decContext dcmul;                /* context for the multiplication  */
  uInt needbytes;                  /* for space calculations  */
  decNumber bufa[D2N(DECBUFFER*2+1)];
  decNumber *allocbufa=nullptr;       /* -> allocated bufa, iff allocated  */
  decNumber *acc;                  /* accumulator pointer  */
  decNumber dzero;                 /* work  */

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  if (decCheckOperands(res, fhs, DECUNUSED, set)) return res;
  #endif

  do {                                  /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) {               /* [undefined if subset]  */
      status|=DEC_Invalid_operation;
      break;}
    #endif
    /* Check math restrictions [these ensure no overflow or underflow]  */
    if ((!decNumberIsSpecial(lhs) && decCheckMath(lhs, set, &status))
     || (!decNumberIsSpecial(rhs) && decCheckMath(rhs, set, &status))
     || (!decNumberIsSpecial(fhs) && decCheckMath(fhs, set, &status))) break;
    /* set up context for multiply  */
    dcmul=*set;
    dcmul.digits=lhs->digits+rhs->digits; /* just enough  */
    /* [The above may be an over-estimate for subset arithmetic, but that's OK]  */
    dcmul.emax=DEC_MAX_EMAX;            /* effectively unbounded ..  */
    dcmul.emin=DEC_MIN_EMIN;            /* [thanks to Math restrictions]  */
    /* set up decNumber space to receive the result of the multiply  */
    acc=bufa;                           /* may fit  */
    needbytes=sizeof(decNumber)+(D2U(dcmul.digits)-1)*sizeof(Unit);
    if (needbytes>sizeof(bufa)) {       /* need malloc space  */
      allocbufa=(decNumber *)malloc(needbytes);
      if (allocbufa==nullptr) {            /* hopeless -- abandon  */
        status|=DEC_Insufficient_storage;
        break;}
      acc=allocbufa;                    /* use the allocated space  */
      }
    /* multiply with extended range and necessary precision  */
    /*printf("emin=%ld\n", dcmul.emin);  */
    decMultiplyOp(acc, lhs, rhs, &dcmul, &status);
    /* Only Invalid operation (from sNaN or Inf * 0) is possible in  */
    /* status; if either is seen than ignore fhs (in case it is  */
    /* another sNaN) and set acc to NaN unless we had an sNaN  */
    /* [decMultiplyOp leaves that to caller]  */
    /* Note sNaN has to go through addOp to shorten payload if  */
    /* necessary  */
    if ((status&DEC_Invalid_operation)!=0) {
      if (!(status&DEC_sNaN)) {         /* but be true invalid  */
        uprv_decNumberZero(res);             /* acc not yet set  */
        res->bits=DECNAN;
        break;
        }
      uprv_decNumberZero(&dzero);            /* make 0 (any non-NaN would do)  */
      fhs=&dzero;                       /* use that  */
      }
    #if DECCHECK
     else { /* multiply was OK  */
      if (status!=0) printf("Status=%08lx after FMA multiply\n", (LI)status);
      }
    #endif
    /* add the third operand and result -> res, and all is done  */
    decAddOp(res, acc, fhs, set, 0, &status);
    } while(0);                         /* end protected  */

  if (allocbufa!=nullptr) free(allocbufa); /* drop any storage used  */
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberFMA  */

/* ------------------------------------------------------------------ */
/* decNumberInvert -- invert a Number, digitwise                      */
/*                                                                    */
/*   This computes C = ~A                                             */
/*                                                                    */
/*   res is C, the result.  C may be A (e.g., X=~X)                   */
/*   rhs is A                                                         */
/*   set is the context (used for result length and error report)     */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Logical function restrictions apply (see above); a NaN is          */
/* returned with Invalid_operation if a restriction is violated.      */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberInvert(decNumber *res, const decNumber *rhs,
                            decContext *set) {
  const Unit *ua, *msua;                /* -> operand and its msu  */
  Unit  *uc, *msuc;                     /* -> result and its msu  */
  Int   msudigs;                        /* digits in res msu  */
  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  if (rhs->exponent!=0 || decNumberIsSpecial(rhs) || decNumberIsNegative(rhs)) {
    decStatus(res, DEC_Invalid_operation, set);
    return res;
    }
  /* operand is valid  */
  ua=rhs->lsu;                          /* bottom-up  */
  uc=res->lsu;                          /* ..  */
  msua=ua+D2U(rhs->digits)-1;           /* -> msu of rhs  */
  msuc=uc+D2U(set->digits)-1;           /* -> msu of result  */
  msudigs=MSUDIGITS(set->digits);       /* [faster than remainder]  */
  for (; uc<=msuc; ua++, uc++) {        /* Unit loop  */
    Unit a;                             /* extract unit  */
    Int  i, j;                          /* work  */
    if (ua>msua) a=0;
     else a=*ua;
    *uc=0;                              /* can now write back  */
    /* always need to examine all bits in rhs  */
    /* This loop could be unrolled and/or use BIN2BCD tables  */
    for (i=0; i<DECDPUN; i++) {
      if ((~a)&1) *uc=*uc+(Unit)powers[i];   /* effect INVERT  */
      j=a%10;
      a=a/10;
      if (j>1) {
        decStatus(res, DEC_Invalid_operation, set);
        return res;
        }
      if (uc==msuc && i==msudigs-1) break;   /* just did final digit  */
      } /* each digit  */
    } /* each unit  */
  /* [here uc-1 is the msu of the result]  */
  res->digits=decGetDigits(res->lsu, static_cast<int32_t>(uc - res->lsu));
  res->exponent=0;                      /* integer  */
  res->bits=0;                          /* sign=0  */
  return res;  /* [no status to set]  */
  } /* decNumberInvert  */

/* ------------------------------------------------------------------ */
/* decNumberLn -- natural logarithm                                   */
/*                                                                    */
/*   This computes C = ln(A)                                          */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context; note that rounding mode has no effect        */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Notable cases:                                                     */
/*   A<0 -> Invalid                                                   */
/*   A=0 -> -Infinity (Exact)                                         */
/*   A=+Infinity -> +Infinity (Exact)                                 */
/*   A=1 exactly -> 0 (Exact)                                         */
/*                                                                    */
/* Mathematical function restrictions apply (see above); a NaN is     */
/* returned with Invalid_operation if a restriction is violated.      */
/*                                                                    */
/* An Inexact result is rounded using DEC_ROUND_HALF_EVEN; it will    */
/* almost always be correctly rounded, but may be up to 1 ulp in      */
/* error in rare cases.                                               */
/* ------------------------------------------------------------------ */
/* This is a wrapper for decLnOp which can handle the slightly wider  */
/* (+11) range needed by Ln, Log10, etc. (which may have to be able   */
/* to calculate at p+e+2).                                            */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberLn(decNumber *res, const decNumber *rhs,
                        decContext *set) {
  uInt status=0;                   /* accumulator  */
  #if DECSUBSET
  decNumber *allocrhs=nullptr;        /* non-nullptr if rounded rhs allocated  */
  #endif

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  /* Check restrictions; this is a math function; if not violated  */
  /* then carry out the operation.  */
  if (!decCheckMath(rhs, set, &status)) do { /* protect allocation  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operand and set lostDigits status, as needed  */
      if (rhs->digits>set->digits) {
        allocrhs=decRoundOperand(rhs, set, &status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      /* special check in subset for rhs=0  */
      if (ISZERO(rhs)) {                /* +/- zeros -> error  */
        status|=DEC_Invalid_operation;
        break;}
      } /* extended=0  */
    #endif
    decLnOp(res, rhs, set, &status);
    } while(0);                         /* end protected  */

  #if DECSUBSET
  if (allocrhs !=nullptr) free(allocrhs);  /* drop any storage used  */
  #endif
  /* apply significant status  */
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberLn  */

/* ------------------------------------------------------------------ */
/* decNumberLogB - get adjusted exponent, by 754 rules                */
/*                                                                    */
/*   This computes C = adjustedexponent(A)                            */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context, used only for digits and status              */
/*                                                                    */
/* C must have space for 10 digits (A might have 10**9 digits and     */
/* an exponent of +999999999, or one digit and an exponent of         */
/* -1999999999).                                                      */
/*                                                                    */
/* This returns the adjusted exponent of A after (in theory) padding  */
/* with zeros on the right to set->digits digits while keeping the    */
/* same value.  The exponent is not limited by emin/emax.             */
/*                                                                    */
/* Notable cases:                                                     */
/*   A<0 -> Use |A|                                                   */
/*   A=0 -> -Infinity (Division by zero)                              */
/*   A=Infinite -> +Infinity (Exact)                                  */
/*   A=1 exactly -> 0 (Exact)                                         */
/*   NaNs are propagated as usual                                     */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberLogB(decNumber *res, const decNumber *rhs,
                          decContext *set) {
  uInt status=0;                   /* accumulator  */

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  /* NaNs as usual; Infinities return +Infinity; 0->oops  */
  if (decNumberIsNaN(rhs)) decNaNs(res, rhs, nullptr, set, &status);
   else if (decNumberIsInfinite(rhs)) uprv_decNumberCopyAbs(res, rhs);
   else if (decNumberIsZero(rhs)) {
    uprv_decNumberZero(res);                 /* prepare for Infinity  */
    res->bits=DECNEG|DECINF;            /* -Infinity  */
    status|=DEC_Division_by_zero;       /* as per 754  */
    }
   else { /* finite non-zero  */
    Int ae=rhs->exponent+rhs->digits-1; /* adjusted exponent  */
    uprv_decNumberFromInt32(res, ae);        /* lay it out  */
    }

  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberLogB  */

/* ------------------------------------------------------------------ */
/* decNumberLog10 -- logarithm in base 10                             */
/*                                                                    */
/*   This computes C = log10(A)                                       */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context; note that rounding mode has no effect        */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Notable cases:                                                     */
/*   A<0 -> Invalid                                                   */
/*   A=0 -> -Infinity (Exact)                                         */
/*   A=+Infinity -> +Infinity (Exact)                                 */
/*   A=10**n (if n is an integer) -> n (Exact)                        */
/*                                                                    */
/* Mathematical function restrictions apply (see above); a NaN is     */
/* returned with Invalid_operation if a restriction is violated.      */
/*                                                                    */
/* An Inexact result is rounded using DEC_ROUND_HALF_EVEN; it will    */
/* almost always be correctly rounded, but may be up to 1 ulp in      */
/* error in rare cases.                                               */
/* ------------------------------------------------------------------ */
/* This calculates ln(A)/ln(10) using appropriate precision.  For     */
/* ln(A) this is the max(p, rhs->digits + t) + 3, where p is the      */
/* requested digits and t is the number of digits in the exponent     */
/* (maximum 6).  For ln(10) it is p + 3; this is often handled by the */
/* fastpath in decLnOp.  The final division is done to the requested  */
/* precision.                                                         */
/* ------------------------------------------------------------------ */
#if defined(__clang__) || U_GCC_MAJOR_MINOR >= 406
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
U_CAPI decNumber * U_EXPORT2 uprv_decNumberLog10(decNumber *res, const decNumber *rhs,
                          decContext *set) {
  uInt status=0, ignore=0;         /* status accumulators  */
  uInt needbytes;                  /* for space calculations  */
  Int p;                           /* working precision  */
  Int t;                           /* digits in exponent of A  */

  /* buffers for a and b working decimals  */
  /* (adjustment calculator, same size)  */
  decNumber bufa[D2N(DECBUFFER+2)];
  decNumber *allocbufa=nullptr;       /* -> allocated bufa, iff allocated  */
  decNumber *a=bufa;               /* temporary a  */
  decNumber bufb[D2N(DECBUFFER+2)];
  decNumber *allocbufb=nullptr;       /* -> allocated bufb, iff allocated  */
  decNumber *b=bufb;               /* temporary b  */
  decNumber bufw[D2N(10)];         /* working 2-10 digit number  */
  decNumber *w=bufw;               /* ..  */
  #if DECSUBSET
  decNumber *allocrhs=nullptr;        /* non-nullptr if rounded rhs allocated  */
  #endif

  decContext aset;                 /* working context  */

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  /* Check restrictions; this is a math function; if not violated  */
  /* then carry out the operation.  */
  if (!decCheckMath(rhs, set, &status)) do { /* protect malloc  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operand and set lostDigits status, as needed  */
      if (rhs->digits>set->digits) {
        allocrhs=decRoundOperand(rhs, set, &status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      /* special check in subset for rhs=0  */
      if (ISZERO(rhs)) {                /* +/- zeros -> error  */
        status|=DEC_Invalid_operation;
        break;}
      } /* extended=0  */
    #endif

    uprv_decContextDefault(&aset, DEC_INIT_DECIMAL64); /* clean context  */

    /* handle exact powers of 10; only check if +ve finite  */
    if (!(rhs->bits&(DECNEG|DECSPECIAL)) && !ISZERO(rhs)) {
      Int residue=0;               /* (no residue)  */
      uInt copystat=0;             /* clean status  */

      /* round to a single digit...  */
      aset.digits=1;
      decCopyFit(w, rhs, &aset, &residue, &copystat); /* copy & shorten  */
      /* if exact and the digit is 1, rhs is a power of 10  */
      if (!(copystat&DEC_Inexact) && w->lsu[0]==1) {
        /* the exponent, conveniently, is the power of 10; making  */
        /* this the result needs a little care as it might not fit,  */
        /* so first convert it into the working number, and then move  */
        /* to res  */
        uprv_decNumberFromInt32(w, w->exponent);
        residue=0;
        decCopyFit(res, w, set, &residue, &status); /* copy & round  */
        decFinish(res, set, &residue, &status);     /* cleanup/set flags  */
        break;
        } /* not a power of 10  */
      } /* not a candidate for exact  */

    /* simplify the information-content calculation to use 'total  */
    /* number of digits in a, including exponent' as compared to the  */
    /* requested digits, as increasing this will only rarely cost an  */
    /* iteration in ln(a) anyway  */
    t=6;                                /* it can never be >6  */

    /* allocate space when needed...  */
    p=(rhs->digits+t>set->digits?rhs->digits+t:set->digits)+3;
    needbytes=sizeof(decNumber)+(D2U(p)-1)*sizeof(Unit);
    if (needbytes>sizeof(bufa)) {       /* need malloc space  */
      allocbufa=(decNumber *)malloc(needbytes);
      if (allocbufa==nullptr) {            /* hopeless -- abandon  */
        status|=DEC_Insufficient_storage;
        break;}
      a=allocbufa;                      /* use the allocated space  */
      }
    aset.digits=p;                      /* as calculated  */
    aset.emax=DEC_MAX_MATH;             /* usual bounds  */
    aset.emin=-DEC_MAX_MATH;            /* ..  */
    aset.clamp=0;                       /* and no concrete format  */
    decLnOp(a, rhs, &aset, &status);    /* a=ln(rhs)  */

    /* skip the division if the result so far is infinite, NaN, or  */
    /* zero, or there was an error; note NaN from sNaN needs copy  */
    if (status&DEC_NaNs && !(status&DEC_sNaN)) break;
    if (a->bits&DECSPECIAL || ISZERO(a)) {
      uprv_decNumberCopy(res, a);            /* [will fit]  */
      break;}

    /* for ln(10) an extra 3 digits of precision are needed  */
    p=set->digits+3;
    needbytes=sizeof(decNumber)+(D2U(p)-1)*sizeof(Unit);
    if (needbytes>sizeof(bufb)) {       /* need malloc space  */
      allocbufb=(decNumber *)malloc(needbytes);
      if (allocbufb==nullptr) {            /* hopeless -- abandon  */
        status|=DEC_Insufficient_storage;
        break;}
      b=allocbufb;                      /* use the allocated space  */
      }
    uprv_decNumberZero(w);                   /* set up 10...  */
    #if DECDPUN==1
    w->lsu[1]=1; w->lsu[0]=0;           /* ..  */
    #else
    w->lsu[0]=10;                       /* ..  */
    #endif
    w->digits=2;                        /* ..  */

    aset.digits=p;
    decLnOp(b, w, &aset, &ignore);      /* b=ln(10)  */

    aset.digits=set->digits;            /* for final divide  */
    decDivideOp(res, a, b, &aset, DIVIDE, &status); /* into result  */
    } while(0);                         /* [for break]  */

  if (allocbufa!=nullptr) free(allocbufa); /* drop any storage used  */
  if (allocbufb!=nullptr) free(allocbufb); /* ..  */
  #if DECSUBSET
  if (allocrhs !=nullptr) free(allocrhs);  /* ..  */
  #endif
  /* apply significant status  */
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberLog10  */
#if defined(__clang__) || U_GCC_MAJOR_MINOR >= 406
#pragma GCC diagnostic pop
#endif

/* ------------------------------------------------------------------ */
/* decNumberMax -- compare two Numbers and return the maximum         */
/*                                                                    */
/*   This computes C = A ? B, returning the maximum by 754 rules      */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberMax(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decCompareOp(res, lhs, rhs, set, COMPMAX, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberMax  */

/* ------------------------------------------------------------------ */
/* decNumberMaxMag -- compare and return the maximum by magnitude     */
/*                                                                    */
/*   This computes C = A ? B, returning the maximum by 754 rules      */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberMaxMag(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decCompareOp(res, lhs, rhs, set, COMPMAXMAG, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberMaxMag  */

/* ------------------------------------------------------------------ */
/* decNumberMin -- compare two Numbers and return the minimum         */
/*                                                                    */
/*   This computes C = A ? B, returning the minimum by 754 rules      */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberMin(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decCompareOp(res, lhs, rhs, set, COMPMIN, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberMin  */

/* ------------------------------------------------------------------ */
/* decNumberMinMag -- compare and return the minimum by magnitude     */
/*                                                                    */
/*   This computes C = A ? B, returning the minimum by 754 rules      */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberMinMag(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decCompareOp(res, lhs, rhs, set, COMPMINMAG, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberMinMag  */

/* ------------------------------------------------------------------ */
/* decNumberMinus -- prefix minus operator                            */
/*                                                                    */
/*   This computes C = 0 - A                                          */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* See also decNumberCopyNegate for a quiet bitwise version of this.  */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* Simply use AddOp for the subtract, which will do the necessary.    */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberMinus(decNumber *res, const decNumber *rhs,
                           decContext *set) {
  decNumber dzero;
  uInt status=0;                        /* accumulator  */

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  uprv_decNumberZero(&dzero);                /* make 0  */
  dzero.exponent=rhs->exponent;         /* [no coefficient expansion]  */
  decAddOp(res, &dzero, rhs, set, DECNEG, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberMinus  */

/* ------------------------------------------------------------------ */
/* decNumberNextMinus -- next towards -Infinity                       */
/*                                                                    */
/*   This computes C = A - infinitesimal, rounded towards -Infinity   */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* This is a generalization of 754 NextDown.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberNextMinus(decNumber *res, const decNumber *rhs,
                               decContext *set) {
  decNumber dtiny;                           /* constant  */
  decContext workset=*set;                   /* work  */
  uInt status=0;                             /* accumulator  */
  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  /* +Infinity is the special case  */
  if ((rhs->bits&(DECINF|DECNEG))==DECINF) {
    decSetMaxValue(res, set);                /* is +ve  */
    /* there is no status to set  */
    return res;
    }
  uprv_decNumberZero(&dtiny);                     /* start with 0  */
  dtiny.lsu[0]=1;                            /* make number that is ..  */
  dtiny.exponent=DEC_MIN_EMIN-1;             /* .. smaller than tiniest  */
  workset.round=DEC_ROUND_FLOOR;
  decAddOp(res, rhs, &dtiny, &workset, DECNEG, &status);
  status&=DEC_Invalid_operation|DEC_sNaN;    /* only sNaN Invalid please  */
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberNextMinus  */

/* ------------------------------------------------------------------ */
/* decNumberNextPlus -- next towards +Infinity                        */
/*                                                                    */
/*   This computes C = A + infinitesimal, rounded towards +Infinity   */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* This is a generalization of 754 NextUp.                            */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberNextPlus(decNumber *res, const decNumber *rhs,
                              decContext *set) {
  decNumber dtiny;                           /* constant  */
  decContext workset=*set;                   /* work  */
  uInt status=0;                             /* accumulator  */
  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  /* -Infinity is the special case  */
  if ((rhs->bits&(DECINF|DECNEG))==(DECINF|DECNEG)) {
    decSetMaxValue(res, set);
    res->bits=DECNEG;                        /* negative  */
    /* there is no status to set  */
    return res;
    }
  uprv_decNumberZero(&dtiny);                     /* start with 0  */
  dtiny.lsu[0]=1;                            /* make number that is ..  */
  dtiny.exponent=DEC_MIN_EMIN-1;             /* .. smaller than tiniest  */
  workset.round=DEC_ROUND_CEILING;
  decAddOp(res, rhs, &dtiny, &workset, 0, &status);
  status&=DEC_Invalid_operation|DEC_sNaN;    /* only sNaN Invalid please  */
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberNextPlus  */

/* ------------------------------------------------------------------ */
/* decNumberNextToward -- next towards rhs                            */
/*                                                                    */
/*   This computes C = A +/- infinitesimal, rounded towards           */
/*   +/-Infinity in the direction of B, as per 754-1985 nextafter     */
/*   modified during revision but dropped from 754-2008.              */
/*                                                                    */
/*   res is C, the result.  C may be A or B.                          */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* This is a generalization of 754-1985 NextAfter.                    */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberNextToward(decNumber *res, const decNumber *lhs,
                                const decNumber *rhs, decContext *set) {
  decNumber dtiny;                           /* constant  */
  decContext workset=*set;                   /* work  */
  Int result;                                /* ..  */
  uInt status=0;                             /* accumulator  */
  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  if (decNumberIsNaN(lhs) || decNumberIsNaN(rhs)) {
    decNaNs(res, lhs, rhs, set, &status);
    }
   else { /* Is numeric, so no chance of sNaN Invalid, etc.  */
    result=decCompare(lhs, rhs, 0);     /* sign matters  */
    if (result==BADINT) status|=DEC_Insufficient_storage; /* rare  */
     else { /* valid compare  */
      if (result==0) uprv_decNumberCopySign(res, lhs, rhs); /* easy  */
       else { /* differ: need NextPlus or NextMinus  */
        uByte sub;                      /* add or subtract  */
        if (result<0) {                 /* lhs<rhs, do nextplus  */
          /* -Infinity is the special case  */
          if ((lhs->bits&(DECINF|DECNEG))==(DECINF|DECNEG)) {
            decSetMaxValue(res, set);
            res->bits=DECNEG;           /* negative  */
            return res;                 /* there is no status to set  */
            }
          workset.round=DEC_ROUND_CEILING;
          sub=0;                        /* add, please  */
          } /* plus  */
         else {                         /* lhs>rhs, do nextminus  */
          /* +Infinity is the special case  */
          if ((lhs->bits&(DECINF|DECNEG))==DECINF) {
            decSetMaxValue(res, set);
            return res;                 /* there is no status to set  */
            }
          workset.round=DEC_ROUND_FLOOR;
          sub=DECNEG;                   /* subtract, please  */
          } /* minus  */
        uprv_decNumberZero(&dtiny);          /* start with 0  */
        dtiny.lsu[0]=1;                 /* make number that is ..  */
        dtiny.exponent=DEC_MIN_EMIN-1;  /* .. smaller than tiniest  */
        decAddOp(res, lhs, &dtiny, &workset, sub, &status); /* + or -  */
        /* turn off exceptions if the result is a normal number  */
        /* (including Nmin), otherwise let all status through  */
        if (uprv_decNumberIsNormal(res, set)) status=0;
        } /* unequal  */
      } /* compare OK  */
    } /* numeric  */
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberNextToward  */

/* ------------------------------------------------------------------ */
/* decNumberOr -- OR two Numbers, digitwise                           */
/*                                                                    */
/*   This computes C = A | B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X|X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context (used for result length and error report)     */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Logical function restrictions apply (see above); a NaN is          */
/* returned with Invalid_operation if a restriction is violated.      */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberOr(decNumber *res, const decNumber *lhs,
                        const decNumber *rhs, decContext *set) {
  const Unit *ua, *ub;                  /* -> operands  */
  const Unit *msua, *msub;              /* -> operand msus  */
  Unit  *uc, *msuc;                     /* -> result and its msu  */
  Int   msudigs;                        /* digits in res msu  */
  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  if (lhs->exponent!=0 || decNumberIsSpecial(lhs) || decNumberIsNegative(lhs)
   || rhs->exponent!=0 || decNumberIsSpecial(rhs) || decNumberIsNegative(rhs)) {
    decStatus(res, DEC_Invalid_operation, set);
    return res;
    }
  /* operands are valid  */
  ua=lhs->lsu;                          /* bottom-up  */
  ub=rhs->lsu;                          /* ..  */
  uc=res->lsu;                          /* ..  */
  msua=ua+D2U(lhs->digits)-1;           /* -> msu of lhs  */
  msub=ub+D2U(rhs->digits)-1;           /* -> msu of rhs  */
  msuc=uc+D2U(set->digits)-1;           /* -> msu of result  */
  msudigs=MSUDIGITS(set->digits);       /* [faster than remainder]  */
  for (; uc<=msuc; ua++, ub++, uc++) {  /* Unit loop  */
    Unit a, b;                          /* extract units  */
    if (ua>msua) a=0;
     else a=*ua;
    if (ub>msub) b=0;
     else b=*ub;
    *uc=0;                              /* can now write back  */
    if (a|b) {                          /* maybe 1 bits to examine  */
      Int i, j;
      /* This loop could be unrolled and/or use BIN2BCD tables  */
      for (i=0; i<DECDPUN; i++) {
        if ((a|b)&1) *uc=*uc+(Unit)powers[i];     /* effect OR  */
        j=a%10;
        a=a/10;
        j|=b%10;
        b=b/10;
        if (j>1) {
          decStatus(res, DEC_Invalid_operation, set);
          return res;
          }
        if (uc==msuc && i==msudigs-1) break;      /* just did final digit  */
        } /* each digit  */
      } /* non-zero  */
    } /* each unit  */
  /* [here uc-1 is the msu of the result]  */
  res->digits=decGetDigits(res->lsu, static_cast<int32_t>(uc-res->lsu));
  res->exponent=0;                      /* integer  */
  res->bits=0;                          /* sign=0  */
  return res;  /* [no status to set]  */
  } /* decNumberOr  */

/* ------------------------------------------------------------------ */
/* decNumberPlus -- prefix plus operator                              */
/*                                                                    */
/*   This computes C = 0 + A                                          */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* See also decNumberCopy for a quiet bitwise version of this.        */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* This simply uses AddOp; Add will take fast path after preparing A. */
/* Performance is a concern here, as this routine is often used to    */
/* check operands and apply rounding and overflow/underflow testing.  */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberPlus(decNumber *res, const decNumber *rhs,
                          decContext *set) {
  decNumber dzero;
  uInt status=0;                        /* accumulator  */
  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  uprv_decNumberZero(&dzero);                /* make 0  */
  dzero.exponent=rhs->exponent;         /* [no coefficient expansion]  */
  decAddOp(res, &dzero, rhs, set, 0, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberPlus  */

/* ------------------------------------------------------------------ */
/* decNumberMultiply -- multiply two Numbers                          */
/*                                                                    */
/*   This computes C = A x B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X+X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberMultiply(decNumber *res, const decNumber *lhs,
                              const decNumber *rhs, decContext *set) {
  uInt status=0;                   /* accumulator  */
  decMultiplyOp(res, lhs, rhs, set, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberMultiply  */

/* ------------------------------------------------------------------ */
/* decNumberPower -- raise a number to a power                        */
/*                                                                    */
/*   This computes C = A ** B                                         */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X**X)        */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Mathematical function restrictions apply (see above); a NaN is     */
/* returned with Invalid_operation if a restriction is violated.      */
/*                                                                    */
/* However, if 1999999997<=B<=999999999 and B is an integer then the  */
/* restrictions on A and the context are relaxed to the usual bounds, */
/* for compatibility with the earlier (integer power only) version    */
/* of this function.                                                  */
/*                                                                    */
/* When B is an integer, the result may be exact, even if rounded.    */
/*                                                                    */
/* The final result is rounded according to the context; it will      */
/* almost always be correctly rounded, but may be up to 1 ulp in      */
/* error in rare cases.                                               */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberPower(decNumber *res, const decNumber *lhs,
                           const decNumber *rhs, decContext *set) {
  #if DECSUBSET
  decNumber *alloclhs=nullptr;        /* non-nullptr if rounded lhs allocated  */
  decNumber *allocrhs=nullptr;        /* .., rhs  */
  #endif
  decNumber *allocdac=nullptr;        /* -> allocated acc buffer, iff used  */
  decNumber *allocinv=nullptr;        /* -> allocated 1/x buffer, iff used  */
  Int   reqdigits=set->digits;     /* requested DIGITS  */
  Int   n;                         /* rhs in binary  */
  Flag  rhsint=0;                  /* 1 if rhs is an integer  */
  Flag  useint=0;                  /* 1 if can use integer calculation  */
  Flag  isoddint=0;                /* 1 if rhs is an integer and odd  */
  Int   i;                         /* work  */
  #if DECSUBSET
  Int   dropped;                   /* ..  */
  #endif
  uInt  needbytes;                 /* buffer size needed  */
  Flag  seenbit;                   /* seen a bit while powering  */
  Int   residue=0;                 /* rounding residue  */
  uInt  status=0;                  /* accumulators  */
  uByte bits=0;                    /* result sign if errors  */
  decContext aset;                 /* working context  */
  decNumber dnOne;                 /* work value 1...  */
  /* local accumulator buffer [a decNumber, with digits+elength+1 digits]  */
  decNumber dacbuff[D2N(DECBUFFER+9)];
  decNumber *dac=dacbuff;          /* -> result accumulator  */
  /* same again for possible 1/lhs calculation  */
  decNumber invbuff[D2N(DECBUFFER+9)];

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  do {                             /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) { /* reduce operands and set status, as needed  */
      if (lhs->digits>reqdigits) {
        alloclhs=decRoundOperand(lhs, set, &status);
        if (alloclhs==nullptr) break;
        lhs=alloclhs;
        }
      if (rhs->digits>reqdigits) {
        allocrhs=decRoundOperand(rhs, set, &status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      }
    #endif
    /* [following code does not require input rounding]  */

    /* handle NaNs and rhs Infinity (lhs infinity is harder)  */
    if (SPECIALARGS) {
      if (decNumberIsNaN(lhs) || decNumberIsNaN(rhs)) { /* NaNs  */
        decNaNs(res, lhs, rhs, set, &status);
        break;}
      if (decNumberIsInfinite(rhs)) {   /* rhs Infinity  */
        Flag rhsneg=rhs->bits&DECNEG;   /* save rhs sign  */
        if (decNumberIsNegative(lhs)    /* lhs<0  */
         && !decNumberIsZero(lhs))      /* ..  */
          status|=DEC_Invalid_operation;
         else {                         /* lhs >=0  */
          uprv_decNumberZero(&dnOne);        /* set up 1  */
          dnOne.lsu[0]=1;
          uprv_decNumberCompare(dac, lhs, &dnOne, set); /* lhs ? 1  */
          uprv_decNumberZero(res);           /* prepare for 0/1/Infinity  */
          if (decNumberIsNegative(dac)) {    /* lhs<1  */
            if (rhsneg) res->bits|=DECINF;   /* +Infinity [else is +0]  */
            }
           else if (dac->lsu[0]==0) {        /* lhs=1  */
            /* 1**Infinity is inexact, so return fully-padded 1.0000  */
            Int shift=set->digits-1;
            *res->lsu=1;                     /* was 0, make int 1  */
            res->digits=decShiftToMost(res->lsu, 1, shift);
            res->exponent=-shift;            /* make 1.0000...  */
            status|=DEC_Inexact|DEC_Rounded; /* deemed inexact  */
            }
           else {                            /* lhs>1  */
            if (!rhsneg) res->bits|=DECINF;  /* +Infinity [else is +0]  */
            }
          } /* lhs>=0  */
        break;}
      /* [lhs infinity drops through]  */
      } /* specials  */

    /* Original rhs may be an integer that fits and is in range  */
    n=decGetInt(rhs);
    if (n!=BADINT) {                    /* it is an integer  */
      rhsint=1;                         /* record the fact for 1**n  */
      isoddint=(Flag)n&1;               /* [works even if big]  */
      if (n!=BIGEVEN && n!=BIGODD)      /* can use integer path?  */
        useint=1;                       /* looks good  */
      }

    if (decNumberIsNegative(lhs)        /* -x ..  */
      && isoddint) bits=DECNEG;         /* .. to an odd power  */

    /* handle LHS infinity  */
    if (decNumberIsInfinite(lhs)) {     /* [NaNs already handled]  */
      uByte rbits=rhs->bits;            /* save  */
      uprv_decNumberZero(res);               /* prepare  */
      if (n==0) *res->lsu=1;            /* [-]Inf**0 => 1  */
       else {
        /* -Inf**nonint -> error  */
        if (!rhsint && decNumberIsNegative(lhs)) {
          status|=DEC_Invalid_operation;     /* -Inf**nonint is error  */
          break;}
        if (!(rbits & DECNEG)) bits|=DECINF; /* was not a **-n  */
        /* [otherwise will be 0 or -0]  */
        res->bits=bits;
        }
      break;}

    /* similarly handle LHS zero  */
    if (decNumberIsZero(lhs)) {
      if (n==0) {                            /* 0**0 => Error  */
        #if DECSUBSET
        if (!set->extended) {                /* [unless subset]  */
          uprv_decNumberZero(res);
          *res->lsu=1;                       /* return 1  */
          break;}
        #endif
        status|=DEC_Invalid_operation;
        }
       else {                                /* 0**x  */
        uByte rbits=rhs->bits;               /* save  */
        if (rbits & DECNEG) {                /* was a 0**(-n)  */
          #if DECSUBSET
          if (!set->extended) {              /* [bad if subset]  */
            status|=DEC_Invalid_operation;
            break;}
          #endif
          bits|=DECINF;
          }
        uprv_decNumberZero(res);                  /* prepare  */
        /* [otherwise will be 0 or -0]  */
        res->bits=bits;
        }
      break;}

    /* here both lhs and rhs are finite; rhs==0 is handled in the  */
    /* integer path.  Next handle the non-integer cases  */
    if (!useint) {                      /* non-integral rhs  */
      /* any -ve lhs is bad, as is either operand or context out of  */
      /* bounds  */
      if (decNumberIsNegative(lhs)) {
        status|=DEC_Invalid_operation;
        break;}
      if (decCheckMath(lhs, set, &status)
       || decCheckMath(rhs, set, &status)) break; /* variable status  */

      uprv_decContextDefault(&aset, DEC_INIT_DECIMAL64); /* clean context  */
      aset.emax=DEC_MAX_MATH;           /* usual bounds  */
      aset.emin=-DEC_MAX_MATH;          /* ..  */
      aset.clamp=0;                     /* and no concrete format  */

      /* calculate the result using exp(ln(lhs)*rhs), which can  */
      /* all be done into the accumulator, dac.  The precision needed  */
      /* is enough to contain the full information in the lhs (which  */
      /* is the total digits, including exponent), or the requested  */
      /* precision, if larger, + 4; 6 is used for the exponent  */
      /* maximum length, and this is also used when it is shorter  */
      /* than the requested digits as it greatly reduces the >0.5 ulp  */
      /* cases at little cost (because Ln doubles digits each  */
      /* iteration so a few extra digits rarely causes an extra  */
      /* iteration)  */
      aset.digits=MAXI(lhs->digits, set->digits)+6+4;
      } /* non-integer rhs  */

     else { /* rhs is in-range integer  */
      if (n==0) {                       /* x**0 = 1  */
        /* (0**0 was handled above)  */
        uprv_decNumberZero(res);             /* result=1  */
        *res->lsu=1;                    /* ..  */
        break;}
      /* rhs is a non-zero integer  */
      if (n<0) n=-n;                    /* use abs(n)  */

      aset=*set;                        /* clone the context  */
      aset.round=DEC_ROUND_HALF_EVEN;   /* internally use balanced  */
      /* calculate the working DIGITS  */
      aset.digits=reqdigits+(rhs->digits+rhs->exponent)+2;
      #if DECSUBSET
      if (!set->extended) aset.digits--;     /* use classic precision  */
      #endif
      /* it's an error if this is more than can be handled  */
      if (aset.digits>DECNUMMAXP) {status|=DEC_Invalid_operation; break;}
      } /* integer path  */

    /* aset.digits is the count of digits for the accumulator needed  */
    /* if accumulator is too long for local storage, then allocate  */
    needbytes=sizeof(decNumber)+(D2U(aset.digits)-1)*sizeof(Unit);
    /* [needbytes also used below if 1/lhs needed]  */
    if (needbytes>sizeof(dacbuff)) {
      allocdac=(decNumber *)malloc(needbytes);
      if (allocdac==nullptr) {   /* hopeless -- abandon  */
        status|=DEC_Insufficient_storage;
        break;}
      dac=allocdac;           /* use the allocated space  */
      }
    /* here, aset is set up and accumulator is ready for use  */

    if (!useint) {                           /* non-integral rhs  */
      /* x ** y; special-case x=1 here as it will otherwise always  */
      /* reduce to integer 1; decLnOp has a fastpath which detects  */
      /* the case of x=1  */
      decLnOp(dac, lhs, &aset, &status);     /* dac=ln(lhs)  */
      /* [no error possible, as lhs 0 already handled]  */
      if (ISZERO(dac)) {                     /* x==1, 1.0, etc.  */
        /* need to return fully-padded 1.0000 etc., but rhsint->1  */
        *dac->lsu=1;                         /* was 0, make int 1  */
        if (!rhsint) {                       /* add padding  */
          Int shift=set->digits-1;
          dac->digits=decShiftToMost(dac->lsu, 1, shift);
          dac->exponent=-shift;              /* make 1.0000...  */
          status|=DEC_Inexact|DEC_Rounded;   /* deemed inexact  */
          }
        }
       else {
        decMultiplyOp(dac, dac, rhs, &aset, &status);  /* dac=dac*rhs  */
        decExpOp(dac, dac, &aset, &status);            /* dac=exp(dac)  */
        }
      /* and drop through for final rounding  */
      } /* non-integer rhs  */

     else {                             /* carry on with integer  */
      uprv_decNumberZero(dac);               /* acc=1  */
      *dac->lsu=1;                      /* ..  */

      /* if a negative power the constant 1 is needed, and if not subset  */
      /* invert the lhs now rather than inverting the result later  */
      if (decNumberIsNegative(rhs)) {   /* was a **-n [hence digits>0]  */
        decNumber *inv=invbuff;         /* assume use fixed buffer  */
        uprv_decNumberCopy(&dnOne, dac);     /* dnOne=1;  [needed now or later]  */
        #if DECSUBSET
        if (set->extended) {            /* need to calculate 1/lhs  */
        #endif
          /* divide lhs into 1, putting result in dac [dac=1/dac]  */
          decDivideOp(dac, &dnOne, lhs, &aset, DIVIDE, &status);
          /* now locate or allocate space for the inverted lhs  */
          if (needbytes>sizeof(invbuff)) {
            allocinv=(decNumber *)malloc(needbytes);
            if (allocinv==nullptr) {       /* hopeless -- abandon  */
              status|=DEC_Insufficient_storage;
              break;}
            inv=allocinv;               /* use the allocated space  */
            }
          /* [inv now points to big-enough buffer or allocated storage]  */
          uprv_decNumberCopy(inv, dac);      /* copy the 1/lhs  */
          uprv_decNumberCopy(dac, &dnOne);   /* restore acc=1  */
          lhs=inv;                      /* .. and go forward with new lhs  */
        #if DECSUBSET
          }
        #endif
        }

      /* Raise-to-the-power loop...  */
      seenbit=0;                   /* set once a 1-bit is encountered  */
      for (i=1;;i++){              /* for each bit [top bit ignored]  */
        /* abandon if had overflow or terminal underflow  */
        if (status & (DEC_Overflow|DEC_Underflow)) { /* interesting?  */
          if (status&DEC_Overflow || ISZERO(dac)) break;
          }
        /* [the following two lines revealed an optimizer bug in a C++  */
        /* compiler, with symptom: 5**3 -> 25, when n=n+n was used]  */
        n=n<<1;                    /* move next bit to testable position  */
        if (n<0) {                 /* top bit is set  */
          seenbit=1;               /* OK, significant bit seen  */
          decMultiplyOp(dac, dac, lhs, &aset, &status); /* dac=dac*x  */
          }
        if (i==31) break;          /* that was the last bit  */
        if (!seenbit) continue;    /* no need to square 1  */
        decMultiplyOp(dac, dac, dac, &aset, &status); /* dac=dac*dac [square]  */
        } /*i*/ /* 32 bits  */

      /* complete internal overflow or underflow processing  */
      if (status & (DEC_Overflow|DEC_Underflow)) {
        #if DECSUBSET
        /* If subset, and power was negative, reverse the kind of -erflow  */
        /* [1/x not yet done]  */
        if (!set->extended && decNumberIsNegative(rhs)) {
          if (status & DEC_Overflow)
            status^=DEC_Overflow | DEC_Underflow | DEC_Subnormal;
           else { /* trickier -- Underflow may or may not be set  */
            status&=~(DEC_Underflow | DEC_Subnormal); /* [one or both]  */
            status|=DEC_Overflow;
            }
          }
        #endif
        dac->bits=(dac->bits & ~DECNEG) | bits; /* force correct sign  */
        /* round subnormals [to set.digits rather than aset.digits]  */
        /* or set overflow result similarly as required  */
        decFinalize(dac, set, &residue, &status);
        uprv_decNumberCopy(res, dac);   /* copy to result (is now OK length)  */
        break;
        }

      #if DECSUBSET
      if (!set->extended &&                  /* subset math  */
          decNumberIsNegative(rhs)) {        /* was a **-n [hence digits>0]  */
        /* so divide result into 1 [dac=1/dac]  */
        decDivideOp(dac, &dnOne, dac, &aset, DIVIDE, &status);
        }
      #endif
      } /* rhs integer path  */

    /* reduce result to the requested length and copy to result  */
    decCopyFit(res, dac, set, &residue, &status);
    decFinish(res, set, &residue, &status);  /* final cleanup  */
    #if DECSUBSET
    if (!set->extended) decTrim(res, set, 0, 1, &dropped); /* trailing zeros  */
    #endif
    } while(0);                         /* end protected  */

  if (allocdac!=nullptr) free(allocdac);   /* drop any storage used  */
  if (allocinv!=nullptr) free(allocinv);   /* ..  */
  #if DECSUBSET
  if (alloclhs!=nullptr) free(alloclhs);   /* ..  */
  if (allocrhs!=nullptr) free(allocrhs);   /* ..  */
  #endif
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberPower  */

/* ------------------------------------------------------------------ */
/* decNumberQuantize -- force exponent to requested value             */
/*                                                                    */
/*   This computes C = op(A, B), where op adjusts the coefficient     */
/*   of C (by rounding or shifting) such that the exponent (-scale)   */
/*   of C has exponent of B.  The numerical value of C will equal A,  */
/*   except for the effects of any rounding that occurred.            */
/*                                                                    */
/*   res is C, the result.  C may be A or B                           */
/*   lhs is A, the number to adjust                                   */
/*   rhs is B, the number with exponent to match                      */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Unless there is an error or the result is infinite, the exponent   */
/* after the operation is guaranteed to be equal to that of B.        */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberQuantize(decNumber *res, const decNumber *lhs,
                              const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decQuantizeOp(res, lhs, rhs, set, 1, &status);
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberQuantize  */

/* ------------------------------------------------------------------ */
/* decNumberReduce -- remove trailing zeros                           */
/*                                                                    */
/*   This computes C = 0 + A, and normalizes the result               */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* Previously known as Normalize  */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberNormalize(decNumber *res, const decNumber *rhs,
                               decContext *set) {
  return uprv_decNumberReduce(res, rhs, set);
  } /* decNumberNormalize  */

U_CAPI decNumber * U_EXPORT2 uprv_decNumberReduce(decNumber *res, const decNumber *rhs,
                            decContext *set) {
  #if DECSUBSET
  decNumber *allocrhs=nullptr;        /* non-nullptr if rounded rhs allocated  */
  #endif
  uInt status=0;                   /* as usual  */
  Int  residue=0;                  /* as usual  */
  Int  dropped;                    /* work  */

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  do {                             /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operand and set lostDigits status, as needed  */
      if (rhs->digits>set->digits) {
        allocrhs=decRoundOperand(rhs, set, &status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      }
    #endif
    /* [following code does not require input rounding]  */

    /* Infinities copy through; NaNs need usual treatment  */
    if (decNumberIsNaN(rhs)) {
      decNaNs(res, rhs, nullptr, set, &status);
      break;
      }

    /* reduce result to the requested length and copy to result  */
    decCopyFit(res, rhs, set, &residue, &status); /* copy & round  */
    decFinish(res, set, &residue, &status);       /* cleanup/set flags  */
    decTrim(res, set, 1, 0, &dropped);            /* normalize in place  */
                                                  /* [may clamp]  */
    } while(0);                              /* end protected  */

  #if DECSUBSET
  if (allocrhs !=nullptr) free(allocrhs);       /* ..  */
  #endif
  if (status!=0) decStatus(res, status, set);/* then report status  */
  return res;
  } /* decNumberReduce  */

/* ------------------------------------------------------------------ */
/* decNumberRescale -- force exponent to requested value              */
/*                                                                    */
/*   This computes C = op(A, B), where op adjusts the coefficient     */
/*   of C (by rounding or shifting) such that the exponent (-scale)   */
/*   of C has the value B.  The numerical value of C will equal A,    */
/*   except for the effects of any rounding that occurred.            */
/*                                                                    */
/*   res is C, the result.  C may be A or B                           */
/*   lhs is A, the number to adjust                                   */
/*   rhs is B, the requested exponent                                 */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Unless there is an error or the result is infinite, the exponent   */
/* after the operation is guaranteed to be equal to B.                */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberRescale(decNumber *res, const decNumber *lhs,
                             const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decQuantizeOp(res, lhs, rhs, set, 0, &status);
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberRescale  */

/* ------------------------------------------------------------------ */
/* decNumberRemainder -- divide and return remainder                  */
/*                                                                    */
/*   This computes C = A % B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X%X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberRemainder(decNumber *res, const decNumber *lhs,
                               const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decDivideOp(res, lhs, rhs, set, REMAINDER, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberRemainder  */

/* ------------------------------------------------------------------ */
/* decNumberRemainderNear -- divide and return remainder from nearest */
/*                                                                    */
/*   This computes C = A % B, where % is the IEEE remainder operator  */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X%X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberRemainderNear(decNumber *res, const decNumber *lhs,
                                   const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */
  decDivideOp(res, lhs, rhs, set, REMNEAR, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberRemainderNear  */

/* ------------------------------------------------------------------ */
/* decNumberRotate -- rotate the coefficient of a Number left/right   */
/*                                                                    */
/*   This computes C = A rot B  (in base ten and rotating set->digits */
/*   digits).                                                         */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=XrotX)       */
/*   lhs is A                                                         */
/*   rhs is B, the number of digits to rotate (-ve to right)          */
/*   set is the context                                               */
/*                                                                    */
/* The digits of the coefficient of A are rotated to the left (if B   */
/* is positive) or to the right (if B is negative) without adjusting  */
/* the exponent or the sign of A.  If lhs->digits is less than        */
/* set->digits the coefficient is padded with zeros on the left       */
/* before the rotate.  Any leading zeros in the result are removed    */
/* as usual.                                                          */
/*                                                                    */
/* B must be an integer (q=0) and in the range -set->digits through   */
/* +set->digits.                                                      */
/* C must have space for set->digits digits.                          */
/* NaNs are propagated as usual.  Infinities are unaffected (but      */
/* B must be valid).  No status is set unless B is invalid or an      */
/* operand is an sNaN.                                                */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberRotate(decNumber *res, const decNumber *lhs,
                           const decNumber *rhs, decContext *set) {
  uInt status=0;              /* accumulator  */
  Int  rotate;                /* rhs as an Int  */

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  /* NaNs propagate as normal  */
  if (decNumberIsNaN(lhs) || decNumberIsNaN(rhs))
    decNaNs(res, lhs, rhs, set, &status);
   /* rhs must be an integer  */
   else if (decNumberIsInfinite(rhs) || rhs->exponent!=0)
    status=DEC_Invalid_operation;
   else { /* both numeric, rhs is an integer  */
    rotate=decGetInt(rhs);                   /* [cannot fail]  */
    if (rotate==BADINT                       /* something bad ..  */
     || rotate==BIGODD || rotate==BIGEVEN    /* .. very big ..  */
     || abs(rotate)>set->digits)             /* .. or out of range  */
      status=DEC_Invalid_operation;
     else {                                  /* rhs is OK  */
      uprv_decNumberCopy(res, lhs);
      /* convert -ve rotate to equivalent positive rotation  */
      if (rotate<0) rotate=set->digits+rotate;
      if (rotate!=0 && rotate!=set->digits   /* zero or full rotation  */
       && !decNumberIsInfinite(res)) {       /* lhs was infinite  */
        /* left-rotate to do; 0 < rotate < set->digits  */
        uInt units, shift;                   /* work  */
        uInt msudigits;                      /* digits in result msu  */
        Unit *msu=res->lsu+D2U(res->digits)-1;    /* current msu  */
        Unit *msumax=res->lsu+D2U(set->digits)-1; /* rotation msu  */
        for (msu++; msu<=msumax; msu++) *msu=0;   /* ensure high units=0  */
        res->digits=set->digits;                  /* now full-length  */
        msudigits=MSUDIGITS(res->digits);         /* actual digits in msu  */

        /* rotation here is done in-place, in three steps  */
        /* 1. shift all to least up to one unit to unit-align final  */
        /*    lsd [any digits shifted out are rotated to the left,  */
        /*    abutted to the original msd (which may require split)]  */
        /*  */
        /*    [if there are no whole units left to rotate, the  */
        /*    rotation is now complete]  */
        /*  */
        /* 2. shift to least, from below the split point only, so that  */
        /*    the final msd is in the right place in its Unit [any  */
        /*    digits shifted out will fit exactly in the current msu,  */
        /*    left aligned, no split required]  */
        /*  */
        /* 3. rotate all the units by reversing left part, right  */
        /*    part, and then whole  */
        /*  */
        /* example: rotate right 8 digits (2 units + 2), DECDPUN=3.  */
        /*  */
        /*   start: 00a bcd efg hij klm npq  */
        /*  */
        /*      1a  000 0ab cde fgh|ijk lmn [pq saved]  */
        /*      1b  00p qab cde fgh|ijk lmn  */
        /*  */
        /*      2a  00p qab cde fgh|00i jkl [mn saved]  */
        /*      2b  mnp qab cde fgh|00i jkl  */
        /*  */
        /*      3a  fgh cde qab mnp|00i jkl  */
        /*      3b  fgh cde qab mnp|jkl 00i  */
        /*      3c  00i jkl mnp qab cde fgh  */

        /* Step 1: amount to shift is the partial right-rotate count  */
        rotate=set->digits-rotate;      /* make it right-rotate  */
        units=rotate/DECDPUN;           /* whole units to rotate  */
        shift=rotate%DECDPUN;           /* left-over digits count  */
        if (shift>0) {                  /* not an exact number of units  */
          uInt save=res->lsu[0]%powers[shift];    /* save low digit(s)  */
          decShiftToLeast(res->lsu, D2U(res->digits), shift);
          if (shift>msudigits) {        /* msumax-1 needs >0 digits  */
            uInt rem=save%powers[shift-msudigits];/* split save  */
            *msumax=(Unit)(save/powers[shift-msudigits]); /* and insert  */
            *(msumax-1)=*(msumax-1)
                       +(Unit)(rem*powers[DECDPUN-(shift-msudigits)]); /* ..  */
            }
           else { /* all fits in msumax  */
            *msumax=*msumax+(Unit)(save*powers[msudigits-shift]); /* [maybe *1]  */
            }
          } /* digits shift needed  */

        /* If whole units to rotate...  */
        if (units>0) {                  /* some to do  */
          /* Step 2: the units to touch are the whole ones in rotate,  */
          /*   if any, and the shift is DECDPUN-msudigits (which may be  */
          /*   0, again)  */
          shift=DECDPUN-msudigits;
          if (shift>0) {                /* not an exact number of units  */
            uInt save=res->lsu[0]%powers[shift];  /* save low digit(s)  */
            decShiftToLeast(res->lsu, units, shift);
            *msumax=*msumax+(Unit)(save*powers[msudigits]);
            } /* partial shift needed  */

          /* Step 3: rotate the units array using triple reverse  */
          /* (reversing is easy and fast)  */
          decReverse(res->lsu+units, msumax);     /* left part  */
          decReverse(res->lsu, res->lsu+units-1); /* right part  */
          decReverse(res->lsu, msumax);           /* whole  */
          } /* whole units to rotate  */
        /* the rotation may have left an undetermined number of zeros  */
        /* on the left, so true length needs to be calculated  */
        res->digits=decGetDigits(res->lsu, static_cast<int32_t>(msumax-res->lsu+1));
        } /* rotate needed  */
      } /* rhs OK  */
    } /* numerics  */
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberRotate  */

/* ------------------------------------------------------------------ */
/* decNumberSameQuantum -- test for equal exponents                   */
/*                                                                    */
/*   res is the result number, which will contain either 0 or 1       */
/*   lhs is a number to test                                          */
/*   rhs is the second (usually a pattern)                            */
/*                                                                    */
/* No errors are possible and no context is needed.                   */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberSameQuantum(decNumber *res, const decNumber *lhs,
                                 const decNumber *rhs) {
  Unit ret=0;                      /* return value  */

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, DECUNCONT)) return res;
  #endif

  if (SPECIALARGS) {
    if (decNumberIsNaN(lhs) && decNumberIsNaN(rhs)) ret=1;
     else if (decNumberIsInfinite(lhs) && decNumberIsInfinite(rhs)) ret=1;
     /* [anything else with a special gives 0]  */
    }
   else if (lhs->exponent==rhs->exponent) ret=1;

  uprv_decNumberZero(res);              /* OK to overwrite an operand now  */
  *res->lsu=ret;
  return res;
  } /* decNumberSameQuantum  */

/* ------------------------------------------------------------------ */
/* decNumberScaleB -- multiply by a power of 10                       */
/*                                                                    */
/* This computes C = A x 10**B where B is an integer (q=0) with       */
/* maximum magnitude 2*(emax+digits)                                  */
/*                                                                    */
/*   res is C, the result.  C may be A or B                           */
/*   lhs is A, the number to adjust                                   */
/*   rhs is B, the requested power of ten to use                      */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* The result may underflow or overflow.                              */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberScaleB(decNumber *res, const decNumber *lhs,
                            const decNumber *rhs, decContext *set) {
  Int  reqexp;                /* requested exponent change [B]  */
  uInt status=0;              /* accumulator  */
  Int  residue;               /* work  */

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  /* Handle special values except lhs infinite  */
  if (decNumberIsNaN(lhs) || decNumberIsNaN(rhs))
    decNaNs(res, lhs, rhs, set, &status);
    /* rhs must be an integer  */
   else if (decNumberIsInfinite(rhs) || rhs->exponent!=0)
    status=DEC_Invalid_operation;
   else {
    /* lhs is a number; rhs is a finite with q==0  */
    reqexp=decGetInt(rhs);                   /* [cannot fail]  */
    if (reqexp==BADINT                       /* something bad ..  */
     || reqexp==BIGODD || reqexp==BIGEVEN    /* .. very big ..  */
     || abs(reqexp)>(2*(set->digits+set->emax))) /* .. or out of range  */
      status=DEC_Invalid_operation;
     else {                                  /* rhs is OK  */
      uprv_decNumberCopy(res, lhs);               /* all done if infinite lhs  */
      if (!decNumberIsInfinite(res)) {       /* prepare to scale  */
        res->exponent+=reqexp;               /* adjust the exponent  */
        residue=0;
        decFinalize(res, set, &residue, &status); /* .. and check  */
        } /* finite LHS  */
      } /* rhs OK  */
    } /* rhs finite  */
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberScaleB  */

/* ------------------------------------------------------------------ */
/* decNumberShift -- shift the coefficient of a Number left or right  */
/*                                                                    */
/*   This computes C = A << B or C = A >> -B  (in base ten).          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X<<X)        */
/*   lhs is A                                                         */
/*   rhs is B, the number of digits to shift (-ve to right)           */
/*   set is the context                                               */
/*                                                                    */
/* The digits of the coefficient of A are shifted to the left (if B   */
/* is positive) or to the right (if B is negative) without adjusting  */
/* the exponent or the sign of A.                                     */
/*                                                                    */
/* B must be an integer (q=0) and in the range -set->digits through   */
/* +set->digits.                                                      */
/* C must have space for set->digits digits.                          */
/* NaNs are propagated as usual.  Infinities are unaffected (but      */
/* B must be valid).  No status is set unless B is invalid or an      */
/* operand is an sNaN.                                                */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberShift(decNumber *res, const decNumber *lhs,
                           const decNumber *rhs, decContext *set) {
  uInt status=0;              /* accumulator  */
  Int  shift;                 /* rhs as an Int  */

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  /* NaNs propagate as normal  */
  if (decNumberIsNaN(lhs) || decNumberIsNaN(rhs))
    decNaNs(res, lhs, rhs, set, &status);
   /* rhs must be an integer  */
   else if (decNumberIsInfinite(rhs) || rhs->exponent!=0)
    status=DEC_Invalid_operation;
   else { /* both numeric, rhs is an integer  */
    shift=decGetInt(rhs);                    /* [cannot fail]  */
    if (shift==BADINT                        /* something bad ..  */
     || shift==BIGODD || shift==BIGEVEN      /* .. very big ..  */
     || abs(shift)>set->digits)              /* .. or out of range  */
      status=DEC_Invalid_operation;
     else {                                  /* rhs is OK  */
      uprv_decNumberCopy(res, lhs);
      if (shift!=0 && !decNumberIsInfinite(res)) { /* something to do  */
        if (shift>0) {                       /* to left  */
          if (shift==set->digits) {          /* removing all  */
            *res->lsu=0;                     /* so place 0  */
            res->digits=1;                   /* ..  */
            }
           else {                            /*  */
            /* first remove leading digits if necessary  */
            if (res->digits+shift>set->digits) {
              decDecap(res, res->digits+shift-set->digits);
              /* that updated res->digits; may have gone to 1 (for a  */
              /* single digit or for zero  */
              }
            if (res->digits>1 || *res->lsu)  /* if non-zero..  */
              res->digits=decShiftToMost(res->lsu, res->digits, shift);
            } /* partial left  */
          } /* left  */
         else { /* to right  */
          if (-shift>=res->digits) {         /* discarding all  */
            *res->lsu=0;                     /* so place 0  */
            res->digits=1;                   /* ..  */
            }
           else {
            decShiftToLeast(res->lsu, D2U(res->digits), -shift);
            res->digits-=(-shift);
            }
          } /* to right  */
        } /* non-0 non-Inf shift  */
      } /* rhs OK  */
    } /* numerics  */
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberShift  */

/* ------------------------------------------------------------------ */
/* decNumberSquareRoot -- square root operator                        */
/*                                                                    */
/*   This computes C = squareroot(A)                                  */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context; note that rounding mode has no effect        */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* This uses the following varying-precision algorithm in:            */
/*                                                                    */
/*   Properly Rounded Variable Precision Square Root, T. E. Hull and  */
/*   A. Abrham, ACM Transactions on Mathematical Software, Vol 11 #3, */
/*   pp229-237, ACM, September 1985.                                  */
/*                                                                    */
/* The square-root is calculated using Newton's method, after which   */
/* a check is made to ensure the result is correctly rounded.         */
/*                                                                    */
/* % [Reformatted original Numerical Turing source code follows.]     */
/* function sqrt(x : real) : real                                     */
/* % sqrt(x) returns the properly rounded approximation to the square */
/* % root of x, in the precision of the calling environment, or it    */
/* % fails if x < 0.                                                  */
/* % t e hull and a abrham, august, 1984                              */
/* if x <= 0 then                                                     */
/*   if x < 0 then                                                    */
/*     assert false                                                   */
/*   else                                                             */
/*     result 0                                                       */
/*   end if                                                           */
/* end if                                                             */
/* var f := setexp(x, 0)  % fraction part of x   [0.1 <= x < 1]       */
/* var e := getexp(x)     % exponent part of x                        */
/* var approx : real                                                  */
/* if e mod 2 = 0  then                                               */
/*   approx := .259 + .819 * f   % approx to root of f                */
/* else                                                               */
/*   f := f/l0                   % adjustments                        */
/*   e := e + 1                  %   for odd                          */
/*   approx := .0819 + 2.59 * f  %   exponent                         */
/* end if                                                             */
/*                                                                    */
/* var p:= 3                                                          */
/* const maxp := currentprecision + 2                                 */
/* loop                                                               */
/*   p := min(2*p - 2, maxp)     % p = 4,6,10, . . . , maxp           */
/*   precision p                                                      */
/*   approx := .5 * (approx + f/approx)                               */
/*   exit when p = maxp                                               */
/* end loop                                                           */
/*                                                                    */
/* % approx is now within 1 ulp of the properly rounded square root   */
/* % of f; to ensure proper rounding, compare squares of (approx -    */
/* % l/2 ulp) and (approx + l/2 ulp) with f.                          */
/* p := currentprecision                                              */
/* begin                                                              */
/*   precision p + 2                                                  */
/*   const approxsubhalf := approx - setexp(.5, -p)                   */
/*   if mulru(approxsubhalf, approxsubhalf) > f then                  */
/*     approx := approx - setexp(.l, -p + 1)                          */
/*   else                                                             */
/*     const approxaddhalf := approx + setexp(.5, -p)                 */
/*     if mulrd(approxaddhalf, approxaddhalf) < f then                */
/*       approx := approx + setexp(.l, -p + 1)                        */
/*     end if                                                         */
/*   end if                                                           */
/* end                                                                */
/* result setexp(approx, e div 2)  % fix exponent                     */
/* end sqrt                                                           */
/* ------------------------------------------------------------------ */
#if defined(__clang__) || U_GCC_MAJOR_MINOR >= 406
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
U_CAPI decNumber * U_EXPORT2 uprv_decNumberSquareRoot(decNumber *res, const decNumber *rhs,
                                decContext *set) {
  decContext workset, approxset;   /* work contexts  */
  decNumber dzero;                 /* used for constant zero  */
  Int  maxp;                       /* largest working precision  */
  Int  workp;                      /* working precision  */
  Int  residue=0;                  /* rounding residue  */
  uInt status=0, ignore=0;         /* status accumulators  */
  uInt rstatus;                    /* ..  */
  Int  exp;                        /* working exponent  */
  Int  ideal;                      /* ideal (preferred) exponent  */
  Int  needbytes;                  /* work  */
  Int  dropped;                    /* ..  */

  #if DECSUBSET
  decNumber *allocrhs=nullptr;        /* non-nullptr if rounded rhs allocated  */
  #endif
  /* buffer for f [needs +1 in case DECBUFFER 0]  */
  decNumber buff[D2N(DECBUFFER+1)];
  /* buffer for a [needs +2 to match likely maxp]  */
  decNumber bufa[D2N(DECBUFFER+2)];
  /* buffer for temporary, b [must be same size as a]  */
  decNumber bufb[D2N(DECBUFFER+2)];
  decNumber *allocbuff=nullptr;       /* -> allocated buff, iff allocated  */
  decNumber *allocbufa=nullptr;       /* -> allocated bufa, iff allocated  */
  decNumber *allocbufb=nullptr;       /* -> allocated bufb, iff allocated  */
  decNumber *f=buff;               /* reduced fraction  */
  decNumber *a=bufa;               /* approximation to result  */
  decNumber *b=bufb;               /* intermediate result  */
  /* buffer for temporary variable, up to 3 digits  */
  decNumber buft[D2N(3)];
  decNumber *t=buft;               /* up-to-3-digit constant or work  */

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  do {                             /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operand and set lostDigits status, as needed  */
      if (rhs->digits>set->digits) {
        allocrhs=decRoundOperand(rhs, set, &status);
        if (allocrhs==nullptr) break;
        /* [Note: 'f' allocation below could reuse this buffer if  */
        /* used, but as this is rare they are kept separate for clarity.]  */
        rhs=allocrhs;
        }
      }
    #endif
    /* [following code does not require input rounding]  */

    /* handle infinities and NaNs  */
    if (SPECIALARG) {
      if (decNumberIsInfinite(rhs)) {         /* an infinity  */
        if (decNumberIsNegative(rhs)) status|=DEC_Invalid_operation;
         else uprv_decNumberCopy(res, rhs);        /* +Infinity  */
        }
       else decNaNs(res, rhs, nullptr, set, &status); /* a NaN  */
      break;
      }

    /* calculate the ideal (preferred) exponent [floor(exp/2)]  */
    /* [It would be nicer to write: ideal=rhs->exponent>>1, but this  */
    /* generates a compiler warning.  Generated code is the same.]  */
    ideal=(rhs->exponent&~1)/2;         /* target  */

    /* handle zeros  */
    if (ISZERO(rhs)) {
      uprv_decNumberCopy(res, rhs);          /* could be 0 or -0  */
      res->exponent=ideal;              /* use the ideal [safe]  */
      /* use decFinish to clamp any out-of-range exponent, etc.  */
      decFinish(res, set, &residue, &status);
      break;
      }

    /* any other -x is an oops  */
    if (decNumberIsNegative(rhs)) {
      status|=DEC_Invalid_operation;
      break;
      }

    /* space is needed for three working variables  */
    /*   f -- the same precision as the RHS, reduced to 0.01->0.99...  */
    /*   a -- Hull's approximation -- precision, when assigned, is  */
    /*        currentprecision+1 or the input argument precision,  */
    /*        whichever is larger (+2 for use as temporary)  */
    /*   b -- intermediate temporary result (same size as a)  */
    /* if any is too long for local storage, then allocate  */
    workp=MAXI(set->digits+1, rhs->digits);  /* actual rounding precision  */
    workp=MAXI(workp, 7);                    /* at least 7 for low cases  */
    maxp=workp+2;                            /* largest working precision  */

    needbytes=sizeof(decNumber)+(D2U(rhs->digits)-1)*sizeof(Unit);
    if (needbytes>(Int)sizeof(buff)) {
      allocbuff=(decNumber *)malloc(needbytes);
      if (allocbuff==nullptr) {  /* hopeless -- abandon  */
        status|=DEC_Insufficient_storage;
        break;}
      f=allocbuff;            /* use the allocated space  */
      }
    /* a and b both need to be able to hold a maxp-length number  */
    needbytes=sizeof(decNumber)+(D2U(maxp)-1)*sizeof(Unit);
    if (needbytes>(Int)sizeof(bufa)) {            /* [same applies to b]  */
      allocbufa=(decNumber *)malloc(needbytes);
      allocbufb=(decNumber *)malloc(needbytes);
      if (allocbufa==nullptr || allocbufb==nullptr) {   /* hopeless  */
        status|=DEC_Insufficient_storage;
        break;}
      a=allocbufa;            /* use the allocated spaces  */
      b=allocbufb;            /* ..  */
      }

    /* copy rhs -> f, save exponent, and reduce so 0.1 <= f < 1  */
    uprv_decNumberCopy(f, rhs);
    exp=f->exponent+f->digits;               /* adjusted to Hull rules  */
    f->exponent=-(f->digits);                /* to range  */

    /* set up working context  */
    uprv_decContextDefault(&workset, DEC_INIT_DECIMAL64);
    workset.emax=DEC_MAX_EMAX;
    workset.emin=DEC_MIN_EMIN;

    /* [Until further notice, no error is possible and status bits  */
    /* (Rounded, etc.) should be ignored, not accumulated.]  */

    /* Calculate initial approximation, and allow for odd exponent  */
    workset.digits=workp;                    /* p for initial calculation  */
    t->bits=0; t->digits=3;
    a->bits=0; a->digits=3;
    if ((exp & 1)==0) {                      /* even exponent  */
      /* Set t=0.259, a=0.819  */
      t->exponent=-3;
      a->exponent=-3;
      #if DECDPUN>=3
        t->lsu[0]=259;
        a->lsu[0]=819;
      #elif DECDPUN==2
        t->lsu[0]=59; t->lsu[1]=2;
        a->lsu[0]=19; a->lsu[1]=8;
      #else
        t->lsu[0]=9; t->lsu[1]=5; t->lsu[2]=2;
        a->lsu[0]=9; a->lsu[1]=1; a->lsu[2]=8;
      #endif
      }
     else {                                  /* odd exponent  */
      /* Set t=0.0819, a=2.59  */
      f->exponent--;                         /* f=f/10  */
      exp++;                                 /* e=e+1  */
      t->exponent=-4;
      a->exponent=-2;
      #if DECDPUN>=3
        t->lsu[0]=819;
        a->lsu[0]=259;
      #elif DECDPUN==2
        t->lsu[0]=19; t->lsu[1]=8;
        a->lsu[0]=59; a->lsu[1]=2;
      #else
        t->lsu[0]=9; t->lsu[1]=1; t->lsu[2]=8;
        a->lsu[0]=9; a->lsu[1]=5; a->lsu[2]=2;
      #endif
      }

    decMultiplyOp(a, a, f, &workset, &ignore);    /* a=a*f  */
    decAddOp(a, a, t, &workset, 0, &ignore);      /* ..+t  */
    /* [a is now the initial approximation for sqrt(f), calculated with  */
    /* currentprecision, which is also a's precision.]  */

    /* the main calculation loop  */
    uprv_decNumberZero(&dzero);                   /* make 0  */
    uprv_decNumberZero(t);                        /* set t = 0.5  */
    t->lsu[0]=5;                             /* ..  */
    t->exponent=-1;                          /* ..  */
    workset.digits=3;                        /* initial p  */
    for (; workset.digits<maxp;) {
      /* set p to min(2*p - 2, maxp)  [hence 3; or: 4, 6, 10, ... , maxp]  */
      workset.digits=MINI(workset.digits*2-2, maxp);
      /* a = 0.5 * (a + f/a)  */
      /* [calculated at p then rounded to currentprecision]  */
      decDivideOp(b, f, a, &workset, DIVIDE, &ignore); /* b=f/a  */
      decAddOp(b, b, a, &workset, 0, &ignore);         /* b=b+a  */
      decMultiplyOp(a, b, t, &workset, &ignore);       /* a=b*0.5  */
      } /* loop  */

    /* Here, 0.1 <= a < 1 [Hull], and a has maxp digits  */
    /* now reduce to length, etc.; this needs to be done with a  */
    /* having the correct exponent so as to handle subnormals  */
    /* correctly  */
    approxset=*set;                          /* get emin, emax, etc.  */
    approxset.round=DEC_ROUND_HALF_EVEN;
    a->exponent+=exp/2;                      /* set correct exponent  */
    rstatus=0;                               /* clear status  */
    residue=0;                               /* .. and accumulator  */
    decCopyFit(a, a, &approxset, &residue, &rstatus);  /* reduce (if needed)  */
    decFinish(a, &approxset, &residue, &rstatus);      /* clean and finalize  */

    /* Overflow was possible if the input exponent was out-of-range,  */
    /* in which case quit  */
    if (rstatus&DEC_Overflow) {
      status=rstatus;                        /* use the status as-is  */
      uprv_decNumberCopy(res, a);                 /* copy to result  */
      break;
      }

    /* Preserve status except Inexact/Rounded  */
    status|=(rstatus & ~(DEC_Rounded|DEC_Inexact));

    /* Carry out the Hull correction  */
    a->exponent-=exp/2;                      /* back to 0.1->1  */

    /* a is now at final precision and within 1 ulp of the properly  */
    /* rounded square root of f; to ensure proper rounding, compare  */
    /* squares of (a - l/2 ulp) and (a + l/2 ulp) with f.  */
    /* Here workset.digits=maxp and t=0.5, and a->digits determines  */
    /* the ulp  */
    workset.digits--;                             /* maxp-1 is OK now  */
    t->exponent=-a->digits-1;                     /* make 0.5 ulp  */
    decAddOp(b, a, t, &workset, DECNEG, &ignore); /* b = a - 0.5 ulp  */
    workset.round=DEC_ROUND_UP;
    decMultiplyOp(b, b, b, &workset, &ignore);    /* b = mulru(b, b)  */
    decCompareOp(b, f, b, &workset, COMPARE, &ignore); /* b ? f, reversed  */
    if (decNumberIsNegative(b)) {                 /* f < b [i.e., b > f]  */
      /* this is the more common adjustment, though both are rare  */
      t->exponent++;                              /* make 1.0 ulp  */
      t->lsu[0]=1;                                /* ..  */
      decAddOp(a, a, t, &workset, DECNEG, &ignore); /* a = a - 1 ulp  */
      /* assign to approx [round to length]  */
      approxset.emin-=exp/2;                      /* adjust to match a  */
      approxset.emax-=exp/2;
      decAddOp(a, &dzero, a, &approxset, 0, &ignore);
      }
     else {
      decAddOp(b, a, t, &workset, 0, &ignore);    /* b = a + 0.5 ulp  */
      workset.round=DEC_ROUND_DOWN;
      decMultiplyOp(b, b, b, &workset, &ignore);  /* b = mulrd(b, b)  */
      decCompareOp(b, b, f, &workset, COMPARE, &ignore);   /* b ? f  */
      if (decNumberIsNegative(b)) {               /* b < f  */
        t->exponent++;                            /* make 1.0 ulp  */
        t->lsu[0]=1;                              /* ..  */
        decAddOp(a, a, t, &workset, 0, &ignore);  /* a = a + 1 ulp  */
        /* assign to approx [round to length]  */
        approxset.emin-=exp/2;                    /* adjust to match a  */
        approxset.emax-=exp/2;
        decAddOp(a, &dzero, a, &approxset, 0, &ignore);
        }
      }
    /* [no errors are possible in the above, and rounding/inexact during  */
    /* estimation are irrelevant, so status was not accumulated]  */

    /* Here, 0.1 <= a < 1  (still), so adjust back  */
    a->exponent+=exp/2;                      /* set correct exponent  */

    /* count droppable zeros [after any subnormal rounding] by  */
    /* trimming a copy  */
    uprv_decNumberCopy(b, a);
    decTrim(b, set, 1, 1, &dropped);         /* [drops trailing zeros]  */

    /* Set Inexact and Rounded.  The answer can only be exact if  */
    /* it is short enough so that squaring it could fit in workp  */
    /* digits, so this is the only (relatively rare) condition that  */
    /* a careful check is needed  */
    if (b->digits*2-1 > workp) {             /* cannot fit  */
      status|=DEC_Inexact|DEC_Rounded;
      }
     else {                                  /* could be exact/unrounded  */
      uInt mstatus=0;                        /* local status  */
      decMultiplyOp(b, b, b, &workset, &mstatus); /* try the multiply  */
      if (mstatus&DEC_Overflow) {            /* result just won't fit  */
        status|=DEC_Inexact|DEC_Rounded;
        }
       else {                                /* plausible  */
        decCompareOp(t, b, rhs, &workset, COMPARE, &mstatus); /* b ? rhs  */
        if (!ISZERO(t)) status|=DEC_Inexact|DEC_Rounded; /* not equal  */
         else {                              /* is Exact  */
          /* here, dropped is the count of trailing zeros in 'a'  */
          /* use closest exponent to ideal...  */
          Int todrop=ideal-a->exponent;      /* most that can be dropped  */
          if (todrop<0) status|=DEC_Rounded; /* ideally would add 0s  */
           else {                            /* unrounded  */
            /* there are some to drop, but emax may not allow all  */
            Int maxexp=set->emax-set->digits+1;
            Int maxdrop=maxexp-a->exponent;
            if (todrop>maxdrop && set->clamp) { /* apply clamping  */
              todrop=maxdrop;
              status|=DEC_Clamped;
              }
            if (dropped<todrop) {            /* clamp to those available  */
              todrop=dropped;
              status|=DEC_Clamped;
              }
            if (todrop>0) {                  /* have some to drop  */
              decShiftToLeast(a->lsu, D2U(a->digits), todrop);
              a->exponent+=todrop;           /* maintain numerical value  */
              a->digits-=todrop;             /* new length  */
              }
            }
          }
        }
      }

    /* double-check Underflow, as perhaps the result could not have  */
    /* been subnormal (initial argument too big), or it is now Exact  */
    if (status&DEC_Underflow) {
      Int ae=rhs->exponent+rhs->digits-1;    /* adjusted exponent  */
      /* check if truly subnormal  */
      #if DECEXTFLAG                         /* DEC_Subnormal too  */
        if (ae>=set->emin*2) status&=~(DEC_Subnormal|DEC_Underflow);
      #else
        if (ae>=set->emin*2) status&=~DEC_Underflow;
      #endif
      /* check if truly inexact  */
      if (!(status&DEC_Inexact)) status&=~DEC_Underflow;
      }

    uprv_decNumberCopy(res, a);                   /* a is now the result  */
    } while(0);                              /* end protected  */

  if (allocbuff!=nullptr) free(allocbuff);      /* drop any storage used  */
  if (allocbufa!=nullptr) free(allocbufa);      /* ..  */
  if (allocbufb!=nullptr) free(allocbufb);      /* ..  */
  #if DECSUBSET
  if (allocrhs !=nullptr) free(allocrhs);       /* ..  */
  #endif
  if (status!=0) decStatus(res, status, set);/* then report status  */
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberSquareRoot  */
#if defined(__clang__) || U_GCC_MAJOR_MINOR >= 406
#pragma GCC diagnostic pop
#endif

/* ------------------------------------------------------------------ */
/* decNumberSubtract -- subtract two Numbers                          */
/*                                                                    */
/*   This computes C = A - B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X-X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberSubtract(decNumber *res, const decNumber *lhs,
                              const decNumber *rhs, decContext *set) {
  uInt status=0;                        /* accumulator  */

  decAddOp(res, lhs, rhs, set, DECNEG, &status);
  if (status!=0) decStatus(res, status, set);
  #if DECCHECK
  decCheckInexact(res, set);
  #endif
  return res;
  } /* decNumberSubtract  */

/* ------------------------------------------------------------------ */
/* decNumberToIntegralExact -- round-to-integral-value with InExact   */
/* decNumberToIntegralValue -- round-to-integral-value                */
/*                                                                    */
/*   res is the result                                                */
/*   rhs is input number                                              */
/*   set is the context                                               */
/*                                                                    */
/* res must have space for any value of rhs.                          */
/*                                                                    */
/* This implements the IEEE special operators and therefore treats    */
/* special values as valid.  For finite numbers it returns            */
/* rescale(rhs, 0) if rhs->exponent is <0.                            */
/* Otherwise the result is rhs (so no error is possible, except for   */
/* sNaN).                                                             */
/*                                                                    */
/* The context is used for rounding mode and status after sNaN, but   */
/* the digits setting is ignored.  The Exact version will signal      */
/* Inexact if the result differs numerically from rhs; the other      */
/* never signals Inexact.                                             */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberToIntegralExact(decNumber *res, const decNumber *rhs,
                                     decContext *set) {
  decNumber dn;
  decContext workset;              /* working context  */
  uInt status=0;                   /* accumulator  */

  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  /* handle infinities and NaNs  */
  if (SPECIALARG) {
    if (decNumberIsInfinite(rhs)) uprv_decNumberCopy(res, rhs); /* an Infinity  */
     else decNaNs(res, rhs, nullptr, set, &status); /* a NaN  */
    }
   else { /* finite  */
    /* have a finite number; no error possible (res must be big enough)  */
    if (rhs->exponent>=0) return uprv_decNumberCopy(res, rhs);
    /* that was easy, but if negative exponent there is work to do...  */
    workset=*set;                  /* clone rounding, etc.  */
    workset.digits=rhs->digits;    /* no length rounding  */
    workset.traps=0;               /* no traps  */
    uprv_decNumberZero(&dn);            /* make a number with exponent 0  */
    uprv_decNumberQuantize(res, rhs, &dn, &workset);
    status|=workset.status;
    }
  if (status!=0) decStatus(res, status, set);
  return res;
  } /* decNumberToIntegralExact  */

U_CAPI decNumber * U_EXPORT2 uprv_decNumberToIntegralValue(decNumber *res, const decNumber *rhs,
                                     decContext *set) {
  decContext workset=*set;         /* working context  */
  workset.traps=0;                 /* no traps  */
  uprv_decNumberToIntegralExact(res, rhs, &workset);
  /* this never affects set, except for sNaNs; NaN will have been set  */
  /* or propagated already, so no need to call decStatus  */
  set->status|=workset.status&DEC_Invalid_operation;
  return res;
  } /* decNumberToIntegralValue  */

/* ------------------------------------------------------------------ */
/* decNumberXor -- XOR two Numbers, digitwise                         */
/*                                                                    */
/*   This computes C = A ^ B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X^X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context (used for result length and error report)     */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Logical function restrictions apply (see above); a NaN is          */
/* returned with Invalid_operation if a restriction is violated.      */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberXor(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, decContext *set) {
  const Unit *ua, *ub;                  /* -> operands  */
  const Unit *msua, *msub;              /* -> operand msus  */
  Unit  *uc, *msuc;                     /* -> result and its msu  */
  Int   msudigs;                        /* digits in res msu  */
  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  if (lhs->exponent!=0 || decNumberIsSpecial(lhs) || decNumberIsNegative(lhs)
   || rhs->exponent!=0 || decNumberIsSpecial(rhs) || decNumberIsNegative(rhs)) {
    decStatus(res, DEC_Invalid_operation, set);
    return res;
    }
  /* operands are valid  */
  ua=lhs->lsu;                          /* bottom-up  */
  ub=rhs->lsu;                          /* ..  */
  uc=res->lsu;                          /* ..  */
  msua=ua+D2U(lhs->digits)-1;           /* -> msu of lhs  */
  msub=ub+D2U(rhs->digits)-1;           /* -> msu of rhs  */
  msuc=uc+D2U(set->digits)-1;           /* -> msu of result  */
  msudigs=MSUDIGITS(set->digits);       /* [faster than remainder]  */
  for (; uc<=msuc; ua++, ub++, uc++) {  /* Unit loop  */
    Unit a, b;                          /* extract units  */
    if (ua>msua) a=0;
     else a=*ua;
    if (ub>msub) b=0;
     else b=*ub;
    *uc=0;                              /* can now write back  */
    if (a|b) {                          /* maybe 1 bits to examine  */
      Int i, j;
      /* This loop could be unrolled and/or use BIN2BCD tables  */
      for (i=0; i<DECDPUN; i++) {
        if ((a^b)&1) *uc=*uc+(Unit)powers[i];     /* effect XOR  */
        j=a%10;
        a=a/10;
        j|=b%10;
        b=b/10;
        if (j>1) {
          decStatus(res, DEC_Invalid_operation, set);
          return res;
          }
        if (uc==msuc && i==msudigs-1) break;      /* just did final digit  */
        } /* each digit  */
      } /* non-zero  */
    } /* each unit  */
  /* [here uc-1 is the msu of the result]  */
  res->digits=decGetDigits(res->lsu, static_cast<int32_t>(uc-res->lsu));
  res->exponent=0;                      /* integer  */
  res->bits=0;                          /* sign=0  */
  return res;  /* [no status to set]  */
  } /* decNumberXor  */


/* ================================================================== */
/* Utility routines                                                   */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* decNumberClass -- return the decClass of a decNumber               */
/*   dn -- the decNumber to test                                      */
/*   set -- the context to use for Emin                               */
/*   returns the decClass enum                                        */
/* ------------------------------------------------------------------ */
enum decClass uprv_decNumberClass(const decNumber *dn, decContext *set) {
  if (decNumberIsSpecial(dn)) {
    if (decNumberIsQNaN(dn)) return DEC_CLASS_QNAN;
    if (decNumberIsSNaN(dn)) return DEC_CLASS_SNAN;
    /* must be an infinity  */
    if (decNumberIsNegative(dn)) return DEC_CLASS_NEG_INF;
    return DEC_CLASS_POS_INF;
    }
  /* is finite  */
  if (uprv_decNumberIsNormal(dn, set)) { /* most common  */
    if (decNumberIsNegative(dn)) return DEC_CLASS_NEG_NORMAL;
    return DEC_CLASS_POS_NORMAL;
    }
  /* is subnormal or zero  */
  if (decNumberIsZero(dn)) {    /* most common  */
    if (decNumberIsNegative(dn)) return DEC_CLASS_NEG_ZERO;
    return DEC_CLASS_POS_ZERO;
    }
  if (decNumberIsNegative(dn)) return DEC_CLASS_NEG_SUBNORMAL;
  return DEC_CLASS_POS_SUBNORMAL;
  } /* decNumberClass  */

/* ------------------------------------------------------------------ */
/* decNumberClassToString -- convert decClass to a string             */
/*                                                                    */
/*  eclass is a valid decClass                                        */
/*  returns a constant string describing the class (max 13+1 chars)   */
/* ------------------------------------------------------------------ */
const char *uprv_decNumberClassToString(enum decClass eclass) {
  if (eclass==DEC_CLASS_POS_NORMAL)    return DEC_ClassString_PN;
  if (eclass==DEC_CLASS_NEG_NORMAL)    return DEC_ClassString_NN;
  if (eclass==DEC_CLASS_POS_ZERO)      return DEC_ClassString_PZ;
  if (eclass==DEC_CLASS_NEG_ZERO)      return DEC_ClassString_NZ;
  if (eclass==DEC_CLASS_POS_SUBNORMAL) return DEC_ClassString_PS;
  if (eclass==DEC_CLASS_NEG_SUBNORMAL) return DEC_ClassString_NS;
  if (eclass==DEC_CLASS_POS_INF)       return DEC_ClassString_PI;
  if (eclass==DEC_CLASS_NEG_INF)       return DEC_ClassString_NI;
  if (eclass==DEC_CLASS_QNAN)          return DEC_ClassString_QN;
  if (eclass==DEC_CLASS_SNAN)          return DEC_ClassString_SN;
  return DEC_ClassString_UN;           /* Unknown  */
  } /* decNumberClassToString  */

/* ------------------------------------------------------------------ */
/* decNumberCopy -- copy a number                                     */
/*                                                                    */
/*   dest is the target decNumber                                     */
/*   src  is the source decNumber                                     */
/*   returns dest                                                     */
/*                                                                    */
/* (dest==src is allowed and is a no-op)                              */
/* All fields are updated as required.  This is a utility operation,  */
/* so special values are unchanged and no error is possible.          */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberCopy(decNumber *dest, const decNumber *src) {

  #if DECCHECK
  if (src==nullptr) return uprv_decNumberZero(dest);
  #endif

  if (dest==src) return dest;                /* no copy required  */

  /* Use explicit assignments here as structure assignment could copy  */
  /* more than just the lsu (for small DECDPUN).  This would not affect  */
  /* the value of the results, but could disturb test harness spill  */
  /* checking.  */
  dest->bits=src->bits;
  dest->exponent=src->exponent;
  dest->digits=src->digits;
  dest->lsu[0]=src->lsu[0];
  if (src->digits>DECDPUN) {                 /* more Units to come  */
    const Unit *smsup, *s;                   /* work  */
    Unit  *d;                                /* ..  */
    /* memcpy for the remaining Units would be safe as they cannot  */
    /* overlap.  However, this explicit loop is faster in short cases.  */
    d=dest->lsu+1;                           /* -> first destination  */
    smsup=src->lsu+D2U(src->digits);         /* -> source msu+1  */
    for (s=src->lsu+1; s<smsup; s++, d++) *d=*s;
    }
  return dest;
  } /* decNumberCopy  */

/* ------------------------------------------------------------------ */
/* decNumberCopyAbs -- quiet absolute value operator                  */
/*                                                                    */
/*   This sets C = abs(A)                                             */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* No exception or error can occur; this is a quiet bitwise operation.*/
/* See also decNumberAbs for a checking version of this.              */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberCopyAbs(decNumber *res, const decNumber *rhs) {
  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, DECUNCONT)) return res;
  #endif
  uprv_decNumberCopy(res, rhs);
  res->bits&=~DECNEG;                   /* turn off sign  */
  return res;
  } /* decNumberCopyAbs  */

/* ------------------------------------------------------------------ */
/* decNumberCopyNegate -- quiet negate value operator                 */
/*                                                                    */
/*   This sets C = negate(A)                                          */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* No exception or error can occur; this is a quiet bitwise operation.*/
/* See also decNumberMinus for a checking version of this.            */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberCopyNegate(decNumber *res, const decNumber *rhs) {
  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, DECUNCONT)) return res;
  #endif
  uprv_decNumberCopy(res, rhs);
  res->bits^=DECNEG;                    /* invert the sign  */
  return res;
  } /* decNumberCopyNegate  */

/* ------------------------------------------------------------------ */
/* decNumberCopySign -- quiet copy and set sign operator              */
/*                                                                    */
/*   This sets C = A with the sign of B                               */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* No exception or error can occur; this is a quiet bitwise operation.*/
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberCopySign(decNumber *res, const decNumber *lhs,
                              const decNumber *rhs) {
  uByte sign;                           /* rhs sign  */
  #if DECCHECK
  if (decCheckOperands(res, DECUNUSED, rhs, DECUNCONT)) return res;
  #endif
  sign=rhs->bits & DECNEG;              /* save sign bit  */
  uprv_decNumberCopy(res, lhs);
  res->bits&=~DECNEG;                   /* clear the sign  */
  res->bits|=sign;                      /* set from rhs  */
  return res;
  } /* decNumberCopySign  */

/* ------------------------------------------------------------------ */
/* decNumberGetBCD -- get the coefficient in BCD8                     */
/*   dn is the source decNumber                                       */
/*   bcd is the uInt array that will receive dn->digits BCD bytes,    */
/*     most-significant at offset 0                                   */
/*   returns bcd                                                      */
/*                                                                    */
/* bcd must have at least dn->digits bytes.  No error is possible; if */
/* dn is a NaN or Infinite, digits must be 1 and the coefficient 0.   */
/* ------------------------------------------------------------------ */
U_CAPI uByte * U_EXPORT2 uprv_decNumberGetBCD(const decNumber *dn, uByte *bcd) {
  uByte *ub=bcd+dn->digits-1;      /* -> lsd  */
  const Unit *up=dn->lsu;          /* Unit pointer, -> lsu  */

  #if DECDPUN==1                   /* trivial simple copy  */
    for (; ub>=bcd; ub--, up++) *ub=*up;
  #else                            /* chopping needed  */
    uInt u=*up;                    /* work  */
    uInt cut=DECDPUN;              /* downcounter through unit  */
    for (; ub>=bcd; ub--) {
      *ub=(uByte)(u%10);           /* [*6554 trick inhibits, here]  */
      u=u/10;
      cut--;
      if (cut>0) continue;         /* more in this unit  */
      up++;
      u=*up;
      cut=DECDPUN;
      }
  #endif
  return bcd;
  } /* decNumberGetBCD  */

/* ------------------------------------------------------------------ */
/* decNumberSetBCD -- set (replace) the coefficient from BCD8         */
/*   dn is the target decNumber                                       */
/*   bcd is the uInt array that will source n BCD bytes, most-        */
/*     significant at offset 0                                        */
/*   n is the number of digits in the source BCD array (bcd)          */
/*   returns dn                                                       */
/*                                                                    */
/* dn must have space for at least n digits.  No error is possible;   */
/* if dn is a NaN, or Infinite, or is to become a zero, n must be 1   */
/* and bcd[0] zero.                                                   */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberSetBCD(decNumber *dn, const uByte *bcd, uInt n) {
  Unit *up=dn->lsu+D2U(dn->digits)-1;   /* -> msu [target pointer]  */
  const uByte *ub=bcd;                  /* -> source msd  */

  #if DECDPUN==1                        /* trivial simple copy  */
    for (; ub<bcd+n; ub++, up--) *up=*ub;
  #else                                 /* some assembly needed  */
    /* calculate how many digits in msu, and hence first cut  */
    Int cut=MSUDIGITS(n);               /* [faster than remainder]  */
    for (;up>=dn->lsu; up--) {          /* each Unit from msu  */
      *up=0;                            /* will take <=DECDPUN digits  */
      for (; cut>0; ub++, cut--) *up=X10(*up)+*ub;
      cut=DECDPUN;                      /* next Unit has all digits  */
      }
  #endif
  dn->digits=n;                         /* set digit count  */
  return dn;
  } /* decNumberSetBCD  */

/* ------------------------------------------------------------------ */
/* decNumberIsNormal -- test normality of a decNumber                 */
/*   dn is the decNumber to test                                      */
/*   set is the context to use for Emin                               */
/*   returns 1 if |dn| is finite and >=Nmin, 0 otherwise              */
/* ------------------------------------------------------------------ */
Int uprv_decNumberIsNormal(const decNumber *dn, decContext *set) {
  Int ae;                               /* adjusted exponent  */
  #if DECCHECK
  if (decCheckOperands(DECUNRESU, DECUNUSED, dn, set)) return 0;
  #endif

  if (decNumberIsSpecial(dn)) return 0; /* not finite  */
  if (decNumberIsZero(dn)) return 0;    /* not non-zero  */

  ae=dn->exponent+dn->digits-1;         /* adjusted exponent  */
  if (ae<set->emin) return 0;           /* is subnormal  */
  return 1;
  } /* decNumberIsNormal  */

/* ------------------------------------------------------------------ */
/* decNumberIsSubnormal -- test subnormality of a decNumber           */
/*   dn is the decNumber to test                                      */
/*   set is the context to use for Emin                               */
/*   returns 1 if |dn| is finite, non-zero, and <Nmin, 0 otherwise    */
/* ------------------------------------------------------------------ */
Int uprv_decNumberIsSubnormal(const decNumber *dn, decContext *set) {
  Int ae;                               /* adjusted exponent  */
  #if DECCHECK
  if (decCheckOperands(DECUNRESU, DECUNUSED, dn, set)) return 0;
  #endif

  if (decNumberIsSpecial(dn)) return 0; /* not finite  */
  if (decNumberIsZero(dn)) return 0;    /* not non-zero  */

  ae=dn->exponent+dn->digits-1;         /* adjusted exponent  */
  if (ae<set->emin) return 1;           /* is subnormal  */
  return 0;
  } /* decNumberIsSubnormal  */

/* ------------------------------------------------------------------ */
/* decNumberTrim -- remove insignificant zeros                        */
/*                                                                    */
/*   dn is the number to trim                                         */
/*   returns dn                                                       */
/*                                                                    */
/* All fields are updated as required.  This is a utility operation,  */
/* so special values are unchanged and no error is possible.  The     */
/* zeros are removed unconditionally.                                 */
/* ------------------------------------------------------------------ */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberTrim(decNumber *dn) {
  Int  dropped;                    /* work  */
  decContext set;                  /* ..  */
  #if DECCHECK
  if (decCheckOperands(DECUNRESU, DECUNUSED, dn, DECUNCONT)) return dn;
  #endif
  uprv_decContextDefault(&set, DEC_INIT_BASE);    /* clamp=0  */
  return decTrim(dn, &set, 0, 1, &dropped);
  } /* decNumberTrim  */

/* ------------------------------------------------------------------ */
/* decNumberVersion -- return the name and version of this module     */
/*                                                                    */
/* No error is possible.                                              */
/* ------------------------------------------------------------------ */
const char * uprv_decNumberVersion() {
  return DECVERSION;
  } /* decNumberVersion  */

/* ------------------------------------------------------------------ */
/* decNumberZero -- set a number to 0                                 */
/*                                                                    */
/*   dn is the number to set, with space for one digit                */
/*   returns dn                                                       */
/*                                                                    */
/* No error is possible.                                              */
/* ------------------------------------------------------------------ */
/* Memset is not used as it is much slower in some environments.  */
U_CAPI decNumber * U_EXPORT2 uprv_decNumberZero(decNumber *dn) {

  #if DECCHECK
  if (decCheckOperands(dn, DECUNUSED, DECUNUSED, DECUNCONT)) return dn;
  #endif

  dn->bits=0;
  dn->exponent=0;
  dn->digits=1;
  dn->lsu[0]=0;
  return dn;
  } /* decNumberZero  */

/* ================================================================== */
/* Local routines                                                     */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* decToString -- lay out a number into a string                      */
/*                                                                    */
/*   dn     is the number to lay out                                  */
/*   string is where to lay out the number                            */
/*   eng    is 1 if Engineering, 0 if Scientific                      */
/*                                                                    */
/* string must be at least dn->digits+14 characters long              */
/* No error is possible.                                              */
/*                                                                    */
/* Note that this routine can generate a -0 or 0.000.  These are      */
/* never generated in subset to-number or arithmetic, but can occur   */
/* in non-subset arithmetic (e.g., -1*0 or 1.234-1.234).              */
/* ------------------------------------------------------------------ */
/* If DECCHECK is enabled the string "?" is returned if a number is  */
/* invalid.  */
static void decToString(const decNumber *dn, char *string, Flag eng) {
  Int exp=dn->exponent;       /* local copy  */
  Int e;                      /* E-part value  */
  Int pre;                    /* digits before the '.'  */
  Int cut;                    /* for counting digits in a Unit  */
  char *c=string;             /* work [output pointer]  */
  const Unit *up=dn->lsu+D2U(dn->digits)-1; /* -> msu [input pointer]  */
  uInt u, pow;                /* work  */

  #if DECCHECK
  if (decCheckOperands(DECUNRESU, dn, DECUNUSED, DECUNCONT)) {
    strcpy(string, "?");
    return;}
  #endif

  if (decNumberIsNegative(dn)) {   /* Negatives get a minus  */
    *c='-';
    c++;
    }
  if (dn->bits&DECSPECIAL) {       /* Is a special value  */
    if (decNumberIsInfinite(dn)) {
      strcpy(c,   "Inf");
      strcpy(c+3, "inity");
      return;}
    /* a NaN  */
    if (dn->bits&DECSNAN) {        /* signalling NaN  */
      *c='s';
      c++;
      }
    strcpy(c, "NaN");
    c+=3;                          /* step past  */
    /* if not a clean non-zero coefficient, that's all there is in a  */
    /* NaN string  */
    if (exp!=0 || (*dn->lsu==0 && dn->digits==1)) return;
    /* [drop through to add integer]  */
    }

  /* calculate how many digits in msu, and hence first cut  */
  cut=MSUDIGITS(dn->digits);       /* [faster than remainder]  */
  cut--;                           /* power of ten for digit  */

  if (exp==0) {                    /* simple integer [common fastpath]  */
    for (;up>=dn->lsu; up--) {     /* each Unit from msu  */
      u=*up;                       /* contains DECDPUN digits to lay out  */
      for (; cut>=0; c++, cut--) TODIGIT(u, cut, c, pow);
      cut=DECDPUN-1;               /* next Unit has all digits  */
      }
    *c='\0';                       /* terminate the string  */
    return;}

  /* non-0 exponent -- assume plain form */
  pre=dn->digits+exp;              /* digits before '.'  */
  e=0;                             /* no E  */
  if ((exp>0) || (pre<-5)) {       /* need exponential form  */
    e=exp+dn->digits-1;            /* calculate E value  */
    pre=1;                         /* assume one digit before '.'  */
    if (eng && (e!=0)) {           /* engineering: may need to adjust  */
      Int adj;                     /* adjustment  */
      /* The C remainder operator is undefined for negative numbers, so  */
      /* a positive remainder calculation must be used here  */
      if (e<0) {
        adj=(-e)%3;
        if (adj!=0) adj=3-adj;
        }
       else { /* e>0  */
        adj=e%3;
        }
      e=e-adj;
      /* if dealing with zero still produce an exponent which is a  */
      /* multiple of three, as expected, but there will only be the  */
      /* one zero before the E, still.  Otherwise note the padding.  */
      if (!ISZERO(dn)) pre+=adj;
       else {  /* is zero  */
        if (adj!=0) {              /* 0.00Esnn needed  */
          e=e+3;
          pre=-(2-adj);
          }
        } /* zero  */
      } /* eng  */
    } /* need exponent  */

  /* lay out the digits of the coefficient, adding 0s and . as needed */
  u=*up;
  if (pre>0) {                     /* xxx.xxx or xx00 (engineering) form  */
    Int n=pre;
    for (; pre>0; pre--, c++, cut--) {
      if (cut<0) {                 /* need new Unit  */
        if (up==dn->lsu) break;    /* out of input digits (pre>digits)  */
        up--;
        cut=DECDPUN-1;
        u=*up;
        }
      TODIGIT(u, cut, c, pow);
      }
    if (n<dn->digits) {            /* more to come, after '.'  */
      *c='.'; c++;
      for (;; c++, cut--) {
        if (cut<0) {               /* need new Unit  */
          if (up==dn->lsu) break;  /* out of input digits  */
          up--;
          cut=DECDPUN-1;
          u=*up;
          }
        TODIGIT(u, cut, c, pow);
        }
      }
     else for (; pre>0; pre--, c++) *c='0'; /* 0 padding (for engineering) needed  */
    }
   else {                          /* 0.xxx or 0.000xxx form  */
    *c='0'; c++;
    *c='.'; c++;
    for (; pre<0; pre++, c++) *c='0';   /* add any 0's after '.'  */
    for (; ; c++, cut--) {
      if (cut<0) {                 /* need new Unit  */
        if (up==dn->lsu) break;    /* out of input digits  */
        up--;
        cut=DECDPUN-1;
        u=*up;
        }
      TODIGIT(u, cut, c, pow);
      }
    }

  /* Finally add the E-part, if needed.  It will never be 0, has a
     base maximum and minimum of +999999999 through -999999999, but
     could range down to -1999999998 for abnormal numbers */
  if (e!=0) {
    Flag had=0;               /* 1=had non-zero  */
    *c='E'; c++;
    *c='+'; c++;              /* assume positive  */
    u=e;                      /* ..  */
    if (e<0) {
      *(c-1)='-';             /* oops, need -  */
      u=-e;                   /* uInt, please  */
      }
    /* lay out the exponent [_itoa or equivalent is not ANSI C]  */
    for (cut=9; cut>=0; cut--) {
      TODIGIT(u, cut, c, pow);
      if (*c=='0' && !had) continue;    /* skip leading zeros  */
      had=1;                            /* had non-0  */
      c++;                              /* step for next  */
      } /* cut  */
    }
  *c='\0';          /* terminate the string (all paths)  */
  } /* decToString  */

/* ------------------------------------------------------------------ */
/* decAddOp -- add/subtract operation                                 */
/*                                                                    */
/*   This computes C = A + B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X+X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*   negate is DECNEG if rhs should be negated, or 0 otherwise        */
/*   status accumulates status for the caller                         */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* Inexact in status must be 0 for correct Exact zero sign in result  */
/* ------------------------------------------------------------------ */
/* If possible, the coefficient is calculated directly into C.        */
/* However, if:                                                       */
/*   -- a digits+1 calculation is needed because the numbers are      */
/*      unaligned and span more than set->digits digits               */
/*   -- a carry to digits+1 digits looks possible                     */
/*   -- C is the same as A or B, and the result would destructively   */
/*      overlap the A or B coefficient                                */
/* then the result must be calculated into a temporary buffer.  In    */
/* this case a local (stack) buffer is used if possible, and only if  */
/* too long for that does malloc become the final resort.             */
/*                                                                    */
/* Misalignment is handled as follows:                                */
/*   Apad: (AExp>BExp) Swap operands and proceed as for BExp>AExp.    */
/*   BPad: Apply the padding by a combination of shifting (whole      */
/*         units) and multiplication (part units).                    */
/*                                                                    */
/* Addition, especially x=x+1, is speed-critical.                     */
/* The static buffer is larger than might be expected to allow for    */
/* calls from higher-level functions (notable exp).                    */
/* ------------------------------------------------------------------ */
static decNumber * decAddOp(decNumber *res, const decNumber *lhs,
                            const decNumber *rhs, decContext *set,
                            uByte negate, uInt *status) {
  #if DECSUBSET
  decNumber *alloclhs=nullptr;        /* non-nullptr if rounded lhs allocated  */
  decNumber *allocrhs=nullptr;        /* .., rhs  */
  #endif
  Int   rhsshift;                  /* working shift (in Units)  */
  Int   maxdigits;                 /* longest logical length  */
  Int   mult;                      /* multiplier  */
  Int   residue;                   /* rounding accumulator  */
  uByte bits;                      /* result bits  */
  Flag  diffsign;                  /* non-0 if arguments have different sign  */
  Unit  *acc;                      /* accumulator for result  */
  Unit  accbuff[SD2U(DECBUFFER*2+20)]; /* local buffer [*2+20 reduces many  */
                                   /* allocations when called from  */
                                   /* other operations, notable exp]  */
  Unit  *allocacc=nullptr;            /* -> allocated acc buffer, iff allocated  */
  Int   reqdigits=set->digits;     /* local copy; requested DIGITS  */
  Int   padding;                   /* work  */

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  do {                             /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operands and set lostDigits status, as needed  */
      if (lhs->digits>reqdigits) {
        alloclhs=decRoundOperand(lhs, set, status);
        if (alloclhs==nullptr) break;
        lhs=alloclhs;
        }
      if (rhs->digits>reqdigits) {
        allocrhs=decRoundOperand(rhs, set, status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      }
    #endif
    /* [following code does not require input rounding]  */

    /* note whether signs differ [used all paths]  */
    diffsign = static_cast<Flag>((lhs->bits ^ rhs->bits ^ negate) & DECNEG);

    /* handle infinities and NaNs  */
    if (SPECIALARGS) {                  /* a special bit set  */
      if (SPECIALARGS & (DECSNAN | DECNAN))  /* a NaN  */
        decNaNs(res, lhs, rhs, set, status);
       else { /* one or two infinities  */
        if (decNumberIsInfinite(lhs)) { /* LHS is infinity  */
          /* two infinities with different signs is invalid  */
          if (decNumberIsInfinite(rhs) && diffsign) {
            *status|=DEC_Invalid_operation;
            break;
            }
          bits=lhs->bits & DECNEG;      /* get sign from LHS  */
          }
         else bits=(rhs->bits^negate) & DECNEG;/* RHS must be Infinity  */
        bits|=DECINF;
        uprv_decNumberZero(res);
        res->bits=bits;                 /* set +/- infinity  */
        } /* an infinity  */
      break;
      }

    /* Quick exit for add 0s; return the non-0, modified as need be  */
    if (ISZERO(lhs)) {
      Int adjust;                       /* work  */
      Int lexp=lhs->exponent;           /* save in case LHS==RES  */
      bits=lhs->bits;                   /* ..  */
      residue=0;                        /* clear accumulator  */
      decCopyFit(res, rhs, set, &residue, status); /* copy (as needed)  */
      res->bits^=negate;                /* flip if rhs was negated  */
      #if DECSUBSET
      if (set->extended) {              /* exponents on zeros count  */
      #endif
        /* exponent will be the lower of the two  */
        adjust=lexp-res->exponent;      /* adjustment needed [if -ve]  */
        if (ISZERO(res)) {              /* both 0: special IEEE 754 rules  */
          if (adjust<0) res->exponent=lexp;  /* set exponent  */
          /* 0-0 gives +0 unless rounding to -infinity, and -0-0 gives -0  */
          if (diffsign) {
            if (set->round!=DEC_ROUND_FLOOR) res->bits=0;
             else res->bits=DECNEG;     /* preserve 0 sign  */
            }
          }
         else { /* non-0 res  */
          if (adjust<0) {     /* 0-padding needed  */
            if ((res->digits-adjust)>set->digits) {
              adjust=res->digits-set->digits;     /* to fit exactly  */
              *status|=DEC_Rounded;               /* [but exact]  */
              }
            res->digits=decShiftToMost(res->lsu, res->digits, -adjust);
            res->exponent+=adjust;                /* set the exponent.  */
            }
          } /* non-0 res  */
      #if DECSUBSET
        } /* extended  */
      #endif
      decFinish(res, set, &residue, status);      /* clean and finalize  */
      break;}

    if (ISZERO(rhs)) {                  /* [lhs is non-zero]  */
      Int adjust;                       /* work  */
      Int rexp=rhs->exponent;           /* save in case RHS==RES  */
      bits=rhs->bits;                   /* be clean  */
      residue=0;                        /* clear accumulator  */
      decCopyFit(res, lhs, set, &residue, status); /* copy (as needed)  */
      #if DECSUBSET
      if (set->extended) {              /* exponents on zeros count  */
      #endif
        /* exponent will be the lower of the two  */
        /* [0-0 case handled above]  */
        adjust=rexp-res->exponent;      /* adjustment needed [if -ve]  */
        if (adjust<0) {     /* 0-padding needed  */
          if ((res->digits-adjust)>set->digits) {
            adjust=res->digits-set->digits;     /* to fit exactly  */
            *status|=DEC_Rounded;               /* [but exact]  */
            }
          res->digits=decShiftToMost(res->lsu, res->digits, -adjust);
          res->exponent+=adjust;                /* set the exponent.  */
          }
      #if DECSUBSET
        } /* extended  */
      #endif
      decFinish(res, set, &residue, status);      /* clean and finalize  */
      break;}

    /* [NB: both fastpath and mainpath code below assume these cases  */
    /* (notably 0-0) have already been handled]  */

    /* calculate the padding needed to align the operands  */
    padding=rhs->exponent-lhs->exponent;

    /* Fastpath cases where the numbers are aligned and normal, the RHS  */
    /* is all in one unit, no operand rounding is needed, and no carry,  */
    /* lengthening, or borrow is needed  */
    if (padding==0
        && rhs->digits<=DECDPUN
        && rhs->exponent>=set->emin     /* [some normals drop through]  */
        && rhs->exponent<=set->emax-set->digits+1 /* [could clamp]  */
        && rhs->digits<=reqdigits
        && lhs->digits<=reqdigits) {
      Int partial=*lhs->lsu;
      if (!diffsign) {                  /* adding  */
        partial+=*rhs->lsu;
        if ((partial<=DECDPUNMAX)       /* result fits in unit  */
         && (lhs->digits>=DECDPUN ||    /* .. and no digits-count change  */
             partial < static_cast<Int>(powers[lhs->digits]))) { /* ..  */
          if (res!=lhs) uprv_decNumberCopy(res, lhs);  /* not in place  */
          *res->lsu = static_cast<Unit>(partial); /* [copy could have overwritten RHS]  */
          break;
          }
        /* else drop out for careful add  */
        }
       else {                           /* signs differ  */
        partial-=*rhs->lsu;
        if (partial>0) { /* no borrow needed, and non-0 result  */
          if (res!=lhs) uprv_decNumberCopy(res, lhs);  /* not in place  */
          *res->lsu = static_cast<Unit>(partial);
          /* this could have reduced digits [but result>0]  */
          res->digits=decGetDigits(res->lsu, D2U(res->digits));
          break;
          }
        /* else drop out for careful subtract  */
        }
      }

    /* Now align (pad) the lhs or rhs so they can be added or  */
    /* subtracted, as necessary.  If one number is much larger than  */
    /* the other (that is, if in plain form there is a least one  */
    /* digit between the lowest digit of one and the highest of the  */
    /* other) padding with up to DIGITS-1 trailing zeros may be  */
    /* needed; then apply rounding (as exotic rounding modes may be  */
    /* affected by the residue).  */
    rhsshift=0;               /* rhs shift to left (padding) in Units  */
    bits=lhs->bits;           /* assume sign is that of LHS  */
    mult=1;                   /* likely multiplier  */

    /* [if padding==0 the operands are aligned; no padding is needed]  */
    if (padding!=0) {
      /* some padding needed; always pad the RHS, as any required  */
      /* padding can then be effected by a simple combination of  */
      /* shifts and a multiply  */
      Flag swapped=0;
      if (padding<0) {                  /* LHS needs the padding  */
        const decNumber *t;
        padding=-padding;               /* will be +ve  */
        bits = static_cast<uByte>(rhs->bits ^ negate); /* assumed sign is now that of RHS  */
        t=lhs; lhs=rhs; rhs=t;
        swapped=1;
        }

      /* If, after pad, rhs would be longer than lhs by digits+1 or  */
      /* more then lhs cannot affect the answer, except as a residue,  */
      /* so only need to pad up to a length of DIGITS+1.  */
      if (rhs->digits+padding > lhs->digits+reqdigits+1) {
        /* The RHS is sufficient  */
        /* for residue use the relative sign indication...  */
        Int shift=reqdigits-rhs->digits;     /* left shift needed  */
        residue=1;                           /* residue for rounding  */
        if (diffsign) residue=-residue;      /* signs differ  */
        /* copy, shortening if necessary  */
        decCopyFit(res, rhs, set, &residue, status);
        /* if it was already shorter, then need to pad with zeros  */
        if (shift>0) {
          res->digits=decShiftToMost(res->lsu, res->digits, shift);
          res->exponent-=shift;              /* adjust the exponent.  */
          }
        /* flip the result sign if unswapped and rhs was negated  */
        if (!swapped) res->bits^=negate;
        decFinish(res, set, &residue, status);    /* done  */
        break;}

      /* LHS digits may affect result  */
      rhsshift=D2U(padding+1)-1;        /* this much by Unit shift ..  */
      mult=powers[padding-(rhsshift*DECDPUN)]; /* .. this by multiplication  */
      } /* padding needed  */

    if (diffsign) mult=-mult;           /* signs differ  */

    /* determine the longer operand  */
    maxdigits=rhs->digits+padding;      /* virtual length of RHS  */
    if (lhs->digits>maxdigits) maxdigits=lhs->digits;

    /* Decide on the result buffer to use; if possible place directly  */
    /* into result.  */
    acc=res->lsu;                       /* assume add direct to result  */
    /* If destructive overlap, or the number is too long, or a carry or  */
    /* borrow to DIGITS+1 might be possible, a buffer must be used.  */
    /* [Might be worth more sophisticated tests when maxdigits==reqdigits]  */
    if ((maxdigits>=reqdigits)          /* is, or could be, too large  */
     || (res==rhs && rhsshift>0)) {     /* destructive overlap  */
      /* buffer needed, choose it; units for maxdigits digits will be  */
      /* needed, +1 Unit for carry or borrow  */
      Int need=D2U(maxdigits)+1;
      acc=accbuff;                      /* assume use local buffer  */
      if (need*sizeof(Unit)>sizeof(accbuff)) {
        /* printf("malloc add %ld %ld\n", need, sizeof(accbuff));  */
        allocacc = static_cast<Unit*>(malloc(need * sizeof(Unit)));
        if (allocacc==nullptr) {           /* hopeless -- abandon  */
          *status|=DEC_Insufficient_storage;
          break;}
        acc=allocacc;
        }
      }

    res->bits = static_cast<uByte>(bits & DECNEG); /* it's now safe to overwrite..  */
    res->exponent=lhs->exponent;        /* .. operands (even if aliased)  */

    #if DECTRACE
      decDumpAr('A', lhs->lsu, D2U(lhs->digits));
      decDumpAr('B', rhs->lsu, D2U(rhs->digits));
      printf("  :h: %ld %ld\n", rhsshift, mult);
    #endif

    /* add [A+B*m] or subtract [A+B*(-m)]  */
    U_ASSERT(rhs->digits > 0);
    U_ASSERT(lhs->digits > 0);
    res->digits=decUnitAddSub(lhs->lsu, D2U(lhs->digits),
                              rhs->lsu, D2U(rhs->digits),
                              rhsshift, acc, mult)
               *DECDPUN;           /* [units -> digits]  */
    if (res->digits<0) {           /* borrowed...  */
      res->digits=-res->digits;
      res->bits^=DECNEG;           /* flip the sign  */
      }
    #if DECTRACE
      decDumpAr('+', acc, D2U(res->digits));
    #endif

    /* If a buffer was used the result must be copied back, possibly  */
    /* shortening.  (If no buffer was used then the result must have  */
    /* fit, so can't need rounding and residue must be 0.)  */
    residue=0;                     /* clear accumulator  */
    if (acc!=res->lsu) {
      #if DECSUBSET
      if (set->extended) {         /* round from first significant digit  */
      #endif
        /* remove leading zeros that were added due to rounding up to  */
        /* integral Units -- before the test for rounding.  */
        if (res->digits>reqdigits)
          res->digits=decGetDigits(acc, D2U(res->digits));
        decSetCoeff(res, set, acc, res->digits, &residue, status);
      #if DECSUBSET
        }
       else { /* subset arithmetic rounds from original significant digit  */
        /* May have an underestimate.  This only occurs when both  */
        /* numbers fit in DECDPUN digits and are padding with a  */
        /* negative multiple (-10, -100...) and the top digit(s) become  */
        /* 0.  (This only matters when using X3.274 rules where the  */
        /* leading zero could be included in the rounding.)  */
        if (res->digits<maxdigits) {
          *(acc+D2U(res->digits))=0; /* ensure leading 0 is there  */
          res->digits=maxdigits;
          }
         else {
          /* remove leading zeros that added due to rounding up to  */
          /* integral Units (but only those in excess of the original  */
          /* maxdigits length, unless extended) before test for rounding.  */
          if (res->digits>reqdigits) {
            res->digits=decGetDigits(acc, D2U(res->digits));
            if (res->digits<maxdigits) res->digits=maxdigits;
            }
          }
        decSetCoeff(res, set, acc, res->digits, &residue, status);
        /* Now apply rounding if needed before removing leading zeros.  */
        /* This is safe because subnormals are not a possibility  */
        if (residue!=0) {
          decApplyRound(res, set, residue, status);
          residue=0;                 /* did what needed to be done  */
          }
        } /* subset  */
      #endif
      } /* used buffer  */

    /* strip leading zeros [these were left on in case of subset subtract]  */
    res->digits=decGetDigits(res->lsu, D2U(res->digits));

    /* apply checks and rounding  */
    decFinish(res, set, &residue, status);

    /* "When the sum of two operands with opposite signs is exactly  */
    /* zero, the sign of that sum shall be '+' in all rounding modes  */
    /* except round toward -Infinity, in which mode that sign shall be  */
    /* '-'."  [Subset zeros also never have '-', set by decFinish.]  */
    if (ISZERO(res) && diffsign
     #if DECSUBSET
     && set->extended
     #endif
     && (*status&DEC_Inexact)==0) {
      if (set->round==DEC_ROUND_FLOOR) res->bits|=DECNEG;   /* sign -  */
                                  else res->bits&=~DECNEG;  /* sign +  */
      }
    } while(0);                              /* end protected  */

  if (allocacc!=nullptr) free(allocacc);        /* drop any storage used  */
  #if DECSUBSET
  if (allocrhs!=nullptr) free(allocrhs);        /* ..  */
  if (alloclhs!=nullptr) free(alloclhs);        /* ..  */
  #endif
  return res;
  } /* decAddOp  */

/* ------------------------------------------------------------------ */
/* decDivideOp -- division operation                                  */
/*                                                                    */
/*  This routine performs the calculations for all four division      */
/*  operators (divide, divideInteger, remainder, remainderNear).      */
/*                                                                    */
/*  C=A op B                                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X/X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*   op  is DIVIDE, DIVIDEINT, REMAINDER, or REMNEAR respectively.    */
/*   status is the usual accumulator                                  */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* ------------------------------------------------------------------ */
/*   The underlying algorithm of this routine is the same as in the   */
/*   1981 S/370 implementation, that is, non-restoring long division  */
/*   with bi-unit (rather than bi-digit) estimation for each unit     */
/*   multiplier.  In this pseudocode overview, complications for the  */
/*   Remainder operators and division residues for exact rounding are */
/*   omitted for clarity.                                             */
/*                                                                    */
/*     Prepare operands and handle special values                     */
/*     Test for x/0 and then 0/x                                      */
/*     Exp =Exp1 - Exp2                                               */
/*     Exp =Exp +len(var1) -len(var2)                                 */
/*     Sign=Sign1 * Sign2                                             */
/*     Pad accumulator (Var1) to double-length with 0's (pad1)        */
/*     Pad Var2 to same length as Var1                                */
/*     msu2pair/plus=1st 2 or 1 units of var2, +1 to allow for round  */
/*     have=0                                                         */
/*     Do until (have=digits+1 OR residue=0)                          */
/*       if exp<0 then if integer divide/residue then leave           */
/*       this_unit=0                                                  */
/*       Do forever                                                   */
/*          compare numbers                                           */
/*          if <0 then leave inner_loop                               */
/*          if =0 then (* quick exit without subtract *) do           */
/*             this_unit=this_unit+1; output this_unit                */
/*             leave outer_loop; end                                  */
/*          Compare lengths of numbers (mantissae):                   */
/*          If same then tops2=msu2pair -- {units 1&2 of var2}        */
/*                  else tops2=msu2plus -- {0, unit 1 of var2}        */
/*          tops1=first_unit_of_Var1*10**DECDPUN +second_unit_of_var1 */
/*          mult=tops1/tops2  -- Good and safe guess at divisor       */
/*          if mult=0 then mult=1                                     */
/*          this_unit=this_unit+mult                                  */
/*          subtract                                                  */
/*          end inner_loop                                            */
/*        if have\=0 | this_unit\=0 then do                           */
/*          output this_unit                                          */
/*          have=have+1; end                                          */
/*        var2=var2/10                                                */
/*        exp=exp-1                                                   */
/*        end outer_loop                                              */
/*     exp=exp+1   -- set the proper exponent                         */
/*     if have=0 then generate answer=0                               */
/*     Return (Result is defined by Var1)                             */
/*                                                                    */
/* ------------------------------------------------------------------ */
/* Two working buffers are needed during the division; one (digits+   */
/* 1) to accumulate the result, and the other (up to 2*digits+1) for  */
/* long subtractions.  These are acc and var1 respectively.           */
/* var1 is a copy of the lhs coefficient, var2 is the rhs coefficient.*/
/* The static buffers may be larger than might be expected to allow   */
/* for calls from higher-level functions (notable exp).                */
/* ------------------------------------------------------------------ */
static decNumber * decDivideOp(decNumber *res,
                               const decNumber *lhs, const decNumber *rhs,
                               decContext *set, Flag op, uInt *status) {
  #if DECSUBSET
  decNumber *alloclhs=nullptr;        /* non-nullptr if rounded lhs allocated  */
  decNumber *allocrhs=nullptr;        /* .., rhs  */
  #endif
  Unit  accbuff[SD2U(DECBUFFER+DECDPUN+10)]; /* local buffer  */
  Unit  *acc=accbuff;              /* -> accumulator array for result  */
  Unit  *allocacc=nullptr;            /* -> allocated buffer, iff allocated  */
  Unit  *accnext;                  /* -> where next digit will go  */
  Int   acclength;                 /* length of acc needed [Units]  */
  Int   accunits;                  /* count of units accumulated  */
  Int   accdigits;                 /* count of digits accumulated  */

  Unit  varbuff[SD2U(DECBUFFER*2+DECDPUN)];  /* buffer for var1  */
  Unit  *var1=varbuff;             /* -> var1 array for long subtraction  */
  Unit  *varalloc=nullptr;            /* -> allocated buffer, iff used  */
  Unit  *msu1;                     /* -> msu of var1  */

  const Unit *var2;                /* -> var2 array  */
  const Unit *msu2;                /* -> msu of var2  */
  Int   msu2plus;                  /* msu2 plus one [does not vary]  */
  eInt  msu2pair;                  /* msu2 pair plus one [does not vary]  */

  Int   var1units, var2units;      /* actual lengths  */
  Int   var2ulen;                  /* logical length (units)  */
  Int   var1initpad=0;             /* var1 initial padding (digits)  */
  Int   maxdigits;                 /* longest LHS or required acc length  */
  Int   mult;                      /* multiplier for subtraction  */
  Unit  thisunit;                  /* current unit being accumulated  */
  Int   residue;                   /* for rounding  */
  Int   reqdigits=set->digits;     /* requested DIGITS  */
  Int   exponent;                  /* working exponent  */
  Int   maxexponent=0;             /* DIVIDE maximum exponent if unrounded  */
  uByte bits;                      /* working sign  */
  Unit  *target;                   /* work  */
  const Unit *source;              /* ..  */
  uInt  const *pow;                /* ..  */
  Int   shift, cut;                /* ..  */
  #if DECSUBSET
  Int   dropped;                   /* work  */
  #endif

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  do {                             /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operands and set lostDigits status, as needed  */
      if (lhs->digits>reqdigits) {
        alloclhs=decRoundOperand(lhs, set, status);
        if (alloclhs==nullptr) break;
        lhs=alloclhs;
        }
      if (rhs->digits>reqdigits) {
        allocrhs=decRoundOperand(rhs, set, status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      }
    #endif
    /* [following code does not require input rounding]  */

    bits=(lhs->bits^rhs->bits)&DECNEG;  /* assumed sign for divisions  */

    /* handle infinities and NaNs  */
    if (SPECIALARGS) {                  /* a special bit set  */
      if (SPECIALARGS & (DECSNAN | DECNAN)) { /* one or two NaNs  */
        decNaNs(res, lhs, rhs, set, status);
        break;
        }
      /* one or two infinities  */
      if (decNumberIsInfinite(lhs)) {   /* LHS (dividend) is infinite  */
        if (decNumberIsInfinite(rhs) || /* two infinities are invalid ..  */
            op & (REMAINDER | REMNEAR)) { /* as is remainder of infinity  */
          *status|=DEC_Invalid_operation;
          break;
          }
        /* [Note that infinity/0 raises no exceptions]  */
        uprv_decNumberZero(res);
        res->bits=bits|DECINF;          /* set +/- infinity  */
        break;
        }
       else {                           /* RHS (divisor) is infinite  */
        residue=0;
        if (op&(REMAINDER|REMNEAR)) {
          /* result is [finished clone of] lhs  */
          decCopyFit(res, lhs, set, &residue, status);
          }
         else {  /* a division  */
          uprv_decNumberZero(res);
          res->bits=bits;               /* set +/- zero  */
          /* for DIVIDEINT the exponent is always 0.  For DIVIDE, result  */
          /* is a 0 with infinitely negative exponent, clamped to minimum  */
          if (op&DIVIDE) {
            res->exponent=set->emin-set->digits+1;
            *status|=DEC_Clamped;
            }
          }
        decFinish(res, set, &residue, status);
        break;
        }
      }

    /* handle 0 rhs (x/0)  */
    if (ISZERO(rhs)) {                  /* x/0 is always exceptional  */
      if (ISZERO(lhs)) {
        uprv_decNumberZero(res);             /* [after lhs test]  */
        *status|=DEC_Division_undefined;/* 0/0 will become NaN  */
        }
       else {
        uprv_decNumberZero(res);
        if (op&(REMAINDER|REMNEAR)) *status|=DEC_Invalid_operation;
         else {
          *status|=DEC_Division_by_zero; /* x/0  */
          res->bits=bits|DECINF;         /* .. is +/- Infinity  */
          }
        }
      break;}

    /* handle 0 lhs (0/x)  */
    if (ISZERO(lhs)) {                  /* 0/x [x!=0]  */
      #if DECSUBSET
      if (!set->extended) uprv_decNumberZero(res);
       else {
      #endif
        if (op&DIVIDE) {
          residue=0;
          exponent=lhs->exponent-rhs->exponent; /* ideal exponent  */
          uprv_decNumberCopy(res, lhs);      /* [zeros always fit]  */
          res->bits=bits;               /* sign as computed  */
          res->exponent=exponent;       /* exponent, too  */
          decFinalize(res, set, &residue, status);   /* check exponent  */
          }
         else if (op&DIVIDEINT) {
          uprv_decNumberZero(res);           /* integer 0  */
          res->bits=bits;               /* sign as computed  */
          }
         else {                         /* a remainder  */
          exponent=rhs->exponent;       /* [save in case overwrite]  */
          uprv_decNumberCopy(res, lhs);      /* [zeros always fit]  */
          if (exponent<res->exponent) res->exponent=exponent; /* use lower  */
          }
      #if DECSUBSET
        }
      #endif
      break;}

    /* Precalculate exponent.  This starts off adjusted (and hence fits  */
    /* in 31 bits) and becomes the usual unadjusted exponent as the  */
    /* division proceeds.  The order of evaluation is important, here,  */
    /* to avoid wrap.  */
    exponent=(lhs->exponent+lhs->digits)-(rhs->exponent+rhs->digits);

    /* If the working exponent is -ve, then some quick exits are  */
    /* possible because the quotient is known to be <1  */
    /* [for REMNEAR, it needs to be < -1, as -0.5 could need work]  */
    if (exponent<0 && !(op==DIVIDE)) {
      if (op&DIVIDEINT) {
        uprv_decNumberZero(res);                  /* integer part is 0  */
        #if DECSUBSET
        if (set->extended)
        #endif
          res->bits=bits;                    /* set +/- zero  */
        break;}
      /* fastpath remainders so long as the lhs has the smaller  */
      /* (or equal) exponent  */
      if (lhs->exponent<=rhs->exponent) {
        if (op&REMAINDER || exponent<-1) {
          /* It is REMAINDER or safe REMNEAR; result is [finished  */
          /* clone of] lhs  (r = x - 0*y)  */
          residue=0;
          decCopyFit(res, lhs, set, &residue, status);
          decFinish(res, set, &residue, status);
          break;
          }
        /* [unsafe REMNEAR drops through]  */
        }
      } /* fastpaths  */

    /* Long (slow) division is needed; roll up the sleeves... */

    /* The accumulator will hold the quotient of the division.  */
    /* If it needs to be too long for stack storage, then allocate.  */
    acclength=D2U(reqdigits+DECDPUN);   /* in Units  */
    if (acclength*sizeof(Unit)>sizeof(accbuff)) {
      /* printf("malloc dvacc %ld units\n", acclength);  */
      allocacc = static_cast<Unit*>(malloc(acclength * sizeof(Unit)));
      if (allocacc==nullptr) {             /* hopeless -- abandon  */
        *status|=DEC_Insufficient_storage;
        break;}
      acc=allocacc;                     /* use the allocated space  */
      }

    /* var1 is the padded LHS ready for subtractions.  */
    /* If it needs to be too long for stack storage, then allocate.  */
    /* The maximum units needed for var1 (long subtraction) is:  */
    /* Enough for  */
    /*     (rhs->digits+reqdigits-1) -- to allow full slide to right  */
    /* or  (lhs->digits)             -- to allow for long lhs  */
    /* whichever is larger  */
    /*   +1                -- for rounding of slide to right  */
    /*   +1                -- for leading 0s  */
    /*   +1                -- for pre-adjust if a remainder or DIVIDEINT  */
    /* [Note: unused units do not participate in decUnitAddSub data]  */
    maxdigits=rhs->digits+reqdigits-1;
    if (lhs->digits>maxdigits) maxdigits=lhs->digits;
    var1units=D2U(maxdigits)+2;
    /* allocate a guard unit above msu1 for REMAINDERNEAR  */
    if (!(op&DIVIDE)) var1units++;
    if ((var1units+1)*sizeof(Unit)>sizeof(varbuff)) {
      /* printf("malloc dvvar %ld units\n", var1units+1);  */
      varalloc = static_cast<Unit*>(malloc((var1units + 1) * sizeof(Unit)));
      if (varalloc==nullptr) {             /* hopeless -- abandon  */
        *status|=DEC_Insufficient_storage;
        break;}
      var1=varalloc;                    /* use the allocated space  */
      }

    /* Extend the lhs and rhs to full long subtraction length.  The lhs  */
    /* is truly extended into the var1 buffer, with 0 padding, so a  */
    /* subtract in place is always possible.  The rhs (var2) has  */
    /* virtual padding (implemented by decUnitAddSub).  */
    /* One guard unit was allocated above msu1 for rem=rem+rem in  */
    /* REMAINDERNEAR.  */
    msu1=var1+var1units-1;              /* msu of var1  */
    source=lhs->lsu+D2U(lhs->digits)-1; /* msu of input array  */
    for (target=msu1; source>=lhs->lsu; source--, target--) *target=*source;
    for (; target>=var1; target--) *target=0;

    /* rhs (var2) is left-aligned with var1 at the start  */
    var2ulen=var1units;                 /* rhs logical length (units)  */
    var2units=D2U(rhs->digits);         /* rhs actual length (units)  */
    var2=rhs->lsu;                      /* -> rhs array  */
    msu2=var2+var2units-1;              /* -> msu of var2 [never changes]  */
    /* now set up the variables which will be used for estimating the  */
    /* multiplication factor.  If these variables are not exact, add  */
    /* 1 to make sure that the multiplier is never overestimated.  */
    msu2plus=*msu2;                     /* it's value ..  */
    if (var2units>1) msu2plus++;        /* .. +1 if any more  */
    msu2pair = static_cast<eInt>(*msu2) * (DECDPUNMAX + 1); /* top two pair ..  */
    if (var2units>1) {                  /* .. [else treat 2nd as 0]  */
      msu2pair+=*(msu2-1);              /* ..  */
      if (var2units>2) msu2pair++;      /* .. +1 if any more  */
      }

    /* The calculation is working in units, which may have leading zeros,  */
    /* but the exponent was calculated on the assumption that they are  */
    /* both left-aligned.  Adjust the exponent to compensate: add the  */
    /* number of leading zeros in var1 msu and subtract those in var2 msu.  */
    /* [This is actually done by counting the digits and negating, as  */
    /* lead1=DECDPUN-digits1, and similarly for lead2.]  */
    for (pow=&powers[1]; *msu1>=*pow; pow++) exponent--;
    for (pow=&powers[1]; *msu2>=*pow; pow++) exponent++;

    /* Now, if doing an integer divide or remainder, ensure that  */
    /* the result will be Unit-aligned.  To do this, shift the var1  */
    /* accumulator towards least if need be.  (It's much easier to  */
    /* do this now than to reassemble the residue afterwards, if  */
    /* doing a remainder.)  Also ensure the exponent is not negative.  */
    if (!(op&DIVIDE)) {
      Unit *u;                          /* work  */
      /* save the initial 'false' padding of var1, in digits  */
      var1initpad=(var1units-D2U(lhs->digits))*DECDPUN;
      /* Determine the shift to do.  */
      if (exponent<0) cut=-exponent;
       else cut=DECDPUN-exponent%DECDPUN;
      decShiftToLeast(var1, var1units, cut);
      exponent+=cut;                    /* maintain numerical value  */
      var1initpad-=cut;                 /* .. and reduce padding  */
      /* clean any most-significant units which were just emptied  */
      for (u=msu1; cut>=DECDPUN; cut-=DECDPUN, u--) *u=0;
      } /* align  */
     else { /* is DIVIDE  */
      maxexponent=lhs->exponent-rhs->exponent;    /* save  */
      /* optimization: if the first iteration will just produce 0,  */
      /* preadjust to skip it [valid for DIVIDE only]  */
      if (*msu1<*msu2) {
        var2ulen--;                     /* shift down  */
        exponent-=DECDPUN;              /* update the exponent  */
        }
      }

    /* ---- start the long-division loops ------------------------------  */
    accunits=0;                         /* no units accumulated yet  */
    accdigits=0;                        /* .. or digits  */
    accnext=acc+acclength-1;            /* -> msu of acc [NB: allows digits+1]  */
    for (;;) {                          /* outer forever loop  */
      thisunit=0;                       /* current unit assumed 0  */
      /* find the next unit  */
      for (;;) {                        /* inner forever loop  */
        /* strip leading zero units [from either pre-adjust or from  */
        /* subtract last time around].  Leave at least one unit.  */
        for (; *msu1==0 && msu1>var1; msu1--) var1units--;

        if (var1units<var2ulen) break;       /* var1 too low for subtract  */
        if (var1units==var2ulen) {           /* unit-by-unit compare needed  */
          /* compare the two numbers, from msu  */
          const Unit *pv1, *pv2;
          Unit v2;                           /* units to compare  */
          pv2=msu2;                          /* -> msu  */
          for (pv1=msu1; ; pv1--, pv2--) {
            /* v1=*pv1 -- always OK  */
            v2=0;                            /* assume in padding  */
            if (pv2>=var2) v2=*pv2;          /* in range  */
            if (*pv1!=v2) break;             /* no longer the same  */
            if (pv1==var1) break;            /* done; leave pv1 as is  */
            }
          /* here when all inspected or a difference seen  */
          if (*pv1<v2) break;                /* var1 too low to subtract  */
          if (*pv1==v2) {                    /* var1 == var2  */
            /* reach here if var1 and var2 are identical; subtraction  */
            /* would increase digit by one, and the residue will be 0 so  */
            /* the calculation is done; leave the loop with residue=0.  */
            thisunit++;                      /* as though subtracted  */
            *var1=0;                         /* set var1 to 0  */
            var1units=1;                     /* ..  */
            break;  /* from inner  */
            } /* var1 == var2  */
          /* *pv1>v2.  Prepare for real subtraction; the lengths are equal  */
          /* Estimate the multiplier (there's always a msu1-1)...  */
          /* Bring in two units of var2 to provide a good estimate.  */
            mult = static_cast<Int>((static_cast<eInt>(*msu1) * (DECDPUNMAX + 1) + *(msu1 - 1)) / msu2pair);
          } /* lengths the same  */
         else { /* var1units > var2ulen, so subtraction is safe  */
          /* The var2 msu is one unit towards the lsu of the var1 msu,  */
          /* so only one unit for var2 can be used.  */
          mult = static_cast<Int>((static_cast<eInt>(*msu1) * (DECDPUNMAX + 1) + *(msu1 - 1)) / msu2plus);
          }
        if (mult==0) mult=1;                 /* must always be at least 1  */
        /* subtraction needed; var1 is > var2  */
        thisunit = static_cast<Unit>(thisunit + mult); /* accumulate  */
        /* subtract var1-var2, into var1; only the overlap needs  */
        /* processing, as this is an in-place calculation  */
        shift=var2ulen-var2units;
        #if DECTRACE
          decDumpAr('1', &var1[shift], var1units-shift);
          decDumpAr('2', var2, var2units);
          printf("m=%ld\n", -mult);
        #endif
        decUnitAddSub(&var1[shift], var1units-shift,
                      var2, var2units, 0,
                      &var1[shift], -mult);
        #if DECTRACE
          decDumpAr('#', &var1[shift], var1units-shift);
        #endif
        /* var1 now probably has leading zeros; these are removed at the  */
        /* top of the inner loop.  */
        } /* inner loop  */

      /* The next unit has been calculated in full; unless it's a  */
      /* leading zero, add to acc  */
      if (accunits!=0 || thisunit!=0) {      /* is first or non-zero  */
        *accnext=thisunit;                   /* store in accumulator  */
        /* account exactly for the new digits  */
        if (accunits==0) {
          accdigits++;                       /* at least one  */
          for (pow=&powers[1]; thisunit>=*pow; pow++) accdigits++;
          }
         else accdigits+=DECDPUN;
        accunits++;                          /* update count  */
        accnext--;                           /* ready for next  */
        if (accdigits>reqdigits) break;      /* have enough digits  */
        }

      /* if the residue is zero, the operation is done (unless divide  */
      /* or divideInteger and still not enough digits yet)  */
      if (*var1==0 && var1units==1) {        /* residue is 0  */
        if (op&(REMAINDER|REMNEAR)) break;
        if ((op&DIVIDE) && (exponent<=maxexponent)) break;
        /* [drop through if divideInteger]  */
        }
      /* also done enough if calculating remainder or integer  */
      /* divide and just did the last ('units') unit  */
      if (exponent==0 && !(op&DIVIDE)) break;

      /* to get here, var1 is less than var2, so divide var2 by the per-  */
      /* Unit power of ten and go for the next digit  */
      var2ulen--;                            /* shift down  */
      exponent-=DECDPUN;                     /* update the exponent  */
      } /* outer loop  */

    /* ---- division is complete ---------------------------------------  */
    /* here: acc      has at least reqdigits+1 of good results (or fewer  */
    /*                if early stop), starting at accnext+1 (its lsu)  */
    /*       var1     has any residue at the stopping point  */
    /*       accunits is the number of digits collected in acc  */
    if (accunits==0) {             /* acc is 0  */
      accunits=1;                  /* show have a unit ..  */
      accdigits=1;                 /* ..  */
      *accnext=0;                  /* .. whose value is 0  */
      }
     else accnext++;               /* back to last placed  */
    /* accnext now -> lowest unit of result  */

    residue=0;                     /* assume no residue  */
    if (op&DIVIDE) {
      /* record the presence of any residue, for rounding  */
      if (*var1!=0 || var1units>1) residue=1;
       else { /* no residue  */
        /* Had an exact division; clean up spurious trailing 0s.  */
        /* There will be at most DECDPUN-1, from the final multiply,  */
        /* and then only if the result is non-0 (and even) and the  */
        /* exponent is 'loose'.  */
        #if DECDPUN>1
        Unit lsu=*accnext;
        if (!(lsu&0x01) && (lsu!=0)) {
          /* count the trailing zeros  */
          Int drop=0;
          for (;; drop++) {    /* [will terminate because lsu!=0]  */
            if (exponent>=maxexponent) break;     /* don't chop real 0s  */
            #if DECDPUN<=4
              if ((lsu-QUOT10(lsu, drop+1)
                  *powers[drop+1])!=0) break;     /* found non-0 digit  */
            #else
              if (lsu%powers[drop+1]!=0) break;   /* found non-0 digit  */
            #endif
            exponent++;
            }
          if (drop>0) {
            accunits=decShiftToLeast(accnext, accunits, drop);
            accdigits=decGetDigits(accnext, accunits);
            accunits=D2U(accdigits);
            /* [exponent was adjusted in the loop]  */
            }
          } /* neither odd nor 0  */
        #endif
        } /* exact divide  */
      } /* divide  */
     else /* op!=DIVIDE */ {
      /* check for coefficient overflow  */
      if (accdigits+exponent>reqdigits) {
        *status|=DEC_Division_impossible;
        break;
        }
      if (op & (REMAINDER|REMNEAR)) {
        /* [Here, the exponent will be 0, because var1 was adjusted  */
        /* appropriately.]  */
        Int postshift;                       /* work  */
        Flag wasodd=0;                       /* integer was odd  */
        Unit *quotlsu;                       /* for save  */
        Int  quotdigits;                     /* ..  */

        bits=lhs->bits;                      /* remainder sign is always as lhs  */

        /* Fastpath when residue is truly 0 is worthwhile [and  */
        /* simplifies the code below]  */
        if (*var1==0 && var1units==1) {      /* residue is 0  */
          Int exp=lhs->exponent;             /* save min(exponents)  */
          if (rhs->exponent<exp) exp=rhs->exponent;
          uprv_decNumberZero(res);                /* 0 coefficient  */
          #if DECSUBSET
          if (set->extended)
          #endif
          res->exponent=exp;                 /* .. with proper exponent  */
          res->bits = static_cast<uByte>(bits & DECNEG); /* [cleaned]  */
          decFinish(res, set, &residue, status);   /* might clamp  */
          break;
          }
        /* note if the quotient was odd  */
        if (*accnext & 0x01) wasodd=1;       /* acc is odd  */
        quotlsu=accnext;                     /* save in case need to reinspect  */
        quotdigits=accdigits;                /* ..  */

        /* treat the residue, in var1, as the value to return, via acc  */
        /* calculate the unused zero digits.  This is the smaller of:  */
        /*   var1 initial padding (saved above)  */
        /*   var2 residual padding, which happens to be given by:  */
        postshift=var1initpad+exponent-lhs->exponent+rhs->exponent;
        /* [the 'exponent' term accounts for the shifts during divide]  */
        if (var1initpad<postshift) postshift=var1initpad;

        /* shift var1 the requested amount, and adjust its digits  */
        var1units=decShiftToLeast(var1, var1units, postshift);
        accnext=var1;
        accdigits=decGetDigits(var1, var1units);
        accunits=D2U(accdigits);

        exponent=lhs->exponent;         /* exponent is smaller of lhs & rhs  */
        if (rhs->exponent<exponent) exponent=rhs->exponent;

        /* Now correct the result if doing remainderNear; if it  */
        /* (looking just at coefficients) is > rhs/2, or == rhs/2 and  */
        /* the integer was odd then the result should be rem-rhs.  */
        if (op&REMNEAR) {
          Int compare, tarunits;        /* work  */
          Unit *up;                     /* ..  */
          /* calculate remainder*2 into the var1 buffer (which has  */
          /* 'headroom' of an extra unit and hence enough space)  */
          /* [a dedicated 'double' loop would be faster, here]  */
          tarunits=decUnitAddSub(accnext, accunits, accnext, accunits,
                                 0, accnext, 1);
          /* decDumpAr('r', accnext, tarunits);  */

          /* Here, accnext (var1) holds tarunits Units with twice the  */
          /* remainder's coefficient, which must now be compared to the  */
          /* RHS.  The remainder's exponent may be smaller than the RHS's.  */
          compare=decUnitCompare(accnext, tarunits, rhs->lsu, D2U(rhs->digits),
                                 rhs->exponent-exponent);
          if (compare==BADINT) {             /* deep trouble  */
            *status|=DEC_Insufficient_storage;
            break;}

          /* now restore the remainder by dividing by two; the lsu  */
          /* is known to be even.  */
          for (up=accnext; up<accnext+tarunits; up++) {
            Int half;              /* half to add to lower unit  */
            half=*up & 0x01;
            *up/=2;                /* [shift]  */
            if (!half) continue;
            *(up-1)+=(DECDPUNMAX+1)/2;
            }
          /* [accunits still describes the original remainder length]  */

          if (compare>0 || (compare==0 && wasodd)) { /* adjustment needed  */
            Int exp, expunits, exprem;       /* work  */
            /* This is effectively causing round-up of the quotient,  */
            /* so if it was the rare case where it was full and all  */
            /* nines, it would overflow and hence division-impossible  */
            /* should be raised  */
            Flag allnines=0;                 /* 1 if quotient all nines  */
            if (quotdigits==reqdigits) {     /* could be borderline  */
              for (up=quotlsu; ; up++) {
                if (quotdigits>DECDPUN) {
                  if (*up!=DECDPUNMAX) break;/* non-nines  */
                  }
                 else {                      /* this is the last Unit  */
                  if (*up==powers[quotdigits]-1) allnines=1;
                  break;
                  }
                quotdigits-=DECDPUN;         /* checked those digits  */
                } /* up  */
              } /* borderline check  */
            if (allnines) {
              *status|=DEC_Division_impossible;
              break;}

            /* rem-rhs is needed; the sign will invert.  Again, var1  */
            /* can safely be used for the working Units array.  */
            exp=rhs->exponent-exponent;      /* RHS padding needed  */
            /* Calculate units and remainder from exponent.  */
            expunits=exp/DECDPUN;
            exprem=exp%DECDPUN;
            /* subtract [A+B*(-m)]; the result will always be negative  */
            accunits=-decUnitAddSub(accnext, accunits,
                                    rhs->lsu, D2U(rhs->digits),
                                    expunits, accnext, -static_cast<Int>(powers[exprem]));
            accdigits=decGetDigits(accnext, accunits); /* count digits exactly  */
            accunits=D2U(accdigits);    /* and recalculate the units for copy  */
            /* [exponent is as for original remainder]  */
            bits^=DECNEG;               /* flip the sign  */
            }
          } /* REMNEAR  */
        } /* REMAINDER or REMNEAR  */
      } /* not DIVIDE  */

    /* Set exponent and bits  */
    res->exponent=exponent;
    res->bits = static_cast<uByte>(bits & DECNEG); /* [cleaned]  */

    /* Now the coefficient.  */
    decSetCoeff(res, set, accnext, accdigits, &residue, status);

    decFinish(res, set, &residue, status);   /* final cleanup  */

    #if DECSUBSET
    /* If a divide then strip trailing zeros if subset [after round]  */
    if (!set->extended && (op==DIVIDE)) decTrim(res, set, 0, 1, &dropped);
    #endif
    } while(0);                              /* end protected  */

  if (varalloc!=nullptr) free(varalloc);   /* drop any storage used  */
  if (allocacc!=nullptr) free(allocacc);   /* ..  */
  #if DECSUBSET
  if (allocrhs!=nullptr) free(allocrhs);   /* ..  */
  if (alloclhs!=nullptr) free(alloclhs);   /* ..  */
  #endif
  return res;
  } /* decDivideOp  */

/* ------------------------------------------------------------------ */
/* decMultiplyOp -- multiplication operation                          */
/*                                                                    */
/*  This routine performs the multiplication C=A x B.                 */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X*X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*   status is the usual accumulator                                  */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* ------------------------------------------------------------------ */
/* 'Classic' multiplication is used rather than Karatsuba, as the     */
/* latter would give only a minor improvement for the short numbers   */
/* expected to be handled most (and uses much more memory).           */
/*                                                                    */
/* There are two major paths here: the general-purpose ('old code')   */
/* path which handles all DECDPUN values, and a fastpath version      */
/* which is used if 64-bit ints are available, DECDPUN<=4, and more   */
/* than two calls to decUnitAddSub would be made.                     */
/*                                                                    */
/* The fastpath version lumps units together into 8-digit or 9-digit  */
/* chunks, and also uses a lazy carry strategy to minimise expensive  */
/* 64-bit divisions.  The chunks are then broken apart again into     */
/* units for continuing processing.  Despite this overhead, the       */
/* fastpath can speed up some 16-digit operations by 10x (and much    */
/* more for higher-precision calculations).                           */
/*                                                                    */
/* A buffer always has to be used for the accumulator; in the         */
/* fastpath, buffers are also always needed for the chunked copies of */
/* of the operand coefficients.                                       */
/* Static buffers are larger than needed just for multiply, to allow  */
/* for calls from other operations (notably exp).                     */
/* ------------------------------------------------------------------ */
#define FASTMUL (DECUSE64 && DECDPUN<5)
static decNumber * decMultiplyOp(decNumber *res, const decNumber *lhs,
                                 const decNumber *rhs, decContext *set,
                                 uInt *status) {
  Int    accunits;                 /* Units of accumulator in use  */
  Int    exponent;                 /* work  */
  Int    residue=0;                /* rounding residue  */
  uByte  bits;                     /* result sign  */
  Unit  *acc;                      /* -> accumulator Unit array  */
  Int    needbytes;                /* size calculator  */
  void  *allocacc=nullptr;            /* -> allocated accumulator, iff allocated  */
  Unit  accbuff[SD2U(DECBUFFER*4+1)]; /* buffer (+1 for DECBUFFER==0,  */
                                   /* *4 for calls from other operations)  */
  const Unit *mer, *mermsup;       /* work  */
  Int   madlength;                 /* Units in multiplicand  */
  Int   shift;                     /* Units to shift multiplicand by  */

  #if FASTMUL
    /* if DECDPUN is 1 or 3 work in base 10**9, otherwise  */
    /* (DECDPUN is 2 or 4) then work in base 10**8  */
    #if DECDPUN & 1                /* odd  */
      #define FASTBASE 1000000000  /* base  */
      #define FASTDIGS          9  /* digits in base  */
      #define FASTLAZY         18  /* carry resolution point [1->18]  */
    #else
      #define FASTBASE  100000000
      #define FASTDIGS          8
      #define FASTLAZY       1844  /* carry resolution point [1->1844]  */
    #endif
    /* three buffers are used, two for chunked copies of the operands  */
    /* (base 10**8 or base 10**9) and one base 2**64 accumulator with  */
    /* lazy carry evaluation  */
    uInt   zlhibuff[(DECBUFFER*2+1)/8+1]; /* buffer (+1 for DECBUFFER==0)  */
    uInt  *zlhi=zlhibuff;                 /* -> lhs array  */
    uInt  *alloclhi=nullptr;                 /* -> allocated buffer, iff allocated  */
    uInt   zrhibuff[(DECBUFFER*2+1)/8+1]; /* buffer (+1 for DECBUFFER==0)  */
    uInt  *zrhi=zrhibuff;                 /* -> rhs array  */
    uInt  *allocrhi=nullptr;                 /* -> allocated buffer, iff allocated  */
    uLong  zaccbuff[(DECBUFFER*2+1)/4+2]; /* buffer (+1 for DECBUFFER==0)  */
    /* [allocacc is shared for both paths, as only one will run]  */
    uLong *zacc=zaccbuff;          /* -> accumulator array for exact result  */
    #if DECDPUN==1
    Int    zoff;                   /* accumulator offset  */
    #endif
    uInt  *lip, *rip;              /* item pointers  */
    uInt  *lmsi, *rmsi;            /* most significant items  */
    Int    ilhs, irhs, iacc;       /* item counts in the arrays  */
    Int    lazy;                   /* lazy carry counter  */
    uLong  lcarry;                 /* uLong carry  */
    uInt   carry;                  /* carry (NB not uLong)  */
    Int    count;                  /* work  */
    const  Unit *cup;              /* ..  */
    Unit  *up;                     /* ..  */
    uLong *lp;                     /* ..  */
    Int    p;                      /* ..  */
  #endif

  #if DECSUBSET
    decNumber *alloclhs=nullptr;      /* -> allocated buffer, iff allocated  */
    decNumber *allocrhs=nullptr;      /* -> allocated buffer, iff allocated  */
  #endif

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  /* precalculate result sign  */
  bits = static_cast<uByte>((lhs->bits ^ rhs->bits) & DECNEG);

  /* handle infinities and NaNs  */
  if (SPECIALARGS) {               /* a special bit set  */
    if (SPECIALARGS & (DECSNAN | DECNAN)) { /* one or two NaNs  */
      decNaNs(res, lhs, rhs, set, status);
      return res;}
    /* one or two infinities; Infinity * 0 is invalid  */
    if (((lhs->bits & DECINF)==0 && ISZERO(lhs))
      ||((rhs->bits & DECINF)==0 && ISZERO(rhs))) {
      *status|=DEC_Invalid_operation;
      return res;}
    uprv_decNumberZero(res);
    res->bits=bits|DECINF;         /* infinity  */
    return res;}

  /* For best speed, as in DMSRCN [the original Rexx numerics  */
  /* module], use the shorter number as the multiplier (rhs) and  */
  /* the longer as the multiplicand (lhs) to minimise the number of  */
  /* adds (partial products)  */
  if (lhs->digits<rhs->digits) {   /* swap...  */
    const decNumber *hold=lhs;
    lhs=rhs;
    rhs=hold;
    }

  do {                             /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operands and set lostDigits status, as needed  */
      if (lhs->digits>set->digits) {
        alloclhs=decRoundOperand(lhs, set, status);
        if (alloclhs==nullptr) break;
        lhs=alloclhs;
        }
      if (rhs->digits>set->digits) {
        allocrhs=decRoundOperand(rhs, set, status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      }
    #endif
    /* [following code does not require input rounding]  */

    #if FASTMUL                    /* fastpath can be used  */
    /* use the fast path if there are enough digits in the shorter  */
    /* operand to make the setup and takedown worthwhile  */
    #define NEEDTWO (DECDPUN*2)    /* within two decUnitAddSub calls  */
    if (rhs->digits>NEEDTWO) {     /* use fastpath...  */
      /* calculate the number of elements in each array  */
      ilhs=(lhs->digits+FASTDIGS-1)/FASTDIGS; /* [ceiling]  */
      irhs=(rhs->digits+FASTDIGS-1)/FASTDIGS; /* ..  */
      iacc=ilhs+irhs;

      /* allocate buffers if required, as usual  */
      needbytes=ilhs*sizeof(uInt);
      if (needbytes > static_cast<Int>(sizeof(zlhibuff))) {
        alloclhi = static_cast<uInt*>(malloc(needbytes));
        zlhi=alloclhi;}
      needbytes=irhs*sizeof(uInt);
      if (needbytes > static_cast<Int>(sizeof(zrhibuff))) {
        allocrhi = static_cast<uInt*>(malloc(needbytes));
        zrhi=allocrhi;}

      /* Allocating the accumulator space needs a special case when  */
      /* DECDPUN=1 because when converting the accumulator to Units  */
      /* after the multiplication each 8-byte item becomes 9 1-byte  */
      /* units.  Therefore iacc extra bytes are needed at the front  */
      /* (rounded up to a multiple of 8 bytes), and the uLong  */
      /* accumulator starts offset the appropriate number of units  */
      /* to the right to avoid overwrite during the unchunking.  */

      /* Make sure no signed int overflow below. This is always true */
      /* if the given numbers have less digits than DEC_MAX_DIGITS. */
      U_ASSERT((uint32_t)iacc <= INT32_MAX/sizeof(uLong));
      needbytes=iacc*sizeof(uLong);
      #if DECDPUN==1
      zoff=(iacc+7)/8;        /* items to offset by  */
      needbytes+=zoff*8;
      #endif
      if (needbytes > static_cast<Int>(sizeof(zaccbuff))) {
        allocacc = static_cast<uLong*>(malloc(needbytes));
        zacc = static_cast<uLong*>(allocacc);}
      if (zlhi==nullptr||zrhi==nullptr||zacc==nullptr) {
        *status|=DEC_Insufficient_storage;
        break;}

      acc = reinterpret_cast<Unit*>(zacc); /* -> target Unit array  */
      #if DECDPUN==1
      zacc+=zoff;             /* start uLong accumulator to right  */
      #endif

      /* assemble the chunked copies of the left and right sides  */
      for (count=lhs->digits, cup=lhs->lsu, lip=zlhi; count>0; lip++)
        for (p=0, *lip=0; p<FASTDIGS && count>0;
             p+=DECDPUN, cup++, count-=DECDPUN)
          *lip+=*cup*powers[p];
      lmsi=lip-1;     /* save -> msi  */
      for (count=rhs->digits, cup=rhs->lsu, rip=zrhi; count>0; rip++)
        for (p=0, *rip=0; p<FASTDIGS && count>0;
             p+=DECDPUN, cup++, count-=DECDPUN)
          *rip+=*cup*powers[p];
      rmsi=rip-1;     /* save -> msi  */

      /* zero the accumulator  */
      for (lp=zacc; lp<zacc+iacc; lp++) *lp=0;

      /* Start the multiplication */
      /* Resolving carries can dominate the cost of accumulating the  */
      /* partial products, so this is only done when necessary.  */
      /* Each uLong item in the accumulator can hold values up to  */
      /* 2**64-1, and each partial product can be as large as  */
      /* (10**FASTDIGS-1)**2.  When FASTDIGS=9, this can be added to  */
      /* itself 18.4 times in a uLong without overflowing, so during  */
      /* the main calculation resolution is carried out every 18th  */
      /* add -- every 162 digits.  Similarly, when FASTDIGS=8, the  */
      /* partial products can be added to themselves 1844.6 times in  */
      /* a uLong without overflowing, so intermediate carry  */
      /* resolution occurs only every 14752 digits.  Hence for common  */
      /* short numbers usually only the one final carry resolution  */
      /* occurs.  */
      /* (The count is set via FASTLAZY to simplify experiments to  */
      /* measure the value of this approach: a 35% improvement on a  */
      /* [34x34] multiply.)  */
      lazy=FASTLAZY;                         /* carry delay count  */
      for (rip=zrhi; rip<=rmsi; rip++) {     /* over each item in rhs  */
        lp=zacc+(rip-zrhi);                  /* where to add the lhs  */
        for (lip=zlhi; lip<=lmsi; lip++, lp++) { /* over each item in lhs  */
          *lp += static_cast<uLong>(*lip) * (*rip); /* [this should in-line]  */
          } /* lip loop  */
        lazy--;
        if (lazy>0 && rip!=rmsi) continue;
        lazy=FASTLAZY;                       /* reset delay count  */
        /* spin up the accumulator resolving overflows  */
        for (lp=zacc; lp<zacc+iacc; lp++) {
          if (*lp<FASTBASE) continue;        /* it fits  */
          lcarry=*lp/FASTBASE;               /* top part [slow divide]  */
          /* lcarry can exceed 2**32-1, so check again; this check  */
          /* and occasional extra divide (slow) is well worth it, as  */
          /* it allows FASTLAZY to be increased to 18 rather than 4  */
          /* in the FASTDIGS=9 case  */
          if (lcarry<FASTBASE) carry = static_cast<uInt>(lcarry); /* [usual]  */
           else { /* two-place carry [fairly rare]  */
            uInt carry2 = static_cast<uInt>(lcarry / FASTBASE);        /* top top part  */
            *(lp+2)+=carry2;                        /* add to item+2  */
            *lp -= (static_cast<uLong>(FASTBASE) * FASTBASE * carry2); /* [slow]  */
            carry = static_cast<uInt>(lcarry - (static_cast<uLong>(FASTBASE) * carry2)); /* [inline]  */
            }
          *(lp+1)+=carry;                    /* add to item above [inline]  */
          *lp -= (static_cast<uLong>(FASTBASE) * carry); /* [inline]  */
          } /* carry resolution  */
        } /* rip loop  */

      /* The multiplication is complete; time to convert back into  */
      /* units.  This can be done in-place in the accumulator and in  */
      /* 32-bit operations, because carries were resolved after the  */
      /* final add.  This needs N-1 divides and multiplies for  */
      /* each item in the accumulator (which will become up to N  */
      /* units, where 2<=N<=9).  */
      for (lp=zacc, up=acc; lp<zacc+iacc; lp++) {
        uInt item = static_cast<uInt>(*lp); /* decapitate to uInt  */
        for (p=0; p<FASTDIGS-DECDPUN; p+=DECDPUN, up++) {
          uInt part=item/(DECDPUNMAX+1);
          *up = static_cast<Unit>(item - (part * (DECDPUNMAX + 1)));
          item=part;
          } /* p  */
        *up = static_cast<Unit>(item); up++; /* [final needs no division]  */
        } /* lp  */
      accunits = static_cast<int32_t>(up-acc);                       /* count of units  */
      }
     else { /* here to use units directly, without chunking ['old code']  */
    #endif

      /* if accumulator will be too long for local storage, then allocate  */
      acc=accbuff;                 /* -> assume buffer for accumulator  */
      needbytes=(D2U(lhs->digits)+D2U(rhs->digits))*sizeof(Unit);
      if (needbytes > static_cast<Int>(sizeof(accbuff))) {
        allocacc = static_cast<Unit*>(malloc(needbytes));
        if (allocacc==nullptr) {*status|=DEC_Insufficient_storage; break;}
        acc = static_cast<Unit*>(allocacc); /* use the allocated space  */
        }

      /* Now the main long multiplication loop */
      /* Unlike the equivalent in the IBM Java implementation, there  */
      /* is no advantage in calculating from msu to lsu.  So, do it  */
      /* by the book, as it were.  */
      /* Each iteration calculates ACC=ACC+MULTAND*MULT  */
      accunits=1;                  /* accumulator starts at '0'  */
      *acc=0;                      /* .. (lsu=0)  */
      shift=0;                     /* no multiplicand shift at first  */
      madlength=D2U(lhs->digits);  /* this won't change  */
      mermsup=rhs->lsu+D2U(rhs->digits); /* -> msu+1 of multiplier  */

      for (mer=rhs->lsu; mer<mermsup; mer++) {
        /* Here, *mer is the next Unit in the multiplier to use  */
        /* If non-zero [optimization] add it...  */
        if (*mer!=0) accunits=decUnitAddSub(&acc[shift], accunits-shift,
                                            lhs->lsu, madlength, 0,
                                            &acc[shift], *mer)
                                            + shift;
         else { /* extend acc with a 0; it will be used shortly  */
          *(acc+accunits)=0;       /* [this avoids length of <=0 later]  */
          accunits++;
          }
        /* multiply multiplicand by 10**DECDPUN for next Unit to left  */
        shift++;                   /* add this for 'logical length'  */
        } /* n  */
    #if FASTMUL
      } /* unchunked units  */
    #endif
    /* common end-path  */
    #if DECTRACE
      decDumpAr('*', acc, accunits);         /* Show exact result  */
    #endif

    /* acc now contains the exact result of the multiplication,  */
    /* possibly with a leading zero unit; build the decNumber from  */
    /* it, noting if any residue  */
    res->bits=bits;                          /* set sign  */
    res->digits=decGetDigits(acc, accunits); /* count digits exactly  */

    /* There can be a 31-bit wrap in calculating the exponent.  */
    /* This can only happen if both input exponents are negative and  */
    /* both their magnitudes are large.  If there was a wrap, set a  */
    /* safe very negative exponent, from which decFinalize() will  */
    /* raise a hard underflow shortly.  */
    exponent=lhs->exponent+rhs->exponent;    /* calculate exponent  */
    if (lhs->exponent<0 && rhs->exponent<0 && exponent>0)
      exponent=-2*DECNUMMAXE;                /* force underflow  */
    res->exponent=exponent;                  /* OK to overwrite now  */


    /* Set the coefficient.  If any rounding, residue records  */
    decSetCoeff(res, set, acc, res->digits, &residue, status);
    decFinish(res, set, &residue, status);   /* final cleanup  */
    } while(0);                         /* end protected  */

  if (allocacc!=nullptr) free(allocacc);   /* drop any storage used  */
  #if DECSUBSET
  if (allocrhs!=nullptr) free(allocrhs);   /* ..  */
  if (alloclhs!=nullptr) free(alloclhs);   /* ..  */
  #endif
  #if FASTMUL
  if (allocrhi!=nullptr) free(allocrhi);   /* ..  */
  if (alloclhi!=nullptr) free(alloclhi);   /* ..  */
  #endif
  return res;
  } /* decMultiplyOp  */

/* ------------------------------------------------------------------ */
/* decExpOp -- effect exponentiation                                  */
/*                                                                    */
/*   This computes C = exp(A)                                         */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context; note that rounding mode has no effect        */
/*                                                                    */
/* C must have space for set->digits digits. status is updated but    */
/* not set.                                                           */
/*                                                                    */
/* Restrictions:                                                      */
/*                                                                    */
/*   digits, emax, and -emin in the context must be less than         */
/*   2*DEC_MAX_MATH (1999998), and the rhs must be within these       */
/*   bounds or a zero.  This is an internal routine, so these         */
/*   restrictions are contractual and not enforced.                   */
/*                                                                    */
/* A finite result is rounded using DEC_ROUND_HALF_EVEN; it will      */
/* almost always be correctly rounded, but may be up to 1 ulp in      */
/* error in rare cases.                                               */
/*                                                                    */
/* Finite results will always be full precision and Inexact, except   */
/* when A is a zero or -Infinity (giving 1 or 0 respectively).        */
/* ------------------------------------------------------------------ */
/* This approach used here is similar to the algorithm described in   */
/*                                                                    */
/*   Variable Precision Exponential Function, T. E. Hull and          */
/*   A. Abrham, ACM Transactions on Mathematical Software, Vol 12 #2, */
/*   pp79-91, ACM, June 1986.                                         */
/*                                                                    */
/* with the main difference being that the iterations in the series   */
/* evaluation are terminated dynamically (which does not require the  */
/* extra variable-precision variables which are expensive in this     */
/* context).                                                          */
/*                                                                    */
/* The error analysis in Hull & Abrham's paper applies except for the */
/* round-off error accumulation during the series evaluation.  This   */
/* code does not precalculate the number of iterations and so cannot  */
/* use Horner's scheme.  Instead, the accumulation is done at double- */
/* precision, which ensures that the additions of the terms are exact */
/* and do not accumulate round-off (and any round-off errors in the   */
/* terms themselves move 'to the right' faster than they can          */
/* accumulate).  This code also extends the calculation by allowing,  */
/* in the spirit of other decNumber operators, the input to be more   */
/* precise than the result (the precision used is based on the more   */
/* precise of the input or requested result).                         */
/*                                                                    */
/* Implementation notes:                                              */
/*                                                                    */
/* 1. This is separated out as decExpOp so it can be called from      */
/*    other Mathematical functions (notably Ln) with a wider range    */
/*    than normal.  In particular, it can handle the slightly wider   */
/*    (double) range needed by Ln (which has to be able to calculate  */
/*    exp(-x) where x can be the tiniest number (Ntiny).              */
/*                                                                    */
/* 2. Normalizing x to be <=0.1 (instead of <=1) reduces loop         */
/*    iterations by approximately a third with additional (although    */
/*    diminishing) returns as the range is reduced to even smaller    */
/*    fractions.  However, h (the power of 10 used to correct the     */
/*    result at the end, see below) must be kept <=8 as otherwise     */
/*    the final result cannot be computed.  Hence the leverage is a   */
/*    sliding value (8-h), where potentially the range is reduced     */
/*    more for smaller values.                                        */
/*                                                                    */
/*    The leverage that can be applied in this way is severely        */
/*    limited by the cost of the raise-to-the power at the end,       */
/*    which dominates when the number of iterations is small (less    */
/*    than ten) or when rhs is short.  As an example, the adjustment  */
/*    x**10,000,000 needs 31 multiplications, all but one full-width. */
/*                                                                    */
/* 3. The restrictions (especially precision) could be raised with    */
/*    care, but the full decNumber range seems very hard within the   */
/*    32-bit limits.                                                  */
/*                                                                    */
/* 4. The working precisions for the static buffers are twice the     */
/*    obvious size to allow for calls from decNumberPower.            */
/* ------------------------------------------------------------------ */
decNumber * decExpOp(decNumber *res, const decNumber *rhs,
                         decContext *set, uInt *status) {
  uInt ignore=0;                   /* working status  */
  Int h;                           /* adjusted exponent for 0.xxxx  */
  Int p;                           /* working precision  */
  Int residue;                     /* rounding residue  */
  uInt needbytes;                  /* for space calculations  */
  const decNumber *x=rhs;          /* (may point to safe copy later)  */
  decContext aset, tset, dset;     /* working contexts  */
  Int comp;                        /* work  */

  /* the argument is often copied to normalize it, so (unusually) it  */
  /* is treated like other buffers, using DECBUFFER, +1 in case  */
  /* DECBUFFER is 0  */
  decNumber bufr[D2N(DECBUFFER*2+1)];
  decNumber *allocrhs=nullptr;        /* non-nullptr if rhs buffer allocated  */

  /* the working precision will be no more than set->digits+8+1  */
  /* so for on-stack buffers DECBUFFER+9 is used, +1 in case DECBUFFER  */
  /* is 0 (and twice that for the accumulator)  */

  /* buffer for t, term (working precision plus)  */
  decNumber buft[D2N(DECBUFFER*2+9+1)];
  decNumber *allocbuft=nullptr;       /* -> allocated buft, iff allocated  */
  decNumber *t=buft;               /* term  */
  /* buffer for a, accumulator (working precision * 2), at least 9  */
  decNumber bufa[D2N(DECBUFFER*4+18+1)];
  decNumber *allocbufa=nullptr;       /* -> allocated bufa, iff allocated  */
  decNumber *a=bufa;               /* accumulator  */
  /* decNumber for the divisor term; this needs at most 9 digits  */
  /* and so can be fixed size [16 so can use standard context]  */
  decNumber bufd[D2N(16)];
  decNumber *d=bufd;               /* divisor  */
  decNumber numone;                /* constant 1  */

  #if DECCHECK
  Int iterations=0;                /* for later sanity check  */
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  do {                                  /* protect allocated storage  */
    if (SPECIALARG) {                   /* handle infinities and NaNs  */
      if (decNumberIsInfinite(rhs)) {   /* an infinity  */
        if (decNumberIsNegative(rhs))   /* -Infinity -> +0  */
          uprv_decNumberZero(res);
         else uprv_decNumberCopy(res, rhs);  /* +Infinity -> self  */
        }
       else decNaNs(res, rhs, nullptr, set, status); /* a NaN  */
      break;}

    if (ISZERO(rhs)) {                  /* zeros -> exact 1  */
      uprv_decNumberZero(res);               /* make clean 1  */
      *res->lsu=1;                      /* ..  */
      break;}                           /* [no status to set]  */

    /* e**x when 0 < x < 0.66 is < 1+3x/2, hence can fast-path  */
    /* positive and negative tiny cases which will result in inexact  */
    /* 1.  This also allows the later add-accumulate to always be  */
    /* exact (because its length will never be more than twice the  */
    /* working precision).  */
    /* The comparator (tiny) needs just one digit, so use the  */
    /* decNumber d for it (reused as the divisor, etc., below); its  */
    /* exponent is such that if x is positive it will have  */
    /* set->digits-1 zeros between the decimal point and the digit,  */
    /* which is 4, and if x is negative one more zero there as the  */
    /* more precise result will be of the form 0.9999999 rather than  */
    /* 1.0000001.  Hence, tiny will be 0.0000004  if digits=7 and x>0  */
    /* or 0.00000004 if digits=7 and x<0.  If RHS not larger than  */
    /* this then the result will be 1.000000  */
    uprv_decNumberZero(d);                   /* clean  */
    *d->lsu=4;                          /* set 4 ..  */
    d->exponent=-set->digits;           /* * 10**(-d)  */
    if (decNumberIsNegative(rhs)) d->exponent--;  /* negative case  */
    comp=decCompare(d, rhs, 1);         /* signless compare  */
    if (comp==BADINT) {
      *status|=DEC_Insufficient_storage;
      break;}
    if (comp>=0) {                      /* rhs < d  */
      Int shift=set->digits-1;
      uprv_decNumberZero(res);               /* set 1  */
      *res->lsu=1;                      /* ..  */
      res->digits=decShiftToMost(res->lsu, 1, shift);
      res->exponent=-shift;                  /* make 1.0000...  */
      *status|=DEC_Inexact | DEC_Rounded;    /* .. inexactly  */
      break;} /* tiny  */

    /* set up the context to be used for calculating a, as this is  */
    /* used on both paths below  */
    uprv_decContextDefault(&aset, DEC_INIT_DECIMAL64);
    /* accumulator bounds are as requested (could underflow)  */
    aset.emax=set->emax;                /* usual bounds  */
    aset.emin=set->emin;                /* ..  */
    aset.clamp=0;                       /* and no concrete format  */

    /* calculate the adjusted (Hull & Abrham) exponent (where the  */
    /* decimal point is just to the left of the coefficient msd)  */
    h=rhs->exponent+rhs->digits;
    /* if h>8 then 10**h cannot be calculated safely; however, when  */
    /* h=8 then exp(|rhs|) will be at least exp(1E+7) which is at  */
    /* least 6.59E+4342944, so (due to the restriction on Emax/Emin)  */
    /* overflow (or underflow to 0) is guaranteed -- so this case can  */
    /* be handled by simply forcing the appropriate excess  */
    if (h>8) {                          /* overflow/underflow  */
      /* set up here so Power call below will over or underflow to  */
      /* zero; set accumulator to either 2 or 0.02  */
      /* [stack buffer for a is always big enough for this]  */
      uprv_decNumberZero(a);
      *a->lsu=2;                        /* not 1 but < exp(1)  */
      if (decNumberIsNegative(rhs)) a->exponent=-2; /* make 0.02  */
      h=8;                              /* clamp so 10**h computable  */
      p=9;                              /* set a working precision  */
      }
     else {                             /* h<=8  */
      Int maxlever=(rhs->digits>8?1:0);
      /* [could/should increase this for precisions >40 or so, too]  */

      /* if h is 8, cannot normalize to a lower upper limit because  */
      /* the final result will not be computable (see notes above),  */
      /* but leverage can be applied whenever h is less than 8.  */
      /* Apply as much as possible, up to a MAXLEVER digits, which  */
      /* sets the tradeoff against the cost of the later a**(10**h).  */
      /* As h is increased, the working precision below also  */
      /* increases to compensate for the "constant digits at the  */
      /* front" effect.  */
      Int lever=MINI(8-h, maxlever);    /* leverage attainable  */
      Int use=-rhs->digits-lever;       /* exponent to use for RHS  */
      h+=lever;                         /* apply leverage selected  */
      if (h<0) {                        /* clamp  */
        use+=h;                         /* [may end up subnormal]  */
        h=0;
        }
      /* Take a copy of RHS if it needs normalization (true whenever x>=1)  */
      if (rhs->exponent!=use) {
        decNumber *newrhs=bufr;         /* assume will fit on stack  */
        needbytes=sizeof(decNumber)+(D2U(rhs->digits)-1)*sizeof(Unit);
        if (needbytes>sizeof(bufr)) {   /* need malloc space  */
          allocrhs = static_cast<decNumber*>(malloc(needbytes));
          if (allocrhs==nullptr) {         /* hopeless -- abandon  */
            *status|=DEC_Insufficient_storage;
            break;}
          newrhs=allocrhs;              /* use the allocated space  */
          }
        uprv_decNumberCopy(newrhs, rhs);     /* copy to safe space  */
        newrhs->exponent=use;           /* normalize; now <1  */
        x=newrhs;                       /* ready for use  */
        /* decNumberShow(x);  */
        }

      /* Now use the usual power series to evaluate exp(x).  The  */
      /* series starts as 1 + x + x^2/2 ... so prime ready for the  */
      /* third term by setting the term variable t=x, the accumulator  */
      /* a=1, and the divisor d=2.  */

      /* First determine the working precision.  From Hull & Abrham  */
      /* this is set->digits+h+2.  However, if x is 'over-precise' we  */
      /* need to allow for all its digits to potentially participate  */
      /* (consider an x where all the excess digits are 9s) so in  */
      /* this case use x->digits+h+2  */
      p=MAXI(x->digits, set->digits)+h+2;    /* [h<=8]  */

      /* a and t are variable precision, and depend on p, so space  */
      /* must be allocated for them if necessary  */

      /* the accumulator needs to be able to hold 2p digits so that  */
      /* the additions on the second and subsequent iterations are  */
      /* sufficiently exact.  */
      needbytes=sizeof(decNumber)+(D2U(p*2)-1)*sizeof(Unit);
      if (needbytes>sizeof(bufa)) {     /* need malloc space  */
        allocbufa = static_cast<decNumber*>(malloc(needbytes));
        if (allocbufa==nullptr) {          /* hopeless -- abandon  */
          *status|=DEC_Insufficient_storage;
          break;}
        a=allocbufa;                    /* use the allocated space  */
        }
      /* the term needs to be able to hold p digits (which is  */
      /* guaranteed to be larger than x->digits, so the initial copy  */
      /* is safe); it may also be used for the raise-to-power  */
      /* calculation below, which needs an extra two digits  */
      needbytes=sizeof(decNumber)+(D2U(p+2)-1)*sizeof(Unit);
      if (needbytes>sizeof(buft)) {     /* need malloc space  */
        allocbuft = static_cast<decNumber*>(malloc(needbytes));
        if (allocbuft==nullptr) {          /* hopeless -- abandon  */
          *status|=DEC_Insufficient_storage;
          break;}
        t=allocbuft;                    /* use the allocated space  */
        }

      uprv_decNumberCopy(t, x);              /* term=x  */
      uprv_decNumberZero(a); *a->lsu=1;      /* accumulator=1  */
      uprv_decNumberZero(d); *d->lsu=2;      /* divisor=2  */
      uprv_decNumberZero(&numone); *numone.lsu=1; /* constant 1 for increment  */

      /* set up the contexts for calculating a, t, and d  */
      uprv_decContextDefault(&tset, DEC_INIT_DECIMAL64);
      dset=tset;
      /* accumulator bounds are set above, set precision now  */
      aset.digits=p*2;                  /* double  */
      /* term bounds avoid any underflow or overflow  */
      tset.digits=p;
      tset.emin=DEC_MIN_EMIN;           /* [emax is plenty]  */
      /* [dset.digits=16, etc., are sufficient]  */

      /* finally ready to roll  */
      for (;;) {
        #if DECCHECK
        iterations++;
        #endif
        /* only the status from the accumulation is interesting  */
        /* [but it should remain unchanged after first add]  */
        decAddOp(a, a, t, &aset, 0, status);           /* a=a+t  */
        decMultiplyOp(t, t, x, &tset, &ignore);        /* t=t*x  */
        decDivideOp(t, t, d, &tset, DIVIDE, &ignore);  /* t=t/d  */
        /* the iteration ends when the term cannot affect the result,  */
        /* if rounded to p digits, which is when its value is smaller  */
        /* than the accumulator by p+1 digits.  There must also be  */
        /* full precision in a.  */
        if (((a->digits+a->exponent)>=(t->digits+t->exponent+p+1))
            && (a->digits>=p)) break;
        decAddOp(d, d, &numone, &dset, 0, &ignore);    /* d=d+1  */
        } /* iterate  */

      #if DECCHECK
      /* just a sanity check; comment out test to show always  */
      if (iterations>p+3)
        printf("Exp iterations=%ld, status=%08lx, p=%ld, d=%ld\n",
               (LI)iterations, (LI)*status, (LI)p, (LI)x->digits);
      #endif
      } /* h<=8  */

    /* apply postconditioning: a=a**(10**h) -- this is calculated  */
    /* at a slightly higher precision than Hull & Abrham suggest  */
    if (h>0) {
      Int seenbit=0;               /* set once a 1-bit is seen  */
      Int i;                       /* counter  */
      Int n=powers[h];             /* always positive  */
      aset.digits=p+2;             /* sufficient precision  */
      /* avoid the overhead and many extra digits of decNumberPower  */
      /* as all that is needed is the short 'multipliers' loop; here  */
      /* accumulate the answer into t  */
      uprv_decNumberZero(t); *t->lsu=1; /* acc=1  */
      for (i=1;;i++){              /* for each bit [top bit ignored]  */
        /* abandon if have had overflow or terminal underflow  */
        if (*status & (DEC_Overflow|DEC_Underflow)) { /* interesting?  */
          if (*status&DEC_Overflow || ISZERO(t)) break;}
        n=n<<1;                    /* move next bit to testable position  */
        if (n<0) {                 /* top bit is set  */
          seenbit=1;               /* OK, have a significant bit  */
          decMultiplyOp(t, t, a, &aset, status); /* acc=acc*x  */
          }
        if (i==31) break;          /* that was the last bit  */
        if (!seenbit) continue;    /* no need to square 1  */
        decMultiplyOp(t, t, t, &aset, status); /* acc=acc*acc [square]  */
        } /*i*/ /* 32 bits  */
      /* decNumberShow(t);  */
      a=t;                         /* and carry on using t instead of a  */
      }

    /* Copy and round the result to res  */
    residue=1;                          /* indicate dirt to right ..  */
    if (ISZERO(a)) residue=0;           /* .. unless underflowed to 0  */
    aset.digits=set->digits;            /* [use default rounding]  */
    decCopyFit(res, a, &aset, &residue, status); /* copy & shorten  */
    decFinish(res, set, &residue, status);       /* cleanup/set flags  */
    } while(0);                         /* end protected  */

  if (allocrhs !=nullptr) free(allocrhs);  /* drop any storage used  */
  if (allocbufa!=nullptr) free(allocbufa); /* ..  */
  if (allocbuft!=nullptr) free(allocbuft); /* ..  */
  /* [status is handled by caller]  */
  return res;
  } /* decExpOp  */

/* ------------------------------------------------------------------ */
/* Initial-estimate natural logarithm table                           */
/*                                                                    */
/*   LNnn -- 90-entry 16-bit table for values from .10 through .99.   */
/*           The result is a 4-digit encode of the coefficient (c=the */
/*           top 14 bits encoding 0-9999) and a 2-digit encode of the */
/*           exponent (e=the bottom 2 bits encoding 0-3)              */
/*                                                                    */
/*           The resulting value is given by:                         */
/*                                                                    */
/*             v = -c * 10**(-e-3)                                    */
/*                                                                    */
/*           where e and c are extracted from entry k = LNnn[x-10]    */
/*           where x is truncated (NB) into the range 10 through 99,  */
/*           and then c = k>>2 and e = k&3.                           */
/* ------------------------------------------------------------------ */
static const uShort LNnn[90]={9016,  8652,  8316,  8008,  7724,  7456,  7208,
  6972,  6748,  6540,  6340,  6148,  5968,  5792,  5628,  5464,  5312,
  5164,  5020,  4884,  4748,  4620,  4496,  4376,  4256,  4144,  4032,
 39233, 38181, 37157, 36157, 35181, 34229, 33297, 32389, 31501, 30629,
 29777, 28945, 28129, 27329, 26545, 25777, 25021, 24281, 23553, 22837,
 22137, 21445, 20769, 20101, 19445, 18801, 18165, 17541, 16925, 16321,
 15721, 15133, 14553, 13985, 13421, 12865, 12317, 11777, 11241, 10717,
 10197,  9685,  9177,  8677,  8185,  7697,  7213,  6737,  6269,  5801,
  5341,  4889,  4437, 39930, 35534, 31186, 26886, 22630, 18418, 14254,
 10130,  6046, 20055};

/* ------------------------------------------------------------------ */
/* decLnOp -- effect natural logarithm                                */
/*                                                                    */
/*   This computes C = ln(A)                                          */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context; note that rounding mode has no effect        */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Notable cases:                                                     */
/*   A<0 -> Invalid                                                   */
/*   A=0 -> -Infinity (Exact)                                         */
/*   A=+Infinity -> +Infinity (Exact)                                 */
/*   A=1 exactly -> 0 (Exact)                                         */
/*                                                                    */
/* Restrictions (as for Exp):                                         */
/*                                                                    */
/*   digits, emax, and -emin in the context must be less than         */
/*   DEC_MAX_MATH+11 (1000010), and the rhs must be within these      */
/*   bounds or a zero.  This is an internal routine, so these         */
/*   restrictions are contractual and not enforced.                   */
/*                                                                    */
/* A finite result is rounded using DEC_ROUND_HALF_EVEN; it will      */
/* almost always be correctly rounded, but may be up to 1 ulp in      */
/* error in rare cases.                                               */
/* ------------------------------------------------------------------ */
/* The result is calculated using Newton's method, with each          */
/* iteration calculating a' = a + x * exp(-a) - 1.  See, for example, */
/* Epperson 1989.                                                     */
/*                                                                    */
/* The iteration ends when the adjustment x*exp(-a)-1 is tiny enough. */
/* This has to be calculated at the sum of the precision of x and the */
/* working precision.                                                 */
/*                                                                    */
/* Implementation notes:                                              */
/*                                                                    */
/* 1. This is separated out as decLnOp so it can be called from       */
/*    other Mathematical functions (e.g., Log 10) with a wider range  */
/*    than normal.  In particular, it can handle the slightly wider   */
/*    (+9+2) range needed by a power function.                        */
/*                                                                    */
/* 2. The speed of this function is about 10x slower than exp, as     */
/*    it typically needs 4-6 iterations for short numbers, and the    */
/*    extra precision needed adds a squaring effect, twice.           */
/*                                                                    */
/* 3. Fastpaths are included for ln(10) and ln(2), up to length 40,   */
/*    as these are common requests.  ln(10) is used by log10(x).      */
/*                                                                    */
/* 4. An iteration might be saved by widening the LNnn table, and     */
/*    would certainly save at least one if it were made ten times     */
/*    bigger, too (for truncated fractions 0.100 through 0.999).      */
/*    However, for most practical evaluations, at least four or five  */
/*    iterations will be needed -- so this would only speed up by      */
/*    20-25% and that probably does not justify increasing the table  */
/*    size.                                                           */
/*                                                                    */
/* 5. The static buffers are larger than might be expected to allow   */
/*    for calls from decNumberPower.                                  */
/* ------------------------------------------------------------------ */
#if defined(__clang__) || U_GCC_MAJOR_MINOR >= 406
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
decNumber * decLnOp(decNumber *res, const decNumber *rhs,
                    decContext *set, uInt *status) {
  uInt ignore=0;                   /* working status accumulator  */
  uInt needbytes;                  /* for space calculations  */
  Int residue;                     /* rounding residue  */
  Int r;                           /* rhs=f*10**r [see below]  */
  Int p;                           /* working precision  */
  Int pp;                          /* precision for iteration  */
  Int t;                           /* work  */

  /* buffers for a (accumulator, typically precision+2) and b  */
  /* (adjustment calculator, same size)  */
  decNumber bufa[D2N(DECBUFFER+12)];
  decNumber *allocbufa=nullptr;       /* -> allocated bufa, iff allocated  */
  decNumber *a=bufa;               /* accumulator/work  */
  decNumber bufb[D2N(DECBUFFER*2+2)];
  decNumber *allocbufb=nullptr;       /* -> allocated bufa, iff allocated  */
  decNumber *b=bufb;               /* adjustment/work  */

  decNumber  numone;               /* constant 1  */
  decNumber  cmp;                  /* work  */
  decContext aset, bset;           /* working contexts  */

  #if DECCHECK
  Int iterations=0;                /* for later sanity check  */
  if (decCheckOperands(res, DECUNUSED, rhs, set)) return res;
  #endif

  do {                                  /* protect allocated storage  */
    if (SPECIALARG) {                   /* handle infinities and NaNs  */
      if (decNumberIsInfinite(rhs)) {   /* an infinity  */
        if (decNumberIsNegative(rhs))   /* -Infinity -> error  */
          *status|=DEC_Invalid_operation;
         else uprv_decNumberCopy(res, rhs);  /* +Infinity -> self  */
        }
       else decNaNs(res, rhs, nullptr, set, status); /* a NaN  */
      break;}

    if (ISZERO(rhs)) {                  /* +/- zeros -> -Infinity  */
      uprv_decNumberZero(res);               /* make clean  */
      res->bits=DECINF|DECNEG;          /* set - infinity  */
      break;}                           /* [no status to set]  */

    /* Non-zero negatives are bad...  */
    if (decNumberIsNegative(rhs)) {     /* -x -> error  */
      *status|=DEC_Invalid_operation;
      break;}

    /* Here, rhs is positive, finite, and in range  */

    /* lookaside fastpath code for ln(2) and ln(10) at common lengths  */
    if (rhs->exponent==0 && set->digits<=40) {
      #if DECDPUN==1
      if (rhs->lsu[0]==0 && rhs->lsu[1]==1 && rhs->digits==2) { /* ln(10)  */
      #else
      if (rhs->lsu[0]==10 && rhs->digits==2) {                  /* ln(10)  */
      #endif
        aset=*set; aset.round=DEC_ROUND_HALF_EVEN;
        #define LN10 "2.302585092994045684017991454684364207601"
        uprv_decNumberFromString(res, LN10, &aset);
        *status|=(DEC_Inexact | DEC_Rounded); /* is inexact  */
        break;}
      if (rhs->lsu[0]==2 && rhs->digits==1) { /* ln(2)  */
        aset=*set; aset.round=DEC_ROUND_HALF_EVEN;
        #define LN2 "0.6931471805599453094172321214581765680755"
        uprv_decNumberFromString(res, LN2, &aset);
        *status|=(DEC_Inexact | DEC_Rounded);
        break;}
      } /* integer and short  */

    /* Determine the working precision.  This is normally the  */
    /* requested precision + 2, with a minimum of 9.  However, if  */
    /* the rhs is 'over-precise' then allow for all its digits to  */
    /* potentially participate (consider an rhs where all the excess  */
    /* digits are 9s) so in this case use rhs->digits+2.  */
    p=MAXI(rhs->digits, MAXI(set->digits, 7))+2;

    /* Allocate space for the accumulator and the high-precision  */
    /* adjustment calculator, if necessary.  The accumulator must  */
    /* be able to hold p digits, and the adjustment up to  */
    /* rhs->digits+p digits.  They are also made big enough for 16  */
    /* digits so that they can be used for calculating the initial  */
    /* estimate.  */
    needbytes=sizeof(decNumber)+(D2U(MAXI(p,16))-1)*sizeof(Unit);
    if (needbytes>sizeof(bufa)) {     /* need malloc space  */
      allocbufa = static_cast<decNumber*>(malloc(needbytes));
      if (allocbufa==nullptr) {          /* hopeless -- abandon  */
        *status|=DEC_Insufficient_storage;
        break;}
      a=allocbufa;                    /* use the allocated space  */
      }
    pp=p+rhs->digits;
    needbytes=sizeof(decNumber)+(D2U(MAXI(pp,16))-1)*sizeof(Unit);
    if (needbytes>sizeof(bufb)) {     /* need malloc space  */
      allocbufb = static_cast<decNumber*>(malloc(needbytes));
      if (allocbufb==nullptr) {          /* hopeless -- abandon  */
        *status|=DEC_Insufficient_storage;
        break;}
      b=allocbufb;                    /* use the allocated space  */
      }

    /* Prepare an initial estimate in acc. Calculate this by  */
    /* considering the coefficient of x to be a normalized fraction,  */
    /* f, with the decimal point at far left and multiplied by  */
    /* 10**r.  Then, rhs=f*10**r and 0.1<=f<1, and  */
    /*   ln(x) = ln(f) + ln(10)*r  */
    /* Get the initial estimate for ln(f) from a small lookup  */
    /* table (see above) indexed by the first two digits of f,  */
    /* truncated.  */

    uprv_decContextDefault(&aset, DEC_INIT_DECIMAL64); /* 16-digit extended  */
    r=rhs->exponent+rhs->digits;        /* 'normalised' exponent  */
    uprv_decNumberFromInt32(a, r);           /* a=r  */
    uprv_decNumberFromInt32(b, 2302585);     /* b=ln(10) (2.302585)  */
    b->exponent=-6;                     /*  ..  */
    decMultiplyOp(a, a, b, &aset, &ignore);  /* a=a*b  */
    /* now get top two digits of rhs into b by simple truncate and  */
    /* force to integer  */
    residue=0;                          /* (no residue)  */
    aset.digits=2; aset.round=DEC_ROUND_DOWN;
    decCopyFit(b, rhs, &aset, &residue, &ignore); /* copy & shorten  */
    b->exponent=0;                      /* make integer  */
    t=decGetInt(b);                     /* [cannot fail]  */
    if (t<10) t=X10(t);                 /* adjust single-digit b  */
    t=LNnn[t-10];                       /* look up ln(b)  */
    uprv_decNumberFromInt32(b, t>>2);        /* b=ln(b) coefficient  */
    b->exponent=-(t&3)-3;               /* set exponent  */
    b->bits=DECNEG;                     /* ln(0.10)->ln(0.99) always -ve  */
    aset.digits=16; aset.round=DEC_ROUND_HALF_EVEN; /* restore  */
    decAddOp(a, a, b, &aset, 0, &ignore); /* acc=a+b  */
    /* the initial estimate is now in a, with up to 4 digits correct.  */
    /* When rhs is at or near Nmax the estimate will be low, so we  */
    /* will approach it from below, avoiding overflow when calling exp.  */

    uprv_decNumberZero(&numone); *numone.lsu=1;   /* constant 1 for adjustment  */

    /* accumulator bounds are as requested (could underflow, but  */
    /* cannot overflow)  */
    aset.emax=set->emax;
    aset.emin=set->emin;
    aset.clamp=0;                       /* no concrete format  */
    /* set up a context to be used for the multiply and subtract  */
    bset=aset;
    bset.emax=DEC_MAX_MATH*2;           /* use double bounds for the  */
    bset.emin=-DEC_MAX_MATH*2;          /* adjustment calculation  */
                                        /* [see decExpOp call below]  */
    /* for each iteration double the number of digits to calculate,  */
    /* up to a maximum of p  */
    pp=9;                               /* initial precision  */
    /* [initially 9 as then the sequence starts 7+2, 16+2, and  */
    /* 34+2, which is ideal for standard-sized numbers]  */
    aset.digits=pp;                     /* working context  */
    bset.digits=pp+rhs->digits;         /* wider context  */
    for (;;) {                          /* iterate  */
      #if DECCHECK
      iterations++;
      if (iterations>24) break;         /* consider 9 * 2**24  */
      #endif
      /* calculate the adjustment (exp(-a)*x-1) into b.  This is a  */
      /* catastrophic subtraction but it really is the difference  */
      /* from 1 that is of interest.  */
      /* Use the internal entry point to Exp as it allows the double  */
      /* range for calculating exp(-a) when a is the tiniest subnormal.  */
      a->bits^=DECNEG;                  /* make -a  */
      decExpOp(b, a, &bset, &ignore);   /* b=exp(-a)  */
      a->bits^=DECNEG;                  /* restore sign of a  */
      /* now multiply by rhs and subtract 1, at the wider precision  */
      decMultiplyOp(b, b, rhs, &bset, &ignore);        /* b=b*rhs  */
      decAddOp(b, b, &numone, &bset, DECNEG, &ignore); /* b=b-1  */

      /* the iteration ends when the adjustment cannot affect the  */
      /* result by >=0.5 ulp (at the requested digits), which  */
      /* is when its value is smaller than the accumulator by  */
      /* set->digits+1 digits (or it is zero) -- this is a looser  */
      /* requirement than for Exp because all that happens to the  */
      /* accumulator after this is the final rounding (but note that  */
      /* there must also be full precision in a, or a=0).  */

      if (decNumberIsZero(b) ||
          (a->digits+a->exponent)>=(b->digits+b->exponent+set->digits+1)) {
        if (a->digits==p) break;
        if (decNumberIsZero(a)) {
          decCompareOp(&cmp, rhs, &numone, &aset, COMPARE, &ignore); /* rhs=1 ?  */
          if (cmp.lsu[0]==0) a->exponent=0;            /* yes, exact 0  */
           else *status|=(DEC_Inexact | DEC_Rounded);  /* no, inexact  */
          break;
          }
        /* force padding if adjustment has gone to 0 before full length  */
        if (decNumberIsZero(b)) b->exponent=a->exponent-p;
        }

      /* not done yet ...  */
      decAddOp(a, a, b, &aset, 0, &ignore);  /* a=a+b for next estimate  */
      if (pp==p) continue;                   /* precision is at maximum  */
      /* lengthen the next calculation  */
      pp=pp*2;                               /* double precision  */
      if (pp>p) pp=p;                        /* clamp to maximum  */
      aset.digits=pp;                        /* working context  */
      bset.digits=pp+rhs->digits;            /* wider context  */
      } /* Newton's iteration  */

    #if DECCHECK
    /* just a sanity check; remove the test to show always  */
    if (iterations>24)
      printf("Ln iterations=%ld, status=%08lx, p=%ld, d=%ld\n",
            (LI)iterations, (LI)*status, (LI)p, (LI)rhs->digits);
    #endif

    /* Copy and round the result to res  */
    residue=1;                          /* indicate dirt to right  */
    if (ISZERO(a)) residue=0;           /* .. unless underflowed to 0  */
    aset.digits=set->digits;            /* [use default rounding]  */
    decCopyFit(res, a, &aset, &residue, status); /* copy & shorten  */
    decFinish(res, set, &residue, status);       /* cleanup/set flags  */
    } while(0);                         /* end protected  */

  if (allocbufa!=nullptr) free(allocbufa); /* drop any storage used  */
  if (allocbufb!=nullptr) free(allocbufb); /* ..  */
  /* [status is handled by caller]  */
  return res;
  } /* decLnOp  */
#if defined(__clang__) || U_GCC_MAJOR_MINOR >= 406
#pragma GCC diagnostic pop
#endif

/* ------------------------------------------------------------------ */
/* decQuantizeOp  -- force exponent to requested value                */
/*                                                                    */
/*   This computes C = op(A, B), where op adjusts the coefficient     */
/*   of C (by rounding or shifting) such that the exponent (-scale)   */
/*   of C has the value B or matches the exponent of B.               */
/*   The numerical value of C will equal A, except for the effects of */
/*   any rounding that occurred.                                      */
/*                                                                    */
/*   res is C, the result.  C may be A or B                           */
/*   lhs is A, the number to adjust                                   */
/*   rhs is B, the requested exponent                                 */
/*   set is the context                                               */
/*   quant is 1 for quantize or 0 for rescale                         */
/*   status is the status accumulator (this can be called without     */
/*          risk of control loss)                                     */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Unless there is an error or the result is infinite, the exponent   */
/* after the operation is guaranteed to be that requested.            */
/* ------------------------------------------------------------------ */
static decNumber * decQuantizeOp(decNumber *res, const decNumber *lhs,
                                 const decNumber *rhs, decContext *set,
                                 Flag quant, uInt *status) {
  #if DECSUBSET
  decNumber *alloclhs=nullptr;        /* non-nullptr if rounded lhs allocated  */
  decNumber *allocrhs=nullptr;        /* .., rhs  */
  #endif
  const decNumber *inrhs=rhs;      /* save original rhs  */
  Int   reqdigits=set->digits;     /* requested DIGITS  */
  Int   reqexp;                    /* requested exponent [-scale]  */
  Int   residue=0;                 /* rounding residue  */
  Int   etiny=set->emin-(reqdigits-1);

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  do {                             /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operands and set lostDigits status, as needed  */
      if (lhs->digits>reqdigits) {
        alloclhs=decRoundOperand(lhs, set, status);
        if (alloclhs==nullptr) break;
        lhs=alloclhs;
        }
      if (rhs->digits>reqdigits) { /* [this only checks lostDigits]  */
        allocrhs=decRoundOperand(rhs, set, status);
        if (allocrhs==nullptr) break;
        rhs=allocrhs;
        }
      }
    #endif
    /* [following code does not require input rounding]  */

    /* Handle special values  */
    if (SPECIALARGS) {
      /* NaNs get usual processing  */
      if (SPECIALARGS & (DECSNAN | DECNAN))
        decNaNs(res, lhs, rhs, set, status);
      /* one infinity but not both is bad  */
      else if ((lhs->bits ^ rhs->bits) & DECINF)
        *status|=DEC_Invalid_operation;
      /* both infinity: return lhs  */
      else uprv_decNumberCopy(res, lhs);          /* [nop if in place]  */
      break;
      }

    /* set requested exponent  */
    if (quant) reqexp=inrhs->exponent;  /* quantize -- match exponents  */
     else {                             /* rescale -- use value of rhs  */
      /* Original rhs must be an integer that fits and is in range,  */
      /* which could be from -1999999997 to +999999999, thanks to  */
      /* subnormals  */
      reqexp=decGetInt(inrhs);               /* [cannot fail]  */
      }

    #if DECSUBSET
    if (!set->extended) etiny=set->emin;     /* no subnormals  */
    #endif

    if (reqexp==BADINT                       /* bad (rescale only) or ..  */
     || reqexp==BIGODD || reqexp==BIGEVEN    /* very big (ditto) or ..  */
     || (reqexp<etiny)                       /* < lowest  */
     || (reqexp>set->emax)) {                /* > emax  */
      *status|=DEC_Invalid_operation;
      break;}

    /* the RHS has been processed, so it can be overwritten now if necessary  */
    if (ISZERO(lhs)) {                       /* zero coefficient unchanged  */
      uprv_decNumberCopy(res, lhs);               /* [nop if in place]  */
      res->exponent=reqexp;                  /* .. just set exponent  */
      #if DECSUBSET
      if (!set->extended) res->bits=0;       /* subset specification; no -0  */
      #endif
      }
     else {                                  /* non-zero lhs  */
      Int adjust=reqexp-lhs->exponent;       /* digit adjustment needed  */
      /* if adjusted coefficient will definitely not fit, give up now  */
      if ((lhs->digits-adjust)>reqdigits) {
        *status|=DEC_Invalid_operation;
        break;
        }

      if (adjust>0) {                        /* increasing exponent  */
        /* this will decrease the length of the coefficient by adjust  */
        /* digits, and must round as it does so  */
        decContext workset;                  /* work  */
        workset=*set;                        /* clone rounding, etc.  */
        workset.digits=lhs->digits-adjust;   /* set requested length  */
        /* [note that the latter can be <1, here]  */
        decCopyFit(res, lhs, &workset, &residue, status); /* fit to result  */
        decApplyRound(res, &workset, residue, status);    /* .. and round  */
        residue=0;                                        /* [used]  */
        /* If just rounded a 999s case, exponent will be off by one;  */
        /* adjust back (after checking space), if so.  */
        if (res->exponent>reqexp) {
          /* re-check needed, e.g., for quantize(0.9999, 0.001) under  */
          /* set->digits==3  */
          if (res->digits==reqdigits) {      /* cannot shift by 1  */
            *status&=~(DEC_Inexact | DEC_Rounded); /* [clean these]  */
            *status|=DEC_Invalid_operation;
            break;
            }
          res->digits=decShiftToMost(res->lsu, res->digits, 1); /* shift  */
          res->exponent--;                   /* (re)adjust the exponent.  */
          }
        #if DECSUBSET
        if (ISZERO(res) && !set->extended) res->bits=0; /* subset; no -0  */
        #endif
        } /* increase  */
       else /* adjust<=0 */ {                /* decreasing or = exponent  */
        /* this will increase the length of the coefficient by -adjust  */
        /* digits, by adding zero or more trailing zeros; this is  */
        /* already checked for fit, above  */
        uprv_decNumberCopy(res, lhs);             /* [it will fit]  */
        /* if padding needed (adjust<0), add it now...  */
        if (adjust<0) {
          res->digits=decShiftToMost(res->lsu, res->digits, -adjust);
          res->exponent+=adjust;             /* adjust the exponent  */
          }
        } /* decrease  */
      } /* non-zero  */

    /* Check for overflow [do not use Finalize in this case, as an  */
    /* overflow here is a "don't fit" situation]  */
    if (res->exponent>set->emax-res->digits+1) {  /* too big  */
      *status|=DEC_Invalid_operation;
      break;
      }
     else {
      decFinalize(res, set, &residue, status);    /* set subnormal flags  */
      *status&=~DEC_Underflow;          /* suppress Underflow [as per 754]  */
      }
    } while(0);                         /* end protected  */

  #if DECSUBSET
  if (allocrhs!=nullptr) free(allocrhs);   /* drop any storage used  */
  if (alloclhs!=nullptr) free(alloclhs);   /* ..  */
  #endif
  return res;
  } /* decQuantizeOp  */

/* ------------------------------------------------------------------ */
/* decCompareOp -- compare, min, or max two Numbers                   */
/*                                                                    */
/*   This computes C = A ? B and carries out one of four operations:  */
/*     COMPARE    -- returns the signum (as a number) giving the      */
/*                   result of a comparison unless one or both        */
/*                   operands is a NaN (in which case a NaN results)  */
/*     COMPSIG    -- as COMPARE except that a quiet NaN raises        */
/*                   Invalid operation.                               */
/*     COMPMAX    -- returns the larger of the operands, using the    */
/*                   754 maxnum operation                             */
/*     COMPMAXMAG -- ditto, comparing absolute values                 */
/*     COMPMIN    -- the 754 minnum operation                         */
/*     COMPMINMAG -- ditto, comparing absolute values                 */
/*     COMTOTAL   -- returns the signum (as a number) giving the      */
/*                   result of a comparison using 754 total ordering  */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*   op  is the operation flag                                        */
/*   status is the usual accumulator                                  */
/*                                                                    */
/* C must have space for one digit for COMPARE or set->digits for     */
/* COMPMAX, COMPMIN, COMPMAXMAG, or COMPMINMAG.                       */
/* ------------------------------------------------------------------ */
/* The emphasis here is on speed for common cases, and avoiding       */
/* coefficient comparison if possible.                                */
/* ------------------------------------------------------------------ */
static decNumber * decCompareOp(decNumber *res, const decNumber *lhs,
                         const decNumber *rhs, decContext *set,
                         Flag op, uInt *status) {
  #if DECSUBSET
  decNumber *alloclhs=nullptr;        /* non-nullptr if rounded lhs allocated  */
  decNumber *allocrhs=nullptr;        /* .., rhs  */
  #endif
  Int   result=0;                  /* default result value  */
  uByte merged;                    /* work  */

  #if DECCHECK
  if (decCheckOperands(res, lhs, rhs, set)) return res;
  #endif

  do {                             /* protect allocated storage  */
    #if DECSUBSET
    if (!set->extended) {
      /* reduce operands and set lostDigits status, as needed  */
      if (lhs->digits>set->digits) {
        alloclhs=decRoundOperand(lhs, set, status);
        if (alloclhs==nullptr) {result=BADINT; break;}
        lhs=alloclhs;
        }
      if (rhs->digits>set->digits) {
        allocrhs=decRoundOperand(rhs, set, status);
        if (allocrhs==nullptr) {result=BADINT; break;}
        rhs=allocrhs;
        }
      }
    #endif
    /* [following code does not require input rounding]  */

    /* If total ordering then handle differing signs 'up front'  */
    if (op==COMPTOTAL) {                /* total ordering  */
      if (decNumberIsNegative(lhs) && !decNumberIsNegative(rhs)) {
        result=-1;
        break;
        }
      if (!decNumberIsNegative(lhs) && decNumberIsNegative(rhs)) {
        result=+1;
        break;
        }
      }

    /* handle NaNs specially; let infinities drop through  */
    /* This assumes sNaN (even just one) leads to NaN.  */
    merged=(lhs->bits | rhs->bits) & (DECSNAN | DECNAN);
    if (merged) {                       /* a NaN bit set  */
      if (op==COMPARE);                 /* result will be NaN  */
       else if (op==COMPSIG)            /* treat qNaN as sNaN  */
        *status|=DEC_Invalid_operation | DEC_sNaN;
       else if (op==COMPTOTAL) {        /* total ordering, always finite  */
        /* signs are known to be the same; compute the ordering here  */
        /* as if the signs are both positive, then invert for negatives  */
        if (!decNumberIsNaN(lhs)) result=-1;
         else if (!decNumberIsNaN(rhs)) result=+1;
         /* here if both NaNs  */
         else if (decNumberIsSNaN(lhs) && decNumberIsQNaN(rhs)) result=-1;
         else if (decNumberIsQNaN(lhs) && decNumberIsSNaN(rhs)) result=+1;
         else { /* both NaN or both sNaN  */
          /* now it just depends on the payload  */
          result=decUnitCompare(lhs->lsu, D2U(lhs->digits),
                                rhs->lsu, D2U(rhs->digits), 0);
          /* [Error not possible, as these are 'aligned']  */
          } /* both same NaNs  */
        if (decNumberIsNegative(lhs)) result=-result;
        break;
        } /* total order  */

       else if (merged & DECSNAN);           /* sNaN -> qNaN  */
       else { /* here if MIN or MAX and one or two quiet NaNs  */
        /* min or max -- 754 rules ignore single NaN  */
        if (!decNumberIsNaN(lhs) || !decNumberIsNaN(rhs)) {
          /* just one NaN; force choice to be the non-NaN operand  */
          op=COMPMAX;
          if (lhs->bits & DECNAN) result=-1; /* pick rhs  */
                             else result=+1; /* pick lhs  */
          break;
          }
        } /* max or min  */
      op=COMPNAN;                            /* use special path  */
      decNaNs(res, lhs, rhs, set, status);   /* propagate NaN  */
      break;
      }
    /* have numbers  */
    if (op==COMPMAXMAG || op==COMPMINMAG) result=decCompare(lhs, rhs, 1);
     else result=decCompare(lhs, rhs, 0);    /* sign matters  */
    } while(0);                              /* end protected  */

  if (result==BADINT) *status|=DEC_Insufficient_storage; /* rare  */
   else {
    if (op==COMPARE || op==COMPSIG ||op==COMPTOTAL) { /* returning signum  */
      if (op==COMPTOTAL && result==0) {
        /* operands are numerically equal or same NaN (and same sign,  */
        /* tested first); if identical, leave result 0  */
        if (lhs->exponent!=rhs->exponent) {
          if (lhs->exponent<rhs->exponent) result=-1;
           else result=+1;
          if (decNumberIsNegative(lhs)) result=-result;
          } /* lexp!=rexp  */
        } /* total-order by exponent  */
      uprv_decNumberZero(res);               /* [always a valid result]  */
      if (result!=0) {                  /* must be -1 or +1  */
        *res->lsu=1;
        if (result<0) res->bits=DECNEG;
        }
      }
     else if (op==COMPNAN);             /* special, drop through  */
     else {                             /* MAX or MIN, non-NaN result  */
      Int residue=0;                    /* rounding accumulator  */
      /* choose the operand for the result  */
      const decNumber *choice;
      if (result==0) { /* operands are numerically equal  */
        /* choose according to sign then exponent (see 754)  */
        uByte slhs=(lhs->bits & DECNEG);
        uByte srhs=(rhs->bits & DECNEG);
        #if DECSUBSET
        if (!set->extended) {           /* subset: force left-hand  */
          op=COMPMAX;
          result=+1;
          }
        else
        #endif
        if (slhs!=srhs) {          /* signs differ  */
          if (slhs) result=-1;     /* rhs is max  */
               else result=+1;     /* lhs is max  */
          }
         else if (slhs && srhs) {  /* both negative  */
          if (lhs->exponent<rhs->exponent) result=+1;
                                      else result=-1;
          /* [if equal, use lhs, technically identical]  */
          }
         else {                    /* both positive  */
          if (lhs->exponent>rhs->exponent) result=+1;
                                      else result=-1;
          /* [ditto]  */
          }
        } /* numerically equal  */
      /* here result will be non-0; reverse if looking for MIN  */
      if (op==COMPMIN || op==COMPMINMAG) result=-result;
      choice=(result>0 ? lhs : rhs);    /* choose  */
      /* copy chosen to result, rounding if need be  */
      decCopyFit(res, choice, set, &residue, status);
      decFinish(res, set, &residue, status);
      }
    }
  #if DECSUBSET
  if (allocrhs!=nullptr) free(allocrhs);   /* free any storage used  */
  if (alloclhs!=nullptr) free(alloclhs);   /* ..  */
  #endif
  return res;
  } /* decCompareOp  */

/* ------------------------------------------------------------------ */
/* decCompare -- compare two decNumbers by numerical value            */
/*                                                                    */
/*  This routine compares A ? B without altering them.                */
/*                                                                    */
/*  Arg1 is A, a decNumber which is not a NaN                         */
/*  Arg2 is B, a decNumber which is not a NaN                         */
/*  Arg3 is 1 for a sign-independent compare, 0 otherwise             */
/*                                                                    */
/*  returns -1, 0, or 1 for A<B, A==B, or A>B, or BADINT if failure   */
/*  (the only possible failure is an allocation error)                */
/* ------------------------------------------------------------------ */
static Int decCompare(const decNumber *lhs, const decNumber *rhs,
                      Flag abs_c) {
  Int   result;                    /* result value  */
  Int   sigr;                      /* rhs signum  */
  Int   compare;                   /* work  */

  result=1;                                  /* assume signum(lhs)  */
  if (ISZERO(lhs)) result=0;
  if (abs_c) {
    if (ISZERO(rhs)) return result;          /* LHS wins or both 0  */
    /* RHS is non-zero  */
    if (result==0) return -1;                /* LHS is 0; RHS wins  */
    /* [here, both non-zero, result=1]  */
    }
   else {                                    /* signs matter  */
    if (result && decNumberIsNegative(lhs)) result=-1;
    sigr=1;                                  /* compute signum(rhs)  */
    if (ISZERO(rhs)) sigr=0;
     else if (decNumberIsNegative(rhs)) sigr=-1;
    if (result > sigr) return +1;            /* L > R, return 1  */
    if (result < sigr) return -1;            /* L < R, return -1  */
    if (result==0) return 0;                   /* both 0  */
    }

  /* signums are the same; both are non-zero  */
  if ((lhs->bits | rhs->bits) & DECINF) {    /* one or more infinities  */
    if (decNumberIsInfinite(rhs)) {
      if (decNumberIsInfinite(lhs)) result=0;/* both infinite  */
       else result=-result;                  /* only rhs infinite  */
      }
    return result;
    }
  /* must compare the coefficients, allowing for exponents  */
  if (lhs->exponent>rhs->exponent) {         /* LHS exponent larger  */
    /* swap sides, and sign  */
    const decNumber *temp=lhs;
    lhs=rhs;
    rhs=temp;
    result=-result;
    }
  compare=decUnitCompare(lhs->lsu, D2U(lhs->digits),
                         rhs->lsu, D2U(rhs->digits),
                         rhs->exponent-lhs->exponent);
  if (compare!=BADINT) compare*=result;      /* comparison succeeded  */
  return compare;
  } /* decCompare  */

/* ------------------------------------------------------------------ */
/* decUnitCompare -- compare two >=0 integers in Unit arrays          */
/*                                                                    */
/*  This routine compares A ? B*10**E where A and B are unit arrays   */
/*  A is a plain integer                                              */
/*  B has an exponent of E (which must be non-negative)               */
/*                                                                    */
/*  Arg1 is A first Unit (lsu)                                        */
/*  Arg2 is A length in Units                                         */
/*  Arg3 is B first Unit (lsu)                                        */
/*  Arg4 is B length in Units                                         */
/*  Arg5 is E (0 if the units are aligned)                            */
/*                                                                    */
/*  returns -1, 0, or 1 for A<B, A==B, or A>B, or BADINT if failure   */
/*  (the only possible failure is an allocation error, which can      */
/*  only occur if E!=0)                                               */
/* ------------------------------------------------------------------ */
static Int decUnitCompare(const Unit *a, Int alength,
                          const Unit *b, Int blength, Int exp) {
  Unit  *acc;                      /* accumulator for result  */
  Unit  accbuff[SD2U(DECBUFFER*2+1)]; /* local buffer  */
  Unit  *allocacc=nullptr;            /* -> allocated acc buffer, iff allocated  */
  Int   accunits, need;            /* units in use or needed for acc  */
  const Unit *l, *r, *u;           /* work  */
  Int   expunits, exprem, result;  /* ..  */

  if (exp==0) {                    /* aligned; fastpath  */
    if (alength>blength) return 1;
    if (alength<blength) return -1;
    /* same number of units in both -- need unit-by-unit compare  */
    l=a+alength-1;
    r=b+alength-1;
    for (;l>=a; l--, r--) {
      if (*l>*r) return 1;
      if (*l<*r) return -1;
      }
    return 0;                      /* all units match  */
    } /* aligned  */

  /* Unaligned.  If one is >1 unit longer than the other, padded  */
  /* approximately, then can return easily  */
  if (alength > blength + static_cast<Int>(D2U(exp))) return 1;
  if (alength + 1 < blength + static_cast<Int>(D2U(exp))) return -1;

  /* Need to do a real subtract.  For this, a result buffer is needed  */
  /* even though only the sign is of interest.  Its length needs  */
  /* to be the larger of alength and padded blength, +2  */
  need=blength+D2U(exp);                /* maximum real length of B  */
  if (need<alength) need=alength;
  need+=2;
  acc=accbuff;                          /* assume use local buffer  */
  if (need*sizeof(Unit)>sizeof(accbuff)) {
    allocacc = static_cast<Unit*>(malloc(need * sizeof(Unit)));
    if (allocacc==nullptr) return BADINT;  /* hopeless -- abandon  */
    acc=allocacc;
    }
  /* Calculate units and remainder from exponent.  */
  expunits=exp/DECDPUN;
  exprem=exp%DECDPUN;
  /* subtract [A+B*(-m)]  */
  accunits=decUnitAddSub(a, alength, b, blength, expunits, acc,
                         -static_cast<Int>(powers[exprem]));
  /* [UnitAddSub result may have leading zeros, even on zero]  */
  if (accunits<0) result=-1;            /* negative result  */
   else {                               /* non-negative result  */
    /* check units of the result before freeing any storage  */
    for (u=acc; u<acc+accunits-1 && *u==0;) u++;
    result=(*u==0 ? 0 : +1);
    }
  /* clean up and return the result  */
  if (allocacc!=nullptr) free(allocacc);   /* drop any storage used  */
  return result;
  } /* decUnitCompare  */

/* ------------------------------------------------------------------ */
/* decUnitAddSub -- add or subtract two >=0 integers in Unit arrays   */
/*                                                                    */
/*  This routine performs the calculation:                            */
/*                                                                    */
/*  C=A+(B*M)                                                         */
/*                                                                    */
/*  Where M is in the range -DECDPUNMAX through +DECDPUNMAX.          */
/*                                                                    */
/*  A may be shorter or longer than B.                                */
/*                                                                    */
/*  Leading zeros are not removed after a calculation.  The result is */
/*  either the same length as the longer of A and B (adding any       */
/*  shift), or one Unit longer than that (if a Unit carry occurred).  */
/*                                                                    */
/*  A and B content are not altered unless C is also A or B.          */
/*  C may be the same array as A or B, but only if no zero padding is */
/*  requested (that is, C may be B only if bshift==0).                */
/*  C is filled from the lsu; only those units necessary to complete  */
/*  the calculation are referenced.                                   */
/*                                                                    */
/*  Arg1 is A first Unit (lsu)                                        */
/*  Arg2 is A length in Units                                         */
/*  Arg3 is B first Unit (lsu)                                        */
/*  Arg4 is B length in Units                                         */
/*  Arg5 is B shift in Units  (>=0; pads with 0 units if positive)    */
/*  Arg6 is C first Unit (lsu)                                        */
/*  Arg7 is M, the multiplier                                         */
/*                                                                    */
/*  returns the count of Units written to C, which will be non-zero   */
/*  and negated if the result is negative.  That is, the sign of the  */
/*  returned Int is the sign of the result (positive for zero) and    */
/*  the absolute value of the Int is the count of Units.              */
/*                                                                    */
/*  It is the caller's responsibility to make sure that C size is     */
/*  safe, allowing space if necessary for a one-Unit carry.           */
/*                                                                    */
/*  This routine is severely performance-critical; *any* change here  */
/*  must be measured (timed) to assure no performance degradation.    */
/*  In particular, trickery here tends to be counter-productive, as   */
/*  increased complexity of code hurts register optimizations on      */
/*  register-poor architectures.  Avoiding divisions is nearly        */
/*  always a Good Idea, however.                                      */
/*                                                                    */
/* Special thanks to Rick McGuire (IBM Cambridge, MA) and Dave Clark  */
/* (IBM Warwick, UK) for some of the ideas used in this routine.      */
/* ------------------------------------------------------------------ */
static Int decUnitAddSub(const Unit *a, Int alength,
                         const Unit *b, Int blength, Int bshift,
                         Unit *c, Int m) {
  const Unit *alsu=a;              /* A lsu [need to remember it]  */
  Unit *clsu=c;                    /* C ditto  */
  Unit *minC;                      /* low water mark for C  */
  Unit *maxC;                      /* high water mark for C  */
  eInt carry=0;                    /* carry integer (could be Long)  */
  Int  add;                        /* work  */
  #if DECDPUN<=4                   /* myriadal, millenary, etc.  */
  Int  est;                        /* estimated quotient  */
  #endif

  #if DECTRACE
  if (alength<1 || blength<1)
    printf("decUnitAddSub: alen blen m %ld %ld [%ld]\n", alength, blength, m);
  #endif

  maxC=c+alength;                  /* A is usually the longer  */
  minC=c+blength;                  /* .. and B the shorter  */
  if (bshift!=0) {                 /* B is shifted; low As copy across  */
    minC+=bshift;
    /* if in place [common], skip copy unless there's a gap [rare]  */
    if (a==c && bshift<=alength) {
      c+=bshift;
      a+=bshift;
      }
     else for (; c<clsu+bshift; a++, c++) {  /* copy needed  */
      if (a<alsu+alength) *c=*a;
       else *c=0;
      }
    }
  if (minC>maxC) { /* swap  */
    Unit *hold=minC;
    minC=maxC;
    maxC=hold;
    }

  /* For speed, do the addition as two loops; the first where both A  */
  /* and B contribute, and the second (if necessary) where only one or  */
  /* other of the numbers contribute.  */
  /* Carry handling is the same (i.e., duplicated) in each case.  */
  for (; c<minC; c++) {
    carry+=*a;
    a++;
    carry += (static_cast<eInt>(*b)) * m; /* [special-casing m=1/-1  */
    b++;                                /* here is not a win]  */
    /* here carry is new Unit of digits; it could be +ve or -ve  */
    if (static_cast<ueInt>(carry) <= DECDPUNMAX) { /* fastpath 0-DECDPUNMAX  */
      *c = static_cast<Unit>(carry);
      carry=0;
      continue;
      }
    #if DECDPUN==4                           /* use divide-by-multiply  */
      if (carry>=0) {
        est=(((ueInt)carry>>11)*53687)>>18;
        *c=(Unit)(carry-est*(DECDPUNMAX+1)); /* remainder  */
        carry=est;                           /* likely quotient [89%]  */
        if (*c<DECDPUNMAX+1) continue;       /* estimate was correct  */
        carry++;
        *c-=DECDPUNMAX+1;
        continue;
        }
      /* negative case  */
      carry=carry+(eInt)(DECDPUNMAX+1)*(DECDPUNMAX+1); /* make positive  */
      est=(((ueInt)carry>>11)*53687)>>18;
      *c=(Unit)(carry-est*(DECDPUNMAX+1));
      carry=est-(DECDPUNMAX+1);              /* correctly negative  */
      if (*c<DECDPUNMAX+1) continue;         /* was OK  */
      carry++;
      *c-=DECDPUNMAX+1;
    #elif DECDPUN==3
      if (carry>=0) {
        est=(((ueInt)carry>>3)*16777)>>21;
        *c=(Unit)(carry-est*(DECDPUNMAX+1)); /* remainder  */
        carry=est;                           /* likely quotient [99%]  */
        if (*c<DECDPUNMAX+1) continue;       /* estimate was correct  */
        carry++;
        *c-=DECDPUNMAX+1;
        continue;
        }
      /* negative case  */
      carry=carry+(eInt)(DECDPUNMAX+1)*(DECDPUNMAX+1); /* make positive  */
      est=(((ueInt)carry>>3)*16777)>>21;
      *c=(Unit)(carry-est*(DECDPUNMAX+1));
      carry=est-(DECDPUNMAX+1);              /* correctly negative  */
      if (*c<DECDPUNMAX+1) continue;         /* was OK  */
      carry++;
      *c-=DECDPUNMAX+1;
    #elif DECDPUN<=2
      /* Can use QUOT10 as carry <= 4 digits  */
      if (carry>=0) {
        est=QUOT10(carry, DECDPUN);
        *c = static_cast<Unit>(carry - est * (DECDPUNMAX + 1)); /* remainder  */
        carry=est;                           /* quotient  */
        continue;
        }
      /* negative case  */
      carry = carry + static_cast<eInt>(DECDPUNMAX + 1) * (DECDPUNMAX + 1); /* make positive  */
      est=QUOT10(carry, DECDPUN);
      *c = static_cast<Unit>(carry - est * (DECDPUNMAX + 1));
      carry=est-(DECDPUNMAX+1);              /* correctly negative  */
    #else
      /* remainder operator is undefined if negative, so must test  */
      if ((ueInt)carry<(DECDPUNMAX+1)*2) {   /* fastpath carry +1  */
        *c=(Unit)(carry-(DECDPUNMAX+1));     /* [helps additions]  */
        carry=1;
        continue;
        }
      if (carry>=0) {
        *c=(Unit)(carry%(DECDPUNMAX+1));
        carry=carry/(DECDPUNMAX+1);
        continue;
        }
      /* negative case  */
      carry=carry+(eInt)(DECDPUNMAX+1)*(DECDPUNMAX+1); /* make positive  */
      *c=(Unit)(carry%(DECDPUNMAX+1));
      carry=carry/(DECDPUNMAX+1)-(DECDPUNMAX+1);
    #endif
    } /* c  */

  /* now may have one or other to complete  */
  /* [pretest to avoid loop setup/shutdown]  */
  if (c<maxC) for (; c<maxC; c++) {
    if (a<alsu+alength) {               /* still in A  */
      carry+=*a;
      a++;
      }
     else {                             /* inside B  */
      carry += static_cast<eInt>(*b) * m;
      b++;
      }
    /* here carry is new Unit of digits; it could be +ve or -ve and  */
    /* magnitude up to DECDPUNMAX squared  */
    if (static_cast<ueInt>(carry) <= DECDPUNMAX) { /* fastpath 0-DECDPUNMAX  */
      *c = static_cast<Unit>(carry);
      carry=0;
      continue;
      }
    /* result for this unit is negative or >DECDPUNMAX  */
    #if DECDPUN==4                           /* use divide-by-multiply  */
      if (carry>=0) {
        est=(((ueInt)carry>>11)*53687)>>18;
        *c=(Unit)(carry-est*(DECDPUNMAX+1)); /* remainder  */
        carry=est;                           /* likely quotient [79.7%]  */
        if (*c<DECDPUNMAX+1) continue;       /* estimate was correct  */
        carry++;
        *c-=DECDPUNMAX+1;
        continue;
        }
      /* negative case  */
      carry=carry+(eInt)(DECDPUNMAX+1)*(DECDPUNMAX+1); /* make positive  */
      est=(((ueInt)carry>>11)*53687)>>18;
      *c=(Unit)(carry-est*(DECDPUNMAX+1));
      carry=est-(DECDPUNMAX+1);              /* correctly negative  */
      if (*c<DECDPUNMAX+1) continue;         /* was OK  */
      carry++;
      *c-=DECDPUNMAX+1;
    #elif DECDPUN==3
      if (carry>=0) {
        est=(((ueInt)carry>>3)*16777)>>21;
        *c=(Unit)(carry-est*(DECDPUNMAX+1)); /* remainder  */
        carry=est;                           /* likely quotient [99%]  */
        if (*c<DECDPUNMAX+1) continue;       /* estimate was correct  */
        carry++;
        *c-=DECDPUNMAX+1;
        continue;
        }
      /* negative case  */
      carry=carry+(eInt)(DECDPUNMAX+1)*(DECDPUNMAX+1); /* make positive  */
      est=(((ueInt)carry>>3)*16777)>>21;
      *c=(Unit)(carry-est*(DECDPUNMAX+1));
      carry=est-(DECDPUNMAX+1);              /* correctly negative  */
      if (*c<DECDPUNMAX+1) continue;         /* was OK  */
      carry++;
      *c-=DECDPUNMAX+1;
    #elif DECDPUN<=2
      if (carry>=0) {
        est=QUOT10(carry, DECDPUN);
        *c = static_cast<Unit>(carry - est * (DECDPUNMAX + 1)); /* remainder  */
        carry=est;                           /* quotient  */
        continue;
        }
      /* negative case  */
      carry = carry + static_cast<eInt>(DECDPUNMAX + 1) * (DECDPUNMAX + 1); /* make positive  */
      est=QUOT10(carry, DECDPUN);
      *c = static_cast<Unit>(carry - est * (DECDPUNMAX + 1));
      carry=est-(DECDPUNMAX+1);              /* correctly negative  */
    #else
      if ((ueInt)carry<(DECDPUNMAX+1)*2){    /* fastpath carry 1  */
        *c=(Unit)(carry-(DECDPUNMAX+1));
        carry=1;
        continue;
        }
      /* remainder operator is undefined if negative, so must test  */
      if (carry>=0) {
        *c=(Unit)(carry%(DECDPUNMAX+1));
        carry=carry/(DECDPUNMAX+1);
        continue;
        }
      /* negative case  */
      carry=carry+(eInt)(DECDPUNMAX+1)*(DECDPUNMAX+1); /* make positive  */
      *c=(Unit)(carry%(DECDPUNMAX+1));
      carry=carry/(DECDPUNMAX+1)-(DECDPUNMAX+1);
    #endif
    } /* c  */

  /* OK, all A and B processed; might still have carry or borrow  */
  /* return number of Units in the result, negated if a borrow  */
  if (carry==0) return static_cast<int32_t>(c-clsu);     /* no carry, so no more to do  */
  if (carry>0) {                   /* positive carry  */
    *c = static_cast<Unit>(carry); /* place as new unit  */
    c++;                           /* ..  */
    return static_cast<int32_t>(c-clsu);
    }
  /* -ve carry: it's a borrow; complement needed  */
  add=1;                           /* temporary carry...  */
  for (c=clsu; c<maxC; c++) {
    add=DECDPUNMAX+add-*c;
    if (add<=DECDPUNMAX) {
      *c = static_cast<Unit>(add);
      add=0;
      }
     else {
      *c=0;
      add=1;
      }
    }
  /* add an extra unit iff it would be non-zero  */
  #if DECTRACE
    printf("UAS borrow: add %ld, carry %ld\n", add, carry);
  #endif
  if ((add-carry-1)!=0) {
    *c = static_cast<Unit>(add - carry - 1);
    c++;                      /* interesting, include it  */
    }
  return static_cast<int32_t>(clsu-c);              /* -ve result indicates borrowed  */
  } /* decUnitAddSub  */

/* ------------------------------------------------------------------ */
/* decTrim -- trim trailing zeros or normalize                        */
/*                                                                    */
/*   dn is the number to trim or normalize                            */
/*   set is the context to use to check for clamp                     */
/*   all is 1 to remove all trailing zeros, 0 for just fraction ones  */
/*   noclamp is 1 to unconditional (unclamped) trim                   */
/*   dropped returns the number of discarded trailing zeros           */
/*   returns dn                                                       */
/*                                                                    */
/* If clamp is set in the context then the number of zeros trimmed    */
/* may be limited if the exponent is high.                            */
/* All fields are updated as required.  This is a utility operation,  */
/* so special values are unchanged and no error is possible.          */
/* ------------------------------------------------------------------ */
static decNumber * decTrim(decNumber *dn, decContext *set, Flag all,
                           Flag noclamp, Int *dropped) {
  Int   d, exp;                    /* work  */
  uInt  cut;                       /* ..  */
  Unit  *up;                       /* -> current Unit  */

  #if DECCHECK
  if (decCheckOperands(dn, DECUNUSED, DECUNUSED, DECUNCONT)) return dn;
  #endif

  *dropped=0;                           /* assume no zeros dropped  */
  if ((dn->bits & DECSPECIAL)           /* fast exit if special ..  */
    || (*dn->lsu & 0x01)) return dn;    /* .. or odd  */
  if (ISZERO(dn)) {                     /* .. or 0  */
    dn->exponent=0;                     /* (sign is preserved)  */
    return dn;
    }

  /* have a finite number which is even  */
  exp=dn->exponent;
  cut=1;                           /* digit (1-DECDPUN) in Unit  */
  up=dn->lsu;                      /* -> current Unit  */
  for (d=0; d<dn->digits-1; d++) { /* [don't strip the final digit]  */
    /* slice by powers  */
    #if DECDPUN<=4
      uInt quot=QUOT10(*up, cut);
      if ((*up-quot*powers[cut])!=0) break;  /* found non-0 digit  */
    #else
      if (*up%powers[cut]!=0) break;         /* found non-0 digit  */
    #endif
    /* have a trailing 0  */
    if (!all) {                    /* trimming  */
      /* [if exp>0 then all trailing 0s are significant for trim]  */
      if (exp<=0) {                /* if digit might be significant  */
        if (exp==0) break;         /* then quit  */
        exp++;                     /* next digit might be significant  */
        }
      }
    cut++;                         /* next power  */
    if (cut>DECDPUN) {             /* need new Unit  */
      up++;
      cut=1;
      }
    } /* d  */
  if (d==0) return dn;             /* none to drop  */

  /* may need to limit drop if clamping  */
  if (set->clamp && !noclamp) {
    Int maxd=set->emax-set->digits+1-dn->exponent;
    if (maxd<=0) return dn;        /* nothing possible  */
    if (d>maxd) d=maxd;
    }

  /* effect the drop  */
  decShiftToLeast(dn->lsu, D2U(dn->digits), d);
  dn->exponent+=d;                 /* maintain numerical value  */
  dn->digits-=d;                   /* new length  */
  *dropped=d;                      /* report the count  */
  return dn;
  } /* decTrim  */

/* ------------------------------------------------------------------ */
/* decReverse -- reverse a Unit array in place                        */
/*                                                                    */
/*   ulo    is the start of the array                                 */
/*   uhi    is the end of the array (highest Unit to include)         */
/*                                                                    */
/* The units ulo through uhi are reversed in place (if the number     */
/* of units is odd, the middle one is untouched).  Note that the      */
/* digit(s) in each unit are unaffected.                              */
/* ------------------------------------------------------------------ */
static void decReverse(Unit *ulo, Unit *uhi) {
  Unit temp;
  for (; ulo<uhi; ulo++, uhi--) {
    temp=*ulo;
    *ulo=*uhi;
    *uhi=temp;
    }
  } /* decReverse  */

/* ------------------------------------------------------------------ */
/* decShiftToMost -- shift digits in array towards most significant   */
/*                                                                    */
/*   uar    is the array                                              */
/*   digits is the count of digits in use in the array                */
/*   shift  is the number of zeros to pad with (least significant);   */
/*     it must be zero or positive                                    */
/*                                                                    */
/*   returns the new length of the integer in the array, in digits    */
/*                                                                    */
/* No overflow is permitted (that is, the uar array must be known to  */
/* be large enough to hold the result, after shifting).               */
/* ------------------------------------------------------------------ */
static Int decShiftToMost(Unit *uar, Int digits, Int shift) {
  Unit  *target, *source, *first;  /* work  */
  Int   cut;                       /* odd 0's to add  */
  uInt  next;                      /* work  */

  if (shift==0) return digits;     /* [fastpath] nothing to do  */
  if ((digits+shift)<=DECDPUN) {   /* [fastpath] single-unit case  */
    *uar = static_cast<Unit>(*uar * powers[shift]);
    return digits+shift;
    }

  next=0;                          /* all paths  */
  source=uar+D2U(digits)-1;        /* where msu comes from  */
  target=source+D2U(shift);        /* where upper part of first cut goes  */
  cut=DECDPUN-MSUDIGITS(shift);    /* where to slice  */
  if (cut==0) {                    /* unit-boundary case  */
    for (; source>=uar; source--, target--) *target=*source;
    }
   else {
    first=uar+D2U(digits+shift)-1; /* where msu of source will end up  */
    for (; source>=uar; source--, target--) {
      /* split the source Unit and accumulate remainder for next  */
      #if DECDPUN<=4
        uInt quot=QUOT10(*source, cut);
        uInt rem=*source-quot*powers[cut];
        next+=quot;
      #else
        uInt rem=*source%powers[cut];
        next+=*source/powers[cut];
      #endif
      if (target <= first) *target = static_cast<Unit>(next); /* write to target iff valid  */
      next=rem*powers[DECDPUN-cut];            /* save remainder for next Unit  */
      }
    } /* shift-move  */

  /* propagate any partial unit to one below and clear the rest  */
  for (; target>=uar; target--) {
    *target = static_cast<Unit>(next);
    next=0;
    }
  return digits+shift;
  } /* decShiftToMost  */

/* ------------------------------------------------------------------ */
/* decShiftToLeast -- shift digits in array towards least significant */
/*                                                                    */
/*   uar   is the array                                               */
/*   units is length of the array, in units                           */
/*   shift is the number of digits to remove from the lsu end; it     */
/*     must be zero or positive and <= than units*DECDPUN.            */
/*                                                                    */
/*   returns the new length of the integer in the array, in units     */
/*                                                                    */
/* Removed digits are discarded (lost).  Units not required to hold   */
/* the final result are unchanged.                                    */
/* ------------------------------------------------------------------ */
static Int decShiftToLeast(Unit *uar, Int units, Int shift) {
  Unit  *target, *up;              /* work  */
  Int   cut, count;                /* work  */
  Int   quot, rem;                 /* for division  */

  if (shift==0) return units;      /* [fastpath] nothing to do  */
  if (shift==units*DECDPUN) {      /* [fastpath] little to do  */
    *uar=0;                        /* all digits cleared gives zero  */
    return 1;                      /* leaves just the one  */
    }

  target=uar;                      /* both paths  */
  cut=MSUDIGITS(shift);
  if (cut==DECDPUN) {              /* unit-boundary case; easy  */
    up=uar+D2U(shift);
    for (; up<uar+units; target++, up++) *target=*up;
    return static_cast<int32_t>(target-uar);
    }

  /* messier  */
  up=uar+D2U(shift-cut);           /* source; correct to whole Units  */
  count=units*DECDPUN-shift;       /* the maximum new length  */
  #if DECDPUN<=4
    quot=QUOT10(*up, cut);
  #else
    quot=*up/powers[cut];
  #endif
  for (; ; target++) {
    *target = static_cast<Unit>(quot);
    count-=(DECDPUN-cut);
    if (count<=0) break;
    up++;
    quot=*up;
    #if DECDPUN<=4
      quot=QUOT10(quot, cut);
      rem=*up-quot*powers[cut];
    #else
      rem=quot%powers[cut];
      quot=quot/powers[cut];
    #endif
    *target = static_cast<Unit>(*target + rem * powers[DECDPUN - cut]);
    count-=cut;
    if (count<=0) break;
    }
  return static_cast<int32_t>(target-uar+1);
  } /* decShiftToLeast  */

#if DECSUBSET
/* ------------------------------------------------------------------ */
/* decRoundOperand -- round an operand  [used for subset only]        */
/*                                                                    */
/*   dn is the number to round (dn->digits is > set->digits)          */
/*   set is the relevant context                                      */
/*   status is the status accumulator                                 */
/*                                                                    */
/*   returns an allocated decNumber with the rounded result.          */
/*                                                                    */
/* lostDigits and other status may be set by this.                    */
/*                                                                    */
/* Since the input is an operand, it must not be modified.            */
/* Instead, return an allocated decNumber, rounded as required.       */
/* It is the caller's responsibility to free the allocated storage.   */
/*                                                                    */
/* If no storage is available then the result cannot be used, so nullptr */
/* is returned.                                                       */
/* ------------------------------------------------------------------ */
static decNumber *decRoundOperand(const decNumber *dn, decContext *set,
                                  uInt *status) {
  decNumber *res;                       /* result structure  */
  uInt newstatus=0;                     /* status from round  */
  Int  residue=0;                       /* rounding accumulator  */

  /* Allocate storage for the returned decNumber, big enough for the  */
  /* length specified by the context  */
  res=(decNumber *)malloc(sizeof(decNumber)
                          +(D2U(set->digits)-1)*sizeof(Unit));
  if (res==nullptr) {
    *status|=DEC_Insufficient_storage;
    return nullptr;
    }
  decCopyFit(res, dn, set, &residue, &newstatus);
  decApplyRound(res, set, residue, &newstatus);

  /* If that set Inexact then "lost digits" is raised...  */
  if (newstatus & DEC_Inexact) newstatus|=DEC_Lost_digits;
  *status|=newstatus;
  return res;
  } /* decRoundOperand  */
#endif

/* ------------------------------------------------------------------ */
/* decCopyFit -- copy a number, truncating the coefficient if needed  */
/*                                                                    */
/*   dest is the target decNumber                                     */
/*   src  is the source decNumber                                     */
/*   set is the context [used for length (digits) and rounding mode]  */
/*   residue is the residue accumulator                               */
/*   status contains the current status to be updated                 */
/*                                                                    */
/* (dest==src is allowed and will be a no-op if fits)                 */
/* All fields are updated as required.                                */
/* ------------------------------------------------------------------ */
static void decCopyFit(decNumber *dest, const decNumber *src,
                       decContext *set, Int *residue, uInt *status) {
  dest->bits=src->bits;
  dest->exponent=src->exponent;
  decSetCoeff(dest, set, src->lsu, src->digits, residue, status);
  } /* decCopyFit  */

/* ------------------------------------------------------------------ */
/* decSetCoeff -- set the coefficient of a number                     */
/*                                                                    */
/*   dn    is the number whose coefficient array is to be set.        */
/*         It must have space for set->digits digits                  */
/*   set   is the context [for size]                                  */
/*   lsu   -> lsu of the source coefficient [may be dn->lsu]          */
/*   len   is digits in the source coefficient [may be dn->digits]    */
/*   residue is the residue accumulator.  This has values as in       */
/*         decApplyRound, and will be unchanged unless the            */
/*         target size is less than len.  In this case, the           */
/*         coefficient is truncated and the residue is updated to     */
/*         reflect the previous residue and the dropped digits.       */
/*   status is the status accumulator, as usual                       */
/*                                                                    */
/* The coefficient may already be in the number, or it can be an      */
/* external intermediate array.  If it is in the number, lsu must ==  */
/* dn->lsu and len must == dn->digits.                                */
/*                                                                    */
/* Note that the coefficient length (len) may be < set->digits, and   */
/* in this case this merely copies the coefficient (or is a no-op     */
/* if dn->lsu==lsu).                                                  */
/*                                                                    */
/* Note also that (only internally, from decQuantizeOp and            */
/* decSetSubnormal) the value of set->digits may be less than one,    */
/* indicating a round to left.  This routine handles that case        */
/* correctly; caller ensures space.                                   */
/*                                                                    */
/* dn->digits, dn->lsu (and as required), and dn->exponent are        */
/* updated as necessary.   dn->bits (sign) is unchanged.              */
/*                                                                    */
/* DEC_Rounded status is set if any digits are discarded.             */
/* DEC_Inexact status is set if any non-zero digits are discarded, or */
/*                       incoming residue was non-0 (implies rounded) */
/* ------------------------------------------------------------------ */
/* mapping array: maps 0-9 to canonical residues, so that a residue  */
/* can be adjusted in the range [-1, +1] and achieve correct rounding  */
/*                             0  1  2  3  4  5  6  7  8  9  */
static const uByte resmap[10]={0, 3, 3, 3, 3, 5, 7, 7, 7, 7};
static void decSetCoeff(decNumber *dn, decContext *set, const Unit *lsu,
                        Int len, Int *residue, uInt *status) {
  Int   discard;              /* number of digits to discard  */
  uInt  cut;                  /* cut point in Unit  */
  const Unit *up;             /* work  */
  Unit  *target;              /* ..  */
  Int   count;                /* ..  */
  #if DECDPUN<=4
  uInt  temp;                 /* ..  */
  #endif

  discard=len-set->digits;    /* digits to discard  */
  if (discard<=0) {           /* no digits are being discarded  */
    if (dn->lsu!=lsu) {       /* copy needed  */
      /* copy the coefficient array to the result number; no shift needed  */
      count=len;              /* avoids D2U  */
      up=lsu;
      for (target=dn->lsu; count>0; target++, up++, count-=DECDPUN)
        *target=*up;
      dn->digits=len;         /* set the new length  */
      }
    /* dn->exponent and residue are unchanged, record any inexactitude  */
    if (*residue!=0) *status|=(DEC_Inexact | DEC_Rounded);
    return;
    }

  /* some digits must be discarded ...  */
  dn->exponent+=discard;      /* maintain numerical value  */
  *status|=DEC_Rounded;       /* accumulate Rounded status  */
  if (*residue>1) *residue=1; /* previous residue now to right, so reduce  */

  if (discard>len) {          /* everything, +1, is being discarded  */
    /* guard digit is 0  */
    /* residue is all the number [NB could be all 0s]  */
    if (*residue<=0) {        /* not already positive  */
      count=len;              /* avoids D2U  */
      for (up=lsu; count>0; up++, count-=DECDPUN) if (*up!=0) { /* found non-0  */
        *residue=1;
        break;                /* no need to check any others  */
        }
      }
    if (*residue!=0) *status|=DEC_Inexact; /* record inexactitude  */
    *dn->lsu=0;               /* coefficient will now be 0  */
    dn->digits=1;             /* ..  */
    return;
    } /* total discard  */

  /* partial discard [most common case]  */
  /* here, at least the first (most significant) discarded digit exists  */

  /* spin up the number, noting residue during the spin, until get to  */
  /* the Unit with the first discarded digit.  When reach it, extract  */
  /* it and remember its position  */
  count=0;
  for (up=lsu;; up++) {
    count+=DECDPUN;
    if (count>=discard) break; /* full ones all checked  */
    if (*up!=0) *residue=1;
    } /* up  */

  /* here up -> Unit with first discarded digit  */
  cut=discard-(count-DECDPUN)-1;
  if (cut==DECDPUN-1) {       /* unit-boundary case (fast)  */
    Unit half = static_cast<Unit>(powers[DECDPUN]) >> 1;
    /* set residue directly  */
    if (*up>=half) {
      if (*up>half) *residue=7;
      else *residue+=5;       /* add sticky bit  */
      }
     else { /* <half  */
      if (*up!=0) *residue=3; /* [else is 0, leave as sticky bit]  */
      }
    if (set->digits<=0) {     /* special for Quantize/Subnormal :-(  */
      *dn->lsu=0;             /* .. result is 0  */
      dn->digits=1;           /* ..  */
      }
     else {                   /* shift to least  */
      count=set->digits;      /* now digits to end up with  */
      dn->digits=count;       /* set the new length  */
      up++;                   /* move to next  */
      /* on unit boundary, so shift-down copy loop is simple  */
      for (target=dn->lsu; count>0; target++, up++, count-=DECDPUN)
        *target=*up;
      }
    } /* unit-boundary case  */

   else { /* discard digit is in low digit(s), and not top digit  */
    uInt  discard1;                /* first discarded digit  */
    uInt  quot, rem;               /* for divisions  */
    if (cut==0) quot=*up;          /* is at bottom of unit  */
     else /* cut>0 */ {            /* it's not at bottom of unit  */
      #if DECDPUN<=4
        U_ASSERT(/* cut >= 0 &&*/ cut <= 4);
        quot=QUOT10(*up, cut);
        rem=*up-quot*powers[cut];
      #else
        rem=*up%powers[cut];
        quot=*up/powers[cut];
      #endif
      if (rem!=0) *residue=1;
      }
    /* discard digit is now at bottom of quot  */
    #if DECDPUN<=4
      temp=(quot*6554)>>16;        /* fast /10  */
      /* Vowels algorithm here not a win (9 instructions)  */
      discard1=quot-X10(temp);
      quot=temp;
    #else
      discard1=quot%10;
      quot=quot/10;
    #endif
    /* here, discard1 is the guard digit, and residue is everything  */
    /* else [use mapping array to accumulate residue safely]  */
    *residue+=resmap[discard1];
    cut++;                         /* update cut  */
    /* here: up -> Unit of the array with bottom digit  */
    /*       cut is the division point for each Unit  */
    /*       quot holds the uncut high-order digits for the current unit  */
    if (set->digits<=0) {          /* special for Quantize/Subnormal :-(  */
      *dn->lsu=0;                  /* .. result is 0  */
      dn->digits=1;                /* ..  */
      }
     else {                        /* shift to least needed  */
      count=set->digits;           /* now digits to end up with  */
      dn->digits=count;            /* set the new length  */
      /* shift-copy the coefficient array to the result number  */
      for (target=dn->lsu; ; target++) {
        *target = static_cast<Unit>(quot);
        count-=(DECDPUN-cut);
        if (count<=0) break;
        up++;
        quot=*up;
        #if DECDPUN<=4
          quot=QUOT10(quot, cut);
          rem=*up-quot*powers[cut];
        #else
          rem=quot%powers[cut];
          quot=quot/powers[cut];
        #endif
        *target = static_cast<Unit>(*target + rem * powers[DECDPUN - cut]);
        count-=cut;
        if (count<=0) break;
        } /* shift-copy loop  */
      } /* shift to least  */
    } /* not unit boundary  */

  if (*residue!=0) *status|=DEC_Inexact; /* record inexactitude  */
  } /* decSetCoeff  */

/* ------------------------------------------------------------------ */
/* decApplyRound -- apply pending rounding to a number                */
/*                                                                    */
/*   dn    is the number, with space for set->digits digits           */
/*   set   is the context [for size and rounding mode]                */
/*   residue indicates pending rounding, being any accumulated        */
/*         guard and sticky information.  It may be:                  */
/*         6-9: rounding digit is >5                                  */
/*         5:   rounding digit is exactly half-way                    */
/*         1-4: rounding digit is <5 and >0                           */
/*         0:   the coefficient is exact                              */
/*        -1:   as 1, but the hidden digits are subtractive, that     */
/*              is, of the opposite sign to dn.  In this case the     */
/*              coefficient must be non-0.  This case occurs when     */
/*              subtracting a small number (which can be reduced to   */
/*              a sticky bit); see decAddOp.                          */
/*   status is the status accumulator, as usual                       */
/*                                                                    */
/* This routine applies rounding while keeping the length of the      */
/* coefficient constant.  The exponent and status are unchanged       */
/* except if:                                                         */
/*                                                                    */
/*   -- the coefficient was increased and is all nines (in which      */
/*      case Overflow could occur, and is handled directly here so    */
/*      the caller does not need to re-test for overflow)             */
/*                                                                    */
/*   -- the coefficient was decreased and becomes all nines (in which */
/*      case Underflow could occur, and is also handled directly).    */
/*                                                                    */
/* All fields in dn are updated as required.                          */
/*                                                                    */
/* ------------------------------------------------------------------ */
static void decApplyRound(decNumber *dn, decContext *set, Int residue,
                          uInt *status) {
  Int  bump;                  /* 1 if coefficient needs to be incremented  */
                              /* -1 if coefficient needs to be decremented  */

  if (residue==0) return;     /* nothing to apply  */

  bump=0;                     /* assume a smooth ride  */

  /* now decide whether, and how, to round, depending on mode  */
  switch (set->round) {
    case DEC_ROUND_05UP: {    /* round zero or five up (for reround)  */
      /* This is the same as DEC_ROUND_DOWN unless there is a  */
      /* positive residue and the lsd of dn is 0 or 5, in which case  */
      /* it is bumped; when residue is <0, the number is therefore  */
      /* bumped down unless the final digit was 1 or 6 (in which  */
      /* case it is bumped down and then up -- a no-op)  */
      Int lsd5=*dn->lsu%5;     /* get lsd and quintate  */
      if (residue<0 && lsd5!=1) bump=-1;
       else if (residue>0 && lsd5==0) bump=1;
      /* [bump==1 could be applied directly; use common path for clarity]  */
      break;} /* r-05  */

    case DEC_ROUND_DOWN: {
      /* no change, except if negative residue  */
      if (residue<0) bump=-1;
      break;} /* r-d  */

    case DEC_ROUND_HALF_DOWN: {
      if (residue>5) bump=1;
      break;} /* r-h-d  */

    case DEC_ROUND_HALF_EVEN: {
      if (residue>5) bump=1;            /* >0.5 goes up  */
       else if (residue==5) {           /* exactly 0.5000...  */
        /* 0.5 goes up iff [new] lsd is odd  */
        if (*dn->lsu & 0x01) bump=1;
        }
      break;} /* r-h-e  */

    case DEC_ROUND_HALF_UP: {
      if (residue>=5) bump=1;
      break;} /* r-h-u  */

    case DEC_ROUND_UP: {
      if (residue>0) bump=1;
      break;} /* r-u  */

    case DEC_ROUND_CEILING: {
      /* same as _UP for positive numbers, and as _DOWN for negatives  */
      /* [negative residue cannot occur on 0]  */
      if (decNumberIsNegative(dn)) {
        if (residue<0) bump=-1;
        }
       else {
        if (residue>0) bump=1;
        }
      break;} /* r-c  */

    case DEC_ROUND_FLOOR: {
      /* same as _UP for negative numbers, and as _DOWN for positive  */
      /* [negative residue cannot occur on 0]  */
      if (!decNumberIsNegative(dn)) {
        if (residue<0) bump=-1;
        }
       else {
        if (residue>0) bump=1;
        }
      break;} /* r-f  */

    default: {      /* e.g., DEC_ROUND_MAX  */
      *status|=DEC_Invalid_context;
      #if DECTRACE || (DECCHECK && DECVERB)
      printf("Unknown rounding mode: %d\n", set->round);
      #endif
      break;}
    } /* switch  */

  /* now bump the number, up or down, if need be  */
  if (bump==0) return;                       /* no action required  */

  /* Simply use decUnitAddSub unless bumping up and the number is  */
  /* all nines.  In this special case set to 100... explicitly  */
  /* and adjust the exponent by one (as otherwise could overflow  */
  /* the array)  */
  /* Similarly handle all-nines result if bumping down.  */
  if (bump>0) {
    Unit *up;                                /* work  */
    uInt count=dn->digits;                   /* digits to be checked  */
    for (up=dn->lsu; ; up++) {
      if (count<=DECDPUN) {
        /* this is the last Unit (the msu)  */
        if (*up!=powers[count]-1) break;     /* not still 9s  */
        /* here if it, too, is all nines  */
        *up = static_cast<Unit>(powers[count - 1]); /* here 999 -> 100 etc.  */
        for (up=up-1; up>=dn->lsu; up--) *up=0; /* others all to 0  */
        dn->exponent++;                      /* and bump exponent  */
        /* [which, very rarely, could cause Overflow...]  */
        if ((dn->exponent+dn->digits)>set->emax+1) {
          decSetOverflow(dn, set, status);
          }
        return;                              /* done  */
        }
      /* a full unit to check, with more to come  */
      if (*up!=DECDPUNMAX) break;            /* not still 9s  */
      count-=DECDPUN;
      } /* up  */
    } /* bump>0  */
   else {                                    /* -1  */
    /* here checking for a pre-bump of 1000... (leading 1, all  */
    /* other digits zero)  */
    Unit *up, *sup;                          /* work  */
    uInt count=dn->digits;                   /* digits to be checked  */
    for (up=dn->lsu; ; up++) {
      if (count<=DECDPUN) {
        /* this is the last Unit (the msu)  */
        if (*up!=powers[count-1]) break;     /* not 100..  */
        /* here if have the 1000... case  */
        sup=up;                              /* save msu pointer  */
        *up = static_cast<Unit>(powers[count]) - 1; /* here 100 in msu -> 999  */
        /* others all to all-nines, too  */
        for (up=up-1; up>=dn->lsu; up--) *up = static_cast<Unit>(powers[DECDPUN]) - 1;
        dn->exponent--;                      /* and bump exponent  */

        /* iff the number was at the subnormal boundary (exponent=etiny)  */
        /* then the exponent is now out of range, so it will in fact get  */
        /* clamped to etiny and the final 9 dropped.  */
        /* printf(">> emin=%d exp=%d sdig=%d\n", set->emin,  */
        /*        dn->exponent, set->digits);  */
        if (dn->exponent+1==set->emin-set->digits+1) {
          if (count==1 && dn->digits==1) *sup=0;  /* here 9 -> 0[.9]  */
           else {
            *sup = static_cast<Unit>(powers[count - 1]) - 1; /* here 999.. in msu -> 99..  */
            dn->digits--;
            }
          dn->exponent++;
          *status|=DEC_Underflow | DEC_Subnormal | DEC_Inexact | DEC_Rounded;
          }
        return;                              /* done  */
        }

      /* a full unit to check, with more to come  */
      if (*up!=0) break;                     /* not still 0s  */
      count-=DECDPUN;
      } /* up  */

    } /* bump<0  */

  /* Actual bump needed.  Do it.  */
  decUnitAddSub(dn->lsu, D2U(dn->digits), uarrone, 1, 0, dn->lsu, bump);
  } /* decApplyRound  */

#if DECSUBSET
/* ------------------------------------------------------------------ */
/* decFinish -- finish processing a number                            */
/*                                                                    */
/*   dn is the number                                                 */
/*   set is the context                                               */
/*   residue is the rounding accumulator (as in decApplyRound)        */
/*   status is the accumulator                                        */
/*                                                                    */
/* This finishes off the current number by:                           */
/*    1. If not extended:                                             */
/*       a. Converting a zero result to clean '0'                     */
/*       b. Reducing positive exponents to 0, if would fit in digits  */
/*    2. Checking for overflow and subnormals (always)                */
/* Note this is just Finalize when no subset arithmetic.              */
/* All fields are updated as required.                                */
/* ------------------------------------------------------------------ */
static void decFinish(decNumber *dn, decContext *set, Int *residue,
                      uInt *status) {
  if (!set->extended) {
    if ISZERO(dn) {                /* value is zero  */
      dn->exponent=0;              /* clean exponent ..  */
      dn->bits=0;                  /* .. and sign  */
      return;                      /* no error possible  */
      }
    if (dn->exponent>=0) {         /* non-negative exponent  */
      /* >0; reduce to integer if possible  */
      if (set->digits >= (dn->exponent+dn->digits)) {
        dn->digits=decShiftToMost(dn->lsu, dn->digits, dn->exponent);
        dn->exponent=0;
        }
      }
    } /* !extended  */

  decFinalize(dn, set, residue, status);
  } /* decFinish  */
#endif

/* ------------------------------------------------------------------ */
/* decFinalize -- final check, clamp, and round of a number           */
/*                                                                    */
/*   dn is the number                                                 */
/*   set is the context                                               */
/*   residue is the rounding accumulator (as in decApplyRound)        */
/*   status is the status accumulator                                 */
/*                                                                    */
/* This finishes off the current number by checking for subnormal     */
/* results, applying any pending rounding, checking for overflow,     */
/* and applying any clamping.                                         */
/* Underflow and overflow conditions are raised as appropriate.       */
/* All fields are updated as required.                                */
/* ------------------------------------------------------------------ */
static void decFinalize(decNumber *dn, decContext *set, Int *residue,
                        uInt *status) {
  Int shift;                            /* shift needed if clamping  */
  Int tinyexp=set->emin-dn->digits+1;   /* precalculate subnormal boundary  */

  /* Must be careful, here, when checking the exponent as the  */
  /* adjusted exponent could overflow 31 bits [because it may already  */
  /* be up to twice the expected].  */

  /* First test for subnormal.  This must be done before any final  */
  /* round as the result could be rounded to Nmin or 0.  */
  if (dn->exponent<=tinyexp) {          /* prefilter  */
    Int comp;
    decNumber nmin;
    /* A very nasty case here is dn == Nmin and residue<0  */
    if (dn->exponent<tinyexp) {
      /* Go handle subnormals; this will apply round if needed.  */
      decSetSubnormal(dn, set, residue, status);
      return;
      }
    /* Equals case: only subnormal if dn=Nmin and negative residue  */
    uprv_decNumberZero(&nmin);
    nmin.lsu[0]=1;
    nmin.exponent=set->emin;
    comp=decCompare(dn, &nmin, 1);                /* (signless compare)  */
    if (comp==BADINT) {                           /* oops  */
      *status|=DEC_Insufficient_storage;          /* abandon...  */
      return;
      }
    if (*residue<0 && comp==0) {                  /* neg residue and dn==Nmin  */
      decApplyRound(dn, set, *residue, status);   /* might force down  */
      decSetSubnormal(dn, set, residue, status);
      return;
      }
    }

  /* now apply any pending round (this could raise overflow).  */
  if (*residue!=0) decApplyRound(dn, set, *residue, status);

  /* Check for overflow [redundant in the 'rare' case] or clamp  */
  if (dn->exponent<=set->emax-set->digits+1) return;   /* neither needed  */


  /* here when might have an overflow or clamp to do  */
  if (dn->exponent>set->emax-dn->digits+1) {           /* too big  */
    decSetOverflow(dn, set, status);
    return;
    }
  /* here when the result is normal but in clamp range  */
  if (!set->clamp) return;

  /* here when need to apply the IEEE exponent clamp (fold-down)  */
  shift=dn->exponent-(set->emax-set->digits+1);

  /* shift coefficient (if non-zero)  */
  if (!ISZERO(dn)) {
    dn->digits=decShiftToMost(dn->lsu, dn->digits, shift);
    }
  dn->exponent-=shift;   /* adjust the exponent to match  */
  *status|=DEC_Clamped;  /* and record the dirty deed  */
  } /* decFinalize  */

/* ------------------------------------------------------------------ */
/* decSetOverflow -- set number to proper overflow value              */
/*                                                                    */
/*   dn is the number (used for sign [only] and result)               */
/*   set is the context [used for the rounding mode, etc.]            */
/*   status contains the current status to be updated                 */
/*                                                                    */
/* This sets the sign of a number and sets its value to either        */
/* Infinity or the maximum finite value, depending on the sign of     */
/* dn and the rounding mode, following IEEE 754 rules.                */
/* ------------------------------------------------------------------ */
static void decSetOverflow(decNumber *dn, decContext *set, uInt *status) {
  Flag needmax=0;                  /* result is maximum finite value  */
  uByte sign=dn->bits&DECNEG;      /* clean and save sign bit  */

  if (ISZERO(dn)) {                /* zero does not overflow magnitude  */
    Int emax=set->emax;                      /* limit value  */
    if (set->clamp) emax-=set->digits-1;     /* lower if clamping  */
    if (dn->exponent>emax) {                 /* clamp required  */
      dn->exponent=emax;
      *status|=DEC_Clamped;
      }
    return;
    }

  uprv_decNumberZero(dn);
  switch (set->round) {
    case DEC_ROUND_DOWN: {
      needmax=1;                   /* never Infinity  */
      break;} /* r-d  */
    case DEC_ROUND_05UP: {
      needmax=1;                   /* never Infinity  */
      break;} /* r-05  */
    case DEC_ROUND_CEILING: {
      if (sign) needmax=1;         /* Infinity if non-negative  */
      break;} /* r-c  */
    case DEC_ROUND_FLOOR: {
      if (!sign) needmax=1;        /* Infinity if negative  */
      break;} /* r-f  */
    default: break;                /* Infinity in all other cases  */
    }
  if (needmax) {
    decSetMaxValue(dn, set);
    dn->bits=sign;                 /* set sign  */
    }
   else dn->bits=sign|DECINF;      /* Value is +/-Infinity  */
  *status|=DEC_Overflow | DEC_Inexact | DEC_Rounded;
  } /* decSetOverflow  */

/* ------------------------------------------------------------------ */
/* decSetMaxValue -- set number to +Nmax (maximum normal value)       */
/*                                                                    */
/*   dn is the number to set                                          */
/*   set is the context [used for digits and emax]                    */
/*                                                                    */
/* This sets the number to the maximum positive value.                */
/* ------------------------------------------------------------------ */
static void decSetMaxValue(decNumber *dn, decContext *set) {
  Unit *up;                        /* work  */
  Int count=set->digits;           /* nines to add  */
  dn->digits=count;
  /* fill in all nines to set maximum value  */
  for (up=dn->lsu; ; up++) {
    if (count>DECDPUN) *up=DECDPUNMAX;  /* unit full o'nines  */
     else {                             /* this is the msu  */
      *up = static_cast<Unit>(powers[count] - 1);
      break;
      }
    count-=DECDPUN;                /* filled those digits  */
    } /* up  */
  dn->bits=0;                      /* + sign  */
  dn->exponent=set->emax-set->digits+1;
  } /* decSetMaxValue  */

/* ------------------------------------------------------------------ */
/* decSetSubnormal -- process value whose exponent is <Emin           */
/*                                                                    */
/*   dn is the number (used as input as well as output; it may have   */
/*         an allowed subnormal value, which may need to be rounded)  */
/*   set is the context [used for the rounding mode]                  */
/*   residue is any pending residue                                   */
/*   status contains the current status to be updated                 */
/*                                                                    */
/* If subset mode, set result to zero and set Underflow flags.        */
/*                                                                    */
/* Value may be zero with a low exponent; this does not set Subnormal */
/* but the exponent will be clamped to Etiny.                         */
/*                                                                    */
/* Otherwise ensure exponent is not out of range, and round as        */
/* necessary.  Underflow is set if the result is Inexact.             */
/* ------------------------------------------------------------------ */
static void decSetSubnormal(decNumber *dn, decContext *set, Int *residue,
                            uInt *status) {
  decContext workset;         /* work  */
  Int        etiny, adjust;   /* ..  */

  #if DECSUBSET
  /* simple set to zero and 'hard underflow' for subset  */
  if (!set->extended) {
    uprv_decNumberZero(dn);
    /* always full overflow  */
    *status|=DEC_Underflow | DEC_Subnormal | DEC_Inexact | DEC_Rounded;
    return;
    }
  #endif

  /* Full arithmetic -- allow subnormals, rounded to minimum exponent  */
  /* (Etiny) if needed  */
  etiny=set->emin-(set->digits-1);      /* smallest allowed exponent  */

  if ISZERO(dn) {                       /* value is zero  */
    /* residue can never be non-zero here  */
    #if DECCHECK
      if (*residue!=0) {
        printf("++ Subnormal 0 residue %ld\n", (LI)*residue);
        *status|=DEC_Invalid_operation;
        }
    #endif
    if (dn->exponent<etiny) {           /* clamp required  */
      dn->exponent=etiny;
      *status|=DEC_Clamped;
      }
    return;
    }

  *status|=DEC_Subnormal;               /* have a non-zero subnormal  */
  adjust=etiny-dn->exponent;            /* calculate digits to remove  */
  if (adjust<=0) {                      /* not out of range; unrounded  */
    /* residue can never be non-zero here, except in the Nmin-residue  */
    /* case (which is a subnormal result), so can take fast-path here  */
    /* it may already be inexact (from setting the coefficient)  */
    if (*status&DEC_Inexact) *status|=DEC_Underflow;
    return;
    }

  /* adjust>0, so need to rescale the result so exponent becomes Etiny  */
  /* [this code is similar to that in rescale]  */
  workset=*set;                         /* clone rounding, etc.  */
  workset.digits=dn->digits-adjust;     /* set requested length  */
  workset.emin-=adjust;                 /* and adjust emin to match  */
  /* [note that the latter can be <1, here, similar to Rescale case]  */
  decSetCoeff(dn, &workset, dn->lsu, dn->digits, residue, status);
  decApplyRound(dn, &workset, *residue, status);

  /* Use 754 default rule: Underflow is set iff Inexact  */
  /* [independent of whether trapped]  */
  if (*status&DEC_Inexact) *status|=DEC_Underflow;

  /* if rounded up a 999s case, exponent will be off by one; adjust  */
  /* back if so [it will fit, because it was shortened earlier]  */
  if (dn->exponent>etiny) {
    dn->digits=decShiftToMost(dn->lsu, dn->digits, 1);
    dn->exponent--;                     /* (re)adjust the exponent.  */
    }

  /* if rounded to zero, it is by definition clamped...  */
  if (ISZERO(dn)) *status|=DEC_Clamped;
  } /* decSetSubnormal  */

/* ------------------------------------------------------------------ */
/* decCheckMath - check entry conditions for a math function          */
/*                                                                    */
/*   This checks the context and the operand                          */
/*                                                                    */
/*   rhs is the operand to check                                      */
/*   set is the context to check                                      */
/*   status is unchanged if both are good                             */
/*                                                                    */
/* returns non-zero if status is changed, 0 otherwise                 */
/*                                                                    */
/* Restrictions enforced:                                             */
/*                                                                    */
/*   digits, emax, and -emin in the context must be less than         */
/*   DEC_MAX_MATH (999999), and A must be within these bounds if      */
/*   non-zero.  Invalid_operation is set in the status if a           */
/*   restriction is violated.                                         */
/* ------------------------------------------------------------------ */
static uInt decCheckMath(const decNumber *rhs, decContext *set,
                         uInt *status) {
  uInt save=*status;                         /* record  */
  if (set->digits>DEC_MAX_MATH
   || set->emax>DEC_MAX_MATH
   || -set->emin>DEC_MAX_MATH) *status|=DEC_Invalid_context;
   else if ((rhs->digits>DEC_MAX_MATH
     || rhs->exponent+rhs->digits>DEC_MAX_MATH+1
     || rhs->exponent+rhs->digits<2*(1-DEC_MAX_MATH))
     && !ISZERO(rhs)) *status|=DEC_Invalid_operation;
  return (*status!=save);
  } /* decCheckMath  */

/* ------------------------------------------------------------------ */
/* decGetInt -- get integer from a number                             */
/*                                                                    */
/*   dn is the number [which will not be altered]                     */
/*                                                                    */
/*   returns one of:                                                  */
/*     BADINT if there is a non-zero fraction                         */
/*     the converted integer                                          */
/*     BIGEVEN if the integer is even and magnitude > 2*10**9         */
/*     BIGODD  if the integer is odd  and magnitude > 2*10**9         */
/*                                                                    */
/* This checks and gets a whole number from the input decNumber.      */
/* The sign can be determined from dn by the caller when BIGEVEN or   */
/* BIGODD is returned.                                                */
/* ------------------------------------------------------------------ */
static Int decGetInt(const decNumber *dn) {
  Int  theInt;                          /* result accumulator  */
  const Unit *up;                       /* work  */
  Int  got;                             /* digits (real or not) processed  */
  Int  ilength=dn->digits+dn->exponent; /* integral length  */
  Flag neg=decNumberIsNegative(dn);     /* 1 if -ve  */

  /* The number must be an integer that fits in 10 digits  */
  /* Assert, here, that 10 is enough for any rescale Etiny  */
  #if DEC_MAX_EMAX > 999999999
    #error GetInt may need updating [for Emax]
  #endif
  #if DEC_MIN_EMIN < -999999999
    #error GetInt may need updating [for Emin]
  #endif
  if (ISZERO(dn)) return 0;             /* zeros are OK, with any exponent  */

  up=dn->lsu;                           /* ready for lsu  */
  theInt=0;                             /* ready to accumulate  */
  if (dn->exponent>=0) {                /* relatively easy  */
    /* no fractional part [usual]; allow for positive exponent  */
    got=dn->exponent;
    }
   else { /* -ve exponent; some fractional part to check and discard  */
    Int count=-dn->exponent;            /* digits to discard  */
    /* spin up whole units until reach the Unit with the unit digit  */
    for (; count>=DECDPUN; up++) {
      if (*up!=0) return BADINT;        /* non-zero Unit to discard  */
      count-=DECDPUN;
      }
    if (count==0) got=0;                /* [a multiple of DECDPUN]  */
     else {                             /* [not multiple of DECDPUN]  */
      Int rem;                          /* work  */
      /* slice off fraction digits and check for non-zero  */
      #if DECDPUN<=4
        theInt=QUOT10(*up, count);
        rem=*up-theInt*powers[count];
      #else
        rem=*up%powers[count];          /* slice off discards  */
        theInt=*up/powers[count];
      #endif
      if (rem!=0) return BADINT;        /* non-zero fraction  */
      /* it looks good  */
      got=DECDPUN-count;                /* number of digits so far  */
      up++;                             /* ready for next  */
      }
    }
  /* now it's known there's no fractional part  */

  /* tricky code now, to accumulate up to 9.3 digits  */
  if (got==0) {theInt=*up; got+=DECDPUN; up++;} /* ensure lsu is there  */

  if (ilength<11) {
    Int save=theInt;
    /* collect any remaining unit(s)  */
    for (; got<ilength; up++) {
      theInt+=*up*powers[got];
      got+=DECDPUN;
      }
    if (ilength==10) {                  /* need to check for wrap  */
      if (theInt / static_cast<Int>(powers[got - DECDPUN]) != static_cast<Int>(*(up - 1))) ilength = 11;
         /* [that test also disallows the BADINT result case]  */
       else if (neg && theInt>1999999997) ilength=11;
       else if (!neg && theInt>999999999) ilength=11;
      if (ilength==11) theInt=save;     /* restore correct low bit  */
      }
    }

  if (ilength>10) {                     /* too big  */
    if (theInt&1) return BIGODD;        /* bottom bit 1  */
    return BIGEVEN;                     /* bottom bit 0  */
    }

  if (neg) theInt=-theInt;              /* apply sign  */
  return theInt;
  } /* decGetInt  */

/* ------------------------------------------------------------------ */
/* decDecap -- decapitate the coefficient of a number                 */
/*                                                                    */
/*   dn   is the number to be decapitated                             */
/*   drop is the number of digits to be removed from the left of dn;  */
/*     this must be <= dn->digits (if equal, the coefficient is       */
/*     set to 0)                                                      */
/*                                                                    */
/* Returns dn; dn->digits will be <= the initial digits less drop     */
/* (after removing drop digits there may be leading zero digits       */
/* which will also be removed).  Only dn->lsu and dn->digits change.  */
/* ------------------------------------------------------------------ */
static decNumber *decDecap(decNumber *dn, Int drop) {
  Unit *msu;                            /* -> target cut point  */
  Int cut;                              /* work  */
  if (drop>=dn->digits) {               /* losing the whole thing  */
    #if DECCHECK
    if (drop>dn->digits)
      printf("decDecap called with drop>digits [%ld>%ld]\n",
             (LI)drop, (LI)dn->digits);
    #endif
    dn->lsu[0]=0;
    dn->digits=1;
    return dn;
    }
  msu=dn->lsu+D2U(dn->digits-drop)-1;   /* -> likely msu  */
  cut=MSUDIGITS(dn->digits-drop);       /* digits to be in use in msu  */
  if (cut!=DECDPUN) *msu%=powers[cut];  /* clear left digits  */
  /* that may have left leading zero digits, so do a proper count...  */
  dn->digits=decGetDigits(dn->lsu, static_cast<int32_t>(msu-dn->lsu+1));
  return dn;
  } /* decDecap  */

/* ------------------------------------------------------------------ */
/* decBiStr -- compare string with pairwise options                   */
/*                                                                    */
/*   targ is the string to compare                                    */
/*   str1 is one of the strings to compare against (length may be 0)  */
/*   str2 is the other; it must be the same length as str1            */
/*                                                                    */
/*   returns 1 if strings compare equal, (that is, it is the same     */
/*   length as str1 and str2, and each character of targ is in either */
/*   str1 or str2 in the corresponding position), or 0 otherwise      */
/*                                                                    */
/* This is used for generic caseless compare, including the awkward   */
/* case of the Turkish dotted and dotless Is.  Use as (for example):  */
/*   if (decBiStr(test, "mike", "MIKE")) ...                          */
/* ------------------------------------------------------------------ */
static Flag decBiStr(const char *targ, const char *str1, const char *str2) {
  for (;;targ++, str1++, str2++) {
    if (*targ!=*str1 && *targ!=*str2) return 0;
    /* *targ has a match in one (or both, if terminator)  */
    if (*targ=='\0') break;
    } /* forever  */
  return 1;
  } /* decBiStr  */

/* ------------------------------------------------------------------ */
/* decNaNs -- handle NaN operand or operands                          */
/*                                                                    */
/*   res     is the result number                                     */
/*   lhs     is the first operand                                     */
/*   rhs     is the second operand, or nullptr if none                   */
/*   context is used to limit payload length                          */
/*   status  contains the current status                              */
/*   returns res in case convenient                                   */
/*                                                                    */
/* Called when one or both operands is a NaN, and propagates the      */
/* appropriate result to res.  When an sNaN is found, it is changed   */
/* to a qNaN and Invalid operation is set.                            */
/* ------------------------------------------------------------------ */
static decNumber * decNaNs(decNumber *res, const decNumber *lhs,
                           const decNumber *rhs, decContext *set,
                           uInt *status) {
  /* This decision tree ends up with LHS being the source pointer,  */
  /* and status updated if need be  */
  if (lhs->bits & DECSNAN)
    *status|=DEC_Invalid_operation | DEC_sNaN;
   else if (rhs==nullptr);
   else if (rhs->bits & DECSNAN) {
    lhs=rhs;
    *status|=DEC_Invalid_operation | DEC_sNaN;
    }
   else if (lhs->bits & DECNAN);
   else lhs=rhs;

  /* propagate the payload  */
  if (lhs->digits<=set->digits) uprv_decNumberCopy(res, lhs); /* easy  */
   else { /* too long  */
    const Unit *ul;
    Unit *ur, *uresp1;
    /* copy safe number of units, then decapitate  */
    res->bits=lhs->bits;                /* need sign etc.  */
    uresp1=res->lsu+D2U(set->digits);
    for (ur=res->lsu, ul=lhs->lsu; ur<uresp1; ur++, ul++) *ur=*ul;
    res->digits=D2U(set->digits)*DECDPUN;
    /* maybe still too long  */
    if (res->digits>set->digits) decDecap(res, res->digits-set->digits);
    }

  res->bits&=~DECSNAN;        /* convert any sNaN to NaN, while  */
  res->bits|=DECNAN;          /* .. preserving sign  */
  res->exponent=0;            /* clean exponent  */
                              /* [coefficient was copied/decapitated]  */
  return res;
  } /* decNaNs  */

/* ------------------------------------------------------------------ */
/* decStatus -- apply non-zero status                                 */
/*                                                                    */
/*   dn     is the number to set if error                             */
/*   status contains the current status (not yet in context)          */
/*   set    is the context                                            */
/*                                                                    */
/* If the status is an error status, the number is set to a NaN,      */
/* unless the error was an overflow, divide-by-zero, or underflow,    */
/* in which case the number will have already been set.               */
/*                                                                    */
/* The context status is then updated with the new status.  Note that */
/* this may raise a signal, so control may never return from this     */
/* routine (hence resources must be recovered before it is called).   */
/* ------------------------------------------------------------------ */
static void decStatus(decNumber *dn, uInt status, decContext *set) {
  if (status & DEC_NaNs) {              /* error status -> NaN  */
    /* if cause was an sNaN, clear and propagate [NaN is already set up]  */
    if (status & DEC_sNaN) status&=~DEC_sNaN;
     else {
      uprv_decNumberZero(dn);                /* other error: clean throughout  */
      dn->bits=DECNAN;                  /* and make a quiet NaN  */
      }
    }
  uprv_decContextSetStatus(set, status);     /* [may not return]  */
  } /* decStatus  */

/* ------------------------------------------------------------------ */
/* decGetDigits -- count digits in a Units array                      */
/*                                                                    */
/*   uar is the Unit array holding the number (this is often an       */
/*          accumulator of some sort)                                 */
/*   len is the length of the array in units [>=1]                    */
/*                                                                    */
/*   returns the number of (significant) digits in the array          */
/*                                                                    */
/* All leading zeros are excluded, except the last if the array has   */
/* only zero Units.                                                   */
/* ------------------------------------------------------------------ */
/* This may be called twice during some operations.  */
static Int decGetDigits(Unit *uar, Int len) {
  Unit *up=uar+(len-1);            /* -> msu  */
  Int  digits=(len-1)*DECDPUN+1;   /* possible digits excluding msu  */
  #if DECDPUN>4
  uInt const *pow;                 /* work  */
  #endif
                                   /* (at least 1 in final msu)  */
  #if DECCHECK
  if (len<1) printf("decGetDigits called with len<1 [%ld]\n", (LI)len);
  #endif

  for (; up>=uar; up--) {
    if (*up==0) {                  /* unit is all 0s  */
      if (digits==1) break;        /* a zero has one digit  */
      digits-=DECDPUN;             /* adjust for 0 unit  */
      continue;}
    /* found the first (most significant) non-zero Unit  */
    #if DECDPUN>1                  /* not done yet  */
    if (*up<10) break;             /* is 1-9  */
    digits++;
    #if DECDPUN>2                  /* not done yet  */
    if (*up<100) break;            /* is 10-99  */
    digits++;
    #if DECDPUN>3                  /* not done yet  */
    if (*up<1000) break;           /* is 100-999  */
    digits++;
    #if DECDPUN>4                  /* count the rest ...  */
    for (pow=&powers[4]; *up>=*pow; pow++) digits++;
    #endif
    #endif
    #endif
    #endif
    break;
    } /* up  */
  return digits;
  } /* decGetDigits  */

#if DECTRACE | DECCHECK
/* ------------------------------------------------------------------ */
/* decNumberShow -- display a number [debug aid]                      */
/*   dn is the number to show                                         */
/*                                                                    */
/* Shows: sign, exponent, coefficient (msu first), digits             */
/*    or: sign, special-value                                         */
/* ------------------------------------------------------------------ */
/* this is public so other modules can use it  */
void uprv_decNumberShow(const decNumber *dn) {
  const Unit *up;                  /* work  */
  uInt u, d;                       /* ..  */
  Int cut;                         /* ..  */
  char isign='+';                  /* main sign  */
  if (dn==nullptr) {
    printf("nullptr\n");
    return;}
  if (decNumberIsNegative(dn)) isign='-';
  printf(" >> %c ", isign);
  if (dn->bits&DECSPECIAL) {       /* Is a special value  */
    if (decNumberIsInfinite(dn)) printf("Infinity");
     else {                                  /* a NaN  */
      if (dn->bits&DECSNAN) printf("sNaN");  /* signalling NaN  */
       else printf("NaN");
      }
    /* if coefficient and exponent are 0, no more to do  */
    if (dn->exponent==0 && dn->digits==1 && *dn->lsu==0) {
      printf("\n");
      return;}
    /* drop through to report other information  */
    printf(" ");
    }

  /* now carefully display the coefficient  */
  up=dn->lsu+D2U(dn->digits)-1;         /* msu  */
  printf("%ld", (LI)*up);
  for (up=up-1; up>=dn->lsu; up--) {
    u=*up;
    printf(":");
    for (cut=DECDPUN-1; cut>=0; cut--) {
      d=u/powers[cut];
      u-=d*powers[cut];
      printf("%ld", (LI)d);
      } /* cut  */
    } /* up  */
  if (dn->exponent!=0) {
    char esign='+';
    if (dn->exponent<0) esign='-';
    printf(" E%c%ld", esign, (LI)abs(dn->exponent));
    }
  printf(" [%ld]\n", (LI)dn->digits);
  } /* decNumberShow  */
#endif

#if DECTRACE || DECCHECK
/* ------------------------------------------------------------------ */
/* decDumpAr -- display a unit array [debug/check aid]                */
/*   name is a single-character tag name                              */
/*   ar   is the array to display                                     */
/*   len  is the length of the array in Units                         */
/* ------------------------------------------------------------------ */
static void decDumpAr(char name, const Unit *ar, Int len) {
  Int i;
  const char *spec;
  #if DECDPUN==9
    spec="%09d ";
  #elif DECDPUN==8
    spec="%08d ";
  #elif DECDPUN==7
    spec="%07d ";
  #elif DECDPUN==6
    spec="%06d ";
  #elif DECDPUN==5
    spec="%05d ";
  #elif DECDPUN==4
    spec="%04d ";
  #elif DECDPUN==3
    spec="%03d ";
  #elif DECDPUN==2
    spec="%02d ";
  #else
    spec="%d ";
  #endif
  printf("  :%c: ", name);
  for (i=len-1; i>=0; i--) {
    if (i==len-1) printf("%ld ", (LI)ar[i]);
     else printf(spec, ar[i]);
    }
  printf("\n");
  return;}
#endif

#if DECCHECK
/* ------------------------------------------------------------------ */
/* decCheckOperands -- check operand(s) to a routine                  */
/*   res is the result structure (not checked; it will be set to      */
/*          quiet NaN if error found (and it is not nullptr))            */
/*   lhs is the first operand (may be DECUNRESU)                      */
/*   rhs is the second (may be DECUNUSED)                             */
/*   set is the context (may be DECUNCONT)                            */
/*   returns 0 if both operands, and the context are clean, or 1      */
/*     otherwise (in which case the context will show an error,       */
/*     unless nullptr).  Note that res is not cleaned; caller should     */
/*     handle this so res=nullptr case is safe.                          */
/* The caller is expected to abandon immediately if 1 is returned.    */
/* ------------------------------------------------------------------ */
static Flag decCheckOperands(decNumber *res, const decNumber *lhs,
                             const decNumber *rhs, decContext *set) {
  Flag bad=0;
  if (set==nullptr) {                 /* oops; hopeless  */
    #if DECTRACE || DECVERB
    printf("Reference to context is nullptr.\n");
    #endif
    bad=1;
    return 1;}
   else if (set!=DECUNCONT
     && (set->digits<1 || set->round>=DEC_ROUND_MAX)) {
    bad=1;
    #if DECTRACE || DECVERB
    printf("Bad context [digits=%ld round=%ld].\n",
           (LI)set->digits, (LI)set->round);
    #endif
    }
   else {
    if (res==nullptr) {
      bad=1;
      #if DECTRACE
      /* this one not DECVERB as standard tests include nullptr  */
      printf("Reference to result is nullptr.\n");
      #endif
      }
    if (!bad && lhs!=DECUNUSED) bad=(decCheckNumber(lhs));
    if (!bad && rhs!=DECUNUSED) bad=(decCheckNumber(rhs));
    }
  if (bad) {
    if (set!=DECUNCONT) uprv_decContextSetStatus(set, DEC_Invalid_operation);
    if (res!=DECUNRESU && res!=nullptr) {
      uprv_decNumberZero(res);
      res->bits=DECNAN;       /* qNaN  */
      }
    }
  return bad;
  } /* decCheckOperands  */

/* ------------------------------------------------------------------ */
/* decCheckNumber -- check a number                                   */
/*   dn is the number to check                                        */
/*   returns 0 if the number is clean, or 1 otherwise                 */
/*                                                                    */
/* The number is considered valid if it could be a result from some   */
/* operation in some valid context.                                   */
/* ------------------------------------------------------------------ */
static Flag decCheckNumber(const decNumber *dn) {
  const Unit *up;             /* work  */
  uInt maxuint;               /* ..  */
  Int ae, d, digits;          /* ..  */
  Int emin, emax;             /* ..  */

  if (dn==nullptr) {             /* hopeless  */
    #if DECTRACE
    /* this one not DECVERB as standard tests include nullptr  */
    printf("Reference to decNumber is nullptr.\n");
    #endif
    return 1;}

  /* check special values  */
  if (dn->bits & DECSPECIAL) {
    if (dn->exponent!=0) {
      #if DECTRACE || DECVERB
      printf("Exponent %ld (not 0) for a special value [%02x].\n",
             (LI)dn->exponent, dn->bits);
      #endif
      return 1;}

    /* 2003.09.08: NaNs may now have coefficients, so next tests Inf only  */
    if (decNumberIsInfinite(dn)) {
      if (dn->digits!=1) {
        #if DECTRACE || DECVERB
        printf("Digits %ld (not 1) for an infinity.\n", (LI)dn->digits);
        #endif
        return 1;}
      if (*dn->lsu!=0) {
        #if DECTRACE || DECVERB
        printf("LSU %ld (not 0) for an infinity.\n", (LI)*dn->lsu);
        #endif
        decDumpAr('I', dn->lsu, D2U(dn->digits));
        return 1;}
      } /* Inf  */
    /* 2002.12.26: negative NaNs can now appear through proposed IEEE  */
    /*             concrete formats (decimal64, etc.).  */
    return 0;
    }

  /* check the coefficient  */
  if (dn->digits<1 || dn->digits>DECNUMMAXP) {
    #if DECTRACE || DECVERB
    printf("Digits %ld in number.\n", (LI)dn->digits);
    #endif
    return 1;}

  d=dn->digits;

  for (up=dn->lsu; d>0; up++) {
    if (d>DECDPUN) maxuint=DECDPUNMAX;
     else {                   /* reached the msu  */
      maxuint=powers[d]-1;
      if (dn->digits>1 && *up<powers[d-1]) {
        #if DECTRACE || DECVERB
        printf("Leading 0 in number.\n");
        uprv_decNumberShow(dn);
        #endif
        return 1;}
      }
    if (*up>maxuint) {
      #if DECTRACE || DECVERB
      printf("Bad Unit [%08lx] in %ld-digit number at offset %ld [maxuint %ld].\n",
              (LI)*up, (LI)dn->digits, (LI)(up-dn->lsu), (LI)maxuint);
      #endif
      return 1;}
    d-=DECDPUN;
    }

  /* check the exponent.  Note that input operands can have exponents  */
  /* which are out of the set->emin/set->emax and set->digits range  */
  /* (just as they can have more digits than set->digits).  */
  ae=dn->exponent+dn->digits-1;    /* adjusted exponent  */
  emax=DECNUMMAXE;
  emin=DECNUMMINE;
  digits=DECNUMMAXP;
  if (ae<emin-(digits-1)) {
    #if DECTRACE || DECVERB
    printf("Adjusted exponent underflow [%ld].\n", (LI)ae);
    uprv_decNumberShow(dn);
    #endif
    return 1;}
  if (ae>+emax) {
    #if DECTRACE || DECVERB
    printf("Adjusted exponent overflow [%ld].\n", (LI)ae);
    uprv_decNumberShow(dn);
    #endif
    return 1;}

  return 0;              /* it's OK  */
  } /* decCheckNumber  */

/* ------------------------------------------------------------------ */
/* decCheckInexact -- check a normal finite inexact result has digits */
/*   dn is the number to check                                        */
/*   set is the context (for status and precision)                    */
/*   sets Invalid operation, etc., if some digits are missing         */
/* [this check is not made for DECSUBSET compilation or when          */
/* subnormal is not set]                                              */
/* ------------------------------------------------------------------ */
static void decCheckInexact(const decNumber *dn, decContext *set) {
  #if !DECSUBSET && DECEXTFLAG
    if ((set->status & (DEC_Inexact|DEC_Subnormal))==DEC_Inexact
     && (set->digits!=dn->digits) && !(dn->bits & DECSPECIAL)) {
      #if DECTRACE || DECVERB
      printf("Insufficient digits [%ld] on normal Inexact result.\n",
             (LI)dn->digits);
      uprv_decNumberShow(dn);
      #endif
      uprv_decContextSetStatus(set, DEC_Invalid_operation);
      }
  #else
    /* next is a noop for quiet compiler  */
    if (dn!=nullptr && dn->digits==0) set->status|=DEC_Invalid_operation;
  #endif
  return;
  } /* decCheckInexact  */
#endif

#if DECALLOC
#undef malloc
#undef free
/* ------------------------------------------------------------------ */
/* decMalloc -- accountable allocation routine                        */
/*   n is the number of bytes to allocate                             */
/*                                                                    */
/* Semantics is the same as the stdlib malloc routine, but bytes      */
/* allocated are accounted for globally, and corruption fences are    */
/* added before and after the 'actual' storage.                       */
/* ------------------------------------------------------------------ */
/* This routine allocates storage with an extra twelve bytes; 8 are   */
/* at the start and hold:                                             */
/*   0-3 the original length requested                                */
/*   4-7 buffer corruption detection fence (DECFENCE, x4)             */
/* The 4 bytes at the end also hold a corruption fence (DECFENCE, x4) */
/* ------------------------------------------------------------------ */
static void *decMalloc(size_t n) {
  uInt  size=n+12;                 /* true size  */
  void  *alloc;                    /* -> allocated storage  */
  uByte *b, *b0;                   /* work  */
  uInt  uiwork;                    /* for macros  */

  alloc=malloc(size);              /* -> allocated storage  */
  if (alloc==nullptr) return nullptr;    /* out of strorage  */
  b0=(uByte *)alloc;               /* as bytes  */
  decAllocBytes+=n;                /* account for storage  */
  UBFROMUI(alloc, n);              /* save n  */
  /* printf(" alloc ++ dAB: %ld (%ld)\n", (LI)decAllocBytes, (LI)n);  */
  for (b=b0+4; b<b0+8; b++) *b=DECFENCE;
  for (b=b0+n+8; b<b0+n+12; b++) *b=DECFENCE;
  return b0+8;                     /* -> play area  */
  } /* decMalloc  */

/* ------------------------------------------------------------------ */
/* decFree -- accountable free routine                                */
/*   alloc is the storage to free                                     */
/*                                                                    */
/* Semantics is the same as the stdlib malloc routine, except that    */
/* the global storage accounting is updated and the fences are        */
/* checked to ensure that no routine has written 'out of bounds'.     */
/* ------------------------------------------------------------------ */
/* This routine first checks that the fences have not been corrupted. */
/* It then frees the storage using the 'truw' storage address (that   */
/* is, offset by 8).                                                  */
/* ------------------------------------------------------------------ */
static void decFree(void *alloc) {
  uInt  n;                         /* original length  */
  uByte *b, *b0;                   /* work  */
  uInt  uiwork;                    /* for macros  */

  if (alloc==nullptr) return;         /* allowed; it's a nop  */
  b0=(uByte *)alloc;               /* as bytes  */
  b0-=8;                           /* -> true start of storage  */
  n=UBTOUI(b0);                    /* lift length  */
  for (b=b0+4; b<b0+8; b++) if (*b!=DECFENCE)
    printf("=== Corrupt byte [%02x] at offset %d from %ld ===\n", *b,
           b-b0-8, (LI)b0);
  for (b=b0+n+8; b<b0+n+12; b++) if (*b!=DECFENCE)
    printf("=== Corrupt byte [%02x] at offset +%d from %ld, n=%ld ===\n", *b,
           b-b0-8, (LI)b0, (LI)n);
  free(b0);                        /* drop the storage  */
  decAllocBytes-=n;                /* account for storage  */
  /* printf(" free -- dAB: %d (%d)\n", decAllocBytes, -n);  */
  } /* decFree  */
#define malloc(a) decMalloc(a)
#define free(a) decFree(a)
#endif
