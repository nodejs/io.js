// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_ARRAY_BUILDER_H_
#define V8_INTERPRETER_BYTECODE_ARRAY_BUILDER_H_

#include <optional>

#include "src/ast/ast.h"
#include "src/base/export-template.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-writer.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/interpreter/bytecode-register-allocator.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecode-source-info.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/constant-array-builder.h"
#include "src/interpreter/handler-table-builder.h"

namespace v8 {
namespace internal {

class BytecodeArray;
class FeedbackVectorSpec;
class Isolate;

namespace interpreter {

class BytecodeLabel;
class BytecodeLoopHeader;
class BytecodeNode;
class BytecodeRegisterOptimizer;
class BytecodeJumpTable;
class Register;

class V8_EXPORT_PRIVATE BytecodeArrayBuilder final {
 public:
  BytecodeArrayBuilder(
      Zone* zone, int parameter_count, int locals_count,
      FeedbackVectorSpec* feedback_vector_spec = nullptr,
      SourcePositionTableBuilder::RecordingMode source_position_mode =
          SourcePositionTableBuilder::RECORD_SOURCE_POSITIONS);

  BytecodeArrayBuilder(const BytecodeArrayBuilder&) = delete;
  BytecodeArrayBuilder& operator=(const BytecodeArrayBuilder&) = delete;

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  Handle<BytecodeArray> ToBytecodeArray(IsolateT* isolate);
  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  DirectHandle<TrustedByteArray> ToSourcePositionTable(IsolateT* isolate);

#ifdef DEBUG
  int CheckBytecodeMatches(Tagged<BytecodeArray> bytecode);
#endif

  // Get the number of parameters expected by function.
  uint16_t parameter_count() const { return parameter_count_; }
  uint16_t max_arguments() const { return max_arguments_; }

  void UpdateMaxArguments(uint16_t max_arguments) {
    max_arguments_ = std::max(max_arguments_, max_arguments);
  }

  // Get the number of locals required for bytecode array.
  int locals_count() const {
    DCHECK_GE(local_register_count_, 0);
    return local_register_count_;
  }

  // Returns the number of fixed (non-temporary) registers.
  int fixed_register_count() const { return locals_count(); }

  // Returns the number of fixed and temporary registers.
  int total_register_count() const {
    DCHECK_LE(fixed_register_count(),
              register_allocator()->maximum_register_count());
    return register_allocator()->maximum_register_count();
  }

  Register Local(int index) const;
  Register Parameter(int parameter_index) const;
  Register Receiver() const;

  // Constant loads to accumulator.
  BytecodeArrayBuilder& LoadConstantPoolEntry(size_t entry);
  BytecodeArrayBuilder& LoadLiteral(Tagged<Smi> value);
  BytecodeArrayBuilder& LoadLiteral(double value);
  BytecodeArrayBuilder& LoadLiteral(const AstRawString* raw_string);
  BytecodeArrayBuilder& LoadLiteral(const AstConsString* cons_string);
  BytecodeArrayBuilder& LoadLiteral(const Scope* scope);
  BytecodeArrayBuilder& LoadLiteral(AstBigInt bigint);
  BytecodeArrayBuilder& LoadUndefined();
  BytecodeArrayBuilder& LoadNull();
  BytecodeArrayBuilder& LoadTheHole();
  BytecodeArrayBuilder& LoadTrue();
  BytecodeArrayBuilder& LoadFalse();
  BytecodeArrayBuilder& LoadBoolean(bool value);

  // Global loads to the accumulator and stores from the accumulator.
  BytecodeArrayBuilder& LoadGlobal(const AstRawString* name, int feedback_slot,
                                   TypeofMode typeof_mode);
  BytecodeArrayBuilder& StoreGlobal(const AstRawString* name,
                                    int feedback_slot);

  // Load the object at |variable| at |depth| in the context chain starting
  // with |context| into the accumulator.
  enum ContextSlotMutability { kImmutableSlot, kMutableSlot };
  BytecodeArrayBuilder& LoadContextSlot(Register context, Variable* variable,
                                        int depth,
                                        ContextSlotMutability immutable);

  // Stores the object in the accumulator into |variable| at |depth| in the
  // context chain starting with |context|.
  BytecodeArrayBuilder& StoreContextSlot(Register context, Variable* variable,
                                         int depth);

  // Load from a module variable into the accumulator. |depth| is the depth of
  // the current context relative to the module context.
  BytecodeArrayBuilder& LoadModuleVariable(int cell_index, int depth);

  // Store from the accumulator into a module variable. |depth| is the depth of
  // the current context relative to the module context.
  BytecodeArrayBuilder& StoreModuleVariable(int cell_index, int depth);

  // Register-accumulator transfers.
  BytecodeArrayBuilder& LoadAccumulatorWithRegister(Register reg);
  BytecodeArrayBuilder& StoreAccumulatorInRegister(Register reg);

  // Register-register transfer.
  BytecodeArrayBuilder& MoveRegister(Register from, Register to);

  // Named load property.
  BytecodeArrayBuilder& LoadNamedProperty(Register object,
                                          const AstRawString* name,
                                          int feedback_slot);

  BytecodeArrayBuilder& LoadNamedPropertyFromSuper(Register object,
                                                   const AstRawString* name,
                                                   int feedback_slot);

  // Keyed load property. The key should be in the accumulator.
  BytecodeArrayBuilder& LoadKeyedProperty(Register object, int feedback_slot);

  BytecodeArrayBuilder& LoadEnumeratedKeyedProperty(Register object,
                                                    Register enum_index,
                                                    Register cache_type,
                                                    int feedback_slot);

  // Named load property of the @@iterator symbol.
  BytecodeArrayBuilder& LoadIteratorProperty(Register object,
                                             int feedback_slot);

  // Load and call property of the @@iterator symbol
  BytecodeArrayBuilder& GetIterator(Register object, int load_feedback_slot,
                                    int call_feedback_slot);

  // Named load property of the @@asyncIterator symbol.
  BytecodeArrayBuilder& LoadAsyncIteratorProperty(Register object,
                                                  int feedback_slot);

  // Store properties. Flag for NeedsSetFunctionName() should
  // be in the accumulator.
  BytecodeArrayBuilder& DefineKeyedOwnPropertyInLiteral(
      Register object, Register name,
      DefineKeyedOwnPropertyInLiteralFlags flags, int feedback_slot);

  // Set a property named by a property name, trigger the setters and
  // set traps if necessary. The value to be set should be in the
  // accumulator.
  BytecodeArrayBuilder& SetNamedProperty(Register object,
                                         const AstRawString* name,
                                         int feedback_slot,
                                         LanguageMode language_mode);

  // Set a property named by a constant from the constant pool,
  // trigger the setters and set traps if necessary. The value to be
  // set should be in the accumulator.
  BytecodeArrayBuilder& SetNamedProperty(Register object,
                                         size_t constant_pool_entry,
                                         int feedback_slot,
                                         LanguageMode language_mode);

  // Define an own property named by a constant from the constant pool,
  // trigger the defineProperty traps if necessary. The value to be
  // defined should be in the accumulator.
  BytecodeArrayBuilder& DefineNamedOwnProperty(Register object,
                                               const AstRawString* name,
                                               int feedback_slot);

  // Set a property keyed by a value in a register, trigger the setters and
  // set traps if necessary. The value to be set should be in the
  // accumulator.
  BytecodeArrayBuilder& SetKeyedProperty(Register object, Register key,
                                         int feedback_slot,
                                         LanguageMode language_mode);

  // Define an own property keyed by a value in a register, trigger the
  // defineProperty traps if necessary. The value to be defined should be
  // in the accumulator.
  BytecodeArrayBuilder& DefineKeyedOwnProperty(
      Register object, Register key, DefineKeyedOwnPropertyFlags flags,
      int feedback_slot);

  // Store an own element in an array literal. The value to be stored should be
  // in the accumulator.
  BytecodeArrayBuilder& StoreInArrayLiteral(Register array, Register index,
                                            int feedback_slot);

  // Store the class fields property. The initializer to be stored should
  // be in the accumulator.
  BytecodeArrayBuilder& StoreClassFieldsInitializer(Register constructor,
                                                    int feedback_slot);

  // Load class fields property.
  BytecodeArrayBuilder& LoadClassFieldsInitializer(Register constructor,
                                                   int feedback_slot);

  // Lookup the variable with |name|.
  BytecodeArrayBuilder& LoadLookupSlot(const AstRawString* name,
                                       TypeofMode typeof_mode);

  // Lookup the variable with |name|, which is known to be at |slot_index| at
  // |depth| in the context chain if not shadowed by a context extension
  // somewhere in that context chain.
  BytecodeArrayBuilder& LoadLookupContextSlot(const AstRawString* name,
                                              TypeofMode typeof_mode,
                                              ContextKind context_kind,
                                              int slot_index, int depth);

  // Lookup the variable with |name|, which has its feedback in |feedback_slot|
  // and is known to be global if not shadowed by a context extension somewhere
  // up to |depth| in that context chain.
  BytecodeArrayBuilder& LoadLookupGlobalSlot(const AstRawString* name,
                                             TypeofMode typeof_mode,
                                             int feedback_slot, int depth);

  // Store value in the accumulator into the variable with |name|.
  BytecodeArrayBuilder& StoreLookupSlot(
      const AstRawString* name, LanguageMode language_mode,
      LookupHoistingMode lookup_hoisting_mode);

  // Create a new closure for a SharedFunctionInfo which will be inserted at
  // constant pool index |shared_function_info_entry|.
  BytecodeArrayBuilder& CreateClosure(size_t shared_function_info_entry,
                                      int slot, int flags);

  // Create a new local context for a |scope|.
  BytecodeArrayBuilder& CreateBlockContext(const Scope* scope);

  // Create a new context for a catch block with |exception| and |scope|.
  BytecodeArrayBuilder& CreateCatchContext(Register exception,
                                           const Scope* scope);

  // Create a new context with the given |scope| and size |slots|.
  BytecodeArrayBuilder& CreateFunctionContext(const Scope* scope, int slots);

  // Create a new eval context with the given |scope| and size |slots|.
  BytecodeArrayBuilder& CreateEvalContext(const Scope* scope, int slots);

  // Creates a new context with the given |scope| for a with-statement
  // with the |object| in a register.
  BytecodeArrayBuilder& CreateWithContext(Register object, const Scope* scope);

  // Create a new arguments object in the accumulator.
  BytecodeArrayBuilder& CreateArguments(CreateArgumentsType type);

  // Literals creation.  Constant elements should be in the accumulator.
  BytecodeArrayBuilder& CreateRegExpLiteral(const AstRawString* pattern,
                                            int literal_index, int flags);
  BytecodeArrayBuilder& CreateArrayLiteral(size_t constant_elements_entry,
                                           int literal_index, int flags);
  BytecodeArrayBuilder& CreateEmptyArrayLiteral(int literal_index);
  BytecodeArrayBuilder& CreateArrayFromIterable();
  BytecodeArrayBuilder& CreateObjectLiteral(size_t constant_properties_entry,
                                            int literal_index, int flags);
  BytecodeArrayBuilder& CreateEmptyObjectLiteral();
  BytecodeArrayBuilder& CloneObject(Register source, int flags,
                                    int feedback_slot);

  // Gets or creates the template for a TemplateObjectDescription which will
  // be inserted at constant pool index |template_object_description_entry|.
  BytecodeArrayBuilder& GetTemplateObject(
      size_t template_object_description_entry, int feedback_slot);

  // Push the context in accumulator as the new context, and store in register
  // |context|.
  BytecodeArrayBuilder& PushContext(Register context);

  // Pop the current context and replace with |context|.
  BytecodeArrayBuilder& PopContext(Register context);

  // Call a JS function which is known to be a property of a JS object. The
  // JSFunction or Callable to be called should be in |callable|. The arguments
  // should be in |args|, with the receiver in |args[0]|. Type feedback is
  // recorded in the |feedback_slot| in the type feedback vector.
  BytecodeArrayBuilder& CallProperty(Register callable, RegisterList args,
                                     int feedback_slot);

  // Call a JS function with an known undefined receiver. The JSFunction or
  // Callable to be called should be in |callable|. The arguments should be in
  // |args|, with no receiver as it is implicitly set to undefined. Type
  // feedback is recorded in the |feedback_slot| in the type feedback vector.
  BytecodeArrayBuilder& CallUndefinedReceiver(Register callable,
                                              RegisterList args,
                                              int feedback_slot);

  // Call a JS function with an any receiver, possibly (but not necessarily)
  // undefined. The JSFunction or Callable to be called should be in |callable|.
  // The arguments should be in |args|, with the receiver in |args[0]|. Type
  // feedback is recorded in the |feedback_slot| in the type feedback vector.
  BytecodeArrayBuilder& CallAnyReceiver(Register callable, RegisterList args,
                                        int feedback_slot);

  // Tail call into a JS function. The JSFunction or Callable to be called
  // should be in |callable|. The arguments should be in |args|, with the
  // receiver in |args[0]|. Type feedback is recorded in the |feedback_slot| in
  // the type feedback vector.
  BytecodeArrayBuilder& TailCall(Register callable, RegisterList args,
                                 int feedback_slot);

  // Call a JS function. The JSFunction or Callable to be called should be in
  // |callable|, the receiver in |args[0]| and the arguments in |args[1]|
  // onwards. The final argument must be a spread.
  BytecodeArrayBuilder& CallWithSpread(Register callable, RegisterList args,
                                       int feedback_slot);

  // Call the Construct operator. The accumulator holds the |new_target|.
  // The |constructor| is in a register and arguments are in |args|.
  BytecodeArrayBuilder& Construct(Register constructor, RegisterList args,
                                  int feedback_slot);

  // Call the Construct operator for use with a spread. The accumulator holds
  // the |new_target|. The |constructor| is in a register and arguments are in
  // |args|. The final argument must be a spread.
  BytecodeArrayBuilder& ConstructWithSpread(Register constructor,
                                            RegisterList args,
                                            int feedback_slot);

  // Call the Construct operator, forwarding all arguments passed to the current
  // interpreted frame, including the receiver. The accumulator holds the
  // |new_target|. The |constructor| is in a register.
  BytecodeArrayBuilder& ConstructForwardAllArgs(Register constructor,
                                                int feedback_slot);

  // Call the runtime function with |function_id| and arguments |args|.
  BytecodeArrayBuilder& CallRuntime(Runtime::FunctionId function_id,
                                    RegisterList args);
  // Call the runtime function with |function_id| with single argument |arg|.
  BytecodeArrayBuilder& CallRuntime(Runtime::FunctionId function_id,
                                    Register arg);
  // Call the runtime function with |function_id| with no arguments.
  BytecodeArrayBuilder& CallRuntime(Runtime::FunctionId function_id);

  // Call the runtime function with |function_id| and arguments |args|, that
  // returns a pair of values. The return values will be returned in
  // |return_pair|.
  BytecodeArrayBuilder& CallRuntimeForPair(Runtime::FunctionId function_id,
                                           RegisterList args,
                                           RegisterList return_pair);
  // Call the runtime function with |function_id| with single argument |arg|
  // that returns a pair of values. The return values will be returned in
  // |return_pair|.
  BytecodeArrayBuilder& CallRuntimeForPair(Runtime::FunctionId function_id,
                                           Register arg,
                                           RegisterList return_pair);

  // Call the JS runtime function with |context_index| and arguments |args|,
  // with no receiver as it is implicitly set to undefined.
  BytecodeArrayBuilder& CallJSRuntime(int context_index, RegisterList args);

  // Operators (register holds the lhs value, accumulator holds the rhs value).
  // Type feedback will be recorded in the |feedback_slot|
  BytecodeArrayBuilder& BinaryOperation(Token::Value binop, Register reg,
                                        int feedback_slot);
  // Same as above, but lhs in the accumulator and rhs in |literal|.
  BytecodeArrayBuilder& BinaryOperationSmiLiteral(Token::Value binop,
                                                  Tagged<Smi> literal,
                                                  int feedback_slot);

  // Unary and Count Operators (value stored in accumulator).
  // Type feedback will be recorded in the |feedback_slot|
  BytecodeArrayBuilder& UnaryOperation(Token::Value op, int feedback_slot);

  enum class ToBooleanMode {
    kConvertToBoolean,  // Perform ToBoolean conversion on accumulator.
    kAlreadyBoolean,    // Accumulator is already a Boolean.
  };

  // Unary Operators.
  BytecodeArrayBuilder& LogicalNot(ToBooleanMode mode);
  BytecodeArrayBuilder& TypeOf(int feedback_slot);

  // Expects a heap object in the accumulator. Returns its super constructor in
  // the register |out| if it passes the IsConstructor test. Otherwise, it
  // throws a TypeError exception.
  BytecodeArrayBuilder& GetSuperConstructor(Register out);

  BytecodeArrayBuilder& FindNonDefaultConstructorOrConstruct(
      Register this_function, Register new_target, RegisterList output);

  // Deletes property from an object. This expects that accumulator contains
  // the key to be deleted and the register contains a reference to the object.
  BytecodeArrayBuilder& Delete(Register object, LanguageMode language_mode);

  // JavaScript defines two kinds of 'nil'.
  enum NilValue { kNullValue, kUndefinedValue };

  // Tests.
  BytecodeArrayBuilder& CompareOperation(Token::Value op, Register reg,
                                         int feedback_slot);
  BytecodeArrayBuilder& CompareReference(Register reg);
  BytecodeArrayBuilder& CompareUndetectable();
  BytecodeArrayBuilder& CompareUndefined();
  BytecodeArrayBuilder& CompareNull();
  BytecodeArrayBuilder& CompareNil(Token::Value op, NilValue nil);
  BytecodeArrayBuilder& CompareTypeOf(
      TestTypeOfFlags::LiteralFlag literal_flag);

  // Converts accumulator and stores result in register |out|.
  BytecodeArrayBuilder& ToObject(Register out);

  // Converts accumulator and stores result back in accumulator.
  BytecodeArrayBuilder& ToName();
  BytecodeArrayBuilder& ToString();
  BytecodeArrayBuilder& ToBoolean(ToBooleanMode mode);
  BytecodeArrayBuilder& ToNumber(int feedback_slot);
  BytecodeArrayBuilder& ToNumeric(int feedback_slot);

  // Exception handling.
  BytecodeArrayBuilder& MarkHandler(int handler_id,
                                    HandlerTable::CatchPrediction will_catch);
  BytecodeArrayBuilder& MarkTryBegin(int handler_id, Register context);
  BytecodeArrayBuilder& MarkTryEnd(int handler_id);

  // Flow Control.
  BytecodeArrayBuilder& Bind(BytecodeLabel* label);
  BytecodeArrayBuilder& Bind(BytecodeLoopHeader* label);
  BytecodeArrayBuilder& Bind(BytecodeJumpTable* jump_table, int case_value);

  BytecodeArrayBuilder& Jump(BytecodeLabel* label);
  BytecodeArrayBuilder& JumpLoop(BytecodeLoopHeader* loop_header,
                                 int loop_depth, int position,
                                 int feedback_slot);

  BytecodeArrayBuilder& JumpIfTrue(ToBooleanMode mode, BytecodeLabel* label);
  BytecodeArrayBuilder& JumpIfFalse(ToBooleanMode mode, BytecodeLabel* label);
  BytecodeArrayBuilder& JumpIfJSReceiver(BytecodeLabel* label);
  BytecodeArrayBuilder& JumpIfNull(BytecodeLabel* label);
  BytecodeArrayBuilder& JumpIfNotNull(BytecodeLabel* label);
  BytecodeArrayBuilder& JumpIfUndefined(BytecodeLabel* label);
  BytecodeArrayBuilder& JumpIfUndefinedOrNull(BytecodeLabel* label);
  BytecodeArrayBuilder& JumpIfNotUndefined(BytecodeLabel* label);
  BytecodeArrayBuilder& JumpIfNil(BytecodeLabel* label, Token::Value op,
                                  NilValue nil);
  BytecodeArrayBuilder& JumpIfNotNil(BytecodeLabel* label, Token::Value op,
                                     NilValue nil);
  BytecodeArrayBuilder& JumpIfForInDone(BytecodeLabel* label, Register index,
                                        Register cache_length);

  BytecodeArrayBuilder& SwitchOnSmiNoFeedback(BytecodeJumpTable* jump_table);

  // Sets the pending message to the value in the accumulator, and returns the
  // previous pending message in the accumulator.
  BytecodeArrayBuilder& SetPendingMessage();

  BytecodeArrayBuilder& Throw();
  BytecodeArrayBuilder& ReThrow();
  BytecodeArrayBuilder& Abort(AbortReason reason);
  BytecodeArrayBuilder& Return();
  BytecodeArrayBuilder& ThrowReferenceErrorIfHole(const AstRawString* name);
  BytecodeArrayBuilder& ThrowSuperNotCalledIfHole();
  BytecodeArrayBuilder& ThrowSuperAlreadyCalledIfNotHole();
  BytecodeArrayBuilder& ThrowIfNotSuperConstructor(Register constructor);

  // Debugger.
  BytecodeArrayBuilder& Debugger();

  // Increment the block counter at the given slot (block code coverage).
  BytecodeArrayBuilder& IncBlockCounter(int slot);

  // Complex flow control.
  BytecodeArrayBuilder& ForInEnumerate(Register receiver);
  BytecodeArrayBuilder& ForInPrepare(RegisterList cache_info_triple,
                                     int feedback_slot);
  BytecodeArrayBuilder& ForInNext(Register receiver, Register index,
                                  RegisterList cache_type_array_pair,
                                  int feedback_slot);
  BytecodeArrayBuilder& ForInStep(Register index);

  // Generators.
  BytecodeArrayBuilder& SuspendGenerator(Register generator,
                                         RegisterList registers,
                                         int suspend_id);
  BytecodeArrayBuilder& SwitchOnGeneratorState(Register generator,
                                               BytecodeJumpTable* jump_table);
  BytecodeArrayBuilder& ResumeGenerator(Register generator,
                                        RegisterList registers);

  // Creates a new handler table entry and returns a {hander_id} identifying the
  // entry, so that it can be referenced by above exception handling support.
  int NewHandlerEntry() { return handler_table_builder()->NewHandlerEntry(); }

  // Allocates a new jump table of given |size| and |case_value_base| in the
  // constant pool.
  BytecodeJumpTable* AllocateJumpTable(int size, int case_value_base);

  BytecodeRegisterOptimizer* GetRegisterOptimizer() {
    return register_optimizer_;
  }

  // Gets a constant pool entry.
  size_t GetConstantPoolEntry(const AstRawString* raw_string);
  size_t GetConstantPoolEntry(const AstConsString* cons_string);
  size_t GetConstantPoolEntry(AstBigInt bigint);
  size_t GetConstantPoolEntry(const Scope* scope);
  size_t GetConstantPoolEntry(double number);
#define ENTRY_GETTER(NAME, ...) size_t NAME##ConstantPoolEntry();
  SINGLETON_CONSTANT_ENTRY_TYPES(ENTRY_GETTER)
#undef ENTRY_GETTER

  // Allocates a slot in the constant pool which can later be set.
  size_t AllocateDeferredConstantPoolEntry();
  // Sets the deferred value into an allocated constant pool entry.
  void SetDeferredConstantPoolEntry(size_t entry, Handle<Object> object);

  void InitializeReturnPosition(FunctionLiteral* literal);

  void SetStatementPosition(Statement* stmt) {
    SetStatementPosition(stmt->position());
  }

  std::optional<BytecodeSourceInfo> MaybePopSourcePosition(int scope_start) {
    if (!latest_source_info_.is_valid() ||
        latest_source_info_.source_position() < scope_start) {
      return std::nullopt;
    }
    BytecodeSourceInfo source_info = latest_source_info_;
    latest_source_info_.set_invalid();
    return source_info;
  }

  void PushSourcePosition(BytecodeSourceInfo source_info) {
    DCHECK(!latest_source_info_.is_valid());
    latest_source_info_ = source_info;
  }

  void SetStatementPosition(int position) {
    if (position == kNoSourcePosition) return;
    latest_source_info_.MakeStatementPosition(position);
  }

  void SetExpressionPosition(Expression* expr) {
    SetExpressionPosition(expr->position());
  }

  void SetExpressionPosition(int position) {
    if (position == kNoSourcePosition) return;
    if (!latest_source_info_.is_statement()) {
      // Ensure the current expression position is overwritten with the
      // latest value.
      latest_source_info_.MakeExpressionPosition(position);
    }
  }

  void SetExpressionAsStatementPosition(Expression* expr) {
    SetStatementPosition(expr->position());
  }

  bool RemainderOfBlockIsDead() const {
    return bytecode_array_writer_.RemainderOfBlockIsDead();
  }

  // Returns the raw operand value for the given register or register list.
  uint32_t GetInputRegisterOperand(Register reg);
  uint32_t GetOutputRegisterOperand(Register reg);
  uint32_t GetInputOutputRegisterOperand(Register reg);
  uint32_t GetInputRegisterListOperand(RegisterList reg_list);
  uint32_t GetOutputRegisterListOperand(RegisterList reg_list);

  // Outputs raw register transfer bytecodes without going through the register
  // optimizer.
  void OutputLdarRaw(Register reg);
  void OutputStarRaw(Register reg);
  void OutputMovRaw(Register src, Register dest);

  void EmitFunctionStartSourcePosition(int position);

  // Accessors
  BytecodeRegisterAllocator* register_allocator() {
    return &register_allocator_;
  }
  const BytecodeRegisterAllocator* register_allocator() const {
    return &register_allocator_;
  }
  Zone* zone() const { return zone_; }

 private:
  friend class BytecodeRegisterAllocator;
  template <Bytecode bytecode, ImplicitRegisterUse implicit_register_use,
            OperandType... operand_types>
  friend class BytecodeNodeBuilder;

  const FeedbackVectorSpec* feedback_vector_spec() const {
    return feedback_vector_spec_;
  }

  // Returns the current source position for the given |bytecode|.
  V8_INLINE BytecodeSourceInfo CurrentSourcePosition(Bytecode bytecode);

#define DECLARE_BYTECODE_OUTPUT(Name, ...)                         \
  template <typename... Operands>                                  \
  V8_INLINE BytecodeNode Create##Name##Node(Operands... operands); \
  template <typename... Operands>                                  \
  V8_INLINE void Output##Name(Operands... operands);               \
  template <typename... Operands>                                  \
  V8_INLINE void Output##Name(BytecodeLabel* label, Operands... operands);
  BYTECODE_LIST(DECLARE_BYTECODE_OUTPUT, DECLARE_BYTECODE_OUTPUT)
#undef DECLARE_OPERAND_TYPE_INFO

  V8_INLINE void OutputJumpLoop(BytecodeLoopHeader* loop_header, int loop_depth,
                                int feedback_slot);
  V8_INLINE void OutputSwitchOnSmiNoFeedback(BytecodeJumpTable* jump_table);

  bool RegisterIsValid(Register reg) const;
  bool RegisterListIsValid(RegisterList reg_list) const;

  // Sets a deferred source info which should be emitted before any future
  // source info (either attached to a following bytecode or as a nop).
  void SetDeferredSourceInfo(BytecodeSourceInfo source_info);
  // Either attach deferred source info to node, or emit it as a nop bytecode
  // if node already have valid source info.
  void AttachOrEmitDeferredSourceInfo(BytecodeNode* node);

  // Write bytecode to bytecode array.
  void Write(BytecodeNode* node);
  void WriteJump(BytecodeNode* node, BytecodeLabel* label);
  void WriteJumpLoop(BytecodeNode* node, BytecodeLoopHeader* loop_header);
  void WriteSwitch(BytecodeNode* node, BytecodeJumpTable* label);

  // Not implemented as the illegal bytecode is used inside internally
  // to indicate a bytecode field is not valid or an error has occurred
  // during bytecode generation.
  BytecodeArrayBuilder& Illegal();

  template <Bytecode bytecode, ImplicitRegisterUse implicit_register_use>
  void PrepareToOutputBytecode();

  BytecodeArrayWriter* bytecode_array_writer() {
    return &bytecode_array_writer_;
  }
  ConstantArrayBuilder* constant_array_builder() {
    return &constant_array_builder_;
  }
  const ConstantArrayBuilder* constant_array_builder() const {
    return &constant_array_builder_;
  }
  HandlerTableBuilder* handler_table_builder() {
    return &handler_table_builder_;
  }

  Zone* zone_;
  FeedbackVectorSpec* feedback_vector_spec_;
  bool bytecode_generated_;
  ConstantArrayBuilder constant_array_builder_;
  HandlerTableBuilder handler_table_builder_;
  uint16_t parameter_count_;
  uint16_t max_arguments_;
  int local_register_count_;
  BytecodeRegisterAllocator register_allocator_;
  BytecodeArrayWriter bytecode_array_writer_;
  BytecodeRegisterOptimizer* register_optimizer_;
  BytecodeSourceInfo latest_source_info_;
  BytecodeSourceInfo deferred_source_info_;
};

V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, const BytecodeArrayBuilder::ToBooleanMode& mode);

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_ARRAY_BUILDER_H_
