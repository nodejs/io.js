// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <fstream>

// We only use U8_* macros, which are entirely inline.
#include "unicode/utf8.h"

// This contains a codepage and ISO 14882:1998 illegality table.
// Use "make gen-table" to rebuild it.
#include "cptbl.h"

/**
 * What is this?
 *
 * "This" is a preprocessor that makes an attempt to convert fully valid C++11 source code
 * in utf-8 into something consumable by certain compilers (Solaris, xlC)
 * which aren't quite standards compliant.
 *
 * - u"<unicode>" or u'<unicode>' gets converted to u"\uNNNN" or u'\uNNNN'
 * - u8"<unicode>" gets converted to "\xAA\xBB\xCC\xDD" etc.
 *   (some compilers do not support the u8 prefix correctly.)
 * - if the system is EBCDIC-based, that is used to correct the input characters.
 *
 * Usage:
 *   escapesrc infile.cpp outfile.cpp
 * Normally this is invoked by the build stage, with a rule such as:
 *
 * _%.cpp: $(srcdir)/%.cpp
 *       @$(BINDIR)/escapesrc$(EXEEXT) $< $@
 * %.o: _%.cpp
 *       $(COMPILE.cc) ... $@ $<
 *
 * In the Makefiles, SKIP_ESCAPING=YES is used to prevent escapesrc.cpp 
 * from being itself escaped.
 */


static const char
  kSPACE   = 0x20,
  kTAB     = 0x09,
  kLF      = 0x0A,
  kCR      = 0x0D;

// For convenience
# define cp1047_to_8859(c) cp1047_8859_1[c]

// Our app's name
std::string prog;

/**
 * Give the usual 1-line documentation and exit
 */
void usage() {
  fprintf(stderr, "%s: usage: %s infile.cpp outfile.cpp\n", prog.c_str(), prog.c_str());
}

/**
 * Delete the output file (if any)
 * We want to delete even if we didn't generate, because it might be stale.
 */
int cleanup(const std::string &outfile) {
  const char *outstr = outfile.c_str();
  if(outstr && *outstr) {
    int rc = std::remove(outstr);
    if(rc == 0) {
      fprintf(stderr, "%s: deleted %s\n", prog.c_str(), outstr);
      return 0;
    } else {
      if( errno == ENOENT ) {
        return 0; // File did not exist - no error.
      } else {
        perror("std::remove");
        return 1;
      }
    }
  }
  return 0;
}

/**
 * Skip across any known whitespace.
 * @param p startpoint
 * @param e limit
 * @return first non-whitespace char
 */
inline const char *skipws(const char *p, const char *e) {
  for(;p<e;p++) {
    switch(*p) {
    case kSPACE:
    case kTAB:
    case kLF:
    case kCR:
      break;
    default:
      return p; // non ws
    }
  }
  return p;
}

/**
 * Append a byte, hex encoded
 * @param outstr sstring to append to
 * @param byte the byte to append
 */
void appendByte(std::string &outstr,
                uint8_t byte) {
    char tmp2[5];
    snprintf(tmp2, sizeof(tmp2), "\\x%02X", 0xFF & static_cast<int>(byte));
    outstr += tmp2;
}

/**
 * Append the bytes from 'linestr' into outstr, with escaping
 * @param outstr the output buffer
 * @param linestr the input buffer
 * @param pos in/out: the current char under consideration
 * @param chars the number of chars to consider
 * @return true on failure
 */
bool appendUtf8(std::string &outstr,
                const std::string &linestr,
                size_t &pos,
                size_t chars) {
  char tmp[9];
  for(size_t i=0;i<chars;i++) {
    tmp[i] = linestr[++pos];
  }
  tmp[chars] = 0;
  unsigned int c;
  sscanf(tmp, "%X", &c);
  UChar32 ch = c & 0x1FFFFF; 

  // now to append \\x%% etc
  uint8_t bytesNeeded = U8_LENGTH(ch);
  if(bytesNeeded == 0) {
    fprintf(stderr, "Illegal code point U+%X\n", ch);
    return true;
  }
  uint8_t bytes[4];
  uint8_t *s = bytes;
  size_t i = 0;
  U8_APPEND_UNSAFE(s, i, ch);
  for(size_t t = 0; t<i; t++) {
    appendByte(outstr, s[t]);
  }
  return false;
}

/**
 * Fixup u8"x"
 * @param linestr string to mutate. Already escaped into \u format.
 * @param origpos beginning, points to 'u8"'
 * @param pos end, points to "
 * @return false for no-problem, true for failure!
 */
bool fixu8(std::string &linestr, size_t origpos, size_t &endpos) {
  size_t pos = origpos + 3;
  std::string outstr;
  outstr += '\"'; // local encoding
  for(;pos<endpos;pos++) {
    char c = linestr[pos];
    if(c == '\\') {
      char c2 = linestr[++pos];
      switch(c2) {
      case '\'':
      case '"':
#if (U_CHARSET_FAMILY == U_EBCDIC_FAMILY)
        c2 = cp1047_to_8859(c2);
#endif
        appendByte(outstr, c2);
        break;
      case 'u':
        appendUtf8(outstr, linestr, pos, 4);
        break;
      case 'U':
        appendUtf8(outstr, linestr, pos, 8);
        break;
      }
    } else {
#if (U_CHARSET_FAMILY == U_EBCDIC_FAMILY)
      c = cp1047_to_8859(c);
#endif
      appendByte(outstr, c);
    }
  }
  outstr += ('\"');

  linestr.replace(origpos, (endpos-origpos+1), outstr);
  
  return false; // OK
}

/**
 * fix the u"x"/u'x'/u8"x" string at the position
 * u8'x' is not supported, sorry.
 * @param linestr the input string
 * @param pos the position
 * @return false = no err, true = had err
 */
bool fixAt(std::string &linestr, size_t pos) {
  size_t origpos = pos;
  
  if(linestr[pos] != 'u') {
    fprintf(stderr, "Not a 'u'?");
    return true;
  }

  pos++; // past 'u'

  bool utf8 = false;
  
  if(linestr[pos] == '8') { // u8"
    utf8 = true;
    pos++;
  }
  
  char quote = linestr[pos];

  if(quote != '\'' && quote != '\"') {
    fprintf(stderr, "Quote is '%c' - not sure what to do.\n", quote);
    return true;
  }

  if(quote == '\'' && utf8) {
    fprintf(stderr, "Cannot do u8'...'\n");
    return true;
  }

  pos ++;

  //printf("u%c…%c\n", quote, quote);

  for(; pos < linestr.size(); pos++) {
    if(linestr[pos] == quote) {
      if(utf8) {
        return fixu8(linestr, origpos, pos); // fix u8"..."
      } else {
        return false; // end of quote
      }
    }
    if(linestr[pos] == '\\') {
      pos++;
      if(linestr[pos] == quote) continue; // quoted quote
      if(linestr[pos] == 'u') continue; // for now ... unicode escape
      if(linestr[pos] == '\\') continue;
      // some other escape… ignore
    } else {
      size_t old_pos = pos;
      int32_t i = pos;
#if (U_CHARSET_FAMILY == U_EBCDIC_FAMILY)
      // mogrify 1-4 bytes from 1047 'back' to utf-8
      char old_byte = linestr[pos];
      linestr[pos] = cp1047_to_8859(linestr[pos]);
      // how many more?
      int32_t trail = U8_COUNT_TRAIL_BYTES(linestr[pos]);
      for(size_t pos2 = pos+1; trail>0; pos2++,trail--) {
        linestr[pos2] = cp1047_to_8859(linestr[pos2]);
        if(linestr[pos2] == 0x0A) {
          linestr[pos2] = 0x85; // NL is ambiguous here
        }
      }
#endif
      
      // Proceed to decode utf-8
      const uint8_t* s = reinterpret_cast<const uint8_t*>(linestr.c_str());
      int32_t length = linestr.size();
      UChar32 c;
      if(U8_IS_SINGLE((uint8_t)s[i]) && oldIllegal[s[i]]) {
#if (U_CHARSET_FAMILY == U_EBCDIC_FAMILY)
        linestr[pos] = old_byte; // put it back
#endif
        continue; // single code point not previously legal for \u escaping
      }

      // otherwise, convert it to \u / \U
      {
        U8_NEXT(s, i, length, c);
      }
      if(c<0) {
        fprintf(stderr, "Illegal utf-8 sequence at Column: %d\n", static_cast<int>(old_pos));
        fprintf(stderr, "Line: >>%s<<\n", linestr.c_str());
        return true;
      }

      size_t seqLen = (i-pos);

      //printf("U+%04X pos %d [len %d]\n", c, pos, seqLen);fflush(stdout);

      char newSeq[20];
      if( c <= 0xFFFF) {
        snprintf(newSeq, sizeof(newSeq), "\\u%04X", c);
      } else {
        snprintf(newSeq, sizeof(newSeq), "\\U%08X", c);
      }
      linestr.replace(pos, seqLen, newSeq);
      pos += strlen(newSeq) - 1;
    }
  }

  return false;
}

/**
 * Fixup an entire line
 * false = no err
 * true = had err
 * @param no the line number (not used)
 * @param linestr the string to fix
 * @return true if any err, else false
 */
bool fixLine(int /*no*/, std::string &linestr) {
  const char *line = linestr.c_str();
  size_t len = linestr.size();

  // no u' in the line?
  if(!strstr(line, "u'") && !strstr(line, "u\"") && !strstr(line, "u8\"")) {
    return false; // Nothing to do. No u' or u" detected
  }

  // start from the end and find all u" cases
  size_t pos = len = linestr.size();
  if(len>INT32_MAX/2) {
    return true;
  }
  while((pos>0) && (pos = linestr.rfind("u\"", pos)) != std::string::npos) {
    //printf("found doublequote at %d\n", pos);
    if(fixAt(linestr, pos)) return true;
    if(pos == 0) break;
    pos--;
  }

  // reset and find all u' cases
  pos = len = linestr.size();
  while((pos>0) && (pos = linestr.rfind("u'", pos)) != std::string::npos) {
    //printf("found singlequote at %d\n", pos);
    if(fixAt(linestr, pos)) return true;
    if(pos == 0) break;
    pos--;
  }

  // reset and find all u8" cases
  pos = len = linestr.size();
  while((pos>0) && (pos = linestr.rfind("u8\"", pos)) != std::string::npos) {
    if(fixAt(linestr, pos)) return true;
    if(pos == 0) break;
    pos--;
  }

  //fprintf(stderr, "%d - fixed\n", no);
  return false;
}

/**
 * Convert a whole file
 * @param infile
 * @param outfile
 * @return 1 on err, 0 otherwise
 */
int convert(const std::string &infile, const std::string &outfile) {
  fprintf(stderr, "escapesrc: %s -> %s\n", infile.c_str(), outfile.c_str());

  std::ifstream inf;
  
  inf.open(infile.c_str(), std::ios::in);

  if(!inf.is_open()) {
    fprintf(stderr, "%s: could not open input file %s\n", prog.c_str(), infile.c_str());
    cleanup(outfile);
    return 1;
  }

  std::ofstream outf;

  outf.open(outfile.c_str(), std::ios::out);

  if(!outf.is_open()) {
    fprintf(stderr, "%s: could not open output file %s\n", prog.c_str(), outfile.c_str());
    return 1;
  }

  // TODO: any platform variations of #line?
  outf << "#line 1 \"" << infile << "\"" << '\n';

  int no = 0;
  std::string linestr;
  while( getline( inf, linestr)) {
    no++;
    if(fixLine(no, linestr)) {
      goto fail;
    }
    outf << linestr << '\n';
  }

  if(inf.eof()) {
    return 0;
  }
fail:
  outf.close();
  fprintf(stderr, "%s:%d: Fixup failed by %s\n", infile.c_str(), no, prog.c_str());
  cleanup(outfile);
  return 1;
}

/**
 * Main function
 */
int main(int argc, const char *argv[]) {
  prog = argv[0];

  if(argc != 3) {
    usage();
    return 1;
  }

  std::string infile = argv[1];
  std::string outfile = argv[2];

  return convert(infile, outfile);
}
