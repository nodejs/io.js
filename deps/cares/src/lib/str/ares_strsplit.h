/* MIT License
 *
 * Copyright (c) 2018 John Schember
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HEADER_CARES_STRSPLIT_H
#define HEADER_CARES_STRSPLIT_H

/* Split a string on delms skipping empty or duplicate elements.
 *
 * param in String to split.
 * param delms String of characters to treat as a delimiter.
 *             Each character in the string is a delimiter so
 *             there can be multiple delimiters to split on.
 *             E.g. ", " will split on all comma's and spaces.
 *             Duplicate (case-insensitive) entries are removed.
 * param num_elm Return parameter of the number of elements
 *               in the result array.
 *
 * returns an allocated array of allocated string elements.
 *
 */
char **ares__strsplit(const char *in, const char *delms, size_t *num_elm);

/* Frees the result returned from ares__strsplit(). */
void   ares__strsplit_free(char **elms, size_t num_elm);

/* Duplicate the array */
char **ares__strsplit_duplicate(char **elms, size_t num_elm);

#endif /* HEADER_CARES_STRSPLIT_H */
