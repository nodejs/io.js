/* MIT License
 *
 * Copyright (c) 2023 Brad House
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
#ifndef __ARES__THREADS_H
#define __ARES__THREADS_H

struct ares_thread_mutex;
typedef struct ares_thread_mutex ares_thread_mutex_t;

ares_thread_mutex_t             *ares_thread_mutex_create(void);
void ares_thread_mutex_destroy(ares_thread_mutex_t *mut);
void ares_thread_mutex_lock(ares_thread_mutex_t *mut);
void ares_thread_mutex_unlock(ares_thread_mutex_t *mut);


struct ares_thread_cond;
typedef struct ares_thread_cond ares_thread_cond_t;

ares_thread_cond_t             *ares_thread_cond_create(void);
void          ares_thread_cond_destroy(ares_thread_cond_t *cond);
void          ares_thread_cond_signal(ares_thread_cond_t *cond);
void          ares_thread_cond_broadcast(ares_thread_cond_t *cond);
ares_status_t ares_thread_cond_wait(ares_thread_cond_t  *cond,
                                    ares_thread_mutex_t *mut);
ares_status_t ares_thread_cond_timedwait(ares_thread_cond_t  *cond,
                                         ares_thread_mutex_t *mut,
                                         unsigned long        timeout_ms);


struct ares_thread;
typedef struct ares_thread ares_thread_t;

typedef void *(*ares_thread_func_t)(void *arg);
ares_status_t ares_thread_create(ares_thread_t    **thread,
                                 ares_thread_func_t func, void *arg);
ares_status_t ares_thread_join(ares_thread_t *thread, void **rv);

#endif
