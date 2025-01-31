#ifndef SRC_ENV_PROPERTIES_H_
#define SRC_ENV_PROPERTIES_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

// PER_ISOLATE_* macros: We have a lot of per-isolate properties
// and adding and maintaining their getters and setters by hand would be
// difficult so let's make the preprocessor generate them for us.
//
// In each macro, `V` is expected to be the name of a macro or function which
// accepts the number of arguments provided in each tuple in the macro body,
// typically two. The named function will be invoked against each tuple.
//
// Make sure that any macro V defined for use with the PER_ISOLATE_* macros is
// undefined again after use.

// Private symbols are per-isolate primitives but Environment proxies them
// for the sake of convenience.  Strings should be ASCII-only and have a
// "node:" prefix to avoid name clashes with third-party code.
#define PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(V)                               \
  V(arrow_message_private_symbol, "node:arrowMessage")                         \
  V(contextify_context_private_symbol, "node:contextify:context")              \
  V(decorated_private_symbol, "node:decorated")                                \
  V(transfer_mode_private_symbol, "node:transfer_mode")                        \
  V(host_defined_option_symbol, "node:host_defined_option_symbol")             \
  V(js_transferable_wrapper_private_symbol, "node:js_transferable_wrapper")    \
  V(entry_point_module_private_symbol, "node:entry_point_module")              \
  V(entry_point_promise_private_symbol, "node:entry_point_promise")            \
  V(module_source_private_symbol, "node:module_source")                        \
  V(module_export_names_private_symbol, "node:module_export_names")            \
  V(module_circular_visited_private_symbol, "node:module_circular_visited")    \
  V(module_export_private_symbol, "node:module_export")                        \
  V(module_parent_private_symbol, "node:module_parent")                        \
  V(napi_type_tag, "node:napi:type_tag")                                       \
  V(napi_wrapper, "node:napi:wrapper")                                         \
  V(untransferable_object_private_symbol, "node:untransferableObject")         \
  V(exit_info_private_symbol, "node:exit_info_private_symbol")                 \
  V(promise_trace_id, "node:promise_trace_id")                                 \
  V(source_map_data_private_symbol, "node:source_map_data_private_symbol")

// Symbols are per-isolate primitives but Environment proxies them
// for the sake of convenience.
#define PER_ISOLATE_SYMBOL_PROPERTIES(V)                                       \
  V(fs_use_promises_symbol, "fs_use_promises_symbol")                          \
  V(async_id_symbol, "async_id_symbol")                                        \
  V(constructor_key_symbol, "constructor_key_symbol")                          \
  V(handle_onclose_symbol, "handle_onclose")                                   \
  V(no_message_symbol, "no_message_symbol")                                    \
  V(messaging_deserialize_symbol, "messaging_deserialize_symbol")              \
  V(imported_cjs_symbol, "imported_cjs_symbol")                                \
  V(messaging_transfer_symbol, "messaging_transfer_symbol")                    \
  V(messaging_clone_symbol, "messaging_clone_symbol")                          \
  V(messaging_transfer_list_symbol, "messaging_transfer_list_symbol")          \
  V(oninit_symbol, "oninit")                                                   \
  V(owner_symbol, "owner_symbol")                                              \
  V(onpskexchange_symbol, "onpskexchange")                                     \
  V(resource_symbol, "resource_symbol")                                        \
  V(trigger_async_id_symbol, "trigger_async_id_symbol")                        \
  V(source_text_module_default_hdo, "source_text_module_default_hdo")          \
  V(vm_context_no_contextify, "vm_context_no_contextify")                      \
  V(vm_dynamic_import_default_internal, "vm_dynamic_import_default_internal")  \
  V(vm_dynamic_import_main_context_default,                                    \
    "vm_dynamic_import_main_context_default")                                  \
  V(vm_dynamic_import_missing_flag, "vm_dynamic_import_missing_flag")          \
  V(vm_dynamic_import_no_callback, "vm_dynamic_import_no_callback")

// Strings are per-isolate primitives but Environment proxies them
// for the sake of convenience.  Strings should be ASCII-only.
#define PER_ISOLATE_STRING_PROPERTIES(V)                                       \
  V(__filename_string, "__filename")                                           \
  V(__dirname_string, "__dirname")                                             \
  V(ack_string, "ack")                                                         \
  V(address_string, "address")                                                 \
  V(aliases_string, "aliases")                                                 \
  V(alpn_callback_string, "ALPNCallback")                                      \
  V(args_string, "args")                                                       \
  V(asn1curve_string, "asn1Curve")                                             \
  V(async_ids_stack_string, "async_ids_stack")                                 \
  V(attributes_string, "attributes")                                           \
  V(base_string, "base")                                                       \
  V(bits_string, "bits")                                                       \
  V(block_list_string, "blockList")                                            \
  V(buffer_string, "buffer")                                                   \
  V(bytes_parsed_string, "bytesParsed")                                        \
  V(bytes_read_string, "bytesRead")                                            \
  V(bytes_written_string, "bytesWritten")                                      \
  V(ca_string, "ca")                                                           \
  V(cached_data_produced_string, "cachedDataProduced")                         \
  V(cached_data_rejected_string, "cachedDataRejected")                         \
  V(cached_data_string, "cachedData")                                          \
  V(cache_key_string, "cacheKey")                                              \
  V(change_string, "change")                                                   \
  V(changes_string, "changes")                                                 \
  V(channel_string, "channel")                                                 \
  V(chunks_sent_since_last_write_string, "chunksSentSinceLastWrite")           \
  V(clone_unsupported_type_str, "Cannot clone object of unsupported type.")    \
  V(clone_transfer_needed_str,                                                 \
    "Object that needs transfer was found in message but not listed in "       \
    "transferList")                                                            \
  V(clone_untransferable_str, "Found invalid value in transferList.")          \
  V(code_string, "code")                                                       \
  V(column_number_string, "columnNumber")                                      \
  V(column_string, "column")                                                   \
  V(commonjs_string, "commonjs")                                               \
  V(config_string, "config")                                                   \
  V(constants_string, "constants")                                             \
  V(crypto_dh_string, "dh")                                                    \
  V(crypto_dsa_string, "dsa")                                                  \
  V(crypto_ec_string, "ec")                                                    \
  V(crypto_ed25519_string, "ed25519")                                          \
  V(crypto_ed448_string, "ed448")                                              \
  V(crypto_x25519_string, "x25519")                                            \
  V(crypto_x448_string, "x448")                                                \
  V(crypto_rsa_string, "rsa")                                                  \
  V(crypto_rsa_pss_string, "rsa-pss")                                          \
  V(cwd_string, "cwd")                                                         \
  V(data_string, "data")                                                       \
  V(default_is_true_string, "defaultIsTrue")                                   \
  V(deserialize_info_string, "deserializeInfo")                                \
  V(dest_string, "dest")                                                       \
  V(destroyed_string, "destroyed")                                             \
  V(detached_string, "detached")                                               \
  V(dh_string, "DH")                                                           \
  V(divisor_length_string, "divisorLength")                                    \
  V(dns_a_string, "A")                                                         \
  V(dns_aaaa_string, "AAAA")                                                   \
  V(dns_caa_string, "CAA")                                                     \
  V(dns_critical_string, "critical")                                           \
  V(dns_cname_string, "CNAME")                                                 \
  V(dns_mx_string, "MX")                                                       \
  V(dns_naptr_string, "NAPTR")                                                 \
  V(dns_ns_string, "NS")                                                       \
  V(dns_ptr_string, "PTR")                                                     \
  V(dns_soa_string, "SOA")                                                     \
  V(dns_srv_string, "SRV")                                                     \
  V(dns_txt_string, "TXT")                                                     \
  V(done_string, "done")                                                       \
  V(duration_string, "duration")                                               \
  V(ecdh_string, "ECDH")                                                       \
  V(emit_string, "emit")                                                       \
  V(emit_warning_string, "emitWarning")                                        \
  V(empty_object_string, "{}")                                                 \
  V(encoding_string, "encoding")                                               \
  V(entries_string, "entries")                                                 \
  V(entry_type_string, "entryType")                                            \
  V(env_pairs_string, "envPairs")                                              \
  V(env_var_settings_string, "envVarSettings")                                 \
  V(err_sqlite_error_string, "ERR_SQLITE_ERROR")                               \
  V(errcode_string, "errcode")                                                 \
  V(errno_string, "errno")                                                     \
  V(error_string, "error")                                                     \
  V(errstr_string, "errstr")                                                   \
  V(events_waiting, "eventsWaiting")                                           \
  V(events, "events")                                                          \
  V(exchange_string, "exchange")                                               \
  V(expire_string, "expire")                                                   \
  V(exponent_string, "exponent")                                               \
  V(exports_string, "exports")                                                 \
  V(ext_key_usage_string, "ext_key_usage")                                     \
  V(external_stream_string, "_externalStream")                                 \
  V(family_string, "family")                                                   \
  V(fatal_exception_string, "_fatalException")                                 \
  V(fd_string, "fd")                                                           \
  V(fields_string, "fields")                                                   \
  V(file_string, "file")                                                       \
  V(filename_string, "filename")                                               \
  V(filter_string, "filter")                                                   \
  V(fingerprint256_string, "fingerprint256")                                   \
  V(fingerprint512_string, "fingerprint512")                                   \
  V(fingerprint_string, "fingerprint")                                         \
  V(flags_string, "flags")                                                     \
  V(flowlabel_string, "flowlabel")                                             \
  V(fragment_string, "fragment")                                               \
  V(frames_received_string, "framesReceived")                                  \
  V(frames_sent_string, "framesSent")                                          \
  V(function_name_string, "functionName")                                      \
  V(function_string, "function")                                               \
  V(get_string, "get")                                                         \
  V(get_data_clone_error_string, "_getDataCloneError")                         \
  V(get_shared_array_buffer_id_string, "_getSharedArrayBufferId")              \
  V(gid_string, "gid")                                                         \
  V(h2_string, "h2")                                                           \
  V(handle_string, "handle")                                                   \
  V(hash_algorithm_string, "hashAlgorithm")                                    \
  V(help_text_string, "helpText")                                              \
  V(homedir_string, "homedir")                                                 \
  V(host_string, "host")                                                       \
  V(hostmaster_string, "hostmaster")                                           \
  V(http_1_1_string, "http/1.1")                                               \
  V(id_string, "id")                                                           \
  V(identity_string, "identity")                                               \
  V(ignore_string, "ignore")                                                   \
  V(infoaccess_string, "infoAccess")                                           \
  V(inherit_string, "inherit")                                                 \
  V(input_string, "input")                                                     \
  V(internal_binding_string, "internalBinding")                                \
  V(internal_string, "internal")                                               \
  V(ipv4_string, "IPv4")                                                       \
  V(ipv6_string, "IPv6")                                                       \
  V(isclosing_string, "isClosing")                                             \
  V(isfinished_string, "isFinished")                                           \
  V(issuer_string, "issuer")                                                   \
  V(issuercert_string, "issuerCertificate")                                    \
  V(iterator_string, "Iterator")                                               \
  V(jwk_crv_string, "crv")                                                     \
  V(jwk_d_string, "d")                                                         \
  V(jwk_dp_string, "dp")                                                       \
  V(jwk_dq_string, "dq")                                                       \
  V(jwk_dsa_string, "DSA")                                                     \
  V(jwk_e_string, "e")                                                         \
  V(jwk_ec_string, "EC")                                                       \
  V(jwk_g_string, "g")                                                         \
  V(jwk_k_string, "k")                                                         \
  V(jwk_p_string, "p")                                                         \
  V(jwk_q_string, "q")                                                         \
  V(jwk_qi_string, "qi")                                                       \
  V(jwk_kty_string, "kty")                                                     \
  V(jwk_n_string, "n")                                                         \
  V(jwk_oct_string, "oct")                                                     \
  V(jwk_okp_string, "OKP")                                                     \
  V(jwk_rsa_string, "RSA")                                                     \
  V(jwk_x_string, "x")                                                         \
  V(jwk_y_string, "y")                                                         \
  V(kill_signal_string, "killSignal")                                          \
  V(kind_string, "kind")                                                       \
  V(last_insert_rowid_string, "lastInsertRowid")                               \
  V(length_string, "length")                                                   \
  V(library_string, "library")                                                 \
  V(line_number_string, "lineNumber")                                          \
  V(loop_count, "loopCount")                                                   \
  V(mac_string, "mac")                                                         \
  V(max_buffer_string, "maxBuffer")                                            \
  V(max_concurrent_streams_string, "maxConcurrentStreams")                     \
  V(message_port_constructor_string, "MessagePort")                            \
  V(message_port_string, "messagePort")                                        \
  V(message_string, "message")                                                 \
  V(messageerror_string, "messageerror")                                       \
  V(mgf1_hash_algorithm_string, "mgf1HashAlgorithm")                           \
  V(minttl_string, "minttl")                                                   \
  V(module_string, "module")                                                   \
  V(modulus_string, "modulus")                                                 \
  V(modulus_length_string, "modulusLength")                                    \
  V(name_string, "name")                                                       \
  V(named_curve_string, "namedCurve")                                          \
  V(netmask_string, "netmask")                                                 \
  V(next_string, "next")                                                       \
  V(nistcurve_string, "nistCurve")                                             \
  V(node_string, "node")                                                       \
  V(nsname_string, "nsname")                                                   \
  V(num_cols_string, "num_cols")                                               \
  V(object_string, "Object")                                                   \
  V(ocsp_request_string, "OCSPRequest")                                        \
  V(oncertcb_string, "oncertcb")                                               \
  V(onchange_string, "onchange")                                               \
  V(onclienthello_string, "onclienthello")                                     \
  V(oncomplete_string, "oncomplete")                                           \
  V(onconflict_string, "onConflict")                                           \
  V(onconnection_string, "onconnection")                                       \
  V(ondone_string, "ondone")                                                   \
  V(onerror_string, "onerror")                                                 \
  V(onexit_string, "onexit")                                                   \
  V(onhandshakedone_string, "onhandshakedone")                                 \
  V(onhandshakestart_string, "onhandshakestart")                               \
  V(onkeylog_string, "onkeylog")                                               \
  V(onmessage_string, "onmessage")                                             \
  V(onnewsession_string, "onnewsession")                                       \
  V(onocspresponse_string, "onocspresponse")                                   \
  V(onreadstart_string, "onreadstart")                                         \
  V(onreadstop_string, "onreadstop")                                           \
  V(onshutdown_string, "onshutdown")                                           \
  V(onsignal_string, "onsignal")                                               \
  V(onunpipe_string, "onunpipe")                                               \
  V(onwrite_string, "onwrite")                                                 \
  V(openssl_error_stack, "opensslErrorStack")                                  \
  V(options_string, "options")                                                 \
  V(order_string, "order")                                                     \
  V(original_string, "original")                                               \
  V(output_string, "output")                                                   \
  V(overlapped_string, "overlapped")                                           \
  V(parse_error_string, "Parse Error")                                         \
  V(password_string, "password")                                               \
  V(path_string, "path")                                                       \
  V(pending_handle_string, "pendingHandle")                                    \
  V(permission_string, "permission")                                           \
  V(pid_string, "pid")                                                         \
  V(ping_rtt_string, "pingRTT")                                                \
  V(pipe_source_string, "pipeSource")                                          \
  V(pipe_string, "pipe")                                                       \
  V(pipe_target_string, "pipeTarget")                                          \
  V(port1_string, "port1")                                                     \
  V(port2_string, "port2")                                                     \
  V(port_string, "port")                                                       \
  V(preference_string, "preference")                                           \
  V(primordials_string, "primordials")                                         \
  V(priority_string, "priority")                                               \
  V(process_string, "process")                                                 \
  V(promise_string, "promise")                                                 \
  V(prototype_string, "prototype")                                             \
  V(psk_string, "psk")                                                         \
  V(pubkey_string, "pubkey")                                                   \
  V(public_exponent_string, "publicExponent")                                  \
  V(query_string, "query")                                                     \
  V(rate_string, "rate")                                                       \
  V(raw_string, "raw")                                                         \
  V(read_host_object_string, "_readHostObject")                                \
  V(readable_string, "readable")                                               \
  V(reason_string, "reason")                                                   \
  V(refresh_string, "refresh")                                                 \
  V(regexp_string, "regexp")                                                   \
  V(rename_string, "rename")                                                   \
  V(replacement_string, "replacement")                                         \
  V(required_module_facade_url_string,                                         \
    "node:internal/require_module_default_facade")                             \
  V(required_module_facade_source_string,                                      \
    "export * from 'original'; export { default } from 'original'; export "    \
    "const __esModule = true;")                                                \
  V(require_string, "require")                                                 \
  V(resource_string, "resource")                                               \
  V(retry_string, "retry")                                                     \
  V(return_string, "return")                                                   \
  V(salt_length_string, "saltLength")                                          \
  V(scheme_string, "scheme")                                                   \
  V(scopeid_string, "scopeid")                                                 \
  V(script_id_string, "scriptId")                                              \
  V(script_name_string, "scriptName")                                          \
  V(serial_number_string, "serialNumber")                                      \
  V(serial_string, "serial")                                                   \
  V(servername_string, "servername")                                           \
  V(service_string, "service")                                                 \
  V(session_id_string, "sessionId")                                            \
  V(set_string, "set")                                                         \
  V(shell_string, "shell")                                                     \
  V(signal_string, "signal")                                                   \
  V(sink_string, "sink")                                                       \
  V(size_string, "size")                                                       \
  V(sni_context_err_string, "Invalid SNI context")                             \
  V(sni_context_string, "sni_context")                                         \
  V(source_string, "source")                                                   \
  V(source_map_url_string, "sourceMapURL")                                     \
  V(specifier_string, "specifier")                                             \
  V(stack_string, "stack")                                                     \
  V(standard_name_string, "standardName")                                      \
  V(start_time_string, "startTime")                                            \
  V(state_string, "state")                                                     \
  V(statement_string, "statement")                                             \
  V(stats_string, "stats")                                                     \
  V(status_string, "status")                                                   \
  V(stdio_string, "stdio")                                                     \
  V(stream_average_duration_string, "streamAverageDuration")                   \
  V(stream_count_string, "streamCount")                                        \
  V(subject_string, "subject")                                                 \
  V(subjectaltname_string, "subjectaltname")                                   \
  V(syscall_string, "syscall")                                                 \
  V(target_string, "target")                                                   \
  V(thread_id_string, "threadId")                                              \
  V(ticketkeycallback_string, "onticketkeycallback")                           \
  V(timeout_string, "timeout")                                                 \
  V(time_to_first_byte_string, "timeToFirstByte")                              \
  V(time_to_first_byte_sent_string, "timeToFirstByteSent")                     \
  V(time_to_first_header_string, "timeToFirstHeader")                          \
  V(tls_ticket_string, "tlsTicket")                                            \
  V(transfer_string, "transfer")                                               \
  V(transfer_unsupported_type_str,                                             \
    "Cannot transfer object of unsupported type.")                             \
  V(ttl_string, "ttl")                                                         \
  V(type_string, "type")                                                       \
  V(uid_string, "uid")                                                         \
  V(unknown_string, "<unknown>")                                               \
  V(url_special_ftp_string, "ftp:")                                            \
  V(url_special_file_string, "file:")                                          \
  V(url_special_http_string, "http:")                                          \
  V(url_special_https_string, "https:")                                        \
  V(url_special_ws_string, "ws:")                                              \
  V(url_special_wss_string, "wss:")                                            \
  V(url_string, "url")                                                         \
  V(username_string, "username")                                               \
  V(valid_from_string, "valid_from")                                           \
  V(valid_to_string, "valid_to")                                               \
  V(value_string, "value")                                                     \
  V(verify_error_string, "verifyError")                                        \
  V(version_string, "version")                                                 \
  V(weight_string, "weight")                                                   \
  V(windows_hide_string, "windowsHide")                                        \
  V(windows_verbatim_arguments_string, "windowsVerbatimArguments")             \
  V(wrap_string, "wrap")                                                       \
  V(writable_string, "writable")                                               \
  V(write_host_object_string, "_writeHostObject")                              \
  V(write_queue_size_string, "writeQueueSize")                                 \
  V(x_forwarded_string, "x-forwarded-for")

#define PER_ISOLATE_TEMPLATE_PROPERTIES(V)                                     \
  V(async_wrap_ctor_template, v8::FunctionTemplate)                            \
  V(async_wrap_object_ctor_template, v8::FunctionTemplate)                     \
  V(binding_data_default_template, v8::ObjectTemplate)                         \
  V(blob_constructor_template, v8::FunctionTemplate)                           \
  V(blob_reader_constructor_template, v8::FunctionTemplate)                    \
  V(blocklist_constructor_template, v8::FunctionTemplate)                      \
  V(contextify_global_template, v8::ObjectTemplate)                            \
  V(contextify_wrapper_template, v8::ObjectTemplate)                           \
  V(crypto_key_object_handle_constructor, v8::FunctionTemplate)                \
  V(env_proxy_template, v8::ObjectTemplate)                                    \
  V(env_proxy_ctor_template, v8::FunctionTemplate)                             \
  V(dir_instance_template, v8::ObjectTemplate)                                 \
  V(fd_constructor_template, v8::ObjectTemplate)                               \
  V(fdclose_constructor_template, v8::ObjectTemplate)                          \
  V(fdentry_constructor_template, v8::FunctionTemplate)                        \
  V(filehandlereadwrap_template, v8::ObjectTemplate)                           \
  V(fsreqpromise_constructor_template, v8::ObjectTemplate)                     \
  V(handle_wrap_ctor_template, v8::FunctionTemplate)                           \
  V(histogram_ctor_template, v8::FunctionTemplate)                             \
  V(http2settings_constructor_template, v8::ObjectTemplate)                    \
  V(http2stream_constructor_template, v8::ObjectTemplate)                      \
  V(http2ping_constructor_template, v8::ObjectTemplate)                        \
  V(i18n_converter_template, v8::ObjectTemplate)                               \
  V(intervalhistogram_constructor_template, v8::FunctionTemplate)              \
  V(js_transferable_constructor_template, v8::FunctionTemplate)                \
  V(libuv_stream_wrap_ctor_template, v8::FunctionTemplate)                     \
  V(message_port_constructor_template, v8::FunctionTemplate)                   \
  V(module_wrap_constructor_template, v8::FunctionTemplate)                    \
  V(microtask_queue_ctor_template, v8::FunctionTemplate)                       \
  V(pipe_constructor_template, v8::FunctionTemplate)                           \
  V(promise_wrap_template, v8::ObjectTemplate)                                 \
  V(sab_lifetimepartner_constructor_template, v8::FunctionTemplate)            \
  V(script_context_constructor_template, v8::FunctionTemplate)                 \
  V(secure_context_constructor_template, v8::FunctionTemplate)                 \
  V(shutdown_wrap_template, v8::ObjectTemplate)                                \
  V(socketaddress_constructor_template, v8::FunctionTemplate)                  \
  V(sqlite_statement_sync_constructor_template, v8::FunctionTemplate)          \
  V(sqlite_session_constructor_template, v8::FunctionTemplate)                 \
  V(streambaseentry_ctor_template, v8::FunctionTemplate)                       \
  V(streambaseoutputstream_constructor_template, v8::ObjectTemplate)           \
  V(streamentry_ctor_template, v8::FunctionTemplate)                           \
  V(streamentry_opaque_ctor_template, v8::FunctionTemplate)                    \
  V(qlogoutputstream_constructor_template, v8::ObjectTemplate)                 \
  V(tcp_constructor_template, v8::FunctionTemplate)                            \
  V(tty_constructor_template, v8::FunctionTemplate)                            \
  V(write_wrap_template, v8::ObjectTemplate)                                   \
  V(worker_heap_snapshot_taker_template, v8::ObjectTemplate)                   \
  V(x509_constructor_template, v8::FunctionTemplate)

#define PER_REALM_STRONG_PERSISTENT_VALUES(V)                                  \
  V(async_hooks_after_function, v8::Function)                                  \
  V(async_hooks_before_function, v8::Function)                                 \
  V(async_hooks_callback_trampoline, v8::Function)                             \
  V(async_hooks_binding, v8::Object)                                           \
  V(async_hooks_destroy_function, v8::Function)                                \
  V(async_hooks_init_function, v8::Function)                                   \
  V(async_hooks_promise_resolve_function, v8::Function)                        \
  V(buffer_prototype_object, v8::Object)                                       \
  V(crypto_key_object_constructor, v8::Function)                               \
  V(crypto_key_object_private_constructor, v8::Function)                       \
  V(crypto_key_object_public_constructor, v8::Function)                        \
  V(crypto_key_object_secret_constructor, v8::Function)                        \
  V(domexception_function, v8::Function)                                       \
  V(enhance_fatal_stack_after_inspector, v8::Function)                         \
  V(enhance_fatal_stack_before_inspector, v8::Function)                        \
  V(get_source_map_error_source, v8::Function)                                 \
  V(host_import_module_dynamically_callback, v8::Function)                     \
  V(host_initialize_import_meta_object_callback, v8::Function)                 \
  V(http2session_on_altsvc_function, v8::Function)                             \
  V(http2session_on_error_function, v8::Function)                              \
  V(http2session_on_frame_error_function, v8::Function)                        \
  V(http2session_on_goaway_data_function, v8::Function)                        \
  V(http2session_on_headers_function, v8::Function)                            \
  V(http2session_on_origin_function, v8::Function)                             \
  V(http2session_on_ping_function, v8::Function)                               \
  V(http2session_on_priority_function, v8::Function)                           \
  V(http2session_on_settings_function, v8::Function)                           \
  V(http2session_on_stream_close_function, v8::Function)                       \
  V(http2session_on_stream_trailers_function, v8::Function)                    \
  V(internal_binding_loader, v8::Function)                                     \
  V(immediate_callback_function, v8::Function)                                 \
  V(inspector_console_extension_installer, v8::Function)                       \
  V(inspector_disable_async_hooks, v8::Function)                               \
  V(inspector_disable_network_tracking, v8::Function)                          \
  V(inspector_enable_async_hooks, v8::Function)                                \
  V(inspector_enable_network_tracking, v8::Function)                           \
  V(maybe_cache_generated_source_map, v8::Function)                            \
  V(messaging_deserialize_create_object, v8::Function)                         \
  V(message_port, v8::Object)                                                  \
  V(builtin_module_require, v8::Function)                                      \
  V(performance_entry_callback, v8::Function)                                  \
  V(prepare_stack_trace_callback, v8::Function)                                \
  V(process_object, v8::Object)                                                \
  V(process_emit_warning_sync, v8::Function)                                   \
  V(primordials, v8::Object)                                                   \
  V(primordials_safe_map_prototype_object, v8::Object)                         \
  V(primordials_safe_set_prototype_object, v8::Object)                         \
  V(primordials_safe_weak_map_prototype_object, v8::Object)                    \
  V(primordials_safe_weak_set_prototype_object, v8::Object)                    \
  V(promise_hook_handler, v8::Function)                                        \
  V(promise_reject_callback, v8::Function)                                     \
  V(snapshot_serialize_callback, v8::Function)                                 \
  V(snapshot_deserialize_callback, v8::Function)                               \
  V(snapshot_deserialize_main, v8::Function)                                   \
  V(source_map_cache_getter, v8::Function)                                     \
  V(tick_callback_function, v8::Function)                                      \
  V(timers_callback_function, v8::Function)                                    \
  V(tls_wrap_constructor_function, v8::Function)                               \
  V(trace_category_state_function, v8::Function)                               \
  V(udp_constructor_function, v8::Function)                                    \
  V(url_constructor_function, v8::Function)                                    \
  V(wasm_streaming_compilation_impl, v8::Function)                             \
  V(wasm_streaming_object_constructor, v8::Function)

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_ENV_PROPERTIES_H_
