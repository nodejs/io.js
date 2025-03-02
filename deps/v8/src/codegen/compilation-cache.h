// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_COMPILATION_CACHE_H_
#define V8_CODEGEN_COMPILATION_CACHE_H_

#include "src/base/hashmap.h"
#include "src/objects/compilation-cache-table.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

class RootVisitor;
struct ScriptDetails;

// The compilation cache consists of several sub-caches: one each for evals and
// scripts, which use this class as a base class, and a separate generational
// sub-cache for RegExps. Since the same source code string has different
// compiled code for scripts and evals, we use separate sub-caches for different
// compilation modes, to avoid retrieving the wrong result.
class CompilationCacheEvalOrScript {
 public:
  explicit CompilationCacheEvalOrScript(Isolate* isolate) : isolate_(isolate) {}

  // Allocates the table if it didn't yet exist.
  Handle<CompilationCacheTable> GetTable();

  // GC support.
  void Iterate(RootVisitor* v);

  // Clears this sub-cache evicting all its content.
  void Clear();

  // Removes given shared function info from sub-cache.
  void Remove(DirectHandle<SharedFunctionInfo> function_info);

 protected:
  Isolate* isolate() const { return isolate_; }

  Isolate* const isolate_;
  Tagged<Object> table_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(CompilationCacheEvalOrScript);
};

// Sub-cache for scripts.
class CompilationCacheScript : public CompilationCacheEvalOrScript {
 public:
  explicit CompilationCacheScript(Isolate* isolate)
      : CompilationCacheEvalOrScript(isolate) {}

  using LookupResult = CompilationCacheScriptLookupResult;
  LookupResult Lookup(Handle<String> source,
                      const ScriptDetails& script_details);

  void Put(Handle<String> source,
           DirectHandle<SharedFunctionInfo> function_info);

  void Age();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(CompilationCacheScript);
};

// Sub-cache for eval scripts. Two caches for eval are used. One for eval calls
// in native contexts and one for eval calls in other contexts. The cache
// considers the following pieces of information when checking for matching
// entries:
// 1. The source string.
// 2. The shared function info of the calling function.
// 3. Whether the source should be compiled as strict code or as sloppy code.
//    Note: Currently there are clients of CompileEval that always compile
//    sloppy code even if the calling function is a strict mode function.
//    More specifically these are the CompileString, DebugEvaluate and
//    DebugEvaluateGlobal runtime functions.
// 4. The start position of the calling scope.
class CompilationCacheEval : public CompilationCacheEvalOrScript {
 public:
  explicit CompilationCacheEval(Isolate* isolate)
      : CompilationCacheEvalOrScript(isolate) {}

  InfoCellPair Lookup(DirectHandle<String> source,
                      DirectHandle<SharedFunctionInfo> outer_info,
                      DirectHandle<NativeContext> native_context,
                      LanguageMode language_mode, int position);

  void Put(DirectHandle<String> source,
           DirectHandle<SharedFunctionInfo> outer_info,
           DirectHandle<SharedFunctionInfo> function_info,
           DirectHandle<NativeContext> native_context,
           DirectHandle<FeedbackCell> feedback_cell, int position);

  void Age();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(CompilationCacheEval);
};

// Sub-cache for regular expressions.
class CompilationCacheRegExp {
 public:
  CompilationCacheRegExp(Isolate* isolate) : isolate_(isolate) {}

  MaybeDirectHandle<RegExpData> Lookup(DirectHandle<String> source,
                                       JSRegExp::Flags flags);

  void Put(DirectHandle<String> source, JSRegExp::Flags flags,
           DirectHandle<RegExpData> data);

  // The number of generations for the RegExp sub cache.
  static const int kGenerations = 2;

  // Gets the compilation cache tables for a specific generation. Allocates the
  // table if it does not yet exist.
  DirectHandle<CompilationCacheTable> GetTable(int generation);

  // Ages the sub-cache by evicting the oldest generation and creating a new
  // young generation.
  void Age();

  // GC support.
  void Iterate(RootVisitor* v);

  // Clears this sub-cache evicting all its content.
  void Clear();

 private:
  Isolate* isolate() const { return isolate_; }

  Isolate* const isolate_;
  Tagged<Object> tables_[kGenerations];  // One for each generation.

  DISALLOW_IMPLICIT_CONSTRUCTORS(CompilationCacheRegExp);
};

// The compilation cache keeps shared function infos for compiled
// scripts and evals. The shared function infos are looked up using
// the source string as the key. For regular expressions the
// compilation data is cached.
class V8_EXPORT_PRIVATE CompilationCache {
 public:
  CompilationCache(const CompilationCache&) = delete;
  CompilationCache& operator=(const CompilationCache&) = delete;

  // Finds the Script and root SharedFunctionInfo for a script source string.
  // Returns empty handles if the cache doesn't contain a script for the given
  // source string with the right origin.
  CompilationCacheScript::LookupResult LookupScript(
      Handle<String> source, const ScriptDetails& script_details,
      LanguageMode language_mode);

  // Finds the shared function info for a source string for eval in a
  // given context.  Returns an empty handle if the cache doesn't
  // contain a script for the given source string.
  InfoCellPair LookupEval(DirectHandle<String> source,
                          DirectHandle<SharedFunctionInfo> outer_info,
                          DirectHandle<Context> context,
                          LanguageMode language_mode, int position);

  // Returns the regexp data associated with the given regexp if it
  // is in cache, otherwise an empty handle.
  MaybeDirectHandle<RegExpData> LookupRegExp(DirectHandle<String> source,
                                             JSRegExp::Flags flags);

  // Associate the (source, kind) pair to the shared function
  // info. This may overwrite an existing mapping.
  void PutScript(Handle<String> source, LanguageMode language_mode,
                 DirectHandle<SharedFunctionInfo> function_info);

  // Associate the (source, context->closure()->shared(), kind) triple
  // with the shared function info. This may overwrite an existing mapping.
  void PutEval(DirectHandle<String> source,
               DirectHandle<SharedFunctionInfo> outer_info,
               DirectHandle<Context> context,
               DirectHandle<SharedFunctionInfo> function_info,
               DirectHandle<FeedbackCell> feedback_cell, int position);

  // Associate the (source, flags) pair to the given regexp data.
  // This may overwrite an existing mapping.
  void PutRegExp(DirectHandle<String> source, JSRegExp::Flags flags,
                 DirectHandle<RegExpData> data);

  // Clear the cache - also used to initialize the cache at startup.
  void Clear();

  // Remove given shared function info from all caches.
  void Remove(DirectHandle<SharedFunctionInfo> function_info);

  // GC support.
  void Iterate(RootVisitor* v);

  // Notify the cache that a mark-sweep garbage collection is about to
  // take place. This is used to retire entries from the cache to
  // avoid keeping them alive too long without using them.
  void MarkCompactPrologue();

  // Enable/disable compilation cache. Used by debugger to disable compilation
  // cache during debugging so that eval and new scripts are always compiled.
  // TODO(bmeurer, chromium:992277): The RegExp cache cannot be enabled and/or
  // disabled, since it doesn't affect debugging. However ideally the other
  // caches should also be always on, even in the presence of the debugger,
  // but at this point there are too many unclear invariants, and so I decided
  // to just fix the pressing performance problem for RegExp individually first.
  void EnableScriptAndEval();
  void DisableScriptAndEval();

 private:
  explicit CompilationCache(Isolate* isolate);
  ~CompilationCache() = default;

  base::HashMap* EagerOptimizingSet();

  bool IsEnabledScriptAndEval() const {
    return v8_flags.compilation_cache && enabled_script_and_eval_;
  }
  bool IsEnabledScript(LanguageMode language_mode) {
    // Tests can change v8_flags.use_strict at runtime. The compilation cache
    // only contains scripts which were compiled with the default language mode.
    return IsEnabledScriptAndEval() && language_mode == LanguageMode::kSloppy;
  }

  Isolate* isolate() const { return isolate_; }

  Isolate* isolate_;

  CompilationCacheScript script_;
  CompilationCacheEval eval_global_;
  CompilationCacheEval eval_contextual_;
  CompilationCacheRegExp reg_exp_;

  // Current enable state of the compilation cache for scripts and eval.
  bool enabled_script_and_eval_;

  friend class Isolate;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_COMPILATION_CACHE_H_
