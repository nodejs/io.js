// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#ifndef U_HIDE_DEPRECATED_API

#ifndef MESSAGEFORMAT2_EVALUATION_H
#define MESSAGEFORMAT2_EVALUATION_H

#if U_SHOW_CPLUSPLUS_API

/**
 * \file
 * \brief C++ API: Formats messages using the draft MessageFormat 2.0.
 */

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "unicode/messageformat2_arguments.h"
#include "unicode/messageformat2_data_model.h"
#include "unicode/messageformat2_function_registry.h"
#include "messageformat2_errors.h"

// Auxiliary data structures used during formatting a message

U_NAMESPACE_BEGIN

namespace message2 {

    using namespace data_model;

    // PrioritizedVariant

    // For how this class is used, see the references to (integer, variant) tuples
    // in https://github.com/unicode-org/message-format-wg/blob/main/spec/formatting.md#pattern-selection
    class PrioritizedVariant : public UObject {
    public:
        PrioritizedVariant() = default;
        PrioritizedVariant(PrioritizedVariant&&) = default;
        PrioritizedVariant& operator=(PrioritizedVariant&&) noexcept = default;
        UBool operator<(const PrioritizedVariant&) const;
        int32_t priority;
        /* const */ SelectorKeys keys;
        /* const */ Pattern pat;
        PrioritizedVariant(uint32_t p,
                           const SelectorKeys& k,
                           const Pattern& pattern) noexcept : priority(p), keys(k), pat(pattern) {}
        virtual ~PrioritizedVariant();
    }; // class PrioritizedVariant

    static inline int32_t comparePrioritizedVariants(UElement left, UElement right) {
        const PrioritizedVariant& tuple1 = *(static_cast<const PrioritizedVariant*>(left.pointer));
        const PrioritizedVariant& tuple2 = *(static_cast<const PrioritizedVariant*>(right.pointer));
        if (tuple1 < tuple2) {
            return -1;
        }
        if (tuple1.priority == tuple2.priority) {
            return 0;
        }
        return 1;
    }

    // Encapsulates a value to be scrutinized by a `match` with its resolved
    // options and the name of the selector
    class ResolvedSelector : public UObject {
    public:
        ResolvedSelector() {}
        ResolvedSelector(const FunctionName& fn,
                         Selector* selector,
                         FunctionOptions&& options,
                         FormattedPlaceholder&& value);
        // Used either for errors, or when selector isn't yet known
        explicit ResolvedSelector(FormattedPlaceholder&& value);
        bool hasSelector() const { return selector.isValid(); }
        const FormattedPlaceholder& argument() const { return value; }
        FormattedPlaceholder&& takeArgument() { return std::move(value); }
        const Selector* getSelector() {
            U_ASSERT(selector.isValid());
            return selector.getAlias();
        }
        FunctionOptions&& takeOptions() {
            return std::move(options);
        }
        const FunctionName& getSelectorName() const { return selectorName; }
        virtual ~ResolvedSelector();
        ResolvedSelector& operator=(ResolvedSelector&&) noexcept;
        ResolvedSelector(ResolvedSelector&&);
    private:
        FunctionName selectorName; // For error reporting
        LocalPointer<Selector> selector;
        FunctionOptions options;
        FormattedPlaceholder value;
    }; // class ResolvedSelector

    // Closures and environments
    // -------------------------

    class Environment;

    // A closure represents the right-hand side of a variable
    // declaration, along with an environment giving values
    // to its free variables
    class Closure : public UMemory {
    public:
        const Expression& getExpr() const {
            return expr;
        }
        const Environment& getEnv() const {
            return env;
        }
        Closure(const Expression& expression, const Environment& environment) : expr(expression), env(environment) {}
        Closure(Closure&&) = default;

        virtual ~Closure();
    private:

        // An unevaluated expression
        const Expression& expr;
        // The environment mapping names used in this
        // expression to other expressions
        const Environment& env;
    };

    // An environment is represented as a linked chain of
    // non-empty environments, terminating at an empty environment.
    // It's searched using linear search.
    class Environment : public UMemory {
    public:
        virtual bool has(const VariableName&) const = 0;
        virtual const Closure& lookup(const VariableName&) const = 0;
        static Environment* create(UErrorCode&);
        static Environment* create(const VariableName&, Closure&&, Environment*, UErrorCode&);
        virtual ~Environment();
    };

    class NonEmptyEnvironment;
    class EmptyEnvironment : public Environment {
    public:
        EmptyEnvironment() = default;
        virtual ~EmptyEnvironment();

    private:
        friend class Environment;

        bool has(const VariableName&) const override;
        const Closure& lookup(const VariableName&) const override;
        static EmptyEnvironment* create(UErrorCode&);
        static NonEmptyEnvironment* create(const VariableName&, Closure&&, Environment*, UErrorCode&);
    };

    class NonEmptyEnvironment : public Environment {
    private:
        friend class Environment;

        bool has(const VariableName&) const override;
        const Closure& lookup(const VariableName&) const override;
        static NonEmptyEnvironment* create(const VariableName&, Closure&&, const Environment*, UErrorCode&);
        virtual ~NonEmptyEnvironment();
    private:
        friend class Environment;

        NonEmptyEnvironment(const VariableName& v, Closure&& c, Environment* e) : var(v), rhs(std::move(c)), parent(e) {}

        // Maps VariableName onto Closure*
        // Chain of linked environments
        VariableName var;
        Closure rhs;
        const LocalPointer<Environment> parent;
    };

    // The context contains all the information needed to process
    // an entire message: arguments, formatter cache, and error list

    class MessageContext : public UMemory {
    public:
        MessageContext(const MessageArguments&, const StaticErrors&, UErrorCode&);

        const Formattable* getGlobal(const VariableName&, UErrorCode&) const;

        // If any errors were set, update `status` accordingly
        void checkErrors(UErrorCode& status) const;
        DynamicErrors& getErrors() { return errors; }

        virtual ~MessageContext();

    private:

        const MessageArguments& arguments; // External message arguments
        // Errors accumulated during parsing/formatting
        DynamicErrors errors;
    }; // class MessageContext

} // namespace message2

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif /* U_SHOW_CPLUSPLUS_API */

#endif // MESSAGEFORMAT2_EVALUATION_H

#endif // U_HIDE_DEPRECATED_API
// eof
