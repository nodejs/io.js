
.. _tty:

:c:type:`uv_tty_t` --- TTY handle
=================================

TTY handles represent a stream for the console.

:c:type:`uv_tty_t` is a 'subclass' of :c:type:`uv_stream_t`.


Data types
----------

.. c:type:: uv_tty_t

    TTY handle type.

.. c:enum:: uv_tty_mode_t

    .. versionadded:: 1.2.0

    TTY mode type:

    ::

      typedef enum {
          /* Initial/normal terminal mode */
          UV_TTY_MODE_NORMAL,
          /* Raw input mode (On Windows, ENABLE_WINDOW_INPUT is also enabled) */
          UV_TTY_MODE_RAW,
          /* Binary-safe I/O mode for IPC (Unix-only) */
          UV_TTY_MODE_IO
      } uv_tty_mode_t;

.. c:enum:: uv_tty_vtermstate_t

    Console virtual terminal mode type:

    ::

      typedef enum {
          /*
           * The console supports handling of virtual terminal sequences
           * (Windows10 new console, ConEmu)
           */
          UV_TTY_SUPPORTED,
          /* The console cannot process virtual terminal sequences.  (Legacy
           * console)
           */
          UV_TTY_UNSUPPORTED
      } uv_tty_vtermstate_t



Public members
^^^^^^^^^^^^^^

N/A

.. seealso:: The :c:type:`uv_stream_t` members also apply.


API
---

.. c:function:: int uv_tty_init(uv_loop_t* loop, uv_tty_t* handle, uv_file fd, int unused)

    Initialize a new TTY stream with the given file descriptor. Usually the
    file descriptor will be:

    * 0 = stdin
    * 1 = stdout
    * 2 = stderr

    On Unix this function will determine the path of the fd of the terminal
    using :man:`ttyname_r(3)`, open it, and use it if the passed file descriptor
    refers to a TTY. This lets libuv put the tty in non-blocking mode without
    affecting other processes that share the tty.

    This function is not thread safe on systems that don't support
    ioctl TIOCGPTN or TIOCPTYGNAME, for instance OpenBSD and Solaris.

    .. note::
        If reopening the TTY fails, libuv falls back to blocking writes.

    .. versionchanged:: 1.23.1: the `readable` parameter is now unused and ignored.
                        The correct value will now be auto-detected from the kernel.

    .. versionchanged:: 1.9.0: the path of the TTY is determined by
                        :man:`ttyname_r(3)`. In earlier versions libuv opened
                        `/dev/tty` instead.

    .. versionchanged:: 1.5.0: trying to initialize a TTY stream with a file
                        descriptor that refers to a file returns `UV_EINVAL`
                        on UNIX.

.. c:function:: int uv_tty_set_mode(uv_tty_t* handle, uv_tty_mode_t mode)

    .. versionchanged:: 1.2.0: the mode is specified as a
                        :c:enum:`uv_tty_mode_t` value.

    Set the TTY using the specified terminal mode.

.. c:function:: int uv_tty_reset_mode(void)

    To be called when the program exits. Resets TTY settings to default
    values for the next process to take over.

    This function is async signal-safe on Unix platforms but can fail with error
    code ``UV_EBUSY`` if you call it when execution is inside
    :c:func:`uv_tty_set_mode`.

.. c:function:: int uv_tty_get_winsize(uv_tty_t* handle, int* width, int* height)

    Gets the current Window size. On success it returns 0.

.. seealso:: The :c:type:`uv_stream_t` API functions also apply.

.. c:function:: void uv_tty_set_vterm_state(uv_tty_vtermstate_t state)

    Controls whether console virtual terminal sequences are processed by libuv
    or console.
    Useful in particular for enabling ConEmu support of ANSI X3.64 and Xterm
    256 colors. Otherwise Windows10 consoles are usually detected automatically.

    This function is only meaningful on Windows systems. On Unix it is silently
    ignored.

    .. versionadded:: 1.33.0

.. c:function:: int uv_tty_get_vterm_state(uv_tty_vtermstate_t* state)

    Get the current state of whether console virtual terminal sequences are
    handled by libuv or the console.

    This function is not implemented on Unix, where it returns ``UV_ENOTSUP``.

    .. versionadded:: 1.33.0

