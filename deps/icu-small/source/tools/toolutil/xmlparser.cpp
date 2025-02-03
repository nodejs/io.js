// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2004-2010, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  xmlparser.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2004jul21
*   created by: Andy Heninger
*/

#include <stdio.h>
#include "unicode/uchar.h"
#include "unicode/ucnv.h"
#include "unicode/regex.h"
#include "filestrm.h"
#include "xmlparser.h"

#if !UCONFIG_NO_REGULAR_EXPRESSIONS && !UCONFIG_NO_CONVERSION

// character constants
enum {
    x_QUOT=0x22,
    x_AMP=0x26,
    x_APOS=0x27,
    x_LT=0x3c,
    x_GT=0x3e,
    x_l=0x6c
};

#define  XML_SPACES "[ \\u0009\\u000d\\u000a]"

// XML #4
#define  XML_NAMESTARTCHAR "[[A-Z]:_[a-z][\\u00c0-\\u00d6][\\u00d8-\\u00f6]" \
                    "[\\u00f8-\\u02ff][\\u0370-\\u037d][\\u037F-\\u1FFF][\\u200C-\\u200D]" \
                    "[\\u2070-\\u218F][\\u2C00-\\u2FEF][\\u3001-\\uD7FF][\\uF900-\\uFDCF]" \
                    "[\\uFDF0-\\uFFFD][\\U00010000-\\U000EFFFF]]"

//  XML #5
#define  XML_NAMECHAR "[" XML_NAMESTARTCHAR "\\-.[0-9]\\u00b7[\\u0300-\\u036f][\\u203f-\\u2040]]"

//  XML #6
#define  XML_NAME    XML_NAMESTARTCHAR "(?:" XML_NAMECHAR ")*"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(UXMLParser)
UOBJECT_DEFINE_RTTI_IMPLEMENTATION(UXMLElement)

//
//   UXMLParser constructor.   Mostly just initializes the ICU regexes that are
//                             used for parsing.
//
UXMLParser::UXMLParser(UErrorCode &status) :
      //  XML Declaration.  XML Production #23.
      //      example:  "<?xml version=1.0 encoding="utf-16" ?>
      //      This is a sloppy implementation - just look for the leading <?xml and the closing ?>
      //            allow for a possible leading BOM.
      mXMLDecl(UnicodeString("(?s)\\uFEFF?<\\?xml.+?\\?>", -1, US_INV), 0, status),
      
      //  XML Comment   production #15
      //     example:  "<!-- whatever -->
      //       note, does not detect an illegal "--" within comments
      mXMLComment(UnicodeString("(?s)<!--.+?-->", -1, US_INV), 0, status),
      
      //  XML Spaces
      //      production [3]
      mXMLSP(UnicodeString(XML_SPACES "+", -1, US_INV), 0, status),
      
      //  XML Doctype decl  production #28
      //     example   "<!DOCTYPE foo SYSTEM "somewhere" >
      //       or      "<!DOCTYPE foo [internal dtd]>
      //    TODO:  we don't actually parse the DOCTYPE or internal subsets.
      //           Some internal dtd subsets could confuse this simple-minded
      //           attempt at skipping over them, specifically, occurrences
      //           of closing square brackets.  These could appear in comments, 
      //           or in parameter entity declarations, for example.
      mXMLDoctype(UnicodeString(
           "(?s)<!DOCTYPE.*?(>|\\[.*?\\].*?>)", -1, US_INV
           ), 0, status),
      
      //  XML PI     production #16
      //     example   "<?target stuff?>
      mXMLPI(UnicodeString("(?s)<\\?.+?\\?>", -1, US_INV), 0, status),
      
      //  XML Element Start   Productions #40, #41
      //          example   <foo att1='abc'  att2="d e f" >
      //      capture #1:  the tag name
      //
      mXMLElemStart (UnicodeString("(?s)<(" XML_NAME ")"                                 // match  "<tag_name"
          "(?:" 
                XML_SPACES "+" XML_NAME XML_SPACES "*=" XML_SPACES "*"     // match  "ATTR_NAME = "
                "(?:(?:\\\'[^<\\\']*?\\\')|(?:\\\"[^<\\\"]*?\\\"))"        // match  '"attribute value"'
          ")*"                                                             //   * for zero or more attributes.
          XML_SPACES "*?>", -1, US_INV), 0, status),                               // match " >"
      
      //  XML Element End     production #42
      //     example   </foo>
      mXMLElemEnd (UnicodeString("</(" XML_NAME ")" XML_SPACES "*>", -1, US_INV), 0, status),
      
      // XML Element Empty    production #44
      //     example   <foo att1="abc"   att2="d e f" />
      mXMLElemEmpty (UnicodeString("(?s)<(" XML_NAME ")"                                 // match  "<tag_name"
          "(?:" 
                XML_SPACES "+" XML_NAME XML_SPACES "*=" XML_SPACES "*"     // match  "ATTR_NAME = "
                "(?:(?:\\\'[^<\\\']*?\\\')|(?:\\\"[^<\\\"]*?\\\"))"        // match  '"attribute value"'
          ")*"                                                             //   * for zero or more attributes.
          XML_SPACES "*?/>", -1, US_INV), 0, status),                              // match " />"
      

      // XMLCharData.  Everything but '<'.  Note that & will be dealt with later.
      mXMLCharData(UnicodeString("(?s)[^<]*", -1, US_INV), 0, status),

      // Attribute name = "value".  XML Productions 10, 40/41
      //  Capture group 1 is name, 
      //                2 is the attribute value, including the quotes.
      //
      //   Note that attributes are scanned twice.  The first time is with
      //        the regex for an entire element start.  There, the attributes
      //        are checked syntactically, but not separated out one by one.
      //        Here, we match a single attribute, and make its name and
      //        attribute value available to the parser code.
      mAttrValue(UnicodeString(XML_SPACES "+("  XML_NAME ")"  XML_SPACES "*=" XML_SPACES "*"
         "((?:\\\'[^<\\\']*?\\\')|(?:\\\"[^<\\\"]*?\\\"))", -1, US_INV), 0, status),


      mAttrNormalizer(UnicodeString(XML_SPACES, -1, US_INV), 0, status),

      // Match any of the new-line sequences in content.
      //   All are changed to \u000a.
      mNewLineNormalizer(UnicodeString("\\u000d\\u000a|\\u000d\\u0085|\\u000a|\\u000d|\\u0085|\\u2028", -1, US_INV), 0, status),

      // & char references
      //   We will figure out what we've got based on which capture group has content.
      //   The last one is a catchall for unrecognized entity references..
      //             1     2     3      4      5           6                    7          8
      mAmps(UnicodeString("&(?:(amp;)|(lt;)|(gt;)|(apos;)|(quot;)|#x([0-9A-Fa-f]{1,8});|#([0-9]{1,8});|(.))"),
                0, status),

      fNames(status),
      fElementStack(status),
      fOneLF(static_cast<char16_t>(0x0a)) // Plain new-line string, used in new line normalization.
      {
      }

UXMLParser *
UXMLParser::createParser(UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) {
        return nullptr;
    } else {
        return new UXMLParser(errorCode);
    }
}

UXMLParser::~UXMLParser() {}

UXMLElement *
UXMLParser::parseFile(const char *filename, UErrorCode &errorCode) {
    char bytes[4096], charsetBuffer[100];
    FileStream *f;
    const char *charset, *pb;
    UnicodeString src;
    UConverter *cnv;
    char16_t *buffer, *pu;
    int32_t fileLength, bytesLength, length, capacity;
    UBool flush;

    if(U_FAILURE(errorCode)) {
        return nullptr;
    }

    f=T_FileStream_open(filename, "rb");
    if(f==nullptr) {
        errorCode=U_FILE_ACCESS_ERROR;
        return nullptr;
    }

    bytesLength = T_FileStream_read(f, bytes, static_cast<int32_t>(sizeof(bytes)));
    if (bytesLength < static_cast<int32_t>(sizeof(bytes))) {
        // we have already read the entire file
        fileLength=bytesLength;
    } else {
        // get the file length
        fileLength=T_FileStream_size(f);
    }

    /*
     * get the charset:
     * 1. Unicode signature
     * 2. treat as ISO-8859-1 and read XML encoding="charser"
     * 3. default to UTF-8
     */
    charset=ucnv_detectUnicodeSignature(bytes, bytesLength, nullptr, &errorCode);
    if(U_SUCCESS(errorCode) && charset!=nullptr) {
        // open converter according to Unicode signature
        cnv=ucnv_open(charset, &errorCode);
    } else {
        // read as Latin-1 and parse the XML declaration and encoding
        cnv=ucnv_open("ISO-8859-1", &errorCode);
        if(U_FAILURE(errorCode)) {
            // unexpected error opening Latin-1 converter
            goto exit;
        }

        buffer=toUCharPtr(src.getBuffer(bytesLength));
        if(buffer==nullptr) {
            // unexpected failure to reserve some string capacity
            errorCode=U_MEMORY_ALLOCATION_ERROR;
            goto exit;
        }
        pb=bytes;
        pu=buffer;
        ucnv_toUnicode(
            cnv,
            &pu, buffer+src.getCapacity(),
            &pb, bytes+bytesLength,
            nullptr, true, &errorCode);
        src.releaseBuffer(U_SUCCESS(errorCode) ? static_cast<int32_t>(pu - buffer) : 0);
        ucnv_close(cnv);
        cnv=nullptr;
        if(U_FAILURE(errorCode)) {
            // unexpected error in conversion from Latin-1
            src.remove();
            goto exit;
        }

        // parse XML declaration
        if(mXMLDecl.reset(src).lookingAt(0, errorCode)) {
            int32_t declEnd=mXMLDecl.end(errorCode);
            // go beyond <?xml
            int32_t pos = src.indexOf(static_cast<char16_t>(x_l)) + 1;

            mAttrValue.reset(src);
            while(pos<declEnd && mAttrValue.lookingAt(pos, errorCode)) {  // loop runs once per attribute on this element.
                UnicodeString attName  = mAttrValue.group(1, errorCode);
                UnicodeString attValue = mAttrValue.group(2, errorCode);

                // Trim the quotes from the att value.  These are left over from the original regex
                //   that parsed the attribute, which couldn't conveniently strip them.
                attValue.remove(0,1);                    // one char from the beginning
                attValue.truncate(attValue.length()-1);  // and one from the end.

                if(attName==UNICODE_STRING("encoding", 8)) {
                    length = attValue.extract(0, 0x7fffffff, charsetBuffer, static_cast<int32_t>(sizeof(charsetBuffer)));
                    charset=charsetBuffer;
                    break;
                }
                pos = mAttrValue.end(2, errorCode);
            }

            if(charset==nullptr) {
                // default to UTF-8
                charset="UTF-8";
            }
            cnv=ucnv_open(charset, &errorCode);
        }
    }

    if(U_FAILURE(errorCode)) {
        // unable to open the converter
        goto exit;
    }

    // convert the file contents
    capacity=fileLength;        // estimated capacity
    src.getBuffer(capacity);
    src.releaseBuffer(0);       // zero length
    flush=false;
    for(;;) {
        // convert contents of bytes[bytesLength]
        pb=bytes;
        for(;;) {
            length=src.length();
            buffer=toUCharPtr(src.getBuffer(capacity));
            if(buffer==nullptr) {
                // unexpected failure to reserve some string capacity
                errorCode=U_MEMORY_ALLOCATION_ERROR;
                goto exit;
            }

            pu=buffer+length;
            ucnv_toUnicode(
                cnv, &pu, buffer+src.getCapacity(),
                &pb, bytes+bytesLength,
                nullptr, false, &errorCode);
            src.releaseBuffer(U_SUCCESS(errorCode) ? static_cast<int32_t>(pu - buffer) : 0);
            if(errorCode==U_BUFFER_OVERFLOW_ERROR) {
                errorCode=U_ZERO_ERROR;
                capacity=(3*src.getCapacity())/2; // increase capacity by 50%
            } else {
                break;
            }
        }

        if(U_FAILURE(errorCode)) {
            break; // conversion error
        }

        if(flush) {
            break; // completely converted the file
        }

        // read next block
        bytesLength = T_FileStream_read(f, bytes, static_cast<int32_t>(sizeof(bytes)));
        if(bytesLength==0) {
            // reached end of file, convert once more to flush the converter
            flush=true;
        }
    }

exit:
    ucnv_close(cnv);
    T_FileStream_close(f);

    if(U_SUCCESS(errorCode)) {
        return parse(src, errorCode);
    } else {
        return nullptr;
    }
}

UXMLElement *
UXMLParser::parse(const UnicodeString &src, UErrorCode &status) {
    if(U_FAILURE(status)) {
        return nullptr;
    }

    UXMLElement   *root = nullptr;
    fPos = 0; // TODO use just a local pos variable and pass it into functions
              // where necessary?

    // set all matchers to work on the input string
    mXMLDecl.reset(src);
    mXMLComment.reset(src);
    mXMLSP.reset(src);
    mXMLDoctype.reset(src);
    mXMLPI.reset(src);
    mXMLElemStart.reset(src);
    mXMLElemEnd.reset(src);
    mXMLElemEmpty.reset(src);
    mXMLCharData.reset(src);
    mAttrValue.reset(src);
    mAttrNormalizer.reset(src);
    mNewLineNormalizer.reset(src);
    mAmps.reset(src);

    // Consume the XML Declaration, if present.
    if (mXMLDecl.lookingAt(fPos, status)) {
        fPos = mXMLDecl.end(status);
    }

    // Consume "misc" [XML production 27] appearing before DocType
    parseMisc(status);

    // Consume a DocType declaration, if present.
    if (mXMLDoctype.lookingAt(fPos, status)) {
        fPos = mXMLDoctype.end(status);
    }

    // Consume additional "misc" [XML production 27] appearing after the DocType
    parseMisc(status);

    // Get the root element
    if (mXMLElemEmpty.lookingAt(fPos, status)) {
        // Root is an empty element (no nested elements or content)
        root = createElement(mXMLElemEmpty, status);
        fPos = mXMLElemEmpty.end(status);
    } else {
        if (mXMLElemStart.lookingAt(fPos, status) == false) {
            error("Root Element expected", status);
            goto errorExit;
        }
        root = createElement(mXMLElemStart, status);
        UXMLElement  *el = root;

        //
        // This is the loop that consumes the root element of the document,
        //      including all nested content.   Nested elements are handled by
        //      explicit pushes/pops of the element stack; there is no recursion
        //      in the control flow of this code.
        //      "el" always refers to the current element, the one to which content
        //      is being added.  It is above the top of the element stack.
        for (;;) {
            // Nested Element Start
            if (mXMLElemStart.lookingAt(fPos, status)) {
                UXMLElement *t = createElement(mXMLElemStart, status);
                el->fChildren.addElement(t, status);
                t->fParent = el;
                fElementStack.push(el, status);
                el = t;
                continue;
            }

            // Text Content.  String is concatenated onto the current node's content,
            //                but only if it contains something other than spaces.
            UnicodeString s = scanContent(status);
            if (s.length() > 0) {
                mXMLSP.reset(s);
                if (mXMLSP.matches(status) == false) {
                    // This chunk of text contains something other than just
                    //  white space. Make a child node for it.
                    replaceCharRefs(s, status);
                    el->fChildren.addElement(s.clone(), status);
                }
                mXMLSP.reset(src);    // The matchers need to stay set to the main input string.
                continue;
            }

            // Comments.  Discard.
            if (mXMLComment.lookingAt(fPos, status)) {
                fPos = mXMLComment.end(status);
                continue;
            }

            // PIs.  Discard.
            if (mXMLPI.lookingAt(fPos, status)) {
                fPos = mXMLPI.end(status);
                continue;
            }

            // Element End
            if (mXMLElemEnd.lookingAt(fPos, status)) {
                fPos = mXMLElemEnd.end(0, status);
                const UnicodeString name = mXMLElemEnd.group(1, status);
                if (name != *el->fName) {
                    error("Element start / end tag mismatch", status);
                    goto errorExit;
                }
                if (fElementStack.empty()) {
                    // Close of the root element.  We're done with the doc.
                    el = nullptr;
                    break;
                }
                el = static_cast<UXMLElement*>(fElementStack.pop());
                continue;
            }

            // Empty Element.  Stored as a child of the current element, but not stacked.
            if (mXMLElemEmpty.lookingAt(fPos, status)) {
                UXMLElement *t = createElement(mXMLElemEmpty, status);
                el->fChildren.addElement(t, status);
                continue;
            }

            // Hit something within the document that doesn't match anything.
            //   It's an error.
            error("Unrecognized markup", status);
            break;
        }

        if (el != nullptr || !fElementStack.empty()) {
            // We bailed out early, for some reason.
            error("Root element not closed.", status);
            goto errorExit;
        }
    }

    // Root Element parse is complete.
    // Consume the annoying xml "Misc" that can appear at the end of the doc.
    parseMisc(status);

    // We should have reached the end of the input
    if (fPos != src.length()) {
        error("Extra content at the end of the document", status);
        goto errorExit;
    }

    // Success!
    return root;

errorExit:
    delete root;
    return nullptr;
}

//
//  createElement
//      We've just matched an element start tag.  Create and fill in a UXMLElement object
//      for it.
//
UXMLElement *
UXMLParser::createElement(RegexMatcher  &mEl, UErrorCode &status) {
    // First capture group is the element's name.
    UXMLElement *el = new UXMLElement(this, intern(mEl.group(1, status), status), status);

    // Scan for attributes.
    int32_t   pos = mEl.end(1, status);  // The position after the end of the tag name

    while (mAttrValue.lookingAt(pos, status)) {  // loop runs once per attribute on this element.
        UnicodeString attName  = mAttrValue.group(1, status);
        UnicodeString attValue = mAttrValue.group(2, status);

        // Trim the quotes from the att value.  These are left over from the original regex
        //   that parsed the attribute, which couldn't conveniently strip them.
        attValue.remove(0,1);                    // one char from the beginning
        attValue.truncate(attValue.length()-1);  // and one from the end.
        
        // XML Attribute value normalization. 
        // This is one of the really screwy parts of the XML spec.
        // See http://www.w3.org/TR/2004/REC-xml11-20040204/#AVNormalize
        // Note that non-validating parsers must treat all entities as type CDATA
        //   which simplifies things some.

        // Att normalization step 1:  normalize any newlines in the attribute value
        mNewLineNormalizer.reset(attValue);
        attValue = mNewLineNormalizer.replaceAll(fOneLF, status);

        // Next change all xml white space chars to plain \u0020 spaces.
        mAttrNormalizer.reset(attValue);
        UnicodeString oneSpace(static_cast<char16_t>(0x0020));
        attValue = mAttrNormalizer.replaceAll(oneSpace, status);

        // Replace character entities.
        replaceCharRefs(attValue, status);

        // Save the attribute name and value in our document structure.
        el->fAttNames.addElement((void *)intern(attName, status), status);
        el->fAttValues.addElement(attValue.clone(), status);
        pos = mAttrValue.end(2, status);
    }
    fPos = mEl.end(0, status);
    return el;
}

//
//  parseMisc
//     Consume XML "Misc" [production #27]
//        which is any combination of space, PI and comments
//      Need to watch end-of-input because xml MISC stuff is allowed after
//        the document element, so we WILL scan off the end in this function
//
void
UXMLParser::parseMisc(UErrorCode &status)  {
    for (;;) {
        if (fPos >= mXMLPI.input().length()) {
            break;
        }
        if (mXMLPI.lookingAt(fPos, status)) {
            fPos = mXMLPI.end(status);
            continue;
        }
        if (mXMLSP.lookingAt(fPos, status)) {
            fPos = mXMLSP.end(status);
            continue;
        }
        if (mXMLComment.lookingAt(fPos, status)) {
            fPos = mXMLComment.end(status);
            continue;
        }
        break;
    }
}

//
//  Scan for document content.
//
UnicodeString
UXMLParser::scanContent(UErrorCode &status) {
    UnicodeString  result;
    if (mXMLCharData.lookingAt(fPos, status)) {
        result = mXMLCharData.group(static_cast<int32_t>(0), status);
        // Normalize the new-lines.  (Before char ref substitution)
        mNewLineNormalizer.reset(result);
        result = mNewLineNormalizer.replaceAll(fOneLF, status);
        
        // TODO:  handle CDATA
        fPos = mXMLCharData.end(0, status);
    }

    return result;
}

//
//   replaceCharRefs
//
//      replace the char entities &lt;  &amp; &#123; &#x12ab; etc. in a string
//       with the corresponding actual character.
//
void
UXMLParser::replaceCharRefs(UnicodeString &s, UErrorCode &status) {
    UnicodeString result;
    UnicodeString replacement;
    int     i;

    mAmps.reset(s);
    // See the initialization for the regex matcher mAmps.
    //    Which entity we've matched is determined by which capture group has content,
    //      which is flagged by start() of that group not being -1.
    while (mAmps.find()) {
        if (mAmps.start(1, status) != -1) {
            replacement.setTo(static_cast<char16_t>(x_AMP));
        } else if (mAmps.start(2, status) != -1) {
            replacement.setTo(static_cast<char16_t>(x_LT));
        } else if (mAmps.start(3, status) != -1) {
            replacement.setTo(static_cast<char16_t>(x_GT));
        } else if (mAmps.start(4, status) != -1) {
            replacement.setTo(static_cast<char16_t>(x_APOS));
        } else if (mAmps.start(5, status) != -1) {
            replacement.setTo(static_cast<char16_t>(x_QUOT));
        } else if (mAmps.start(6, status) != -1) {
            UnicodeString hexString = mAmps.group(6, status);
            UChar32 val = 0;
            for (i=0; i<hexString.length(); i++) {
                val = (val << 4) + u_digit(hexString.charAt(i), 16);
            }
            // TODO:  some verification that the character is valid
            replacement.setTo(val);
        } else if (mAmps.start(7, status) != -1) {
            UnicodeString decimalString = mAmps.group(7, status);
            UChar32 val = 0;
            for (i=0; i<decimalString.length(); i++) {
                val = val*10 + u_digit(decimalString.charAt(i), 10);
            }
            // TODO:  some verification that the character is valid
            replacement.setTo(val);
        } else {
            // An unrecognized &entity;  Leave it alone.
            //  TODO:  check that it really looks like an entity, and is not some
            //         random & in the text.
            replacement = mAmps.group(static_cast<int32_t>(0), status);
        }
        mAmps.appendReplacement(result, replacement, status);
    }
    mAmps.appendTail(result);
    s = result;
}

void
UXMLParser::error(const char *message, UErrorCode &status) {
    // TODO:  something better here...
    const UnicodeString &src=mXMLDecl.input();
    int  line = 0;
    int  ci = 0;
    while (ci < fPos && ci>=0) {
        ci = src.indexOf(static_cast<char16_t>(0x0a), ci + 1);
        line++;
    }
    fprintf(stderr, "Error: %s at line %d\n", message, line);
    if (U_SUCCESS(status)) {
        status = U_PARSE_ERROR;
    }
}

// intern strings like in Java

const UnicodeString *
UXMLParser::intern(const UnicodeString &s, UErrorCode &errorCode) {
    const UHashElement *he=fNames.find(s);
    if(he!=nullptr) {
        // already a known name, return its hashed key pointer
        return static_cast<const UnicodeString*>(he->key.pointer);
    } else {
        // add this new name and return its hashed key pointer
        fNames.puti(s, 1, errorCode);
        he=fNames.find(s);
        return static_cast<const UnicodeString*>(he->key.pointer);
    }
}

const UnicodeString *
UXMLParser::findName(const UnicodeString &s) const {
    const UHashElement *he=fNames.find(s);
    if(he!=nullptr) {
        // a known name, return its hashed key pointer
        return static_cast<const UnicodeString*>(he->key.pointer);
    } else {
        // unknown name
        return nullptr;
    }
}

// UXMLElement ------------------------------------------------------------- ***

UXMLElement::UXMLElement(const UXMLParser *parser, const UnicodeString *name, UErrorCode &errorCode) :
   fParser(parser),
   fName(name),
   fAttNames(errorCode),
   fAttValues(errorCode),
   fChildren(errorCode),
   fParent(nullptr)
{
}

UXMLElement::~UXMLElement() {
    int   i;
    // attribute names are owned by the UXMLParser, don't delete them here
    for (i=fAttValues.size()-1; i>=0; i--) {
        delete static_cast<UObject*>(fAttValues.elementAt(i));
    }
    for (i=fChildren.size()-1; i>=0; i--) {
        delete static_cast<UObject*>(fChildren.elementAt(i));
    }
}

const UnicodeString &
UXMLElement::getTagName() const {
    return *fName;
}

UnicodeString
UXMLElement::getText(UBool recurse) const {
    UnicodeString text;
    appendText(text, recurse);
    return text;
}

void
UXMLElement::appendText(UnicodeString &text, UBool recurse) const {
    const UObject *node;
    int32_t i, count=fChildren.size();
    for(i=0; i<count; ++i) {
        node = static_cast<const UObject*>(fChildren.elementAt(i));
        const UnicodeString *s=dynamic_cast<const UnicodeString *>(node);
        if(s!=nullptr) {
            text.append(*s);
        } else if(recurse) /* must be a UXMLElement */ {
            ((const UXMLElement *)node)->appendText(text, recurse);
        }
    }
}

int32_t
UXMLElement::countAttributes() const {
    return fAttNames.size();
}

const UnicodeString *
UXMLElement::getAttribute(int32_t i, UnicodeString &name, UnicodeString &value) const {
    if(0<=i && i<fAttNames.size()) {
        name.setTo(*static_cast<const UnicodeString*>(fAttNames.elementAt(i)));
        value.setTo(*static_cast<const UnicodeString*>(fAttValues.elementAt(i)));
        return &value; // or return (UnicodeString *)fAttValues.elementAt(i);
    } else {
        return nullptr;
    }
}

const UnicodeString *
UXMLElement::getAttribute(const UnicodeString &name) const {
    // search for the attribute name by comparing the interned pointer,
    // not the string contents
    const UnicodeString *p=fParser->findName(name);
    if(p==nullptr) {
        return nullptr; // no such attribute seen by the parser at all
    }

    int32_t i, count=fAttNames.size();
    for(i=0; i<count; ++i) {
        if (p == static_cast<const UnicodeString*>(fAttNames.elementAt(i))) {
            return static_cast<const UnicodeString*>(fAttValues.elementAt(i));
        }
    }
    return nullptr;
}

int32_t
UXMLElement::countChildren() const {
    return fChildren.size();
}

const UObject *
UXMLElement::getChild(int32_t i, UXMLNodeType &type) const {
    if(0<=i && i<fChildren.size()) {
        const UObject* node = static_cast<const UObject*>(fChildren.elementAt(i));
        if(dynamic_cast<const UXMLElement *>(node)!=nullptr) {
            type=UXML_NODE_TYPE_ELEMENT;
        } else {
            type=UXML_NODE_TYPE_STRING;
        }
        return node;
    } else {
        return nullptr;
    }
}

const UXMLElement *
UXMLElement::nextChildElement(int32_t &i) const {
    if(i<0) {
        return nullptr;
    }

    const UObject *node;
    int32_t count=fChildren.size();
    while(i<count) {
        node = static_cast<const UObject*>(fChildren.elementAt(i++));
        const UXMLElement *elem=dynamic_cast<const UXMLElement *>(node);
        if(elem!=nullptr) {
            return elem;
        }
    }
    return nullptr;
}

const UXMLElement *
UXMLElement::getChildElement(const UnicodeString &name) const {
    // search for the element name by comparing the interned pointer,
    // not the string contents
    const UnicodeString *p=fParser->findName(name);
    if(p==nullptr) {
        return nullptr; // no such element seen by the parser at all
    }

    const UObject *node;
    int32_t i, count=fChildren.size();
    for(i=0; i<count; ++i) {
        node = static_cast<const UObject*>(fChildren.elementAt(i));
        const UXMLElement *elem=dynamic_cast<const UXMLElement *>(node);
        if(elem!=nullptr) {
            if(p==elem->fName) {
                return elem;
            }
        }
    }
    return nullptr;
}

U_NAMESPACE_END

#endif /* !UCONFIG_NO_REGULAR_EXPRESSIONS */

