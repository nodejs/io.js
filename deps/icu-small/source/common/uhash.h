// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*   Copyright (C) 1997-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
******************************************************************************
*   Date        Name        Description
*   03/22/00    aliu        Adapted from original C++ ICU Hashtable.
*   07/06/01    aliu        Modified to support int32_t keys on
*                           platforms with sizeof(void*) < 32.
******************************************************************************
*/

#ifndef UHASH_H
#define UHASH_H

#include "unicode/utypes.h"
#include "cmemory.h"
#include "uelement.h"
#include "unicode/localpointer.h"

/**
 * UHashtable stores key-value pairs and does moderately fast lookup
 * based on keys.  It provides a good tradeoff between access time and
 * storage space.  As elements are added to it, it grows to accommodate
 * them.  By default, the table never shrinks, even if all elements
 * are removed from it.
 *
 * Keys and values are stored as void* pointers.  These void* pointers
 * may be actual pointers to strings, objects, or any other structure
 * in memory, or they may simply be integral values cast to void*.
 * UHashtable doesn't care and manipulates them via user-supplied
 * functions.  These functions hash keys, compare keys, delete keys,
 * and delete values.  Some function pointers are optional (may be
 * NULL); others must be supplied.  Several prebuilt functions exist
 * to handle common key types.
 *
 * UHashtable ownership of keys and values is flexible, and controlled
 * by whether or not the key deleter and value deleter functions are
 * set.  If a void* key is actually a pointer to a deletable object,
 * then UHashtable can be made to delete that object by setting the
 * key deleter function pointer to a non-NULL value.  If this is done,
 * then keys passed to uhash_put() are owned by the hashtable and will
 * be deleted by it at some point, either as keys are replaced, or
 * when uhash_close() is finally called.  The same is true of values
 * and the value deleter function pointer.  Keys passed to methods
 * other than uhash_put() are never owned by the hashtable.
 *
 * NULL values are not allowed.  uhash_get() returns NULL to indicate
 * a key that is not in the table, and having a NULL value in the
 * table would generate an ambiguous result.  If a key and a NULL
 * value is passed to uhash_put(), this has the effect of doing a
 * uhash_remove() on that key.  This keeps uhash_get(), uhash_count(),
 * and uhash_nextElement() consistent with one another.
 *
 * Keys and values can be integers.
 * Functions that work with an integer key have an "i" prefix.
 * Functions that work with an integer value have an "i" suffix.
 * As with putting a NULL value pointer, putting a zero value integer removes the item.
 * Except, there are pairs of functions that allow setting zero values
 * and fetching (value, found) pairs.
 *
 * To see everything in a hashtable, use uhash_nextElement() to
 * iterate through its contents.  Each call to this function returns a
 * UHashElement pointer.  A hash element contains a key, value, and
 * hashcode.  During iteration an element may be deleted by calling
 * uhash_removeElement(); iteration may safely continue thereafter.
 * The uhash_remove() function may also be safely called in
 * mid-iteration.  If uhash_put() is called during iteration,
 * the iteration is still guaranteed to terminate reasonably, but
 * there is no guarantee that every element will be returned or that
 * some won't be returned more than once.
 *
 * Under no circumstances should the UHashElement returned by
 * uhash_nextElement be modified directly.
 *
 * By default, the hashtable grows when necessary, but never shrinks,
 * even if all items are removed.  For most applications this is
 * optimal.  However, in a highly dynamic usage where memory is at a
 * premium, the table can be set to both grow and shrink by calling
 * uhash_setResizePolicy() with the policy U_GROW_AND_SHRINK.  In a
 * situation where memory is critical and the client wants a table
 * that does not grow at all, the constant U_FIXED can be used.
 */

/********************************************************************
 * Data Structures
 ********************************************************************/

U_CDECL_BEGIN

/**
 * A key or value within a UHashtable.
 * The hashing and comparison functions take a pointer to a
 * UHashTok, but the deleter receives the void* pointer within it.
 */
typedef UElement UHashTok;

/**
 * This is a single hash element.
 */
struct UHashElement {
    /* Reorder these elements to pack nicely if necessary */
    int32_t  hashcode;
    UHashTok value;
    UHashTok key;
};
typedef struct UHashElement UHashElement;

/**
 * A hashing function.
 * @param key A key stored in a hashtable
 * @return A NON-NEGATIVE hash code for parm.
 */
typedef int32_t U_CALLCONV UHashFunction(const UHashTok key);

/**
 * A key equality (boolean) comparison function.
 */
typedef UElementsAreEqual UKeyComparator;

/**
 * A value equality (boolean) comparison function.
 */
typedef UElementsAreEqual UValueComparator;

/* see cmemory.h for UObjectDeleter and uprv_deleteUObject() */

/**
 * This specifies whether or not, and how, the hashtable resizes itself.
 * See uhash_setResizePolicy().
 */
enum UHashResizePolicy {
    U_GROW,            /* Grow on demand, do not shrink */
    U_GROW_AND_SHRINK, /* Grow and shrink on demand */
    U_FIXED            /* Never change size */
};

/**
 * The UHashtable struct.  Clients should treat this as an opaque data
 * type and manipulate it only through the uhash_... API.
 */
struct UHashtable {

    /* Main key-value pair storage array */

    UHashElement *elements;

    /* Function pointers */

    UHashFunction *keyHasher;      /* Computes hash from key.
                                   * Never null. */
    UKeyComparator *keyComparator; /* Compares keys for equality.
                                   * Never null. */
    UValueComparator *valueComparator; /* Compares the values for equality */

    UObjectDeleter *keyDeleter;    /* Deletes keys when required.
                                   * If NULL won't do anything */
    UObjectDeleter *valueDeleter;  /* Deletes values when required.
                                   * If NULL won't do anything */

    /* Size parameters */

    int32_t     count;      /* The number of key-value pairs in this table.
                             * 0 <= count <= length.  In practice we
                             * never let count == length (see code). */
    int32_t     length;     /* The physical size of the arrays hashes, keys
                             * and values.  Must be prime. */

    /* Rehashing thresholds */

    int32_t     highWaterMark;  /* If count > highWaterMark, rehash */
    int32_t     lowWaterMark;   /* If count < lowWaterMark, rehash */
    float       highWaterRatio; /* 0..1; high water as a fraction of length */
    float       lowWaterRatio;  /* 0..1; low water as a fraction of length */

    int8_t      primeIndex;     /* Index into our prime table for length.
                                 * length == PRIMES[primeIndex] */
    UBool       allocated; /* Was this UHashtable allocated? */
};
typedef struct UHashtable UHashtable;

U_CDECL_END

/********************************************************************
 * API
 ********************************************************************/

/**
 * Initialize a new UHashtable.
 * @param keyHash A pointer to the key hashing function.  Must not be
 * NULL.
 * @param keyComp A pointer to the function that compares keys.  Must
 * not be NULL.
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return A pointer to a UHashtable, or 0 if an error occurred.
 * @see uhash_openSize
 */
U_CAPI UHashtable* U_EXPORT2
uhash_open(UHashFunction *keyHash,
           UKeyComparator *keyComp,
           UValueComparator *valueComp,
           UErrorCode *status);

/**
 * Initialize a new UHashtable with a given initial size.
 * @param keyHash A pointer to the key hashing function.  Must not be
 * NULL.
 * @param keyComp A pointer to the function that compares keys.  Must
 * not be NULL.
 * @param size The initial capacity of this hashtable.
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return A pointer to a UHashtable, or 0 if an error occurred.
 * @see uhash_open
 */
U_CAPI UHashtable* U_EXPORT2
uhash_openSize(UHashFunction *keyHash,
               UKeyComparator *keyComp,
               UValueComparator *valueComp,
               int32_t size,
               UErrorCode *status);

/**
 * Initialize an existing UHashtable.
 * @param keyHash A pointer to the key hashing function.  Must not be
 * NULL.
 * @param keyComp A pointer to the function that compares keys.  Must
 * not be NULL.
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return A pointer to a UHashtable, or 0 if an error occurred.
 * @see uhash_openSize
 */
U_CAPI UHashtable* U_EXPORT2
uhash_init(UHashtable *hash,
           UHashFunction *keyHash,
           UKeyComparator *keyComp,
           UValueComparator *valueComp,
           UErrorCode *status);

/**
 * Initialize an existing UHashtable.
 * @param keyHash A pointer to the key hashing function.  Must not be
 * NULL.
 * @param keyComp A pointer to the function that compares keys.  Must
 * not be NULL.
 * @param size The initial capacity of this hashtable.
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return A pointer to a UHashtable, or 0 if an error occurred.
 * @see uhash_openSize
 */
U_CAPI UHashtable* U_EXPORT2
uhash_initSize(UHashtable *hash,
               UHashFunction *keyHash,
               UKeyComparator *keyComp,
               UValueComparator *valueComp,
               int32_t size,
               UErrorCode *status);

/**
 * Close a UHashtable, releasing the memory used.
 * @param hash The UHashtable to close. If hash is NULL no operation is performed.
 */
U_CAPI void U_EXPORT2
uhash_close(UHashtable *hash);



/**
 * Set the function used to hash keys.
 * @param hash The UHashtable to set
 * @param fn the function to be used hash keys; must not be NULL
 * @return the previous key hasher; non-NULL
 */
U_CAPI UHashFunction *U_EXPORT2
uhash_setKeyHasher(UHashtable *hash, UHashFunction *fn);

/**
 * Set the function used to compare keys.  The default comparison is a
 * void* pointer comparison.
 * @param hash The UHashtable to set
 * @param fn the function to be used compare keys; must not be NULL
 * @return the previous key comparator; non-NULL
 */
U_CAPI UKeyComparator *U_EXPORT2
uhash_setKeyComparator(UHashtable *hash, UKeyComparator *fn);

/**
 * Set the function used to compare values.  The default comparison is a
 * void* pointer comparison.
 * @param hash The UHashtable to set
 * @param fn the function to be used compare keys; must not be NULL
 * @return the previous key comparator; non-NULL
 */
U_CAPI UValueComparator *U_EXPORT2
uhash_setValueComparator(UHashtable *hash, UValueComparator *fn);

/**
 * Set the function used to delete keys.  If this function pointer is
 * NULL, this hashtable does not delete keys.  If it is non-NULL, this
 * hashtable does delete keys.  This function should be set once
 * before any elements are added to the hashtable and should not be
 * changed thereafter.
 * @param hash The UHashtable to set
 * @param fn the function to be used delete keys, or NULL
 * @return the previous key deleter; may be NULL
 */
U_CAPI UObjectDeleter *U_EXPORT2
uhash_setKeyDeleter(UHashtable *hash, UObjectDeleter *fn);

/**
 * Set the function used to delete values.  If this function pointer
 * is NULL, this hashtable does not delete values.  If it is non-NULL,
 * this hashtable does delete values.  This function should be set
 * once before any elements are added to the hashtable and should not
 * be changed thereafter.
 * @param hash The UHashtable to set
 * @param fn the function to be used delete values, or NULL
 * @return the previous value deleter; may be NULL
 */
U_CAPI UObjectDeleter *U_EXPORT2
uhash_setValueDeleter(UHashtable *hash, UObjectDeleter *fn);

/**
 * Specify whether or not, and how, the hashtable resizes itself.
 * By default, tables grow but do not shrink (policy U_GROW).
 * See enum UHashResizePolicy.
 * @param hash The UHashtable to set
 * @param policy The way the hashtable resizes itself, {U_GROW, U_GROW_AND_SHRINK, U_FIXED}
 */
U_CAPI void U_EXPORT2
uhash_setResizePolicy(UHashtable *hash, enum UHashResizePolicy policy);

/**
 * Get the number of key-value pairs stored in a UHashtable.
 * @param hash The UHashtable to query.
 * @return The number of key-value pairs stored in hash.
 */
U_CAPI int32_t U_EXPORT2
uhash_count(const UHashtable *hash);

/**
 * Put a (key=pointer, value=pointer) item in a UHashtable.  If the
 * keyDeleter is non-NULL, then the hashtable owns 'key' after this
 * call.  If the valueDeleter is non-NULL, then the hashtable owns
 * 'value' after this call.  Storing a NULL value is the same as
 * calling uhash_remove().
 * @param hash The target UHashtable.
 * @param key The key to store.
 * @param value The value to store, may be NULL (see above).
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return The previous value, or NULL if none.
 * @see uhash_get
 */
U_CAPI void* U_EXPORT2
uhash_put(UHashtable *hash,
          void *key,
          void *value,
          UErrorCode *status);

/**
 * Put a (key=integer, value=pointer) item in a UHashtable.
 * keyDeleter must be NULL.  If the valueDeleter is non-NULL, then the
 * hashtable owns 'value' after this call.  Storing a NULL value is
 * the same as calling uhash_remove().
 * @param hash The target UHashtable.
 * @param key The integer key to store.
 * @param value The value to store, may be NULL (see above).
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return The previous value, or NULL if none.
 * @see uhash_get
 */
U_CAPI void* U_EXPORT2
uhash_iput(UHashtable *hash,
           int32_t key,
           void* value,
           UErrorCode *status);

/**
 * Put a (key=pointer, value=integer) item in a UHashtable.  If the
 * keyDeleter is non-NULL, then the hashtable owns 'key' after this
 * call.  valueDeleter must be NULL.  Storing a 0 value is the same as
 * calling uhash_remove().
 * @param hash The target UHashtable.
 * @param key The key to store.
 * @param value The integer value to store.
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return The previous value, or 0 if none.
 * @see uhash_get
 */
U_CAPI int32_t U_EXPORT2
uhash_puti(UHashtable *hash,
           void* key,
           int32_t value,
           UErrorCode *status);

/**
 * Put a (key=integer, value=integer) item in a UHashtable.  If the
 * keyDeleter is non-NULL, then the hashtable owns 'key' after this
 * call.  valueDeleter must be NULL.  Storing a 0 value is the same as
 * calling uhash_remove().
 * @param hash The target UHashtable.
 * @param key The key to store.
 * @param value The integer value to store.
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return The previous value, or 0 if none.
 * @see uhash_get
 */
U_CAPI int32_t U_EXPORT2
uhash_iputi(UHashtable *hash,
           int32_t key,
           int32_t value,
           UErrorCode *status);

/**
 * Put a (key=pointer, value=integer) item in a UHashtable.  If the
 * keyDeleter is non-NULL, then the hashtable owns 'key' after this
 * call.  valueDeleter must be NULL.
 * Storing a 0 value is possible; call uhash_igetiAndFound() to retrieve values including zero.
 *
 * @param hash The target UHashtable.
 * @param key The key to store.
 * @param value The integer value to store.
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return The previous value, or 0 if none.
 * @see uhash_getiAndFound
 */
U_CAPI int32_t U_EXPORT2
uhash_putiAllowZero(UHashtable *hash,
                    void *key,
                    int32_t value,
                    UErrorCode *status);

/**
 * Put a (key=integer, value=integer) item in a UHashtable.  If the
 * keyDeleter is non-NULL, then the hashtable owns 'key' after this
 * call.  valueDeleter must be NULL.
 * Storing a 0 value is possible; call uhash_igetiAndFound() to retrieve values including zero.
 *
 * @param hash The target UHashtable.
 * @param key The key to store.
 * @param value The integer value to store.
 * @param status A pointer to an UErrorCode to receive any errors.
 * @return The previous value, or 0 if none.
 * @see uhash_igetiAndFound
 */
U_CAPI int32_t U_EXPORT2
uhash_iputiAllowZero(UHashtable *hash,
                     int32_t key,
                     int32_t value,
                     UErrorCode *status);

/**
 * Retrieve a pointer value from a UHashtable using a pointer key,
 * as previously stored by uhash_put().
 * @param hash The target UHashtable.
 * @param key A pointer key stored in a hashtable
 * @return The requested item, or NULL if not found.
 */
U_CAPI void* U_EXPORT2
uhash_get(const UHashtable *hash,
          const void *key);

/**
 * Retrieve a pointer value from a UHashtable using a integer key,
 * as previously stored by uhash_iput().
 * @param hash The target UHashtable.
 * @param key An integer key stored in a hashtable
 * @return The requested item, or NULL if not found.
 */
U_CAPI void* U_EXPORT2
uhash_iget(const UHashtable *hash,
           int32_t key);

/**
 * Retrieve an integer value from a UHashtable using a pointer key,
 * as previously stored by uhash_puti().
 * @param hash The target UHashtable.
 * @param key A pointer key stored in a hashtable
 * @return The requested item, or 0 if not found.
 */
U_CAPI int32_t U_EXPORT2
uhash_geti(const UHashtable *hash,
           const void* key);
/**
 * Retrieve an integer value from a UHashtable using an integer key,
 * as previously stored by uhash_iputi().
 * @param hash The target UHashtable.
 * @param key An integer key stored in a hashtable
 * @return The requested item, or 0 if not found.
 */
U_CAPI int32_t U_EXPORT2
uhash_igeti(const UHashtable *hash,
           int32_t key);

/**
 * Retrieves an integer value from a UHashtable using a pointer key,
 * as previously stored by uhash_putiAllowZero() or uhash_puti().
 *
 * @param hash The target UHashtable.
 * @param key A pointer key stored in a hashtable
 * @param found A pointer to a boolean which will be set for whether the key was found.
 * @return The requested item, or 0 if not found.
 */
U_CAPI int32_t U_EXPORT2
uhash_getiAndFound(const UHashtable *hash,
                   const void *key,
                   UBool *found);

/**
 * Retrieves an integer value from a UHashtable using an integer key,
 * as previously stored by uhash_iputiAllowZero() or uhash_iputi().
 *
 * @param hash The target UHashtable.
 * @param key An integer key stored in a hashtable
 * @param found A pointer to a boolean which will be set for whether the key was found.
 * @return The requested item, or 0 if not found.
 */
U_CAPI int32_t U_EXPORT2
uhash_igetiAndFound(const UHashtable *hash,
                    int32_t key,
                    UBool *found);

/**
 * Remove an item from a UHashtable stored by uhash_put().
 * @param hash The target UHashtable.
 * @param key A key stored in a hashtable
 * @return The item removed, or NULL if not found.
 */
U_CAPI void* U_EXPORT2
uhash_remove(UHashtable *hash,
             const void *key);

/**
 * Remove an item from a UHashtable stored by uhash_iput().
 * @param hash The target UHashtable.
 * @param key An integer key stored in a hashtable
 * @return The item removed, or NULL if not found.
 */
U_CAPI void* U_EXPORT2
uhash_iremove(UHashtable *hash,
              int32_t key);

/**
 * Remove an item from a UHashtable stored by uhash_puti().
 * @param hash The target UHashtable.
 * @param key An key stored in a hashtable
 * @return The item removed, or 0 if not found.
 */
U_CAPI int32_t U_EXPORT2
uhash_removei(UHashtable *hash,
              const void* key);

/**
 * Remove an item from a UHashtable stored by uhash_iputi().
 * @param hash The target UHashtable.
 * @param key An integer key stored in a hashtable
 * @return The item removed, or 0 if not found.
 */
U_CAPI int32_t U_EXPORT2
uhash_iremovei(UHashtable *hash,
               int32_t key);

/**
 * Remove all items from a UHashtable.
 * @param hash The target UHashtable.
 */
U_CAPI void U_EXPORT2
uhash_removeAll(UHashtable *hash);

/**
 * Returns true if the UHashtable contains an item with this pointer key.
 *
 * @param hash The target UHashtable.
 * @param key A pointer key stored in a hashtable
 * @return true if the key is found.
 */
U_CAPI UBool U_EXPORT2
uhash_containsKey(const UHashtable *hash, const void *key);

/**
 * Returns true if the UHashtable contains an item with this integer key.
 *
 * @param hash The target UHashtable.
 * @param key An integer key stored in a hashtable
 * @return true if the key is found.
 */
U_CAPI UBool U_EXPORT2
uhash_icontainsKey(const UHashtable *hash, int32_t key);

/**
 * Locate an element of a UHashtable.  The caller must not modify the
 * returned object.  The primary use of this function is to obtain the
 * stored key when it may not be identical to the search key.  For
 * example, if the compare function is a case-insensitive string
 * compare, then the hash key may be desired in order to obtain the
 * canonical case corresponding to a search key.
 * @param hash The target UHashtable.
 * @param key A key stored in a hashtable
 * @return a hash element, or NULL if the key is not found.
 */
U_CAPI const UHashElement* U_EXPORT2
uhash_find(const UHashtable *hash, const void* key);

/**
 * \def UHASH_FIRST
 * Constant for use with uhash_nextElement
 * @see uhash_nextElement
 */
#define UHASH_FIRST (-1)

/**
 * Iterate through the elements of a UHashtable.  The caller must not
 * modify the returned object.  However, uhash_removeElement() may be
 * called during iteration to remove an element from the table.
 * Iteration may safely be resumed afterwards.  If uhash_put() is
 * called during iteration the iteration will then be out of sync and
 * should be restarted.
 * @param hash The target UHashtable.
 * @param pos This should be set to UHASH_FIRST initially, and left untouched
 * thereafter.
 * @return a hash element, or NULL if no further key-value pairs
 * exist in the table.
 */
U_CAPI const UHashElement* U_EXPORT2
uhash_nextElement(const UHashtable *hash,
                  int32_t *pos);

/**
 * Remove an element, returned by uhash_nextElement(), from the table.
 * Iteration may be safely continued afterwards.
 * @param hash The hashtable
 * @param e The element, returned by uhash_nextElement(), to remove.
 * Must not be NULL.  Must not be an empty or deleted element (as long
 * as this was returned by uhash_nextElement() it will not be empty or
 * deleted).  Note: Although this parameter is const, it will be
 * modified.
 * @return the value that was removed.
 */
U_CAPI void* U_EXPORT2
uhash_removeElement(UHashtable *hash, const UHashElement* e);

/********************************************************************
 * UHashTok convenience
 ********************************************************************/

/**
 * Return a UHashTok for an integer.
 * @param i The given integer
 * @return a UHashTok for an integer.
 */
/*U_CAPI UHashTok U_EXPORT2
uhash_toki(int32_t i);*/

/**
 * Return a UHashTok for a pointer.
 * @param p The given pointer
 * @return a UHashTok for a pointer.
 */
/*U_CAPI UHashTok U_EXPORT2
uhash_tokp(void* p);*/

/********************************************************************
 * UChar* and char* Support Functions
 ********************************************************************/

/**
 * Generate a hash code for a null-terminated UChar* string.  If the
 * string is not null-terminated do not use this function.  Use
 * together with uhash_compareUChars.
 * @param key The string (const UChar*) to hash.
 * @return A hash code for the key.
 */
U_CAPI int32_t U_EXPORT2
uhash_hashUChars(const UHashTok key);

/**
 * Generate a hash code for a null-terminated char* string.  If the
 * string is not null-terminated do not use this function.  Use
 * together with uhash_compareChars.
 * @param key The string (const char*) to hash.
 * @return A hash code for the key.
 */
U_CAPI int32_t U_EXPORT2
uhash_hashChars(const UHashTok key);

/**
 * Generate a case-insensitive hash code for a null-terminated char*
 * string.  If the string is not null-terminated do not use this
 * function.  Use together with uhash_compareIChars.
 * @param key The string (const char*) to hash.
 * @return A hash code for the key.
 */
U_CAPI int32_t U_EXPORT2
uhash_hashIChars(const UHashTok key);

/**
 * Generate a case-insensitive hash code for a std::string_view.
 * Use together with uhash_compareIStringView.
 * @param key A pointer to the std::string_view to hash.
 * @return A hash code for the key.
 */
U_CAPI int32_t U_EXPORT2
uhash_hashIStringView(const UHashTok key);

/**
 * Comparator for null-terminated UChar* strings.  Use together with
 * uhash_hashUChars.
 * @param key1 The string for comparison
 * @param key2 The string for comparison
 * @return true if key1 and key2 are equal, return false otherwise.
 */
U_CAPI UBool U_EXPORT2
uhash_compareUChars(const UHashTok key1, const UHashTok key2);

/**
 * Comparator for null-terminated char* strings.  Use together with
 * uhash_hashChars.
 * @param key1 The string for comparison
 * @param key2 The string for comparison
 * @return true if key1 and key2 are equal, return false otherwise.
 */
U_CAPI UBool U_EXPORT2
uhash_compareChars(const UHashTok key1, const UHashTok key2);

/**
 * Case-insensitive comparator for null-terminated char* strings.  Use
 * together with uhash_hashIChars.
 * @param key1 The string for comparison
 * @param key2 The string for comparison
 * @return true if key1 and key2 are equal, return false otherwise.
 */
U_CAPI UBool U_EXPORT2
uhash_compareIChars(const UHashTok key1, const UHashTok key2);

/**
 * Case-insensitive comparator for std::string_view.
 * Use together with uhash_hashIStringView.
 * @param key1 A pointer to the std::string_view for comparison
 * @param key2 A pointer to the std::string_view for comparison
 * @return true if key1 and key2 are equal, return false otherwise.
 */
U_CAPI UBool U_EXPORT2
uhash_compareIStringView(const UHashTok key1, const UHashTok key2);

/********************************************************************
 * UnicodeString Support Functions
 ********************************************************************/

/**
 * Hash function for UnicodeString* keys.
 * @param key The string (const char*) to hash.
 * @return A hash code for the key.
 */
U_CAPI int32_t U_EXPORT2
uhash_hashUnicodeString(const UElement key);

/**
 * Hash function for UnicodeString* keys (case insensitive).
 * Make sure to use together with uhash_compareCaselessUnicodeString.
 * @param key The string (const char*) to hash.
 * @return A hash code for the key.
 */
U_CAPI int32_t U_EXPORT2
uhash_hashCaselessUnicodeString(const UElement key);

/********************************************************************
 * int32_t Support Functions
 ********************************************************************/

/**
 * Hash function for 32-bit integer keys.
 * @param key The string (const char*) to hash.
 * @return A hash code for the key.
 */
U_CAPI int32_t U_EXPORT2
uhash_hashLong(const UHashTok key);

/**
 * Comparator function for 32-bit integer keys.
 * @param key1 The integer for comparison
 * @param Key2 The integer for comparison
 * @return true if key1 and key2 are equal, return false otherwise
 */
U_CAPI UBool U_EXPORT2
uhash_compareLong(const UHashTok key1, const UHashTok key2);

/********************************************************************
 * Other Support Functions
 ********************************************************************/

/**
 * Deleter for Hashtable objects.
 * @param obj The object to be deleted
 */
U_CAPI void U_EXPORT2
uhash_deleteHashtable(void *obj);

/* Use uprv_free() itself as a deleter for any key or value allocated using uprv_malloc. */

/**
 * Checks if the given hashtables are equal or not.
 * @param hash1
 * @param hash2
 * @return true if the hashtables are equal and false if not.
 */
U_CAPI UBool U_EXPORT2
uhash_equals(const UHashtable* hash1, const UHashtable* hash2);


#if U_SHOW_CPLUSPLUS_API

U_NAMESPACE_BEGIN

/**
 * \class LocalUHashtablePointer
 * "Smart pointer" class, closes a UHashtable via uhash_close().
 * For most methods see the LocalPointerBase base class.
 *
 * @see LocalPointerBase
 * @see LocalPointer
 * @stable ICU 4.4
 */
U_DEFINE_LOCAL_OPEN_POINTER(LocalUHashtablePointer, UHashtable, uhash_close);

U_NAMESPACE_END

#endif

#endif
