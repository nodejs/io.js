// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 1997-2013, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*
* File resbund.cpp
*
* Modification History:
*
*   Date        Name        Description
*   02/05/97    aliu        Fixed bug in chopLocale.  Added scanForLocaleInFile
*                           based on code taken from scanForLocale.  Added
*                           constructor which attempts to read resource bundle
*                           from a specific file, without searching other files.
*   02/11/97    aliu        Added UErrorCode return values to constructors. Fixed
*                           infinite loops in scanForFile and scanForLocale.
*                           Modified getRawResourceData to not delete storage in
*                           localeData and resourceData which it doesn't own.
*                           Added Mac compatibility #ifdefs for tellp() and
*                           ios::nocreate.
*   03/04/97    aliu        Modified to use ExpandingDataSink objects instead of
*                           the highly inefficient ostrstream objects.
*   03/13/97    aliu        Rewrote to load in entire resource bundle and store
*                           it as a Hashtable of ResourceBundleData objects.
*                           Added state table to govern parsing of files.
*                           Modified to load locale index out of new file distinct
*                           from default.txt.
*   03/25/97    aliu        Modified to support 2-d arrays, needed for timezone data.
*                           Added support for custom file suffixes.  Again, needed
*                           to support timezone data.  Improved error handling to
*                           detect duplicate tags and subtags.
*   04/07/97    aliu        Fixed bug in getHashtableForLocale().  Fixed handling
*                           of failing UErrorCode values on entry to API methods.
*                           Fixed bugs in getArrayItem() for negative indices.
*   04/29/97    aliu        Update to use new Hashtable deletion protocol.
*   05/06/97    aliu        Flattened kTransitionTable for HP compiler.
*                           Fixed usage of CharString.
* 06/11/99      stephen     Removed parsing of .txt files.
*                           Reworked to use new binary format.
*                           Cleaned up.
* 06/14/99      stephen     Removed methods taking a filename suffix.
* 06/22/99      stephen     Added missing T_FileStream_close in parse()
* 11/09/99      weiv        Added getLocale(), rewritten constructForLocale()
* March 2000    weiv        complete overhaul.
******************************************************************************
*/

#include "unicode/utypes.h"
#include "unicode/resbund.h"

#include "cmemory.h"
#include "mutex.h"
#include "uassert.h"
#include "umutex.h"

#include "uresimp.h"

U_NAMESPACE_BEGIN

/*-----------------------------------------------------------------------------
 * Implementation Notes
 *
 * Resource bundles are read in once, and thereafter cached.
 * ResourceBundle statically keeps track of which files have been
 * read, so we are guaranteed that each file is read at most once.
 * Resource bundles can be loaded from different data directories and
 * will be treated as distinct, even if they are for the same locale.
 *
 * Resource bundles are lightweight objects, which have pointers to
 * one or more shared Hashtable objects containing all the data.
 * Copying would be cheap, but there is no copy constructor, since
 * there wasn't one in the original API.
 *
 * The ResourceBundle parsing mechanism is implemented as a transition
 * network, for easy maintenance and modification.  The network is
 * implemented as a matrix (instead of in code) to make this even
 * easier.  The matrix contains Transition objects.  Each Transition
 * object describes a destination node and an action to take before
 * moving to the destination node.  The source node is encoded by the
 * index of the object in the array that contains it.  The pieces
 * needed to understand the transition network are the enums for node
 * IDs and actions, the parse() method, which walks through the
 * network and implements the actions, and the network itself.  The
 * network guarantees certain conditions, for example, that a new
 * resource will not be closed until one has been opened first; or
 * that data will not be stored into a TaggedList until a TaggedList
 * has been created.  Nonetheless, the code in parse() does some
 * consistency checks as it runs the network, and fails with an
 * U_INTERNAL_PROGRAM_ERROR if one of these checks fails.  If the input
 * data has a bad format, an U_INVALID_FORMAT_ERROR is returned.  If you
 * see an U_INTERNAL_PROGRAM_ERROR the transition matrix has a bug in
 * it.
 *
 * Old functionality of multiple locales in a single file is still
 * supported.  For this reason, LOCALE names override FILE names.  If
 * data for en_US is located in the en.txt file, once it is loaded,
 * the code will not care where it came from (other than remembering
 * which directory it came from).  However, if there is an en_US
 * resource in en_US.txt, that will take precedence.  There is no
 * limit to the number or type of resources that can be stored in a
 * file, however, files are only searched in a specific way.  If
 * en_US_CA is requested, then first en_US_CA.txt is searched, then
 * en_US.txt, then en.txt, then default.txt.  So it only makes sense
 * to put certain locales in certain files.  In this example, it would
 * be logical to put en_US_CA, en_US, and en into the en.txt file,
 * since they would be found there if asked for.  The extreme example
 * is to place all locale resources into default.txt, which should
 * also work.
 *
 * Inheritance is implemented.  For example, xx_YY_zz inherits as
 * follows: xx_YY_zz, xx_YY, xx, default.  Inheritance is implemented
 * as an array of hashtables.  There will be from 1 to 4 hashtables in
 * the array.
 *
 * Fallback files are implemented.  The fallback pattern is Language
 * Country Variant (LCV) -> LC -> L.  Fallback is first done for the
 * requested locale.  Then it is done for the default locale, as
 * returned by Locale::getDefault().  Then the special file
 * default.txt is searched for the default locale.  The overall FILE
 * fallback path is LCV -> LC -> L -> dLCV -> dLC -> dL -> default.
 *
 * Note that although file name searching includes the default locale,
 * once a ResourceBundle object is constructed, the inheritance path
 * no longer includes the default locale.  The path is LCV -> LC -> L
 * -> default.
 *
 * File parsing is lazy.  Nothing is parsed unless it is called for by
 * someone.  So when a ResourceBundle for xx_YY_zz is constructed,
 * only that locale is parsed (along with anything else in the same
 * file).  Later, if the FooBar tag is asked for, and if it isn't
 * found in xx_YY_zz, then xx_YY.txt will be parsed and checked, and
 * so forth, until the chain is exhausted or the tag is found.
 *
 * Thread-safety is implemented around caches, both the cache that
 * stores all the resource data, and the cache that stores flags
 * indicating whether or not a file has been visited.  These caches
 * delete their storage at static cleanup time, when the process
 * quits.
 *
 * ResourceBundle supports TableCollation as a special case.  This
 * involves having special ResourceBundle objects which DO own their
 * data, since we don't want large collation rule strings in the
 * ResourceBundle cache (these are already cached in the
 * TableCollation cache).  TableCollation files (.ctx files) have the
 * same format as normal resource data files, with a different
 * interpretation, from the standpoint of ResourceBundle.  .ctx files
 * are loaded into otherwise ordinary ResourceBundle objects.  They
 * don't inherit (that's implemented by TableCollation) and they own
 * their data (as mentioned above).  However, they still support
 * possible multiple locales in a single .ctx file.  (This is in
 * practice a bad idea, since you only want the one locale you're
 * looking for, and only one tag will be present
 * ("CollationElements"), so you don't need an inheritance chain of
 * multiple locales.)  Up to 4 locale resources will be loaded from a
 * .ctx file; everything after the first 4 is ignored (parsed and
 * deleted).  (Normal .txt files have no limit.)  Instead of being
 * loaded into the cache, and then looked up as needed, the locale
 * resources are read straight into the ResourceBundle object.
 *
 * The Index, which used to reside in default.txt, has been moved to a
 * new file, index.txt.  This file contains a slightly modified format
 * with the addition of the "InstalledLocales" tag; it looks like:
 *
 * Index {
 *   InstalledLocales {
 *     ar
 *     ..
 *     zh_TW
 *   }
 * }
 */
//-----------------------------------------------------------------------------

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(ResourceBundle)

ResourceBundle::ResourceBundle(UErrorCode &err)
                                :UObject(), fLocale(nullptr)
{
    fResource = ures_open(nullptr, Locale::getDefault().getName(), &err);
}

ResourceBundle::ResourceBundle(const ResourceBundle &other)
                              :UObject(other), fLocale(nullptr)
{
    UErrorCode status = U_ZERO_ERROR;

    if (other.fResource) {
        fResource = ures_copyResb(nullptr, other.fResource, &status);
    } else {
        /* Copying a bad resource bundle */
        fResource = nullptr;
    }
}

ResourceBundle::ResourceBundle(UResourceBundle *res, UErrorCode& err)
                               :UObject(), fLocale(nullptr)
{
    if (res) {
        fResource = ures_copyResb(nullptr, res, &err);
    } else {
        /* Copying a bad resource bundle */
        fResource = nullptr;
    }
}

ResourceBundle::ResourceBundle(const char* path, const Locale& locale, UErrorCode& err) 
                               :UObject(), fLocale(nullptr)
{
    fResource = ures_open(path, locale.getName(), &err);
}


ResourceBundle& ResourceBundle::operator=(const ResourceBundle& other)
{
    if(this == &other) {
        return *this;
    }
    if (fResource != nullptr) {
        ures_close(fResource);
        fResource = nullptr;
    }
    if (fLocale != nullptr) {
        delete fLocale;
        fLocale = nullptr;
    }
    UErrorCode status = U_ZERO_ERROR;
    if (other.fResource) {
        fResource = ures_copyResb(nullptr, other.fResource, &status);
    } else {
        /* Copying a bad resource bundle */
        fResource = nullptr;
    }
    return *this;
}

ResourceBundle::~ResourceBundle()
{
    if (fResource != nullptr) {
        ures_close(fResource);
    }
    delete fLocale;
}

ResourceBundle *
ResourceBundle::clone() const {
    return new ResourceBundle(*this);
}

UnicodeString ResourceBundle::getString(UErrorCode& status) const {
    int32_t len = 0;
    const char16_t *r = ures_getString(fResource, &len, &status);
    return UnicodeString(true, r, len);
}

const uint8_t *ResourceBundle::getBinary(int32_t& len, UErrorCode& status) const {
    return ures_getBinary(fResource, &len, &status);
}

const int32_t *ResourceBundle::getIntVector(int32_t& len, UErrorCode& status) const {
    return ures_getIntVector(fResource, &len, &status);
}

uint32_t ResourceBundle::getUInt(UErrorCode& status) const {
    return ures_getUInt(fResource, &status);
}

int32_t ResourceBundle::getInt(UErrorCode& status) const {
    return ures_getInt(fResource, &status);
}

const char *ResourceBundle::getName() const {
    return ures_getName(fResource);
}

const char *ResourceBundle::getKey() const {
    return ures_getKey(fResource);
}

UResType ResourceBundle::getType() const {
    return ures_getType(fResource);
}

int32_t ResourceBundle::getSize() const {
    return ures_getSize(fResource);
}

UBool ResourceBundle::hasNext() const {
    return ures_hasNext(fResource);
}

void ResourceBundle::resetIterator() {
    ures_resetIterator(fResource);
}

ResourceBundle ResourceBundle::getNext(UErrorCode& status) {
    UResourceBundle r;

    ures_initStackObject(&r);
    ures_getNextResource(fResource, &r, &status);
    ResourceBundle res(&r, status);
    if (U_SUCCESS(status)) {
        ures_close(&r);
    }
    return res;
}

UnicodeString ResourceBundle::getNextString(UErrorCode& status) {
    int32_t len = 0;
    const char16_t* r = ures_getNextString(fResource, &len, nullptr, &status);
    return UnicodeString(true, r, len);
}

UnicodeString ResourceBundle::getNextString(const char ** key, UErrorCode& status) {
    int32_t len = 0;
    const char16_t* r = ures_getNextString(fResource, &len, key, &status);
    return UnicodeString(true, r, len);
}

ResourceBundle ResourceBundle::get(int32_t indexR, UErrorCode& status) const {
    UResourceBundle r;

    ures_initStackObject(&r);
    ures_getByIndex(fResource, indexR, &r, &status);
    ResourceBundle res(&r, status);
    if (U_SUCCESS(status)) {
        ures_close(&r);
    }
    return res;
}

UnicodeString ResourceBundle::getStringEx(int32_t indexS, UErrorCode& status) const {
    int32_t len = 0;
    const char16_t* r = ures_getStringByIndex(fResource, indexS, &len, &status);
    return UnicodeString(true, r, len);
}

ResourceBundle ResourceBundle::get(const char* key, UErrorCode& status) const {
    UResourceBundle r;

    ures_initStackObject(&r);
    ures_getByKey(fResource, key, &r, &status);
    ResourceBundle res(&r, status);
    if (U_SUCCESS(status)) {
        ures_close(&r);
    }
    return res;
}

ResourceBundle ResourceBundle::getWithFallback(const char* key, UErrorCode& status){
    UResourceBundle r;
    ures_initStackObject(&r);
    ures_getByKeyWithFallback(fResource, key, &r, &status);
    ResourceBundle res(&r, status);
    if(U_SUCCESS(status)){
        ures_close(&r);
    }
    return res;
}
UnicodeString ResourceBundle::getStringEx(const char* key, UErrorCode& status) const {
    int32_t len = 0;
    const char16_t* r = ures_getStringByKey(fResource, key, &len, &status);
    return UnicodeString(true, r, len);
}

const char*
ResourceBundle::getVersionNumber()  const
{
    return ures_getVersionNumberInternal(fResource);
}

void ResourceBundle::getVersion(UVersionInfo versionInfo) const {
    ures_getVersion(fResource, versionInfo);
}

const Locale &ResourceBundle::getLocale() const {
    static UMutex gLocaleLock;
    Mutex lock(&gLocaleLock);
    if (fLocale != nullptr) {
        return *fLocale;
    }
    UErrorCode status = U_ZERO_ERROR;
    const char *localeName = ures_getLocaleInternal(fResource, &status);
    ResourceBundle *ncThis = const_cast<ResourceBundle *>(this);
    ncThis->fLocale = new Locale(localeName);
    return ncThis->fLocale != nullptr ? *ncThis->fLocale : Locale::getDefault();
}

const Locale ResourceBundle::getLocale(ULocDataLocaleType type, UErrorCode &status) const
{
  return ures_getLocaleByType(fResource, type, &status);
}

U_NAMESPACE_END
//eof
