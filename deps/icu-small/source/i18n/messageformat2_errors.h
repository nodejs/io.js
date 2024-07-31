// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#ifndef U_HIDE_DEPRECATED_API

#ifndef MESSAGEFORMAT2_ERRORS_H
#define MESSAGEFORMAT2_ERRORS_H

#if U_SHOW_CPLUSPLUS_API

/**
 * \file
 * \brief C++ API: Formats messages using the draft MessageFormat 2.0.
 */

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "unicode/messageformat2_data_model_names.h"
#include "unicode/unistr.h"

#include "uvector.h"

U_NAMESPACE_BEGIN

namespace message2 {

    using namespace data_model;

    // Errors
    // ----------

    class DynamicErrors;
    class StaticErrors;

    // Internal class -- used as a private field in MessageFormatter
    template <typename ErrorType>
    class Error : public UObject {
    public:
        Error(ErrorType ty) : type(ty) {}
        Error(ErrorType ty, const UnicodeString& s) : type(ty), contents(s) {}
        virtual ~Error();
    private:
        friend class DynamicErrors;
        friend class StaticErrors;

        ErrorType type;
        UnicodeString contents;
    }; // class Error

    enum StaticErrorType {
        DuplicateDeclarationError,
        DuplicateOptionName,
        MissingSelectorAnnotation,
        NonexhaustivePattern,
        SyntaxError,
        UnsupportedStatementError,
        VariantKeyMismatchError
    };

    enum DynamicErrorType {
        UnresolvedVariable,
        FormattingError,
        OperandMismatchError,
        ReservedError,
        SelectorError,
        UnknownFunction,
    };

    using StaticError = Error<StaticErrorType>;
    using DynamicError = Error<DynamicErrorType>;

    // These explicit instantiations have to come before the
    // destructor definitions
    template<>
    Error<StaticErrorType>::~Error();
    template<>
    Error<DynamicErrorType>::~Error();

    class StaticErrors : public UObject {
    private:
        friend class DynamicErrors;

        LocalPointer<UVector> syntaxAndDataModelErrors;
        bool dataModelError = false;
        bool missingSelectorAnnotationError = false;
        bool syntaxError = false;

    public:
        StaticErrors(UErrorCode&);

        void setMissingSelectorAnnotation(UErrorCode&);
        void setDuplicateOptionName(UErrorCode&);
        void addSyntaxError(UErrorCode&);
        bool hasDataModelError() const { return dataModelError; }
        bool hasSyntaxError() const { return syntaxError; }
        bool hasMissingSelectorAnnotationError() const { return missingSelectorAnnotationError; }
        void addError(StaticError&&, UErrorCode&);
        void checkErrors(UErrorCode&);

        const StaticError& first() const;
        StaticErrors(const StaticErrors&, UErrorCode&);
        StaticErrors(StaticErrors&&) noexcept;
        virtual ~StaticErrors();
    }; // class StaticErrors

    class DynamicErrors : public UObject {
    private:
        const StaticErrors& staticErrors;
        LocalPointer<UVector> resolutionAndFormattingErrors;
        bool formattingError = false;
        bool selectorError = false;
        bool unknownFunctionError = false;
        bool unresolvedVariableError = false;

    public:
        DynamicErrors(const StaticErrors&, UErrorCode&);

        int32_t count() const;
        void setSelectorError(const FunctionName&, UErrorCode&);
        void setReservedError(UErrorCode&);
        void setUnresolvedVariable(const VariableName&, UErrorCode&);
        void setUnknownFunction(const FunctionName&, UErrorCode&);
        void setFormattingError(const FunctionName&, UErrorCode&);
        // Used when the name of the offending formatter is unknown
        void setFormattingError(UErrorCode&);
        void setOperandMismatchError(const FunctionName&, UErrorCode&);
        bool hasDataModelError() const { return staticErrors.hasDataModelError(); }
        bool hasFormattingError() const { return formattingError; }
        bool hasSelectorError() const { return selectorError; }
        bool hasSyntaxError() const { return staticErrors.hasSyntaxError(); }
        bool hasUnknownFunctionError() const { return unknownFunctionError; }
        bool hasMissingSelectorAnnotationError() const { return staticErrors.hasMissingSelectorAnnotationError(); }
        bool hasUnresolvedVariableError() const { return unresolvedVariableError; }
        void addError(DynamicError&&, UErrorCode&);
        void checkErrors(UErrorCode&) const;
        bool hasError() const;
        bool hasStaticError() const;

        const DynamicError& first() const;
        virtual ~DynamicErrors();
    }; // class DynamicErrors

} // namespace message2

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif /* U_SHOW_CPLUSPLUS_API */

#endif // MESSAGEFORMAT2_ERRORS_H

#endif // U_HIDE_DEPRECATED_API
// eof
