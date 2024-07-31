/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2014 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef NGHTTP2_CALLBACKS_H
#define NGHTTP2_CALLBACKS_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp2/nghttp2.h>

/*
 * Callback functions.
 */
struct nghttp2_session_callbacks {
  /**
   * Deprecated.  Use send_callback2 instead.  Callback function
   * invoked when the session wants to send data to the remote peer.
   * This callback is not necessary if the application uses solely
   * `nghttp2_session_mem_send()` to serialize data to transmit.
   */
  nghttp2_send_callback send_callback;
  /**
   * Callback function invoked when the session wants to send data to
   * the remote peer.  This callback is not necessary if the
   * application uses solely `nghttp2_session_mem_send2()` to
   * serialize data to transmit.
   */
  nghttp2_send_callback2 send_callback2;
  /**
   * Deprecated.  Use recv_callback2 instead.  Callback function
   * invoked when the session wants to receive data from the remote
   * peer.  This callback is not necessary if the application uses
   * solely `nghttp2_session_mem_recv()` to process received data.
   */
  nghttp2_recv_callback recv_callback;
  /**
   * Callback function invoked when the session wants to receive data
   * from the remote peer.  This callback is not necessary if the
   * application uses solely `nghttp2_session_mem_recv2()` to process
   * received data.
   */
  nghttp2_recv_callback2 recv_callback2;
  /**
   * Callback function invoked by `nghttp2_session_recv()` when a
   * frame is received.
   */
  nghttp2_on_frame_recv_callback on_frame_recv_callback;
  /**
   * Callback function invoked by `nghttp2_session_recv()` when an
   * invalid non-DATA frame is received.
   */
  nghttp2_on_invalid_frame_recv_callback on_invalid_frame_recv_callback;
  /**
   * Callback function invoked when a chunk of data in DATA frame is
   * received.
   */
  nghttp2_on_data_chunk_recv_callback on_data_chunk_recv_callback;
  /**
   * Callback function invoked before a non-DATA frame is sent.
   */
  nghttp2_before_frame_send_callback before_frame_send_callback;
  /**
   * Callback function invoked after a frame is sent.
   */
  nghttp2_on_frame_send_callback on_frame_send_callback;
  /**
   * The callback function invoked when a non-DATA frame is not sent
   * because of an error.
   */
  nghttp2_on_frame_not_send_callback on_frame_not_send_callback;
  /**
   * Callback function invoked when the stream is closed.
   */
  nghttp2_on_stream_close_callback on_stream_close_callback;
  /**
   * Callback function invoked when the reception of header block in
   * HEADERS or PUSH_PROMISE is started.
   */
  nghttp2_on_begin_headers_callback on_begin_headers_callback;
  /**
   * Callback function invoked when a header name/value pair is
   * received.
   */
  nghttp2_on_header_callback on_header_callback;
  nghttp2_on_header_callback2 on_header_callback2;
  /**
   * Callback function invoked when a invalid header name/value pair
   * is received which is silently ignored if these callbacks are not
   * set.
   */
  nghttp2_on_invalid_header_callback on_invalid_header_callback;
  nghttp2_on_invalid_header_callback2 on_invalid_header_callback2;
  /**
   * Deprecated.  Use select_padding_callback2 instead.  Callback
   * function invoked when the library asks application how many
   * padding bytes are required for the transmission of the given
   * frame.
   */
  nghttp2_select_padding_callback select_padding_callback;
  /**
   * Callback function invoked when the library asks application how
   * many padding bytes are required for the transmission of the given
   * frame.
   */
  nghttp2_select_padding_callback2 select_padding_callback2;
  /**
   * Deprecated.  Use read_length_callback2 instead.  The callback
   * function used to determine the length allowed in
   * `nghttp2_data_source_read_callback()`
   */
  nghttp2_data_source_read_length_callback read_length_callback;
  /**
   * The callback function used to determine the length allowed in
   * `nghttp2_data_source_read_callback2()`
   */
  nghttp2_data_source_read_length_callback2 read_length_callback2;
  /**
   * Sets callback function invoked when a frame header is received.
   */
  nghttp2_on_begin_frame_callback on_begin_frame_callback;
  nghttp2_send_data_callback send_data_callback;
  /**
   * Deprecated.  Use pack_extension_callback2 instead.
   */
  nghttp2_pack_extension_callback pack_extension_callback;
  nghttp2_pack_extension_callback2 pack_extension_callback2;
  nghttp2_unpack_extension_callback unpack_extension_callback;
  nghttp2_on_extension_chunk_recv_callback on_extension_chunk_recv_callback;
  nghttp2_error_callback error_callback;
  nghttp2_error_callback2 error_callback2;
};

#endif /* NGHTTP2_CALLBACKS_H */
