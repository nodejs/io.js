// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#ifndef U_HIDE_DEPRECATED_API

#ifndef MESSAGEFORMAT_CHECKER_H
#define MESSAGEFORMAT_CHECKER_H

#if U_SHOW_CPLUSPLUS_API

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "unicode/messageformat2_data_model.h"
#include "messageformat2_errors.h"

U_NAMESPACE_BEGIN

namespace message2 {

    using namespace data_model;

    // Used for checking missing selector annotation errors
    // and duplicate declaration errors (specifically for
    // implicit declarations)
    class TypeEnvironment : public UMemory {
    public:
        // MessageFormat has a simple type system;
        // variables are in-scope and annotated; in-scope and unannotated;
        // or free (a free variable has no explicit declaration in the scope
        // of its use.)
        enum Type {
            Annotated,
            Unannotated,
            FreeVariable
        };
        void extend(const VariableName&, Type, UErrorCode& status);
        Type get(const VariableName&) const;
        bool known(const VariableName&) const;
        TypeEnvironment(UErrorCode& status);

        virtual ~TypeEnvironment();

    private:
        // Stores variables known to be annotated.
        LocalPointer<UVector> annotated; // Vector of `VariableName`s
        // Stores variables that are in-scope but unannotated.
        LocalPointer<UVector> unannotated; // Vector of `VariableName`s
        // Stores free variables that are used in the RHS of a declaration
        LocalPointer<UVector> freeVars; // Vector of `VariableNames`; tracks free variables
                                        // This can't just be "variables that don't appear in
                                        // `annotated` or `unannotated`", as a use introduces
                                        // an explicit declaration
    }; // class TypeEnvironment

    // Checks a data model for semantic errors
    // (Errors are defined in https://github.com/unicode-org/message-format-wg/blob/main/spec/formatting.md       )
    class Checker {
    public:
        void check(UErrorCode&);
        Checker(const MFDataModel& m, StaticErrors& e) : dataModel(m), errors(e) {}
    private:

        void requireAnnotated(const TypeEnvironment&, const Expression&, UErrorCode&);
        void addFreeVars(TypeEnvironment& t, const Operand&, UErrorCode&);
        void addFreeVars(TypeEnvironment& t, const Operator&, UErrorCode&);
        void addFreeVars(TypeEnvironment& t, const OptionMap&, UErrorCode&);
        void addFreeVars(TypeEnvironment& t, const Expression&, UErrorCode&);
        void checkDeclarations(TypeEnvironment&, UErrorCode&);
        void checkSelectors(const TypeEnvironment&, UErrorCode&);
        void checkVariants(UErrorCode&);
        void check(const OptionMap&);
        void check(const Operand&);
        void check(const Expression&);
        void check(const Pattern&);
        const MFDataModel& dataModel;
        StaticErrors& errors;
    }; // class Checker

} // namespace message2

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif /* U_SHOW_CPLUSPLUS_API */

#endif // MESSAGEFORMAT_CHECKER_H

#endif // U_HIDE_DEPRECATED_API
// eof

