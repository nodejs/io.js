// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PENDING_COMPILATION_ERROR_HANDLER_H_
#define V8_PARSING_PENDING_COMPILATION_ERROR_HANDLER_H_

#include <forward_list>

#include "src/base/export-template.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/handles/handles.h"

namespace v8 {
namespace internal {

class AstRawString;
class AstValueFactory;
class Isolate;
class Script;

// Helper class for handling pending compilation errors consistently in various
// compilation phases.
class PendingCompilationErrorHandler {
 public:
  PendingCompilationErrorHandler() = default;
  PendingCompilationErrorHandler(const PendingCompilationErrorHandler&) =
      delete;
  PendingCompilationErrorHandler& operator=(
      const PendingCompilationErrorHandler&) = delete;

  void ReportMessageAt(int start_position, int end_position,
                       MessageTemplate message, const char* arg = nullptr);

  void ReportMessageAt(int start_position, int end_position,
                       MessageTemplate message, const AstRawString* arg);

  void ReportMessageAt(int start_position, int end_position,
                       MessageTemplate message, const AstRawString* arg0,
                       const char* arg1);

  void ReportMessageAt(int start_position, int end_position,
                       MessageTemplate message, const AstRawString* arg0,
                       const AstRawString* arg1, const char* arg2);

  void ReportWarningAt(int start_position, int end_position,
                       MessageTemplate message, const char* arg = nullptr);

  bool stack_overflow() const { return stack_overflow_; }

  void set_stack_overflow() {
    has_pending_error_ = true;
    stack_overflow_ = true;
  }

  bool has_pending_error() const { return has_pending_error_; }
  bool has_pending_warnings() const { return !warning_messages_.empty(); }

  // Handle errors detected during parsing.
  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  void PrepareErrors(IsolateT* isolate, AstValueFactory* ast_value_factory);
  V8_EXPORT_PRIVATE void ReportErrors(Isolate* isolate,
                                      Handle<Script> script) const;

  // Handle warnings detected during compilation.
  template <typename IsolateT>
  void PrepareWarnings(IsolateT* isolate);
  void ReportWarnings(Isolate* isolate, Handle<Script> script) const;

  V8_EXPORT_PRIVATE Handle<String> FormatErrorMessageForTest(Isolate* isolate);

  void set_unidentifiable_error() {
    has_pending_error_ = true;
    unidentifiable_error_ = true;
  }
  void clear_unidentifiable_error() {
    has_pending_error_ = false;
    unidentifiable_error_ = false;
  }
  bool has_error_unidentifiable_by_preparser() const {
    return unidentifiable_error_;
  }

 private:
  class MessageDetails {
   public:
    static constexpr int kMaxArgumentCount = 3;

    MOVE_ONLY_NO_DEFAULT_CONSTRUCTOR(MessageDetails);
    MessageDetails()
        : start_position_(-1),
          end_position_(-1),
          message_(MessageTemplate::kNone) {}
    MessageDetails(int start_position, int end_position,
                   MessageTemplate message, const AstRawString* arg0)
        : start_position_(start_position),
          end_position_(end_position),
          message_(message),
          args_{MessageArgument{arg0}, MessageArgument{}, MessageArgument{}} {}
    MessageDetails(int start_position, int end_position,
                   MessageTemplate message, const AstRawString* arg0,
                   const char* arg1)
        : start_position_(start_position),
          end_position_(end_position),
          message_(message),
          args_{MessageArgument{arg0}, MessageArgument{arg1},
                MessageArgument{}} {
      DCHECK_NOT_NULL(arg0);
      DCHECK_NOT_NULL(arg1);
    }
    MessageDetails(int start_position, int end_position,
                   MessageTemplate message, const AstRawString* arg0,
                   const AstRawString* arg1, const char* arg2)
        : start_position_(start_position),
          end_position_(end_position),
          message_(message),
          args_{MessageArgument{arg0}, MessageArgument{arg1},
                MessageArgument{arg2}} {
      DCHECK_NOT_NULL(arg0);
      DCHECK_NOT_NULL(arg1);
      DCHECK_NOT_NULL(arg2);
    }
    MessageDetails(int start_position, int end_position,
                   MessageTemplate message, const char* arg0)
        : start_position_(start_position),
          end_position_(end_position),
          message_(message),
          args_{MessageArgument{arg0}, MessageArgument{}, MessageArgument{}} {}

    Handle<String> ArgString(Isolate* isolate, int index) const;
    int ArgCount() const {
      int argc = 0;
      for (int i = 0; i < kMaxArgumentCount; i++) {
        if (args_[i].type == kNone) break;
        argc++;
      }
#ifdef DEBUG
      for (int i = argc; i < kMaxArgumentCount; i++) {
        DCHECK_EQ(args_[i].type, kNone);
      }
#endif  // DEBUG
      return argc;
    }

    MessageLocation GetLocation(Handle<Script> script) const;
    int start_pos() const { return start_position_; }
    int end_pos() const { return end_position_; }
    MessageTemplate message() const { return message_; }

    template <typename IsolateT>
    void Prepare(IsolateT* isolate);

   private:
    enum Type { kNone, kAstRawString, kConstCharString, kMainThreadHandle };

    void SetString(int index, Handle<String> string, Isolate* isolate);
    void SetString(int index, Handle<String> string, LocalIsolate* isolate);

    int start_position_;
    int end_position_;

    MessageTemplate message_;

    struct MessageArgument final {
      constexpr MessageArgument() : ast_string(nullptr), type(kNone) {}
      explicit constexpr MessageArgument(const AstRawString* s)
          : ast_string(s), type(s == nullptr ? kNone : kAstRawString) {}
      explicit constexpr MessageArgument(const char* s)
          : c_string(s), type(s == nullptr ? kNone : kConstCharString) {}

      union {
        const AstRawString* ast_string;
        const char* c_string;
        Handle<String> js_string;
      };
      Type type;
    };

    MessageArgument args_[kMaxArgumentCount];
  };

  void ThrowPendingError(Isolate* isolate, Handle<Script> script) const;

  bool has_pending_error_ = false;
  bool stack_overflow_ = false;
  bool unidentifiable_error_ = false;

  MessageDetails error_details_;

  std::forward_list<MessageDetails> warning_messages_;
};

extern template void PendingCompilationErrorHandler::PrepareErrors(
    Isolate* isolate, AstValueFactory* ast_value_factory);
extern template void PendingCompilationErrorHandler::PrepareErrors(
    LocalIsolate* isolate, AstValueFactory* ast_value_factory);
extern template void PendingCompilationErrorHandler::PrepareWarnings(
    Isolate* isolate);
extern template void PendingCompilationErrorHandler::PrepareWarnings(
    LocalIsolate* isolate);

}  // namespace internal
}  // namespace v8
#endif  // V8_PARSING_PENDING_COMPILATION_ERROR_HANDLER_H_
