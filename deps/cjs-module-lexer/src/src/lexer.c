#include "cjs-module-lexer.h"

const uint16_t* STANDARD_IMPORT = (uint16_t*)0x1;
const uint16_t* IMPORT_META = (uint16_t*)0x2;
const uint16_t __empty_char = '\0';
const uint16_t* EMPTY_CHAR = &__empty_char;
// tracked depth of template and brackets
#define STACK_DEPTH 2048
// tracked number of star exports
#define MAX_STAR_EXPORTS 256
const uint16_t* source;

bool lastSlashWasDivision;
uint16_t templateStackDepth;
uint16_t openTokenDepth;
uint16_t templateDepth;
uint16_t braceDepth;
uint16_t* lastTokenPos;
uint16_t* pos;
uint16_t* end;
uint16_t* templateStack;
uint16_t** openTokenPosStack;
StarExportBinding* starExportStack;
bool nextBraceIsClass;

uint16_t* lastReexportStart;
uint16_t* lastReexportEnd;

// Memory Structure:
// -> source
// -> analysis starts after source
uint32_t parse_error;
uint32_t error = 0;
uint32_t sourceLen;

uint16_t templateStack_[STACK_DEPTH];
uint16_t* openTokenPosStack_[STACK_DEPTH];
bool openClassPosStack[STACK_DEPTH];
StarExportBinding starExportStack_[MAX_STAR_EXPORTS];
const StarExportBinding* STAR_EXPORT_STACK_END = &starExportStack_[MAX_STAR_EXPORTS - 1];

void (*addExport)(const uint16_t*, const uint16_t*);
void (*addReexport)(const uint16_t*, const uint16_t*);
void (*addUnsafeGetter)(const uint16_t*, const uint16_t*);
void (*clearReexports)();

// Note: parsing is based on the _assumption_ that the source is already valid
uint32_t parseCJS (uint16_t* _source, uint32_t _sourceLen, void (*_addExport)(const uint16_t*, const uint16_t*), void (*_addReexport)(const uint16_t*, const uint16_t*), void (*_addUnsafeGetter)(const uint16_t*, const uint16_t*), void (*_clearReexports)()) {
  source = _source;
  sourceLen = _sourceLen;
  if (_addExport)
    addExport = _addExport;
  if (_addReexport)
    addReexport = _addReexport;
  if (_addUnsafeGetter)
    addUnsafeGetter = _addUnsafeGetter;

  templateStackDepth = 0;
  openTokenDepth = 0;
  templateDepth = UINT16_MAX;
  lastTokenPos = (uint16_t*)EMPTY_CHAR;
  lastSlashWasDivision = false;
  parse_error = 0;
  error = 0;
  templateStack = &templateStack_[0];
  openTokenPosStack = &openTokenPosStack_[0];
  starExportStack = &starExportStack_[0];
  nextBraceIsClass = false;

  pos = (uint16_t*)(source - 1);
  uint16_t ch = '\0';
  end = pos + sourceLen;

  // Handle #!
  if (*source == '#' && *(source + 1) == '!') {
    if (sourceLen == 2)
      return 0;
    pos += 2;
    while (pos++ < end) {
      ch = *pos;
      if (ch == '\n' || ch == '\r')
        break;
    }
  }

  while (pos++ < end) {
    ch = *pos;

    if (ch == 32 || ch < 14 && ch > 8)
      continue;

    if (openTokenDepth == 0) {
      switch (ch) {
        case 'i':
          if (str_eq5(pos + 1, 'm', 'p', 'o', 'r', 't') && keywordStart(pos))
            throwIfImportStatement();
          lastTokenPos = pos;
          continue;
        case 'r': {
          uint16_t* startPos = pos;
          if (tryParseRequire(Import) && keywordStart(startPos))
            tryBacktrackAddStarExportBinding(startPos - 1);
          lastTokenPos = pos;
          continue;
        }
        case '_':
          if (str_eq22(pos + 1, 'i', 'n', 't', 'e', 'r', 'o', 'p', 'R', 'e', 'q', 'u', 'i', 'r', 'e', 'W', 'i', 'l', 'd', 'c', 'a', 'r', 'd') && (keywordStart(pos) || *(pos - 1) == '.')) {
            uint16_t* startPos = pos;
            pos += 23;
            if (*pos == '(') {
              pos++;
              openTokenPosStack[openTokenDepth++] = lastTokenPos;
              if (tryParseRequire(Import) && keywordStart(startPos))
                tryBacktrackAddStarExportBinding(startPos - 1);
            }
          }
          else if (str_eq7(pos + 1, '_', 'e', 'x', 'p', 'o', 'r', 't') && (keywordStart(pos) || *(pos - 1) == '.')) {
            pos += 8;
            if (str_eq4(pos, 'S', 't', 'a', 'r'))
              pos += 4;
            if (*pos == '(') {
              openTokenPosStack[openTokenDepth++] = lastTokenPos;
              if (*(++pos) == 'r')
                tryParseRequire(ExportStar);
            }
          }
          lastTokenPos = pos;
          continue;
      }
    }

    switch (ch) {
      case 'e':
        if (str_eq5(pos + 1, 'x', 'p', 'o', 'r', 't') && keywordStart(pos)) {
          if (*(pos + 6) == 's')
            tryParseExportsDotAssign(false);
          else if (openTokenDepth == 0)
            throwIfExportStatement();
        }
        break;
      case 'c':
        if (keywordStart(pos) && str_eq4(pos + 1, 'l', 'a', 's', 's') && isBrOrWs(*(pos + 5)))
          nextBraceIsClass = true;
        break;
      case 'm':
        if (str_eq5(pos + 1, 'o', 'd', 'u', 'l', 'e') && keywordStart(pos))
          tryParseModuleExportsDotAssign();
        break;
      case 'O':
        if (str_eq5(pos + 1, 'b', 'j', 'e', 'c', 't') && keywordStart(pos))
          tryParseObjectDefineOrKeys(openTokenDepth == 0);
        break;
      case '(':
        openTokenPosStack[openTokenDepth++] = lastTokenPos;
        break;
      case ')':
        if (openTokenDepth == 0)
          return syntaxError(8), error;
        openTokenDepth--;
        break;
      case '{':
        openClassPosStack[openTokenDepth] = nextBraceIsClass;
        nextBraceIsClass = false;
        openTokenPosStack[openTokenDepth++] = lastTokenPos;
        break;
      case '}':
        if (openTokenDepth == 0)
          return syntaxError(2), false;
        if (openTokenDepth-- == templateDepth) {
          templateDepth = templateStack[--templateStackDepth];
          templateString();
        }
        else {
          if (templateDepth != UINT16_MAX && openTokenDepth < templateDepth)
            return syntaxError(3), error;
        }
        break;
      case '<':
        // TODO: <!-- XML comment support
        break;
      case '\'':
      case '"':
        stringLiteral(ch);
        break;
      case '/': {
        uint16_t next_ch = *(pos + 1);
        if (next_ch == '/') {
          lineComment();
          // dont update lastToken
          continue;
        }
        else if (next_ch == '*') {
          blockComment();
          // dont update lastToken
          continue;
        }
        else {
          // Division / regex ambiguity handling based on checking backtrack analysis of:
          // - what token came previously (lastToken)
          // - if a closing brace or paren, what token came before the corresponding
          //   opening brace or paren (lastOpenTokenIndex)
          uint16_t lastToken = *lastTokenPos;
          if (isExpressionPunctuator(lastToken) &&
              !(lastToken == '.' && (*(lastTokenPos - 1) >= '0' && *(lastTokenPos - 1) <= '9')) &&
              !(lastToken == '+' && *(lastTokenPos - 1) == '+') && !(lastToken == '-' && *(lastTokenPos - 1) == '-') ||
              lastToken == ')' && isParenKeyword(openTokenPosStack[openTokenDepth]) ||
              lastToken == '}' && (isExpressionTerminator(openTokenPosStack[openTokenDepth]) || openClassPosStack[openTokenDepth]) ||
              lastToken == '/' && lastSlashWasDivision ||
              isExpressionKeyword(lastTokenPos) ||
              !lastToken) {
            regularExpression();
            lastSlashWasDivision = false;
          }
          else {
            lastSlashWasDivision = true;
          }
        }
        break;
      }
      case '`':
        if (templateDepth == UINT16_MAX - 1)
          return syntaxError(4), error;
        templateString();
        break;
    }
    lastTokenPos = pos;
  }

  if (templateDepth != UINT16_MAX || openTokenDepth || error)
    return error;

  // success
  return 0;
}

void tryBacktrackAddStarExportBinding (uint16_t* bPos) {
  while (*bPos == ' ' && bPos > source)
    bPos--;
  if (*bPos == '=') {
    bPos--;
    while (*bPos == ' ' && bPos > source)
      bPos--;
    uint32_t charCode;
    uint16_t* id_end = bPos;
    bool identifierStart = false;
    while ((charCode = fullCharCodeAtLast(bPos)) && bPos > source) {
      if (charCode == '\\')
        return;
      if (!isIdentifierChar(charCode))
        break;
      identifierStart = isIdentifierStart(charCode);
      bPos -= charCodeByteLen(charCode);
    }
    if (identifierStart && *bPos == ' ') {
      // gracefully overflow if there are too many star export bindings to track
      if (starExportStack == STAR_EXPORT_STACK_END)
        return;
      starExportStack->id_start = bPos + 1;
      starExportStack->id_end = id_end + 1;
      while (*bPos == ' ' && bPos > source)
        bPos--;
      switch (*bPos) {
        case 'r':
          if (!readPrecedingKeyword2(bPos - 1, 'v', 'a'))
            return;
          break;
        case 't':
          if (!readPrecedingKeyword2(bPos - 1, 'l', 'e') && !readPrecedingKeyword4(bPos - 1, 'c', 'o', 'n', 's'))
            return;
          break;
        default: return;
      }
      starExportStack++;
    }
  }
}

bool tryParseObjectHasOwnProperty (uint16_t* it_id_start, ptrdiff_t it_id_len) {
  uint16_t ch = commentWhitespace();
  if (ch != 'O' || !str_eq5(pos + 1, 'b', 'j', 'e', 'c', 't')) return false;
  pos += 6;
  ch = commentWhitespace();
  if (ch != '.') return false;
  pos++;
  ch = commentWhitespace();
  if (ch == 'p') {
    if (!str_eq8(pos + 1, 'r', 'o', 't', 'o', 't', 'y', 'p', 'e')) return false;
    pos += 9;
    ch = commentWhitespace();
    if (ch != '.') return false;
    pos++;
    ch = commentWhitespace();
  }
  if (ch != 'h' || !str_eq13(pos + 1, 'a', 's', 'O', 'w', 'n', 'P', 'r', 'o', 'p', 'e', 'r', 't', 'y')) return false;
  pos += 14;
  ch = commentWhitespace();
  if (ch != '.') return false;
  pos++;
  ch = commentWhitespace();
  if (ch != 'c' || !str_eq3(pos + 1, 'a', 'l', 'l')) return false;
  pos += 4;
  ch = commentWhitespace();
  if (ch != '(') return false;
  pos++;
  ch = commentWhitespace();
  if (!identifier(ch)) return false;
  ch = commentWhitespace();
  if (ch != ',') return false;
  pos++;
  ch = commentWhitespace();
  if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) return false;
  pos += it_id_len;
  ch = commentWhitespace();
  if (ch != ')') return false;
  pos++;
  return true;
}

void tryParseObjectDefineOrKeys (bool keys) {
  pos += 6;
  uint16_t* revertPos = pos - 1;
  uint16_t ch = commentWhitespace();
  if (ch == '.') {
    pos++;
    ch = commentWhitespace();
    if (ch == 'd' && str_eq13(pos + 1, 'e', 'f', 'i', 'n', 'e', 'P', 'r', 'o', 'p', 'e', 'r', 't', 'y')) {
      uint16_t* exportStart = 0;
      uint16_t* exportEnd = 0;
      while (true) {
        pos += 14;
        revertPos = pos - 1;
        ch = commentWhitespace();
        if (ch != '(') break;
        pos++;
        ch = commentWhitespace();
        if (!readExportsOrModuleDotExports(ch)) break;
        ch = commentWhitespace();
        if (ch != ',') break;
        pos++;
        ch = commentWhitespace();
        if (ch != '\'' && ch != '"') break;
        exportStart = pos;
        stringLiteral(ch);
        exportEnd = ++pos;
        ch = commentWhitespace();
        if (ch != ',') break;
        pos++;
        ch = commentWhitespace();
        if (ch != '{') break;
        pos++;
        ch = commentWhitespace();
        if (ch == 'e') {
          if (!str_eq9(pos + 1, 'n', 'u', 'm', 'e', 'r', 'a', 'b', 'l', 'e')) break;
          pos += 10;
          ch = commentWhitespace();
          if (ch != ':') break;
          pos++;
          ch = commentWhitespace();
          if (ch != 't' || !str_eq3(pos + 1, 'r', 'u', 'e')) break;
          pos += 4;
          ch = commentWhitespace();
          if (ch != 44) break;
          pos++;
          ch = commentWhitespace();
        }
        if (ch == 'v') {
          if (!str_eq4(pos + 1, 'a', 'l', 'u', 'e')) break;
          pos += 5;
          ch = commentWhitespace();
          if (ch != ':') break;
          addExport(exportStart, exportEnd);
          pos = revertPos;
          return;
        }
        else if (ch == 'g') {
          if (!str_eq2(pos + 1, 'e', 't')) break;
          pos += 3;
          ch = commentWhitespace();
          if (ch == ':') {
            pos++;
            ch = commentWhitespace();
            if (ch != 'f') break;
            if (!str_eq7(pos + 1, 'u', 'n', 'c', 't', 'i', 'o', 'n')) break;
            pos += 8;
            uint16_t* lastPos = pos;
            ch = commentWhitespace();
            if (ch != '(' && (lastPos == pos || !identifier(ch))) break;
            ch = commentWhitespace();
          }
          if (ch != '(') break;
          pos++;
          ch = commentWhitespace();
          if (ch != ')') break;
          pos++;
          ch = commentWhitespace();
          if (ch != '{') break;
          pos++;
          ch = commentWhitespace();
          if (ch != 'r') break;
          if (!str_eq5(pos + 1, 'e', 't', 'u', 'r', 'n')) break;
          pos += 6;
          ch = commentWhitespace();
          if (!identifier(ch)) break;
          ch = commentWhitespace();
          if (ch == '.') {
            pos++;
            ch = commentWhitespace();
            if (!identifier(ch)) break;
            ch = commentWhitespace();
          }
          else if (ch == '[') {
            pos++;
            ch = commentWhitespace();
            if (ch == '\'' || ch == '"') stringLiteral(ch);
            else break;
            pos++;
            ch = commentWhitespace();
            if (ch != ']') break;
            pos++;
            ch = commentWhitespace();
          }
          if (ch == ';') {
            pos++;
            ch = commentWhitespace();
          }
          if (ch != '}') break;
          pos++;
          ch = commentWhitespace();
          if (ch == ',') {
            pos++;
            ch = commentWhitespace();
          }
          if (ch != '}') break;
          pos++;
          ch = commentWhitespace();
          if (ch != ')') break;
          addExport(exportStart, exportEnd);
          return;
        }
        break;
      }
      if (exportEnd > 0) {
        addUnsafeGetter(exportStart, exportEnd);
      }
    }
    else if (keys && ch == 'k' && str_eq3(pos + 1, 'e', 'y', 's')) {
      while (true) {
        pos += 4;
        revertPos = pos - 1;
        ch = commentWhitespace();
        if (ch != '(') break;
        pos++;
        ch = commentWhitespace();
        uint16_t* id_start = pos;
        if (!identifier(ch)) break;
        ptrdiff_t id_len = pos - id_start;
        ch = commentWhitespace();
        if (ch != ')') break;

        revertPos = pos++;
        ch = commentWhitespace();
        if (ch != '.') break;
        pos++;
        ch = commentWhitespace();
        if (ch != 'f' || !str_eq6(pos + 1, 'o', 'r', 'E', 'a', 'c', 'h')) break;
        pos += 7;
        ch = commentWhitespace();
        revertPos = pos - 1;
        if (ch != '(') break;
        pos++;
        ch = commentWhitespace();
        if (ch != 'f' || !str_eq7(pos + 1, 'u', 'n', 'c', 't', 'i', 'o', 'n')) break;
        pos += 8;
        ch = commentWhitespace();
        if (ch != '(') break;
        pos++;
        ch = commentWhitespace();
        uint16_t* it_id_start = pos;
        if (!identifier(ch)) break;
        ptrdiff_t it_id_len = pos - it_id_start;
        ch = commentWhitespace();
        if (ch != ')') break;
        pos++;
        ch = commentWhitespace();
        if (ch != '{') break;
        pos++;
        ch = commentWhitespace();
        if (ch != 'i' || *(pos + 1) != 'f') break;
        pos += 2;
        ch = commentWhitespace();
        if (ch != '(') break;
        pos++;
        ch = commentWhitespace();
        if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
        pos += it_id_len;
        ch = commentWhitespace();
        // `if (` IDENTIFIER$2 `===` ( `'default'` | `"default"` ) `||` IDENTIFIER$2 `===` ( '__esModule' | `"__esModule"` ) `) return` `;`? |
        if (ch == '=') {
          if (!str_eq2(pos + 1, '=', '=')) break;
          pos += 3;
          ch = commentWhitespace();
          if (ch != '"' && ch != '\'') break;
          uint16_t quot = ch;
          if (!str_eq7(pos + 1, 'd', 'e', 'f', 'a', 'u', 'l', 't')) break;
          pos += 8;
          ch = commentWhitespace();
          if (ch != quot) break;
          pos += 1;
          ch = commentWhitespace();
          if (ch != '|' || *(pos + 1) != '|') break;
          pos += 2;
          ch = commentWhitespace();
          if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
          pos += it_id_len;
          ch = commentWhitespace();
          if (ch != '=' || !str_eq2(pos + 1, '=', '=')) break;
          pos += 3;
          ch = commentWhitespace();
          if (ch != '"' && ch != '\'') break;
          quot = ch;
          if (!str_eq10(pos + 1, '_', '_', 'e', 's', 'M', 'o', 'd', 'u', 'l', 'e')) break;
          pos += 11;
          ch = commentWhitespace();
          if (ch != quot) break;
          pos += 1;
          ch = commentWhitespace();
          if (ch != ')') break;
          pos += 1;
          ch = commentWhitespace();
          if (ch != 'r' || !str_eq5(pos + 1, 'e', 't', 'u', 'r', 'n')) break;
          pos += 6;
          ch = commentWhitespace();
          if (ch == ';')
            pos++;
          ch = commentWhitespace();

          // `if (`
          if (ch == 'i' && *(pos + 1) == 'f') {
            bool inIf = true;
            pos += 2;
            ch = commentWhitespace();
            if (ch != '(') break;
            pos++;
            uint16_t *ifInnerPos = pos;

            // `Object.prototype.hasOwnProperty.call(`  IDENTIFIER `, ` IDENTIFIER$2 `)) return` `;`?
            if (tryParseObjectHasOwnProperty(it_id_start, it_id_len)) {
              ch = commentWhitespace();
              if (ch != ')') break;
              pos++;
              ch = commentWhitespace();
              if (ch != 'r' || !str_eq5(pos + 1, 'e', 't', 'u', 'r', 'n')) break;
              pos += 6;
              ch = commentWhitespace();
              if (ch == ';')
                pos++;
              ch = commentWhitespace();
              // match next if
              if (ch == 'i' && *(pos + 1) == 'f') {
                pos += 2;
                ch = commentWhitespace();
                if (ch != '(') break;
                pos++;
              }
              else {
                inIf = false;
              }
            }
            else {
              pos = ifInnerPos;
            }

            // `if (` IDENTIFIER$2 `in` EXPORTS_IDENTIFIER `&&` EXPORTS_IDENTIFIER `[` IDENTIFIER$2 `] ===` IDENTIFIER$1 `[` IDENTIFIER$2 `]) return` `;`?
            if (inIf) {
              if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
              pos += it_id_len;
              ch = commentWhitespace();
              if (ch != 'i' || !str_eq2(pos + 1, 'n', ' ')) break;
              pos += 3;
              ch = commentWhitespace();
              if (!readExportsOrModuleDotExports(ch)) break;
              ch = commentWhitespace();
              if (ch != '&' || *(pos + 1) != '&') break;
              pos += 2;
              ch = commentWhitespace();
              if (!readExportsOrModuleDotExports(ch)) break;
              ch = commentWhitespace();
              if (ch != '[') break;
              pos++;
              ch = commentWhitespace();
              if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
              pos += it_id_len;
              ch = commentWhitespace();
              if (ch != ']') break;
              pos++;
              ch = commentWhitespace();
              if (ch != '=' || !str_eq2(pos + 1, '=', '=')) break;
              pos += 3;
              ch = commentWhitespace();
              if (memcmp(pos, id_start, id_len * sizeof(uint16_t)) != 0) break;
              pos += id_len;
              ch = commentWhitespace();
              if (ch != '[') break;
              pos++;
              ch = commentWhitespace();
              if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
              pos += it_id_len;
              ch = commentWhitespace();
              if (ch != ']') break;
              pos++;
              ch = commentWhitespace();
              if (ch != ')') break;
              pos++;
              ch = commentWhitespace();
              if (ch != 'r' || !str_eq5(pos + 1, 'e', 't', 'u', 'r', 'n')) break;
              pos += 6;
              ch = commentWhitespace();
              if (ch == ';')
                pos++;
              ch = commentWhitespace();
            }
          }
        }
        // `if (` IDENTIFIER$2 `!==` ( `'default'` | `"default"` ) (`&& !` IDENTIFIER `.hasOwnProperty(` IDENTIFIER$2 `)`  )? `)`
        else if (ch == '!') {
          if (!str_eq2(pos + 1, '=', '=')) break;
          pos += 3;
          ch = commentWhitespace();
          if (ch != '"' && ch != '\'') break;
          uint16_t quot = ch;
          if (!str_eq7(pos + 1, 'd', 'e', 'f', 'a', 'u', 'l', 't')) break;
          pos += 8;
          ch = commentWhitespace();
          if (ch != quot) break;
          pos += 1;
          ch = commentWhitespace();
          if (ch == '&') {
            if (*(pos + 1) != '&') break;
            pos += 2;
            ch = commentWhitespace();
            if (ch != '!') break;
            pos += 1;
            ch = commentWhitespace();
            if (ch == 'O' && str_eq6(pos + 1, 'b', 'j', 'e', 'c', 't', '.')) {
              if (!tryParseObjectHasOwnProperty(it_id_start, it_id_len)) break;
            }
            else if (identifier(ch)) {
              ch = commentWhitespace();
              if (ch != '.') break;
              pos++;
              ch = commentWhitespace();
              if (ch != 'h' || !str_eq13(pos + 1, 'a', 's', 'O', 'w', 'n', 'P', 'r', 'o', 'p', 'e', 'r', 't', 'y')) break;
              pos += 14;
              ch = commentWhitespace();
              if (ch != '(') break;
              pos += 1;
              ch = commentWhitespace();
              if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
              pos += it_id_len;
              ch = commentWhitespace();
              if (ch != ')') break;
              pos += 1;
            }
            ch = commentWhitespace();
          }
          if (ch != ')') break;
          pos += 1;
          ch = commentWhitespace();
        }
        else break;

        // EXPORTS_IDENTIFIER `[` IDENTIFIER$2 `] =` IDENTIFIER$1 `[` IDENTIFIER$2 `]`
        if (readExportsOrModuleDotExports(ch)) {
          ch = commentWhitespace();
          if (ch != '[') break;
          pos++;
          ch = commentWhitespace();
          if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
          pos += it_id_len;
          ch = commentWhitespace();
          if (ch != ']') break;
          pos++;
          ch = commentWhitespace();
          if (ch != '=') break;
          pos++;
          ch = commentWhitespace();
          if (memcmp(pos, id_start, id_len * sizeof(uint16_t)) != 0) break;
          pos += id_len;
          ch = commentWhitespace();
          if (ch != '[') break;
          pos++;
          ch = commentWhitespace();
          if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
          pos += it_id_len;
          ch = commentWhitespace();
          if (ch != ']') break;
          pos++;
          ch = commentWhitespace();
          if (ch == ';') {
            pos++;
            ch = commentWhitespace();
          }
        }
        // `Object.defineProperty(` EXPORTS_IDENTIFIER `, ` IDENTIFIER$2 `, { enumerable: true, get: function () { return ` IDENTIFIER$1 `[` IDENTIFIER$2 `]; } })`
        else if (ch == 'O') {
          if (!str_eq5(pos + 1, 'b', 'j', 'e', 'c', 't')) break;
          pos += 6;
          ch = commentWhitespace();
          if (ch != '.') break;
          pos++;
          ch = commentWhitespace();
          if (ch != 'd' || !str_eq13(pos + 1, 'e', 'f', 'i', 'n', 'e', 'P', 'r', 'o', 'p', 'e', 'r', 't', 'y')) break;
          pos += 14;
          ch = commentWhitespace();
          if (ch != '(') break;
          pos++;
          ch = commentWhitespace();
          if (!readExportsOrModuleDotExports(ch)) break;
          ch = commentWhitespace();
          if (ch != ',') break;
          pos++;
          ch = commentWhitespace();
          if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
          pos += it_id_len;
          ch = commentWhitespace();
          if (ch != ',') break;
          pos++;
          ch = commentWhitespace();
          if (ch != '{') break;
          pos++;
          ch = commentWhitespace();
          if (ch != 'e' || !str_eq9(pos + 1, 'n', 'u', 'm', 'e', 'r', 'a', 'b', 'l', 'e')) break;
          pos += 10;
          ch = commentWhitespace();
          if (ch != ':') break;
          pos++;
          ch = commentWhitespace();
          if (ch != 't' && !str_eq3(pos + 1, 'r', 'u', 'e')) break;
          pos += 4;
          ch = commentWhitespace();
          if (ch != ',') break;
          pos++;
          ch = commentWhitespace();
          if (ch != 'g' || !str_eq2(pos + 1, 'e', 't')) break;
          pos += 3;
          ch = commentWhitespace();
          if (ch == ':') {
            pos++;
            ch = commentWhitespace();
            if (ch != 'f') break;
            if (!str_eq7(pos + 1, 'u', 'n', 'c', 't', 'i', 'o', 'n')) break;
            pos += 8;
            uint16_t* lastPos = pos;
            ch = commentWhitespace();
            if (ch != '(' && (lastPos == pos || !identifier(ch))) break;
            ch = commentWhitespace();
          }
          if (ch != '(') break;
          pos++;
          ch = commentWhitespace();
          if (ch != ')') break;
          pos++;
          ch = commentWhitespace();
          if (ch != '{') break;
          pos++;
          ch = commentWhitespace();
          if (ch != 'r' || !str_eq5(pos + 1, 'e', 't', 'u', 'r', 'n')) break;
          pos += 6;
          ch = commentWhitespace();
          if (memcmp(pos, id_start, id_len * sizeof(uint16_t)) != 0) break;
          pos += id_len;
          ch = commentWhitespace();
          if (ch != '[') break;
          pos++;
          ch = commentWhitespace();
          if (memcmp(pos, it_id_start, it_id_len * sizeof(uint16_t)) != 0) break;
          pos += it_id_len;
          ch = commentWhitespace();
          if (ch != ']') break;
          pos++;
          ch = commentWhitespace();
          if (ch == ';') {
            pos++;
            ch = commentWhitespace();
          }
          if (ch != '}') break;
          pos++;
          ch = commentWhitespace();
          if (ch == ',') {
            pos++;
            ch = commentWhitespace();
          }
          if (ch != '}') break;
          pos++;
          ch = commentWhitespace();
          if (ch != ')') break;
          pos++;
          ch = commentWhitespace();
          if (ch == ';') {
            pos++;
            ch = commentWhitespace();
          }
        }
        else break;

        if (ch != '}') break;
        pos++;
        ch = commentWhitespace();
        if (ch != ')') break;

        // search through export bindings to see if this is a star export
        // this is done last because it is a linear search through all require bindings
        // and by doing this search last after verifying the structure match,
        // we don't have to worry about the slow search algorithm
        StarExportBinding* curCheckBinding = &starExportStack_[0];
        while (curCheckBinding != starExportStack) {
          if (id_len == curCheckBinding->id_end - curCheckBinding->id_start &&
              0 == memcmp(id_start, curCheckBinding->id_start, id_len * sizeof(uint16_t))) {
            addReexport(curCheckBinding->specifier_start, curCheckBinding->specifier_end);
            pos = revertPos;
            return;
          }
          curCheckBinding++;
        }
        return;
      }
    }
  }
  pos = revertPos;
}

bool readExportsOrModuleDotExports (uint16_t ch) {
  uint16_t* revertPos = pos;
  if (ch == 'm' && str_eq5(pos + 1, 'o', 'd', 'u', 'l', 'e')) {
    pos += 6;
    ch = commentWhitespace();
    if (ch != '.') {
      pos = revertPos;
      return false;
    }
    pos++;
    ch = commentWhitespace();
  }
  if (ch == 'e' && str_eq6(pos + 1, 'x', 'p', 'o', 'r', 't', 's')) {
    pos += 7;
    return true;
  }
  else {
    pos = revertPos;
    return false;
  }
}

void tryParseModuleExportsDotAssign () {
  pos += 6;
  uint16_t* revertPos = pos - 1;
  uint16_t ch = commentWhitespace();
  if (ch == '.') {
    pos++;
    ch = commentWhitespace();
    if (ch == 'e' && str_eq6(pos + 1, 'x', 'p', 'o', 'r', 't', 's')) {
      tryParseExportsDotAssign(true);
      return;
    }
  }
  pos = revertPos;
}

void tryParseExportsDotAssign (bool assign) {
  pos += 7;
  uint16_t* revertPos = pos - 1;
  uint16_t ch = commentWhitespace();
  switch (ch) {
    // exports.asdf
    case '.': {
      pos++;
      ch = commentWhitespace();
      uint16_t* startPos = pos;
      if (identifier(ch)) {
        uint16_t* endPos = pos;
        ch = commentWhitespace();
        if (ch == '=') {
          addExport(startPos, endPos);
          return;
        }
      }
      break;
    }
    // exports['asdf']
    case '[': {
      pos++;
      ch = commentWhitespace();
      if (ch == '\'' || ch == '"') {
        uint16_t* startPos = pos;
        stringLiteral(ch);
        uint16_t* endPos = ++pos;
        ch = commentWhitespace();
        if (ch != ']') break;
        pos++;
        ch = commentWhitespace();
        if (ch != '=') break;
        addExport(startPos, endPos);
      }
      break;
    }
    // module.exports =
    case '=': {
      if (assign) {
        clearReexports();
        pos++;
        ch = commentWhitespace();
        // { ... }
        if (ch == '{') {
          tryParseLiteralExports();
          return;
        }

        // require('...')
        if (ch == 'r')
          tryParseRequire(ExportAssign);
      }
    }
  }
  pos = revertPos;
}

bool tryParseRequire (enum RequireType requireType) {
  uint16_t* revertPos = pos;
  // require('...')
  if (str_eq6(pos + 1, 'e', 'q', 'u', 'i', 'r', 'e')) {
    pos += 7;
    uint16_t ch = commentWhitespace();
    if (ch == '(') {
      pos++;
      ch = commentWhitespace();
      uint16_t* reexportStart = pos;
      if (ch == '\'' || ch == '"') {
        stringLiteral(ch);
        uint16_t* reexportEnd = ++pos;
        ch = commentWhitespace();
        if (ch == ')') {
          switch (requireType) {
            case ExportStar:
              addReexport(reexportStart, reexportEnd);
              return true;
            case ExportAssign:
              addReexport(reexportStart, reexportEnd);
              return true;
            default:
              starExportStack->specifier_start = reexportStart;
              starExportStack->specifier_end = reexportEnd;
              return true;
          }
        }
      }
    }
    pos = revertPos;
  }
  return false;
}

void tryParseLiteralExports () {
  uint16_t* revertPos = pos - 1;
  while (pos++ < end) {
    uint16_t ch = commentWhitespace();
    uint16_t* startPos = pos;
    if (identifier(ch)) {
      uint16_t* endPos = pos;
      ch = commentWhitespace();
      if (ch == ':') {
        pos++;
        ch = commentWhitespace();
        // nothing more complex than identifier expressions for now
        if (!identifier(ch)) {
          pos = revertPos;
          return;
        }
        ch = *pos;
      }
      addExport(startPos, endPos);
    }
    else if (ch == '\'' || ch == '"') {
      uint16_t* startPos = pos;
      stringLiteral(ch);
      uint16_t* endPos = ++pos;
      ch = commentWhitespace();
      if (ch == ':') {
        pos++;
        ch = commentWhitespace();
        // nothing more complex than identifier expressions for now
        if (!identifier(ch)) {
          pos = revertPos;
          return;
        }
        ch = *pos;
        addExport(startPos, endPos);
      }
    }
    else if (ch == '.' && str_eq2(pos + 1, '.', '.')) {
      pos += 3;
      if (*pos == 'r' && tryParseRequire(ExportAssign)) {
        pos++;
      }
      else if (!identifier(*pos)) {
        pos = revertPos;
        return;
      }
      ch = commentWhitespace();
    }
    else {
      pos = revertPos;
      return;
    }

    if (ch == '}')
      return;

    if (ch != ',') {
      pos = revertPos;
      return;
    }
  }
}

// Identifier detection, ported from Acorn
// ## Character categories

// Big ugly regular expressions that match characters in the
// whitespace, identifier, and identifier-start categories. These
// are only applied when a character is found to actually have a
// code point above 128.
// Generated by `bin/generate-identifier-regex.js`.

bool isNonASCIIidentifierStartChar (uint32_t ch) {
  return ch == 0xaa || ch == 0xb5 || ch == 0xba || ch >= 0xc0 && ch <= 0xd6 || ch >= 0xd8 && ch <= 0xf6 || ch >= 0xf8 && ch <= 0x02c1 ||
      ch >= 0x02c6 && ch <= 0x02d1 || ch >= 0x02e0 && ch <= 0x02e4 || ch == 0x02ec || ch == 0x02ee || ch >= 0x0370 && ch <= 0x0374 || ch == 0x0376 || ch == 0x0377 || ch >= 0x037a && ch <= 0x037d || ch == 0x037f || ch == 0x0386 || ch >= 0x0388 && ch <= 0x038a || ch == 0x038c || ch >= 0x038e && ch <= 0x03a1 || ch >= 0x03a3 && ch <= 0x03f5 || ch >= 0x03f7 && ch <= 0x0481 || ch >= 0x048a && ch <= 0x052f || ch >= 0x0531 && ch <= 0x0556 || ch == 0x0559 || ch >= 0x0560 && ch <= 0x0588 || ch >= 0x05d0 && ch <= 0x05ea || ch >= 0x05ef && ch <= 0x05f2 || ch >= 0x0620 && ch <= 0x064a || ch == 0x066e || ch == 0x066f || ch >= 0x0671 && ch <= 0x06d3 || ch == 0x06d5 || ch == 0x06e5 || ch == 0x06e6 || ch == 0x06ee || ch == 0x06ef || ch >= 0x06fa && ch <= 0x06fc || ch == 0x06ff || ch == 0x0710 || ch >= 0x0712 && ch <= 0x072f || ch >= 0x074d && ch <= 0x07a5 || ch == 0x07b1 || ch >= 0x07ca && ch <= 0x07ea || ch == 0x07f4 || ch == 0x07f5 || ch == 0x07fa || ch >= 0x0800 && ch <= 0x0815 || ch == 0x081a || ch == 0x0824 || ch == 0x0828 || ch >= 0x0840 && ch <= 0x0858 || ch >= 0x0860 && ch <= 0x086a || ch >= 0x08a0 && ch <= 0x08b4 || ch >= 0x08b6 && ch <= 0x08bd || ch >= 0x0904 && ch <= 0x0939 || ch == 0x093d || ch == 0x0950 || ch >= 0x0958 && ch <= 0x0961 || ch >= 0x0971 && ch <= 0x0980 || ch >= 0x0985 && ch <= 0x098c || ch == 0x098f || ch == 0x0990 || ch >= 0x0993 && ch <= 0x09a8 || ch >= 0x09aa && ch <= 0x09b0 || ch == 0x09b2 || ch >= 0x09b6 && ch <= 0x09b9 || ch == 0x09bd || ch == 0x09ce || ch == 0x09dc || ch == 0x09dd || ch >= 0x09df && ch <= 0x09e1 || ch == 0x09f0 || ch == 0x09f1 || ch == 0x09fc || ch >= 0x0a05 && ch <= 0x0a0a || ch == 0x0a0f || ch == 0x0a10 || ch >= 0x0a13 && ch <= 0x0a28 || ch >= 0x0a2a && ch <= 0x0a30 || ch == 0x0a32 || ch == 0x0a33 || ch == 0x0a35 || ch == 0x0a36 || ch == 0x0a38 || ch == 0x0a39 || ch >= 0x0a59 && ch <= 0x0a5c || ch == 0x0a5e || ch >= 0x0a72 && ch <= 0x0a74 || ch >= 0x0a85 && ch <= 0x0a8d || ch >= 0x0a8f && ch <= 0x0a91 || ch >= 0x0a93 && ch <= 0x0aa8 || ch >= 0x0aaa && ch <= 0x0ab0 || ch == 0x0ab2 || ch == 0x0ab3 || ch >= 0x0ab5 && ch <= 0x0ab9 || ch == 0x0abd || ch == 0x0ad0 || ch == 0x0ae0 || ch == 0x0ae1 || ch == 0x0af9 || ch >= 0x0b05 && ch <= 0x0b0c || ch == 0x0b0f || ch == 0x0b10 || ch >= 0x0b13 && ch <= 0x0b28 || ch >= 0x0b2a && ch <= 0x0b30 || ch == 0x0b32 || ch == 0x0b33 || ch >= 0x0b35 && ch <= 0x0b39 || ch == 0x0b3d || ch == 0x0b5c || ch == 0x0b5d || ch >= 0x0b5f && ch <= 0x0b61 || ch == 0x0b71 || ch == 0x0b83 || ch >= 0x0b85 && ch <= 0x0b8a || ch >= 0x0b8e && ch <= 0x0b90 || ch >= 0x0b92 && ch <= 0x0b95 || ch == 0x0b99 || ch == 0x0b9a || ch == 0x0b9c || ch == 0x0b9e || ch == 0x0b9f || ch == 0x0ba3 || ch == 0x0ba4 || ch >= 0x0ba8 && ch <= 0x0baa || ch >= 0x0bae && ch <= 0x0bb9 || ch == 0x0bd0 || ch >= 0x0c05 && ch <= 0x0c0c || ch >= 0x0c0e && ch <= 0x0c10 || ch >= 0x0c12 && ch <= 0x0c28 || ch >= 0x0c2a && ch <= 0x0c39 || ch == 0x0c3d || ch >= 0x0c58 && ch <= 0x0c5a || ch == 0x0c60 || ch == 0x0c61 || ch == 0x0c80 || ch >= 0x0c85 && ch <= 0x0c8c || ch >= 0x0c8e && ch <= 0x0c90 || ch >= 0x0c92 && ch <= 0x0ca8 || ch >= 0x0caa && ch <= 0x0cb3 || ch >= 0x0cb5 && ch <= 0x0cb9 || ch == 0x0cbd || ch == 0x0cde || ch == 0x0ce0 || ch == 0x0ce1 || ch == 0x0cf1 || ch == 0x0cf2 || ch >= 0x0d05 && ch <= 0x0d0c || ch >= 0x0d0e && ch <= 0x0d10 || ch >= 0x0d12 && ch <= 0x0d3a || ch == 0x0d3d || ch == 0x0d4e || ch >= 0x0d54 && ch <= 0x0d56 || ch >= 0x0d5f && ch <= 0x0d61 || ch >= 0x0d7a && ch <= 0x0d7f || ch >= 0x0d85 && ch <= 0x0d96 || ch >= 0x0d9a && ch <= 0x0db1 || ch >= 0x0db3 && ch <= 0x0dbb || ch == 0x0dbd || ch >= 0x0dc0 && ch <= 0x0dc6 || ch >= 0x0e01 && ch <= 0x0e30 || ch == 0x0e32 || ch == 0x0e33 || ch >= 0x0e40 && ch <= 0x0e46 || ch == 0x0e81 || ch == 0x0e82 || ch == 0x0e84 || ch == 0x0e87 || ch == 0x0e88 || ch == 0x0e8a || ch == 0x0e8d || ch >= 0x0e94 && ch <= 0x0e97 || ch >= 0x0e99 && ch <= 0x0e9f || ch >= 0x0ea1 && ch <= 0x0ea3 || ch == 0x0ea5 || ch == 0x0ea7 || ch == 0x0eaa || ch == 0x0eab || ch >= 0x0ead && ch <= 0x0eb0 || ch == 0x0eb2 || ch == 0x0eb3 || ch == 0x0ebd || ch >= 0x0ec0 && ch <= 0x0ec4 || ch == 0x0ec6 || ch >= 0x0edc && ch <= 0x0edf || ch == 0x0f00 || ch >= 0x0f40 && ch <= 0x0f47 || ch >= 0x0f49 && ch <= 0x0f6c || ch >= 0x0f88 && ch <= 0x0f8c || ch >= 0x1000 && ch <= 0x102a || ch == 0x103f || ch >= 0x1050 && ch <= 0x1055 || ch >= 0x105a && ch <= 0x105d || ch == 0x1061 || ch == 0x1065 || ch == 0x1066 || ch >= 0x106e && ch <= 0x1070 || ch >= 0x1075 && ch <= 0x1081 || ch == 0x108e || ch >= 0x10a0 && ch <= 0x10c5 || ch == 0x10c7 || ch == 0x10cd || ch >= 0x10d0 && ch <= 0x10fa || ch >= 0x10fc && ch <= 0x1248 || ch >= 0x124a && ch <= 0x124d || ch >= 0x1250 && ch <= 0x1256 || ch == 0x1258 || ch >= 0x125a && ch <= 0x125d || ch >= 0x1260 && ch <= 0x1288 || ch >= 0x128a && ch <= 0x128d || ch >= 0x1290 && ch <= 0x12b0 || ch >= 0x12b2 && ch <= 0x12b5 || ch >= 0x12b8 && ch <= 0x12be || ch == 0x12c0 || ch >= 0x12c2 && ch <= 0x12c5 || ch >= 0x12c8 && ch <= 0x12d6 || ch >= 0x12d8 && ch <= 0x1310 || ch >= 0x1312 && ch <= 0x1315 || ch >= 0x1318 && ch <= 0x135a || ch >= 0x1380 && ch <= 0x138f || ch >= 0x13a0 && ch <= 0x13f5 || ch >= 0x13f8 && ch <= 0x13fd || ch >= 0x1401 && ch <= 0x166c || ch >= 0x166f && ch <= 0x167f || ch >= 0x1681 && ch <= 0x169a || ch >= 0x16a0 && ch <= 0x16ea || ch >= 0x16ee && ch <= 0x16f8 || ch >= 0x1700 && ch <= 0x170c || ch >= 0x170e && ch <= 0x1711 || ch >= 0x1720 && ch <= 0x1731 || ch >= 0x1740 && ch <= 0x1751 || ch >= 0x1760 && ch <= 0x176c || ch >= 0x176e && ch <= 0x1770 || ch >= 0x1780 && ch <= 0x17b3 || ch == 0x17d7 || ch == 0x17dc || ch >= 0x1820 && ch <= 0x1878 || ch >= 0x1880 && ch <= 0x18a8 || ch == 0x18aa || ch >= 0x18b0 && ch <= 0x18f5 || ch >= 0x1900 && ch <= 0x191e || ch >= 0x1950 && ch <= 0x196d || ch >= 0x1970 && ch <= 0x1974 || ch >= 0x1980 && ch <= 0x19ab || ch >= 0x19b0 && ch <= 0x19c9 || ch >= 0x1a00 && ch <= 0x1a16 || ch >= 0x1a20 && ch <= 0x1a54 || ch == 0x1aa7 || ch >= 0x1b05 && ch <= 0x1b33 || ch >= 0x1b45 && ch <= 0x1b4b || ch >= 0x1b83 && ch <= 0x1ba0 || ch == 0x1bae || ch == 0x1baf || ch >= 0x1bba && ch <= 0x1be5 || ch >= 0x1c00 && ch <= 0x1c23 || ch >= 0x1c4d && ch <= 0x1c4f || ch >= 0x1c5a && ch <= 0x1c7d || ch >= 0x1c80 && ch <= 0x1c88 || ch >= 0x1c90 && ch <= 0x1cba || ch >= 0x1cbd && ch <= 0x1cbf || ch >= 0x1ce9 && ch <= 0x1cec || ch >= 0x1cee && ch <= 0x1cf1 || ch == 0x1cf5 || ch == 0x1cf6 || ch >= 0x1d00 && ch <= 0x1dbf || ch >= 0x1e00 && ch <= 0x1f15 || ch >= 0x1f18 && ch <= 0x1f1d || ch >= 0x1f20 && ch <= 0x1f45 || ch >= 0x1f48 && ch <= 0x1f4d || ch >= 0x1f50 && ch <= 0x1f57 || ch == 0x1f59 || ch == 0x1f5b || ch == 0x1f5d || ch >= 0x1f5f && ch <= 0x1f7d || ch >= 0x1f80 && ch <= 0x1fb4 || ch >= 0x1fb6 && ch <= 0x1fbc || ch == 0x1fbe || ch >= 0x1fc2 && ch <= 0x1fc4 || ch >= 0x1fc6 && ch <= 0x1fcc || ch >= 0x1fd0 && ch <= 0x1fd3 || ch >= 0x1fd6 && ch <= 0x1fdb || ch >= 0x1fe0 && ch <= 0x1fec || ch >= 0x1ff2 && ch <= 0x1ff4 || ch >= 0x1ff6 && ch <= 0x1ffc || ch == 0x2071 || ch == 0x207f || ch >= 0x2090 && ch <= 0x209c || ch == 0x2102 || ch == 0x2107 || ch >= 0x210a && ch <= 0x2113 || ch == 0x2115 || ch >= 0x2118 && ch <= 0x211d || ch == 0x2124 || ch == 0x2126 || ch == 0x2128 || ch >= 0x212a && ch <= 0x2139 || ch >= 0x213c && ch <= 0x213f || ch >= 0x2145 && ch <= 0x2149 || ch == 0x214e || ch >= 0x2160 && ch <= 0x2188 || ch >= 0x2c00 && ch <= 0x2c2e || ch >= 0x2c30 && ch <= 0x2c5e || ch >= 0x2c60 && ch <= 0x2ce4 || ch >= 0x2ceb && ch <= 0x2cee || ch == 0x2cf2 || ch == 0x2cf3 || ch >= 0x2d00 && ch <= 0x2d25 || ch == 0x2d27 || ch == 0x2d2d || ch >= 0x2d30 && ch <= 0x2d67 || ch == 0x2d6f || ch >= 0x2d80 && ch <= 0x2d96 || ch >= 0x2da0 && ch <= 0x2da6 || ch >= 0x2da8 && ch <= 0x2dae || ch >= 0x2db0 && ch <= 0x2db6 || ch >= 0x2db8 && ch <= 0x2dbe || ch >= 0x2dc0 && ch <= 0x2dc6 || ch >= 0x2dc8 && ch <= 0x2dce || ch >= 0x2dd0 && ch <= 0x2dd6 || ch >= 0x2dd8 && ch <= 0x2dde || ch >= 0x3005 && ch <= 0x3007 || ch >= 0x3021 && ch <= 0x3029 || ch >= 0x3031 && ch <= 0x3035 || ch >= 0x3038 && ch <= 0x303c || ch >= 0x3041 && ch <= 0x3096 || ch >= 0x309b && ch <= 0x309f || ch >= 0x30a1 && ch <= 0x30fa || ch >= 0x30fc && ch <= 0x30ff || ch >= 0x3105 && ch <= 0x312f || ch >= 0x3131 && ch <= 0x318e || ch >= 0x31a0 && ch <= 0x31ba || ch >= 0x31f0 && ch <= 0x31ff || ch >= 0x3400 && ch <= 0x4db5 || ch >= 0x4e00 && ch <= 0x9fef || ch >= 0xa000 && ch <= 0xa48c || ch >= 0xa4d0 && ch <= 0xa4fd || ch >= 0xa500 && ch <= 0xa60c || ch >= 0xa610 && ch <= 0xa61f || ch == 0xa62a || ch == 0xa62b || ch >= 0xa640 && ch <= 0xa66e || ch >= 0xa67f && ch <= 0xa69d || ch >= 0xa6a0 && ch <= 0xa6ef || ch >= 0xa717 && ch <= 0xa71f || ch >= 0xa722 && ch <= 0xa788 || ch >= 0xa78b && ch <= 0xa7b9 || ch >= 0xa7f7 && ch <= 0xa801 || ch >= 0xa803 && ch <= 0xa805 || ch >= 0xa807 && ch <= 0xa80a || ch >= 0xa80c && ch <= 0xa822 || ch >= 0xa840 && ch <= 0xa873 || ch >= 0xa882 && ch <= 0xa8b3 || ch >= 0xa8f2 && ch <= 0xa8f7 || ch == 0xa8fb || ch == 0xa8fd || ch == 0xa8fe || ch >= 0xa90a && ch <= 0xa925 || ch >= 0xa930 && ch <= 0xa946 || ch >= 0xa960 && ch <= 0xa97c || ch >= 0xa984 && ch <= 0xa9b2 || ch == 0xa9cf || ch >= 0xa9e0 && ch <= 0xa9e4 || ch >= 0xa9e6 && ch <= 0xa9ef || ch >= 0xa9fa && ch <= 0xa9fe || ch >= 0xaa00 && ch <= 0xaa28 || ch >= 0xaa40 && ch <= 0xaa42 || ch >= 0xaa44 && ch <= 0xaa4b || ch >= 0xaa60 && ch <= 0xaa76 || ch == 0xaa7a || ch >= 0xaa7e && ch <= 0xaaaf || ch == 0xaab1 || ch == 0xaab5 || ch == 0xaab6 || ch >= 0xaab9 && ch <= 0xaabd || ch == 0xaac0 || ch == 0xaac2 || ch >= 0xaadb && ch <= 0xaadd || ch >= 0xaae0 && ch <= 0xaaea || ch >= 0xaaf2 && ch <= 0xaaf4 || ch >= 0xab01 && ch <= 0xab06 || ch >= 0xab09 && ch <= 0xab0e || ch >= 0xab11 && ch <= 0xab16 || ch >= 0xab20 && ch <= 0xab26 || ch >= 0xab28 && ch <= 0xab2e ||
      ch >= 0xab30 && ch <= 0xab5a || ch >= 0xab5c && ch <= 0xab65 || ch >= 0xab70 && ch <= 0xabe2 || ch >= 0xac00 && ch <= 0xd7a3 || ch >= 0xd7b0 && ch <= 0xd7c6 || ch >= 0xd7cb && ch <= 0xd7fb || ch >= 0xf900 && ch <= 0xfa6d || ch >= 0xfa70 && ch <= 0xfad9 || ch >= 0xfb00 && ch <= 0xfb06 || ch >= 0xfb13 && ch <= 0xfb17 || ch == 0xfb1d || ch >= 0xfb1f && ch <= 0xfb28 || ch >= 0xfb2a && ch <= 0xfb36 || ch >= 0xfb38 && ch <= 0xfb3c || ch == 0xfb3e || ch == 0xfb40 || ch == 0xfb41 || ch == 0xfb43 || ch == 0xfb44 || ch >= 0xfb46 && ch <= 0xfbb1 || ch >= 0xfbd3 && ch <= 0xfd3d || ch >= 0xfd50 && ch <= 0xfd8f || ch >= 0xfd92 && ch <= 0xfdc7 || ch >= 0xfdf0 && ch <= 0xfdfb || ch >= 0xfe70 && ch <= 0xfe74 || ch >= 0xfe76 && ch <= 0xfefc || ch >= 0xff21 && ch <= 0xff3a || ch >= 0xff41 && ch <= 0xff5a || ch >= 0xff66 && ch <= 0xffbe || ch >= 0xffc2 && ch <= 0xffc7 || ch >= 0xffca && ch <= 0xffcf || ch >= 0xffd2 && ch <= 0xffd7 || ch >= 0xffda && ch <= 0xffdc;
}

bool isNonASCIIidentifierChar (uint32_t ch) {
  return isNonASCIIidentifierStartChar(ch) || ch == 0x200c || ch == 0x200d || ch == 0xb7 || ch >= 0x0300 && ch <= 0x036f || ch == 0x0387 || ch >= 0x0483 && ch <= 0x0487 || ch >= 0x0591 && ch <= 0x05bd || ch == 0x05bf || ch == 0x05c1 || ch == 0x05c2 || ch == 0x05c4 || ch == 0x05c5 || ch == 0x05c7 || ch >= 0x0610 && ch <= 0x061a || ch >= 0x064b && ch <= 0x0669 || ch == 0x0670 || ch >= 0x06d6 && ch <= 0x06dc || ch >= 0x06df && ch <= 0x06e4 || ch == 0x06e7 || ch == 0x06e8 || ch >= 0x06ea && ch <= 0x06ed || ch >= 0x06f0 && ch <= 0x06f9 || ch == 0x0711 || ch >= 0x0730 && ch <= 0x074a || ch >= 0x07a6 && ch <= 0x07b0 || ch >= 0x07c0 && ch <= 0x07c9 || ch >= 0x07eb && ch <= 0x07f3 || ch == 0x07fd || ch >= 0x0816 && ch <= 0x0819 || ch >= 0x081b && ch <= 0x0823 || ch >= 0x0825 && ch <= 0x0827 || ch >= 0x0829 && ch <= 0x082d || ch >= 0x0859 && ch <= 0x085b || ch >= 0x08d3 && ch <= 0x08e1 || ch >= 0x08e3 && ch <= 0x0903 || ch >= 0x093a && ch <= 0x093c || ch >= 0x093e && ch <= 0x094f || ch >= 0x0951 && ch <= 0x0957 || ch == 0x0962 || ch == 0x0963 || ch >= 0x0966 && ch <= 0x096f || ch >= 0x0981 && ch <= 0x0983 || ch == 0x09bc || ch >= 0x09be && ch <= 0x09c4 || ch == 0x09c7 || ch == 0x09c8 || ch >= 0x09cb && ch <= 0x09cd || ch == 0x09d7 || ch == 0x09e2 || ch == 0x09e3 || ch >= 0x09e6 && ch <= 0x09ef || ch == 0x09fe || ch >= 0x0a01 && ch <= 0x0a03 || ch == 0x0a3c || ch >= 0x0a3e && ch <= 0x0a42 || ch == 0x0a47 || ch == 0x0a48 || ch >= 0x0a4b && ch <= 0x0a4d || ch == 0x0a51 || ch >= 0x0a66 && ch <= 0x0a71 || ch == 0x0a75 || ch >= 0x0a81 && ch <= 0x0a83 || ch == 0x0abc || ch >= 0x0abe && ch <= 0x0ac5 || ch >= 0x0ac7 && ch <= 0x0ac9 || ch >= 0x0acb && ch <= 0x0acd || ch == 0x0ae2 || ch == 0x0ae3 || ch >= 0x0ae6 && ch <= 0x0aef || ch >= 0x0afa && ch <= 0x0aff || ch >= 0x0b01 && ch <= 0x0b03 || ch == 0x0b3c || ch >= 0x0b3e && ch <= 0x0b44 || ch == 0x0b47 || ch == 0x0b48 || ch >= 0x0b4b && ch <= 0x0b4d || ch == 0x0b56 || ch == 0x0b57 || ch == 0x0b62 || ch == 0x0b63 || ch >= 0x0b66 && ch <= 0x0b6f || ch == 0x0b82 || ch >= 0x0bbe && ch <= 0x0bc2 || ch >= 0x0bc6 && ch <= 0x0bc8 || ch >= 0x0bca && ch <= 0x0bcd || ch == 0x0bd7 || ch >= 0x0be6 && ch <= 0x0bef || ch >= 0x0c00 && ch <= 0x0c04 || ch >= 0x0c3e && ch <= 0x0c44 || ch >= 0x0c46 && ch <= 0x0c48 || ch >= 0x0c4a && ch <= 0x0c4d || ch == 0x0c55 || ch == 0x0c56 || ch == 0x0c62 || ch == 0x0c63 || ch >= 0x0c66 && ch <= 0x0c6f || ch >= 0x0c81 && ch <= 0x0c83 || ch == 0x0cbc || ch >= 0x0cbe && ch <= 0x0cc4 || ch >= 0x0cc6 && ch <= 0x0cc8 || ch >= 0x0cca && ch <= 0x0ccd || ch == 0x0cd5 || ch == 0x0cd6 || ch == 0x0ce2 || ch == 0x0ce3 || ch >= 0x0ce6 && ch <= 0x0cef || ch >= 0x0d00 && ch <= 0x0d03 || ch == 0x0d3b || ch == 0x0d3c || ch >= 0x0d3e && ch <= 0x0d44 || ch >= 0x0d46 && ch <= 0x0d48 || ch >= 0x0d4a && ch <= 0x0d4d || ch == 0x0d57 || ch == 0x0d62 || ch == 0x0d63 || ch >= 0x0d66 && ch <= 0x0d6f || ch == 0x0d82 || ch == 0x0d83 || ch == 0x0dca || ch >= 0x0dcf && ch <= 0x0dd4 || ch == 0x0dd6 || ch >= 0x0dd8 && ch <= 0x0ddf || ch >= 0x0de6 && ch <= 0x0def || ch == 0x0df2 || ch == 0x0df3 || ch == 0x0e31 || ch >= 0x0e34 && ch <= 0x0e3a || ch >= 0x0e47 && ch <= 0x0e4e || ch >= 0x0e50 && ch <= 0x0e59 || ch == 0x0eb1 || ch >= 0x0eb4 && ch <= 0x0eb9 || ch == 0x0ebb || ch == 0x0ebc || ch >= 0x0ec8 && ch <= 0x0ecd || ch >= 0x0ed0 && ch <= 0x0ed9 || ch == 0x0f18 || ch == 0x0f19 || ch >= 0x0f20 && ch <= 0x0f29 || ch == 0x0f35 || ch == 0x0f37 || ch == 0x0f39 || ch == 0x0f3e || ch == 0x0f3f || ch >= 0x0f71 && ch <= 0x0f84 || ch == 0x0f86 || ch == 0x0f87 || ch >= 0x0f8d && ch <= 0x0f97 || ch >= 0x0f99 && ch <= 0x0fbc || ch == 0x0fc6 || ch >= 0x102b && ch <= 0x103e || ch >= 0x1040 && ch <= 0x1049 || ch >= 0x1056 && ch <= 0x1059 || ch >= 0x105e && ch <= 0x1060 || ch >= 0x1062 && ch <= 0x1064 || ch >= 0x1067 && ch <= 0x106d || ch >= 0x1071 && ch <= 0x1074 || ch >= 0x1082 && ch <= 0x108d || ch >= 0x108f && ch <= 0x109d || ch >= 0x135d && ch <= 0x135f || ch >= 0x1369 && ch <= 0x1371 || ch >= 0x1712 && ch <= 0x1714 || ch >= 0x1732 && ch <= 0x1734 || ch == 0x1752 || ch == 0x1753 || ch == 0x1772 || ch == 0x1773 || ch >= 0x17b4 && ch <= 0x17d3 || ch == 0x17dd || ch >= 0x17e0 && ch <= 0x17e9 || ch >= 0x180b && ch <= 0x180d || ch >= 0x1810 && ch <= 0x1819 || ch == 0x18a9 || ch >= 0x1920 && ch <= 0x192b || ch >= 0x1930 && ch <= 0x193b || ch >= 0x1946 && ch <= 0x194f || ch >= 0x19d0 && ch <= 0x19da || ch >= 0x1a17 && ch <= 0x1a1b || ch >= 0x1a55 && ch <= 0x1a5e || ch >= 0x1a60 && ch <= 0x1a7c || ch >= 0x1a7f && ch <= 0x1a89 || ch >= 0x1a90 && ch <= 0x1a99 || ch >= 0x1ab0 && ch <= 0x1abd || ch >= 0x1b00 && ch <= 0x1b04 || ch >= 0x1b34 && ch <= 0x1b44 || ch >= 0x1b50 && ch <= 0x1b59 || ch >= 0x1b6b && ch <= 0x1b73 || ch >= 0x1b80 && ch <= 0x1b82 || ch >= 0x1ba1 && ch <= 0x1bad || ch >= 0x1bb0 && ch <= 0x1bb9 || ch >= 0x1be6 && ch <= 0x1bf3 || ch >= 0x1c24 && ch <= 0x1c37 || ch >= 0x1c40 && ch <= 0x1c49 || ch >= 0x1c50 && ch <= 0x1c59 || ch >= 0x1cd0 && ch <= 0x1cd2 || ch >= 0x1cd4 && ch <= 0x1ce8 || ch == 0x1ced || ch >= 0x1cf2 && ch <= 0x1cf4 || ch >= 0x1cf7 && ch <= 0x1cf9 || ch >= 0x1dc0 && ch <= 0x1df9 || ch >= 0x1dfb && ch <= 0x1dff || ch == 0x203f || ch == 0x2040 || ch == 0x2054 || ch >= 0x20d0 && ch <= 0x20dc || ch == 0x20e1 || ch >= 0x20e5 && ch <= 0x20f0 || ch >= 0x2cef && ch <= 0x2cf1 || ch == 0x2d7f || ch >= 0x2de0 && ch <= 0x2dff || ch >= 0x302a && ch <= 0x302f || ch == 0x3099 || ch == 0x309a || ch >= 0xa620 && ch <= 0xa629 || ch == 0xa66f || ch >= 0xa674 && ch <= 0xa67d || ch == 0xa69e || ch == 0xa69f || ch == 0xa6f0 || ch == 0xa6f1 || ch == 0xa802 || ch == 0xa806 || ch == 0xa80b || ch >= 0xa823 && ch <= 0xa827 || ch == 0xa880 || ch == 0xa881 || ch >= 0xa8b4 && ch <= 0xa8c5 || ch >= 0xa8d0 && ch <= 0xa8d9 || ch >= 0xa8e0 && ch <= 0xa8f1 || ch >= 0xa8ff && ch <= 0xa909 || ch >= 0xa926 && ch <= 0xa92d || ch >= 0xa947 && ch <= 0xa953 || ch >= 0xa980 && ch <= 0xa983 || ch >= 0xa9b3 && ch <= 0xa9c0 || ch >= 0xa9d0 && ch <= 0xa9d9 || ch == 0xa9e5 || ch >= 0xa9f0 && ch <= 0xa9f9 || ch >= 0xaa29 && ch <= 0xaa36 || ch == 0xaa43 || ch == 0xaa4c || ch == 0xaa4d || ch >= 0xaa50 && ch <= 0xaa59 || ch >= 0xaa7b && ch <= 0xaa7d || ch == 0xaab0 || ch >= 0xaab2 && ch <= 0xaab4 || ch == 0xaab7 || ch == 0xaab8 || ch == 0xaabe || ch == 0xaabf || ch == 0xaac1 || ch >= 0xaaeb && ch <= 0xaaef || ch == 0xaaf5 || ch == 0xaaf6 || ch >= 0xabe3 && ch <= 0xabea || ch == 0xabec || ch == 0xabed || ch >= 0xabf0 && ch <= 0xabf9 || ch == 0xfb1e || ch >= 0xfe00 && ch <= 0xfe0f || ch >= 0xfe20 && ch <= 0xfe2f || ch == 0xfe33 || ch == 0xfe34 || ch >= 0xfe4d && ch <= 0xfe4f || ch >= 0xff10 && ch <= 0xff19 || ch == 0xff3f;
}

// These are a run-length and offset encoded representation of the
// >0xffff code points that are a valid part of identifiers. The
// offset starts at 0x10000, and each pair of numbers represents an
// offset to the next range, and then a size of the range. They were
// generated by bin/generate-identifier-regex.js

static uint32_t astralIdentifierStartCodes[] = { 0,11,2,25,2,18,2,1,2,14,3,13,35,122,70,52,268,28,4,48,48,31,14,29,6,37,11,29,3,35,5,7,2,4,43,157,19,35,5,35,5,39,9,51,157,310,10,21,11,7,153,5,3,0,2,43,2,1,4,0,3,22,11,22,10,30,66,18,2,1,11,21,11,25,71,55,7,1,65,0,16,3,2,2,2,28,43,28,4,28,36,7,2,27,28,53,11,21,11,18,14,17,111,72,56,50,14,50,14,35,349,41,7,1,79,28,11,0,9,21,107,20,28,22,13,52,76,44,33,24,27,35,30,0,3,0,9,34,4,0,13,47,15,3,22,0,2,0,36,17,2,24,85,6,2,0,2,3,2,14,2,9,8,46,39,7,3,1,3,21,2,6,2,1,2,4,4,0,19,0,13,4,159,52,19,3,21,2,31,47,21,1,2,0,185,46,42,3,37,47,21,0,60,42,14,0,72,26,230,43,117,63,32,7,3,0,3,7,2,1,2,23,16,0,2,0,95,7,3,38,17,0,2,0,29,0,11,39,8,0,22,0,12,45,20,0,35,56,264,8,2,36,18,0,50,29,113,6,2,1,2,37,22,0,26,5,2,1,2,31,15,0,328,18,190,0,80,921,103,110,18,195,2749,1070,4050,582,8634,568,8,30,114,29,19,47,17,3,32,20,6,18,689,63,129,74,6,0,67,12,65,1,2,0,29,6135,9,1237,43,8,8952,286,50,2,18,3,9,395,2309,106,6,12,4,8,8,9,5991,84,2,70,2,1,3,0,3,1,3,3,2,11,2,0,2,6,2,64,2,3,3,7,2,6,2,27,2,3,2,4,2,0,4,6,2,339,3,24,2,24,2,30,2,24,2,30,2,24,2,30,2,24,2,30,2,24,2,7,2357,44,11,6,17,0,370,43,1301,196,60,67,8,0,1205,3,2,26,2,1,2,0,3,0,2,9,2,3,2,0,2,0,7,0,5,0,2,0,2,0,2,2,2,1,2,0,3,0,2,0,2,0,2,0,2,0,2,1,2,0,3,3,2,6,2,3,2,3,2,0,2,9,2,16,6,2,2,4,2,16,4421,42717,35,4148,12,221,3,5761,15,7472,3104,541,1507,4938 };

static uint32_t astralIdentifierCodes[] = { 509,0,227,0,150,4,294,9,1368,2,2,1,6,3,41,2,5,0,166,1,574,3,9,9,370,1,154,10,176,2,54,14,32,9,16,3,46,10,54,9,7,2,37,13,2,9,6,1,45,0,13,2,49,13,9,3,2,11,83,11,7,0,161,11,6,9,7,3,56,1,2,6,3,1,3,2,10,0,11,1,3,6,4,4,193,17,10,9,5,0,82,19,13,9,214,6,3,8,28,1,83,16,16,9,82,12,9,9,84,14,5,9,243,14,166,9,71,5,2,1,3,3,2,0,2,1,13,9,120,6,3,6,4,0,29,9,41,6,2,3,9,0,10,10,47,15,406,7,2,7,17,9,57,21,2,13,123,5,4,0,2,1,2,6,2,0,9,9,49,4,2,1,2,4,9,9,330,3,19306,9,135,4,60,6,26,9,1014,0,2,54,8,3,82,0,12,1,19628,1,5319,4,4,5,9,7,3,6,31,3,149,2,1418,49,513,54,5,49,9,0,15,0,23,4,2,14,1361,6,2,16,3,6,2,1,2,4,262,6,10,9,419,13,1495,6,110,6,6,9,4759,9,787719,239 };

// This has a complexity linear to the value of the code. The
// assumption is that looking up astral identifier characters is
// rare.
bool isInAstralIdentifierStartCodes (uint32_t code) {
  uint32_t cur = 0x10000;
  for (unsigned int i = 0; i < sizeof(astralIdentifierStartCodes) / sizeof(uint32_t); i += 2) {
    cur += astralIdentifierStartCodes[i];
    if (cur > code)
      return false;
    cur += astralIdentifierStartCodes[i + 1];
    if (cur >= code)
      return true;
  }
  return false;
}

bool isInAstralIdentifierCodes (uint32_t code) {
  uint32_t cur = 0x10000;
  for (unsigned int i = 0; i < sizeof(astralIdentifierCodes) / sizeof(uint32_t); i += 2) {
    cur += astralIdentifierCodes[i];
    if (cur > code)
      return false;
    cur += astralIdentifierCodes[i + 1];
    if (cur >= code)
      return true;
  }
  return false;
}

// Test whether a given character code starts an identifier.
bool isIdentifierStart(uint32_t code) {
  if (code < 65) return code == 36;
  if (code < 91) return true;
  if (code < 97) return code == 95;
  if (code < 123) return true;
  if (code <= 0xffff) return code >= 0xaa && isNonASCIIidentifierStartChar(code);
  return isInAstralIdentifierStartCodes(code);
}

// Test whether a given character is part of an identifier.

bool isIdentifierChar(uint32_t code) {
  if (code < 48) return code == 36;
  if (code < 58) return true;
  if (code < 65) return false;
  if (code < 91) return true;
  if (code < 97) return code == 95;
  if (code < 123) return true;
  if (code <= 0xffff) return code >= 0xaa && isNonASCIIidentifierChar(code);
  return isInAstralIdentifierStartCodes(code) || isInAstralIdentifierCodes(code);
}

inline int charCodeByteLen(uint32_t ch) {
  if (ch < 0x10000) return 1;
  return 2;
}

uint32_t fullCharCodeAtLast(uint16_t* bPos) {
  // Gives the UTF char for backtracking surrogates
  uint16_t ch = *bPos;
  if ((ch & 0xFC00) == 0xDC00)
    return (((*(bPos - 1) & 0x3FF) << 10) | (ch & 0x3FF)) + 0x10000;
  return ch;
}

uint32_t fullCharCode(uint16_t ch) {
  // Read UTF Surrogates if necessary
  if ((ch & 0xFC00) == 0xD800)
    return (((ch & 0x3FF) << 10) | (*(pos + 1) & 0x3FF)) + 0x10000;
  return ch;
}

bool identifier (uint16_t startCh) {
  uint32_t ch = fullCharCode(startCh);
  if (!isIdentifierStart(ch) && ch != '\\')
    return false;
  pos += charCodeByteLen(ch);
  while (ch = fullCharCode(*pos)) {
    if (isIdentifierChar(ch)) {
      pos += charCodeByteLen(ch);
    }
    else if (ch == '\\') {
      // no identifier escapes support for now
      return false;
    }
    else {
      break;
    }
  }
  return true;
}

void throwIfImportStatement () {
  uint16_t* startPos = pos;
  pos += 6;
  uint16_t ch = commentWhitespace();
  switch (ch) {
    // dynamic import
    case '(':
      openTokenPosStack[openTokenDepth++] = startPos;
      return;
    // import.meta
    case '.':
      syntaxError(5);
      return;
    
    default:
      // no space after "import" -> not an import keyword
      if (pos == startPos + 6)
        break;
    case '"':
    case '\'':
    case '{':
    case '*':
      // import statement only permitted at base-level
      if (openTokenDepth != 0) {
        pos--;
        return;
      }
      // import statements are a syntax error in CommonJS
      syntaxError(6);
  }
}

void throwIfExportStatement () {
  pos += 6;
  uint16_t* curPos = pos;
  uint16_t ch = commentWhitespace();
  if (pos == curPos && !isPunctuator(ch))
    return;
  syntaxError(7);
}

uint16_t commentWhitespace () {
  uint16_t ch;
  do {
    ch = *pos;
    if (ch == '/') {
      uint16_t next_ch = *(pos + 1);
      if (next_ch == '/')
        lineComment();
      else if (next_ch == '*')
        blockComment();
      else
        return ch;
    }
    else if (!isBrOrWs(ch)) {
      return ch;
    }
  } while (pos++ < end);
  return ch;
}

void templateString () {
  while (pos++ < end) {
    uint16_t ch = *pos;
    if (ch == '$' && *(pos + 1) == '{') {
      pos++;
      templateStack[templateStackDepth++] = templateDepth;
      templateDepth = ++openTokenDepth;
      return;
    }
    if (ch == '`')
      return;
    if (ch == '\\')
      pos++;
  }
  syntaxError(8);
}

void blockComment () {
  pos++;
  while (pos++ < end) {
    uint16_t ch = *pos;
    if (ch == '*' && *(pos + 1) == '/') {
      pos++;
      return;
    }
  }
}

void lineComment () {
  while (pos++ < end) {
    uint16_t ch = *pos;
    if (ch == '\n' || ch == '\r')
      return;
  }
}

void stringLiteral (uint16_t quote) {
  while (pos++ < end) {
    uint16_t ch = *pos;
    if (ch == quote)
      return;
    if (ch == '\\') {
      ch = *++pos;
      if (ch == '\r' && *(pos + 1) == '\n')
        pos++;
    }
    else if (isBr(ch))
      break;
  }
  syntaxError(9);
}

uint16_t regexCharacterClass () {
  while (pos++ < end) {
    uint16_t ch = *pos;
    if (ch == ']')
      return ch;
    if (ch == '\\')
      pos++;
    else if (ch == '\n' || ch == '\r')
      break;
  }
  syntaxError(10);
  return '\0';
}

void regularExpression () {
  while (pos++ < end) {
    uint16_t ch = *pos;
    if (ch == '/')
      return;
    if (ch == '[')
      ch = regexCharacterClass();
    else if (ch == '\\')
      pos++;
    else if (ch == '\n' || ch == '\r')
      break;
  }
  syntaxError(11);
}

uint16_t readToWsOrPunctuator (uint16_t ch) {
  do {
    if (isBrOrWs(ch) || isPunctuator(ch))
      return ch;
  } while (ch = *(++pos));
  return ch;
}

// Note: non-asii BR and whitespace checks omitted for perf / footprint
// if there is a significant user need this can be reconsidered
bool isBr (uint16_t c) {
  return c == '\r' || c == '\n';
}

bool isBrOrWs (uint16_t c) {
  return c > 8 && c < 14 || c == 32 || c == 160;
}

bool isBrOrWsOrPunctuatorNotDot (uint16_t c) {
  return c > 8 && c < 14 || c == 32 || c == 160 || isPunctuator(c) && c != '.';
}

bool str_eq2 (uint16_t* pos, uint16_t c1, uint16_t c2) {
  return *pos == c1 && *(pos + 1) == c2;
}

bool str_eq3 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3;
}

bool str_eq4 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4;
}

bool str_eq5 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5;
}

bool str_eq6 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5 && *(pos + 5) == c6;
}

bool str_eq7 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6, uint16_t c7) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5 && *(pos + 5) == c6 && *(pos + 6) == c7;
}

bool str_eq8 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6, uint16_t c7, uint16_t c8) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5 && *(pos + 5) == c6 && *(pos + 6) == c7 && *(pos + 7) == c8;
}

bool str_eq9 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6, uint16_t c7, uint16_t c8, uint16_t c9) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5 && *(pos + 5) == c6 && *(pos + 6) == c7 && *(pos + 7) == c8 && *(pos + 8) == c9;
}

bool str_eq10 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6, uint16_t c7, uint16_t c8, uint16_t c9, uint16_t c10) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5 && *(pos + 5) == c6 && *(pos + 6) == c7 && *(pos + 7) == c8 && *(pos + 8) == c9 && *(pos + 9) == c10;
}

bool str_eq13 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6, uint16_t c7, uint16_t c8, uint16_t c9, uint16_t c10, uint16_t c11, uint16_t c12, uint16_t c13) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5 && *(pos + 5) == c6 && *(pos + 6) == c7 && *(pos + 7) == c8 && *(pos + 8) == c9 && *(pos + 9) == c10 && *(pos + 10) == c11 && *(pos + 11) == c12 && *(pos + 12) == c13;
}

bool str_eq18 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6, uint16_t c7, uint16_t c8, uint16_t c9, uint16_t c10, uint16_t c11, uint16_t c12, uint16_t c13, uint16_t c14, uint16_t c15, uint16_t c16, uint16_t c17, uint16_t c18) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5 && *(pos + 5) == c6 && *(pos + 6) == c7 && *(pos + 7) == c8 && *(pos + 8) == c9 && *(pos + 9) == c10 && *(pos + 10) == c11 && *(pos + 11) == c12 && *(pos + 12) == c13 && *(pos + 13) == c14 && *(pos + 14) == c15 && *(pos + 15) == c16 && *(pos + 16) == c17 && *(pos + 17) == c18;
}

bool str_eq22 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6, uint16_t c7, uint16_t c8, uint16_t c9, uint16_t c10, uint16_t c11, uint16_t c12, uint16_t c13, uint16_t c14, uint16_t c15, uint16_t c16, uint16_t c17, uint16_t c18, uint16_t c19, uint16_t c20, uint16_t c21, uint16_t c22) {
  return *pos == c1 && *(pos + 1) == c2 && *(pos + 2) == c3 && *(pos + 3) == c4 && *(pos + 4) == c5 && *(pos + 5) == c6 && *(pos + 6) == c7 && *(pos + 7) == c8 && *(pos + 8) == c9 && *(pos + 9) == c10 && *(pos + 10) == c11 && *(pos + 11) == c12 && *(pos + 12) == c13 && *(pos + 13) == c14 && *(pos + 14) == c15 && *(pos + 15) == c16 && *(pos + 16) == c17 && *(pos + 17) == c18 && *(pos + 18) == c19 && *(pos + 19) == c20 && *(pos + 20) == c21 && *(pos + 21) == c22;
}

bool keywordStart (uint16_t* pos) {
  return pos == source || isBrOrWsOrPunctuatorNotDot(*(pos - 1));
}

bool readPrecedingKeyword1 (uint16_t* pos, uint16_t c1) {
  if (pos < source) return false;
  return *pos == c1 && (pos == source || isBrOrWsOrPunctuatorNotDot(*(pos - 1)));
}
bool readPrecedingKeyword2 (uint16_t* pos, uint16_t c1, uint16_t c2) {
  if (pos - 1 < source) return false;
  return str_eq2(pos - 1, c1, c2) && (pos - 1 == source || isBrOrWsOrPunctuatorNotDot(*(pos - 2)));
}
bool readPrecedingKeyword3 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3) {
  if (pos - 2 < source) return false;
  return str_eq3(pos - 2, c1, c2, c3) && (pos - 2 == source || isBrOrWsOrPunctuatorNotDot(*(pos - 3)));
}
bool readPrecedingKeyword4 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4) {
  if (pos - 3 < source) return false;
  return str_eq4(pos - 3, c1, c2, c3, c4) && (pos - 3 == source || isBrOrWsOrPunctuatorNotDot(*(pos - 4)));
}
bool readPrecedingKeyword5 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5) {
  if (pos - 4 < source) return false;
  return str_eq5(pos - 4, c1, c2, c3, c4, c5) && (pos - 4 == source || isBrOrWsOrPunctuatorNotDot(*(pos - 5)));
}
bool readPrecedingKeyword6 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6) {
  if (pos - 5 < source) return false;
  return str_eq6(pos - 5, c1, c2, c3, c4, c5, c6) && (pos - 5 == source || isBrOrWsOrPunctuatorNotDot(*(pos - 6)));
}
bool readPrecedingKeyword7 (uint16_t* pos, uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t c5, uint16_t c6, uint16_t c7) {
  if (pos - 6 < source) return false;
  return str_eq7(pos - 6, c1, c2, c3, c4, c5, c6, c7) && (pos - 6 == source || isBrOrWsOrPunctuatorNotDot(*(pos - 7)));
}

// Detects one of case, debugger, delete, do, else, in, instanceof, new,
//   return, throw, typeof, void, yield ,await
bool isExpressionKeyword (uint16_t* pos) {
  switch (*pos) {
    case 'd':
      switch (*(pos - 1)) {
        case 'i':
          // void
          return readPrecedingKeyword2(pos - 2, 'v', 'o');
        case 'l':
          // yield
          return readPrecedingKeyword3(pos - 2, 'y', 'i', 'e');
        default:
          return false;
      }
    case 'e':
      switch (*(pos - 1)) {
        case 's':
          switch (*(pos - 2)) {
            case 'l':
              // else
              return readPrecedingKeyword1(pos - 3, 'e');
            case 'a':
              // case
              return readPrecedingKeyword1(pos - 3, 'c');
            default:
              return false;
          }
        case 't':
          // delete
          return readPrecedingKeyword4(pos - 2, 'd', 'e', 'l', 'e');
        default:
          return false;
      }
    case 'f':
      if (*(pos - 1) != 'o' || *(pos - 2) != 'e')
        return false;
      switch (*(pos - 3)) {
        case 'c':
          // instanceof
          return readPrecedingKeyword6(pos - 4, 'i', 'n', 's', 't', 'a', 'n');
        case 'p':
          // typeof
          return readPrecedingKeyword2(pos - 4, 't', 'y');
        default:
          return false;
      }
    case 'n':
      // in, return
      return readPrecedingKeyword1(pos - 1, 'i') || readPrecedingKeyword5(pos - 1, 'r', 'e', 't', 'u', 'r');
    case 'o':
      // do
      return readPrecedingKeyword1(pos - 1, 'd');
    case 'r':
      // debugger
      return readPrecedingKeyword7(pos - 1, 'd', 'e', 'b', 'u', 'g', 'g', 'e');
    case 't':
      // await
      return readPrecedingKeyword4(pos - 1, 'a', 'w', 'a', 'i');
    case 'w':
      switch (*(pos - 1)) {
        case 'e':
          // new
          return readPrecedingKeyword1(pos - 2, 'n');
        case 'o':
          // throw
          return readPrecedingKeyword3(pos - 2, 't', 'h', 'r');
        default:
          return false; 
      }
  }
  return false;
}

bool isParenKeyword (uint16_t* curPos) {
  return readPrecedingKeyword5(curPos, 'w', 'h', 'i', 'l', 'e') ||
      readPrecedingKeyword3(curPos, 'f', 'o', 'r') ||
      readPrecedingKeyword2(curPos, 'i', 'f');
}

bool isPunctuator (uint16_t ch) {
  // 23 possible punctuator endings: !%&()*+,-./:;<=>?[]^{}|~
  return ch == '!' || ch == '%' || ch == '&' ||
    ch > 39 && ch < 48 || ch > 57 && ch < 64 ||
    ch == '[' || ch == ']' || ch == '^' ||
    ch > 122 && ch < 127;
}

bool isExpressionPunctuator (uint16_t ch) {
  // 20 possible expression endings: !%&(*+,-.:;<=>?[^{|~
  return ch == '!' || ch == '%' || ch == '&' ||
    ch > 39 && ch < 47 && ch != 41 || ch > 57 && ch < 64 ||
    ch == '[' || ch == '^' || ch > 122 && ch < 127 && ch != '}';
}

bool isExpressionTerminator (uint16_t* curPos) {
  // detects:
  // > ; ) -1 finally catch
  // as all of these followed by a { will indicate a statement brace
  switch (*curPos) {
    case '>':
      return *(curPos - 1) == '=';
    case ';':
    case ')':
      return true;
    case 'h':
      return readPrecedingKeyword4(curPos - 1, 'c', 'a', 't', 'c');
    case 'y':
      return readPrecedingKeyword6(curPos - 1, 'f', 'i', 'n', 'a', 'l', 'l');
    case 'e':
      return readPrecedingKeyword3(curPos - 1, 'e', 'l', 's');
  }
  return false;
}

void bail (uint32_t err) {
  error = 1;
  parse_error = err;
  pos = end + 1;
}

void syntaxError (uint32_t code) {
  if (error == 0) error = code;
  parse_error = pos - source;
  pos = end + 1;
}
