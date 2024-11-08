
.. _request:

:c:type:`uv_req_t` --- Base request
===================================

`uv_req_t` is the base type for all libuv request types.

Structures are aligned so that any libuv request can be cast to `uv_req_t`.
All API functions defined here work with any request type.


Data types
----------

.. c:type:: uv_req_t

    The base libuv request structure.

.. c:type:: uv_any_req

    Union of all request types.

.. c:enum:: uv_req_type

    The kind of the libuv request.

    ::

        typedef enum {
            UV_UNKNOWN_REQ = 0,
            UV_REQ,
            UV_CONNECT,
            UV_WRITE,
            UV_SHUTDOWN,
            UV_UDP_SEND,
            UV_FS,
            UV_WORK,
            UV_GETADDRINFO,
            UV_GETNAMEINFO,
            UV_REQ_TYPE_MAX,
        } uv_req_type;


Public members
^^^^^^^^^^^^^^

.. c:member:: void* uv_req_t.data

    Space for user-defined arbitrary data. libuv does not use this field.

.. c:member:: uv_req_type uv_req_t.type

    The :c:enum:`uv_req_type`, indicating the type of the request. Readonly.


API
---

.. c:macro:: UV_REQ_TYPE_MAP(iter_macro)

    Macro that expands to a series of invocations of `iter_macro` for
    each of the request types. `iter_macro` is invoked with two
    arguments: the name of the `uv_req_type` element without the `UV_`
    prefix, and the name of the corresponding structure type without the
    `uv_` prefix and `_t` suffix.

.. c:function:: int uv_cancel(uv_req_t* req)

    Cancel a pending request. Fails if the request is executing or has finished
    executing.

    Returns 0 on success, or an error code < 0 on failure.

    Only cancellation of :c:type:`uv_fs_t`, :c:type:`uv_getaddrinfo_t`,
    :c:type:`uv_getnameinfo_t`, :c:type:`uv_random_t` and :c:type:`uv_work_t`
    requests is currently supported.

    Cancelled requests have their callbacks invoked some time in the future.
    It's **not** safe to free the memory associated with the request until the
    callback is called.

    Here is how cancellation is reported to the callback:

    * A :c:type:`uv_fs_t` request has its req->result field set to `UV_ECANCELED`.

    * A :c:type:`uv_work_t`, :c:type:`uv_getaddrinfo_t`,
      :c:type:`uv_getnameinfo_t` or :c:type:`uv_random_t` request has its
      callback invoked with status == `UV_ECANCELED`.

.. c:function:: size_t uv_req_size(uv_req_type type)

    Returns the size of the given request type. Useful for FFI binding writers
    who don't want to know the structure layout.

.. c:function:: void* uv_req_get_data(const uv_req_t* req)

    Returns `req->data`.

    .. versionadded:: 1.19.0

.. c:function:: void uv_req_set_data(uv_req_t* req, void* data)

    Sets `req->data` to `data`.

    .. versionadded:: 1.19.0

.. c:function:: uv_req_type uv_req_get_type(const uv_req_t* req)

    Returns `req->type`.

    .. versionadded:: 1.19.0

.. c:function:: const char* uv_req_type_name(uv_req_type type)

    Returns the name for the equivalent struct for a given request type,
    e.g. `"connect"` (as in :c:type:`uv_connect_t`) for `UV_CONNECT`.

    If no such request type exists, this returns `NULL`.

    .. versionadded:: 1.19.0
