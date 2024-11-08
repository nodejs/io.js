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
#ifndef __ARES__EVENT_H
#define __ARES__EVENT_H

struct ares_event;
typedef struct ares_event ares_event_t;

typedef enum {
  ARES_EVENT_FLAG_NONE  = 0,
  ARES_EVENT_FLAG_READ  = 1 << 0,
  ARES_EVENT_FLAG_WRITE = 1 << 1,
  ARES_EVENT_FLAG_OTHER = 1 << 2
} ares_event_flags_t;

typedef void (*ares_event_cb_t)(ares_event_thread_t *e, ares_socket_t fd,
                                void *data, ares_event_flags_t flags);

typedef void (*ares_event_free_data_t)(void *data);

typedef void (*ares_event_signal_cb_t)(const ares_event_t *event);

struct ares_event {
  /*! Registered event thread this event is bound to */
  ares_event_thread_t   *e;
  /*! Flags to monitor. OTHER is only allowed if the socket is ARES_SOCKET_BAD.
   */
  ares_event_flags_t     flags;
  /*! Callback to be called when event is triggered */
  ares_event_cb_t        cb;
  /*! Socket to monitor, allowed to be ARES_SOCKET_BAD if not monitoring a
   *  socket. */
  ares_socket_t          fd;
  /*! Data associated with event handle that will be passed to the callback.
   *  Typically OS/event subsystem specific data.
   *  Optional, may be NULL. */
  /*! Data to be passed to callback. Optional, may be NULL. */
  void                  *data;
  /*! When cleaning up the registered event (either when removed or during
   *  shutdown), this function will be called to clean up the user-supplied
   *  data. Optional, May be NULL. */
  ares_event_free_data_t free_data_cb;
  /*! Callback to call to trigger an event. */
  ares_event_signal_cb_t signal_cb;
};

typedef struct {
  const char *name;
  ares_bool_t (*init)(ares_event_thread_t *e);
  void (*destroy)(ares_event_thread_t *e);
  ares_bool_t (*event_add)(ares_event_t *event);
  void (*event_del)(ares_event_t *event);
  void (*event_mod)(ares_event_t *event, ares_event_flags_t new_flags);
  size_t (*wait)(ares_event_thread_t *e, unsigned long timeout_ms);
} ares_event_sys_t;

struct ares_event_configchg;
typedef struct ares_event_configchg ares_event_configchg_t;

ares_status_t ares_event_configchg_init(ares_event_configchg_t **configchg,
                                        ares_event_thread_t     *e);

void          ares_event_configchg_destroy(ares_event_configchg_t *configchg);

struct ares_event_thread {
  /*! Whether the event thread should be online or not.  Checked on every wake
   *  event before sleeping. */
  ares_bool_t             isup;
  /*! Handle to the thread for joining during shutdown */
  ares_thread_t          *thread;
  /*! Lock to protect the data contained within the event thread itself */
  ares_thread_mutex_t    *mutex;
  /*! Reference to the ares channel, for being able to call things like
   *  ares_timeout() and ares_process_fd(). */
  ares_channel_t         *channel;
  /*! Whether or not on the next loop we should process a pending write */
  ares_bool_t             process_pending_write;
  /*! Not-yet-processed event handle updates.  These will get enqueued by a
   *  thread other than the event thread itself. The event thread will then
   *  be woken then process these updates itself */
  ares_llist_t           *ev_updates;
  /*! Registered socket event handles */
  ares_htable_asvp_t     *ev_sock_handles;
  /*! Registered custom event handles. Typically used for external triggering.
   */
  ares_htable_vpvp_t     *ev_cust_handles;
  /*! Pointer to the event handle which is used to signal and wake the event
   *  thread itself.  This is needed to be able to do things like update the
   *  file descriptors being waited on and to wake the event subsystem during
   *  shutdown */
  ares_event_t           *ev_signal;
  /*! Handle for configuration change monitoring */
  ares_event_configchg_t *configchg;
  /* Event subsystem callbacks */
  const ares_event_sys_t *ev_sys;
  /* Event subsystem private data */
  void                   *ev_sys_data;
};

/*! Queue an update for the event handle.
 *
 *  Will search by the fd passed if not ARES_SOCKET_BAD to find a match and
 *  perform an update or delete (depending on flags).  Otherwise will add.
 *  Do not use the event handle returned if its not guaranteed to be an add
 *  operation.
 *
 *  \param[out] event        Event handle. Optional, can be NULL.  This handle
 *                           will be invalidate quickly if the result of the
 *                           operation is not an ADD.
 *  \param[in]  e            pointer to event thread handle
 *  \param[in]  flags        flags for the event handle.  Use
 *                           ARES_EVENT_FLAG_NONE if removing a socket from
 *                           queue (not valid if socket is ARES_SOCKET_BAD).
 *                           Non-socket events cannot be removed, and must have
 *                           ARES_EVENT_FLAG_OTHER set.
 *  \param[in]  cb           Callback to call when
 *                           event is triggered. Required if flags is not
 *                           ARES_EVENT_FLAG_NONE. Not allowed to be
 *                           changed, ignored on modification.
 *  \param[in]  fd           File descriptor/socket to monitor. May
 *                           be ARES_SOCKET_BAD if not monitoring file
 *                           descriptor.
 *  \param[in]  data         Optional. Caller-supplied data to be passed to
 *                           callback. Only allowed on initial add, cannot be
 *                           modified later, ignored on modification.
 *  \param[in]  free_data_cb Optional. Callback to clean up caller-supplied
 *                           data. Only allowed on initial add, cannot be
 *                           modified later, ignored on modification.
 *  \param[in]  signal_cb    Optional. Callback to call to trigger an event.
 *  \return ARES_SUCCESS on success
 */
ares_status_t ares_event_update(ares_event_t **event, ares_event_thread_t *e,
                                ares_event_flags_t flags, ares_event_cb_t cb,
                                ares_socket_t fd, void *data,
                                ares_event_free_data_t free_data_cb,
                                ares_event_signal_cb_t signal_cb);


#ifdef HAVE_PIPE
ares_event_t *ares_pipeevent_create(ares_event_thread_t *e);
#endif

#ifdef HAVE_POLL
extern const ares_event_sys_t ares_evsys_poll;
#endif

#ifdef HAVE_KQUEUE
extern const ares_event_sys_t ares_evsys_kqueue;
#endif

#ifdef HAVE_EPOLL
extern const ares_event_sys_t ares_evsys_epoll;
#endif

#ifdef _WIN32
extern const ares_event_sys_t ares_evsys_win32;
#endif

/* All systems have select(), but not all have a way to wake, so we require
 * pipe() to wake the select() */
#ifdef HAVE_PIPE
extern const ares_event_sys_t ares_evsys_select;
#endif

#endif
