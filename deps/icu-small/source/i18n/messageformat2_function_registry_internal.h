// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#ifndef U_HIDE_DEPRECATED_API

#ifndef MESSAGEFORMAT2_FUNCTION_REGISTRY_INTERNAL_H
#define MESSAGEFORMAT2_FUNCTION_REGISTRY_INTERNAL_H

#if U_SHOW_CPLUSPLUS_API

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "unicode/datefmt.h"
#include "unicode/messageformat2_function_registry.h"

U_NAMESPACE_BEGIN

namespace message2 {

    // Built-in functions
    /*
      The standard functions are :datetime, :date, :time,
      :number, :integer, and :string,
      per https://github.com/unicode-org/message-format-wg/blob/main/spec/registry.md
      as of https://github.com/unicode-org/message-format-wg/releases/tag/LDML45-alpha
    */
    class StandardFunctions {
        friend class MessageFormatter;

        static UnicodeString getStringOption(const FunctionOptions& opts,
                                             const UnicodeString& optionName,
                                             UErrorCode& errorCode);

        class DateTime;

        class DateTimeFactory : public FormatterFactory {
        public:
            Formatter* createFormatter(const Locale& locale, UErrorCode& status) override;
            static DateTimeFactory* date(UErrorCode&);
            static DateTimeFactory* time(UErrorCode&);
            static DateTimeFactory* dateTime(UErrorCode&);
            DateTimeFactory() = delete;
            virtual ~DateTimeFactory();

        private:
            friend class DateTime;

            typedef enum DateTimeType {
                Date,
                Time,
                DateTime
            } DateTimeType;

            DateTimeType type;
            DateTimeFactory(DateTimeType t) : type(t) {}
        };

        class DateTime : public Formatter {
        public:
            FormattedPlaceholder format(FormattedPlaceholder&& toFormat, FunctionOptions&& options, UErrorCode& status) const override;
            virtual ~DateTime();

        private:
            const Locale& locale;
            const DateTimeFactory::DateTimeType type;
            friend class DateTimeFactory;
            DateTime(const Locale& l, DateTimeFactory::DateTimeType t) : locale(l), type(t) {}
            const LocalPointer<icu::DateFormat> icuFormatter;

            /*
              Looks up an option by name, first checking `opts`, then the cached options
              in `toFormat` if applicable, and finally using a default

              Ignores any options with non-string values
             */
            UnicodeString getFunctionOption(const FormattedPlaceholder& toFormat,
                                            const FunctionOptions& opts,
                                            const UnicodeString& optionName) const;
            // Version for options that don't have defaults; sets the error
            // code instead of returning a default value
            UnicodeString getFunctionOption(const FormattedPlaceholder& toFormat,
                                            const FunctionOptions& opts,
                                            const UnicodeString& optionName,
                                            UErrorCode& errorCode) const;

        };

        // Note: IntegerFactory doesn't implement SelectorFactory;
        // instead, an instance of PluralFactory is registered to the integer
        // selector
        // TODO
        class IntegerFactory : public FormatterFactory {
        public:
            Formatter* createFormatter(const Locale& locale, UErrorCode& status) override;
            virtual ~IntegerFactory();
        };

        class NumberFactory : public FormatterFactory {
        public:
            Formatter* createFormatter(const Locale& locale, UErrorCode& status) override;
            virtual ~NumberFactory();
        private:
            friend class IntegerFactory;
            static NumberFactory integer(const Locale& locale, UErrorCode& status);
        };

        class Number : public Formatter {
        public:
            FormattedPlaceholder format(FormattedPlaceholder&& toFormat, FunctionOptions&& options, UErrorCode& status) const override;
            virtual ~Number();

        private:
            friend class NumberFactory;
            friend class StandardFunctions;

            Number(const Locale& loc) : locale(loc), icuFormatter(number::NumberFormatter::withLocale(loc)) {}
            Number(const Locale& loc, bool isInt) : locale(loc), isInteger(isInt), icuFormatter(number::NumberFormatter::withLocale(loc)) {}
            static Number integer(const Locale& loc);

        // These options have their own accessor methods, since they have different default values.
            int32_t maximumFractionDigits(const FunctionOptions& options) const;
            int32_t minimumFractionDigits(const FunctionOptions& options) const;
            int32_t minimumSignificantDigits(const FunctionOptions& options) const;
            int32_t maximumSignificantDigits(const FunctionOptions& options) const;
            int32_t minimumIntegerDigits(const FunctionOptions& options) const;

            bool usePercent(const FunctionOptions& options) const;
            const Locale& locale;
            const bool isInteger = false;
            const number::LocalizedNumberFormatter icuFormatter;
        };

        static number::LocalizedNumberFormatter formatterForOptions(const Number& number,
                                                                    const FunctionOptions& opts,
                                                                    UErrorCode& status);

        class PluralFactory : public SelectorFactory {
        public:
            Selector* createSelector(const Locale& locale, UErrorCode& status) const override;
            virtual ~PluralFactory();

        private:
            friend class IntegerFactory;
            friend class MessageFormatter;

            PluralFactory() {}
            PluralFactory(bool isInt) : isInteger(isInt) {}
            static PluralFactory integer() { return PluralFactory(true);}
            const bool isInteger = false;
        };

        class Plural : public Selector {
        public:
            void selectKey(FormattedPlaceholder&& val,
                           FunctionOptions&& options,
                           const UnicodeString* keys,
                           int32_t keysLen,
                           UnicodeString* prefs,
                           int32_t& prefsLen,
                           UErrorCode& status) const override;
            virtual ~Plural();

        private:
            friend class IntegerFactory;
            friend class PluralFactory;

            // Can't use UPluralType for this since we want to include
            // exact matching as an option
            typedef enum PluralType {
                PLURAL_ORDINAL,
                PLURAL_CARDINAL,
                PLURAL_EXACT
            } PluralType;
            Plural(const Locale& loc, UErrorCode& errorCode);
            Plural(const Locale& loc, bool isInt, UErrorCode& errorCode);
            static Plural integer(const Locale& loc, UErrorCode& errorCode) { return Plural(loc, true, errorCode); }
            PluralType pluralType(const FunctionOptions& opts) const;
            const Locale& locale;
            const bool isInteger = false;
            LocalPointer<StandardFunctions::Number> numberFormatter;
        };

        class TextFactory : public SelectorFactory {
        public:
            Selector* createSelector(const Locale& locale, UErrorCode& status) const override;
            virtual ~TextFactory();
        };

        class TextSelector : public Selector {
        public:
            void selectKey(FormattedPlaceholder&& val,
                           FunctionOptions&& options,
                           const UnicodeString* keys,
                           int32_t keysLen,
                           UnicodeString* prefs,
                           int32_t& prefsLen,
                           UErrorCode& status) const override;
            virtual ~TextSelector();

        private:
            friend class TextFactory;

            // Formatting `value` to a string might require the locale
            const Locale& locale;

            TextSelector(const Locale& l) : locale(l) {}
        };
    };

    extern void formatDateWithDefaults(const Locale& locale, UDate date, UnicodeString&, UErrorCode& errorCode);
    extern number::FormattedNumber formatNumberWithDefaults(const Locale& locale, double toFormat, UErrorCode& errorCode);
    extern number::FormattedNumber formatNumberWithDefaults(const Locale& locale, int32_t toFormat, UErrorCode& errorCode);
    extern number::FormattedNumber formatNumberWithDefaults(const Locale& locale, int64_t toFormat, UErrorCode& errorCode);
    extern number::FormattedNumber formatNumberWithDefaults(const Locale& locale, StringPiece toFormat, UErrorCode& errorCode);
    extern DateFormat* defaultDateTimeInstance(const Locale&, UErrorCode&);

} // namespace message2

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif /* U_SHOW_CPLUSPLUS_API */

#endif // MESSAGEFORMAT2_FUNCTION_REGISTRY_INTERNAL_H

#endif // U_HIDE_DEPRECATED_API
// eof
