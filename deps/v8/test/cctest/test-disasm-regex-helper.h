// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CCTEST_DISASM_REGEX_HELPER_H_
#define V8_CCTEST_DISASM_REGEX_HELPER_H_

#include <algorithm>
#include <iostream>
#include <map>
#include <regex>  // NOLINT(build/c++11)
#include <vector>

#include "src/base/logging.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

// This class provides methods for regular expression matching with an extra
// feature of user defined named capture groups which are alive across
// regex search calls.
//
// The main use case for the class is to test multiple-line assembly
// output with an ability to express dataflow or dependencies by allowing single
// definition / multiple use symbols. When processing output lines and trying to
// match them against the set of patterns a user can define a named group - a
// symbol - and a regex for matching it. If the regex with the definitions is
// matched then whenever this symbol appears again (no redefinitions though) in
// the following patterns the parser will replace the symbol reference in the
// pattern by an actual literal value matched during processing symbol
// definition. This effectively checks that all of the output lines have
// the same literal for the described symbol. To track the symbols this class
// implements a simple single-definition symbol table.
//
// Example: Lets consider a case when we want to test that the assembly
// output consists of two instructions - a load and a store; we also want
// to check that the loaded value is used as store value for the store,
// like here:
//
//    ldr x3, [x4]
//    str x3, [x5]
//
// Using special syntax for symbol definitions and uses one could write the
// following regex making sure that the load register is used by the store:
//
//    'ldr <<NamedReg:x[0-9]+>>, [x[0-9]+]'
//    'str <<NamedReg>>, [x[0-9]+]'
//
// See 'ProcessPattern' for more details.
class RegexParser {
 public:
  RegexParser()
      // Regex to parse symbol references: definitions or uses.
      //                  <<SymbolName[:'def regex']>>
      : symbol_ref_regex_("<<([a-zA-Z_][a-zA-Z0-9_]*)(?::(.*?))?>>") {}

  // Status codes used for return values and error diagnostics.
  enum class Status {
    kSuccess = 0,
    kNotMatched,
    kWrongPattern,
    kDefNotFound,
    kRedefinition,
  };

  // This class holds info on a symbol definition.
  class SymbolInfo {
   public:
    explicit SymbolInfo(const std::string& matched_value)
        : matched_value_(matched_value) {}

    // Returns an actual matched value for the symbol.
    const std::string& matched_value() const { return matched_value_; }

   private:
    std::string matched_value_;
  };

  // This class holds temporary info on a symbol while processing an input line.
  class SymbolVectorElem {
   public:
    SymbolVectorElem(bool is_def, const std::string& symbol_name)
        : is_def_(is_def), symbol_name_(symbol_name) {}

    bool is_def() const { return is_def_; }
    const std::string& symbol_name() const { return symbol_name_; }

   private:
    bool is_def_;
    std::string symbol_name_;
  };

  using SymbolMap = std::map<std::string, SymbolInfo>;
  using MatchVector = std::vector<SymbolVectorElem>;

  // Tries to match (actually search, similar to std::regex_serach) the line
  // against the pattern (possibly containing symbols references) and if
  // matched commits symbols definitions from the pattern to the symbol table.
  //
  // Returns: status of the matching attempt.
  //
  // Important: the format of pattern regexs is based on std::ECMAScript syntax
  // (http://www.cplusplus.com/reference/regex/ECMAScript/) with a few extra
  // restrictions:
  //   * no backreference (or submatch) groups
  //     - when a group (e.g. "(a|b)+") is needed use a passive group
  //       (e.g. "(?:a|b)+").
  //   * special syntax for symbol definitions: <<Name:regex>>
  //     - 'Name' must be c-ctyle variable name ([a-zA-Z_][a-zA-Z0-9_]*).
  //     - 'regex' - is a regex for the actual literal expected in the symbol
  //       definition line. It must not contain any symbol references.
  //   * special syntax for symbol uses <<Name>>
  //
  // Semantical restrictions on symbols references:
  //   * symbols mustn't be referenced before they are defined.
  //     - a pattern R1 which uses symbol 'A' mustn't be processed if a pattern
  //       R2 with the symbol 'A' definition hasn't been yet matched (R1!=R2).
  //     - A pattern mustn't define a symbol and use it inside the same regex.
  //   * symbols mustn't be redefined.
  //     - if a line has been matched against a pattern R1 with symbol 'A'
  //       then other patterns mustn't define symbol 'A'.
  //   * symbols defininitions are only committed and registered if the whole
  //     pattern is successfully matched.
  //
  // Notes:
  //   * A pattern may contain uses of the same or different symbols and
  //     definitions of different symbols however if a symbol is defined in the
  //     pattern it can't be used in the same pattern.
  //
  // Pattern example: "<<A:[0-9]+>> <<B>>, <<B> <<C:[a-z]+>>" (assuming 'B' is
  // defined and matched).
  Status ProcessPattern(const std::string& line, const std::string& pattern) {
    // Processed pattern which is going to be used for std::regex_search; symbol
    // references are replaced accordingly to the reference type - def or use.
    std::string final_pattern;
    // A vector of records for symbols references in the pattern. The format is
    // {is_definition, symbol_name}.
    MatchVector symbols_refs;
    Status status =
        ParseSymbolsInPattern(pattern, &final_pattern, &symbols_refs);
    if (status != Status::kSuccess) {
      return status;
    }

    std::smatch match;
    if (!std::regex_search(line, match, std::regex(final_pattern))) {
      return Status::kNotMatched;
    }

    // This checks that no backreference groups were used in the pattern except
    // for those added by ParseSymbolsInPattern.
    if (symbols_refs.size() != (match.size() - 1)) {
      return Status::kWrongPattern;
    }

    status = CheckSymbolsMatchedValues(symbols_refs, match);
    if (status != Status::kSuccess) {
      return status;
    }

    CommitSymbolsDefinitions(symbols_refs, match);

    return Status::kSuccess;
  }

  // Returns whether a symbol is defined in the symbol name.
  bool IsSymbolDefined(const std::string& symbol_name) const {
    auto symbol_map_iter = map_.find(symbol_name);
    return symbol_map_iter != std::end(map_);
  }

  // Returns the matched value for a symbol.
  std::string GetSymbolMatchedValue(const std::string& symbol_name) const {
    DCHECK(IsSymbolDefined(symbol_name));
    return map_.find(symbol_name)->second.matched_value();
  }

  // Prints the symbol table.
  void PrintSymbols(std::ostream& os) const {
    os << "Printing symbol table..." << std::endl;
    for (const auto& t : map_) {
      const std::string& sym_name = t.first;
      const SymbolInfo& sym_info = t.second;
      os << "<<" << sym_name << ">>: \"" << sym_info.matched_value() << "\""
         << std::endl;
    }
  }

 protected:
  // Fixed layout for the symbol reference match.
  enum SymbolMatchIndex {
    kFullSubmatch = 0,
    kName = 1,
    kDefRegex = 2,
    kSize = kDefRegex + 1,
  };

  // Processes a symbol reference: for definitions it adds the symbol regex, for
  // uses it adds actual literal from a previously matched definition. Also
  // fills the symbol references vector.
  Status ProcessSymbol(const std::smatch& match, MatchVector* symbols_refs,
                       std::string* new_pattern) const {
    bool is_def = match[SymbolMatchIndex::kDefRegex].length() != 0;
    const std::string& symbol_name = match[SymbolMatchIndex::kName];

    if (is_def) {
      // Make sure the symbol isn't already defined.
      auto symbol_iter =
          std::find_if(symbols_refs->begin(), symbols_refs->end(),
                       [symbol_name](const SymbolVectorElem& ref) -> bool {
                         return ref.symbol_name() == symbol_name;
                       });
      if (symbol_iter != std::end(*symbols_refs)) {
        return Status::kRedefinition;
      }

      symbols_refs->emplace_back(true, symbol_name);
      new_pattern->append("(");
      new_pattern->append(match[SymbolMatchIndex::kDefRegex]);
      new_pattern->append(")");
    } else {
      auto symbol_map_iter = map_.find(symbol_name);
      if (symbol_map_iter == std::end(map_)) {
        return Status::kDefNotFound;
      }

      const SymbolInfo& sym_info = symbol_map_iter->second;
      new_pattern->append("(");
      new_pattern->append(sym_info.matched_value());
      new_pattern->append(")");

      symbols_refs->emplace_back(false, symbol_name);
    }
    return Status::kSuccess;
  }

  // Parses the input pattern regex, processes symbols defs and uses inside
  // it, fills a raw pattern used for std::regex_search.
  Status ParseSymbolsInPattern(const std::string& pattern,
                               std::string* raw_pattern,
                               MatchVector* symbols_refs) const {
    std::string::const_iterator low = pattern.cbegin();
    std::string::const_iterator high = pattern.cend();
    std::smatch match;

    while (low != high) {
      // Search for a symbol reference.
      if (!std::regex_search(low, high, match, symbol_ref_regex_)) {
        raw_pattern->append(low, high);
        break;
      }

      if (match.size() != SymbolMatchIndex::kSize) {
        return Status::kWrongPattern;
      }

      raw_pattern->append(match.prefix());

      Status status = ProcessSymbol(match, symbols_refs, raw_pattern);
      if (status != Status::kSuccess) {
        return status;
      }
      low = match[SymbolMatchIndex::kFullSubmatch].second;
    }
    return Status::kSuccess;
  }

  // Checks that there are no symbol redefinitions and the symbols uses matched
  // literal values are equal to corresponding matched definitions.
  Status CheckSymbolsMatchedValues(const MatchVector& symbols_refs,
                                   const std::smatch& match) const {
    // There is a one-to-one correspondence between matched subexpressions and
    // symbols refences in the vector (by construction).
    for (size_t vec_pos = 0, size = symbols_refs.size(); vec_pos < size;
         vec_pos++) {
      auto elem = symbols_refs[vec_pos];
      auto map_iter = map_.find(elem.symbol_name());
      if (elem.is_def()) {
        if (map_iter != std::end(map_)) {
          return Status::kRedefinition;
        }
      } else {
        DCHECK(map_iter != std::end(map_));
        // We replaced use with matched definition value literal.
        DCHECK_EQ(map_iter->second.matched_value().compare(match[vec_pos + 1]),
                  0);
      }
    }
    return Status::kSuccess;
  }

  // Commits symbols definitions and their matched values to the symbol table.
  void CommitSymbolsDefinitions(const MatchVector& groups_vector,
                                const std::smatch& match) {
    for (size_t vec_pos = 0, size = groups_vector.size(); vec_pos < size;
         vec_pos++) {
      size_t match_pos = vec_pos + 1;
      auto elem = groups_vector[vec_pos];
      if (elem.is_def()) {
        auto emplace_res =
            map_.emplace(elem.symbol_name(), SymbolInfo(match[match_pos]));
        USE(emplace_res);  // Silence warning about unused variable.
        DCHECK(emplace_res.second == true);
      }
    }
  }

  const std::regex symbol_ref_regex_;
  SymbolMap map_;
};

bool CheckDisassemblyRegexPatterns(
    const char* function_name, const std::vector<std::string>& patterns_array);

}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_DISASM_REGEX_HELPER_H_
