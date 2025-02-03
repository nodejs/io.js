/* MIT License
 *
 * Copyright (c) 2024 Brad House
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
#ifndef __ARES_EVENT_WIN32_H
#define __ARES_EVENT_WIN32_H

#ifdef _WIN32
#  ifdef HAVE_WINSOCK2_H
#    include <winsock2.h>
#  endif
#  ifdef HAVE_WS2TCPIP_H
#    include <ws2tcpip.h>
#  endif
#  ifdef HAVE_MSWSOCK_H
#    include <mswsock.h>
#  endif
#  ifdef HAVE_WINDOWS_H
#    include <windows.h>
#  endif

/* From winternl.h */

/* If WDK is not installed and not using MinGW, provide the needed definitions
 */
typedef LONG NTSTATUS;

typedef struct _IO_STATUS_BLOCK {
  union {
    NTSTATUS Status;
    PVOID    Pointer;
  };

  ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef VOID(NTAPI *PIO_APC_ROUTINE)(PVOID            ApcContext,
                                     PIO_STATUS_BLOCK IoStatusBlock,
                                     ULONG            Reserved);

/* From ntstatus.h */
#  define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#  ifndef STATUS_PENDING
#    define STATUS_PENDING ((NTSTATUS)0x00000103L)
#  endif
#  define STATUS_CANCELLED ((NTSTATUS)0xC0000120L)
#  define STATUS_NOT_FOUND ((NTSTATUS)0xC0000225L)

typedef struct _UNICODE_STRING {
  USHORT  Length;
  USHORT  MaximumLength;
  LPCWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
  ULONG           Length;
  HANDLE          RootDirectory;
  PUNICODE_STRING ObjectName;
  ULONG           Attributes;
  PVOID           SecurityDescriptor;
  PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#  ifndef FILE_OPEN
#    define FILE_OPEN 0x00000001UL
#  endif

/* Not sure what headers might have these */
#  define IOCTL_AFD_POLL 0x00012024

#  define AFD_POLL_RECEIVE_BIT           0
#  define AFD_POLL_RECEIVE               (1 << AFD_POLL_RECEIVE_BIT)
#  define AFD_POLL_RECEIVE_EXPEDITED_BIT 1
#  define AFD_POLL_RECEIVE_EXPEDITED     (1 << AFD_POLL_RECEIVE_EXPEDITED_BIT)
#  define AFD_POLL_SEND_BIT              2
#  define AFD_POLL_SEND                  (1 << AFD_POLL_SEND_BIT)
#  define AFD_POLL_DISCONNECT_BIT        3
#  define AFD_POLL_DISCONNECT            (1 << AFD_POLL_DISCONNECT_BIT)
#  define AFD_POLL_ABORT_BIT             4
#  define AFD_POLL_ABORT                 (1 << AFD_POLL_ABORT_BIT)
#  define AFD_POLL_LOCAL_CLOSE_BIT       5
#  define AFD_POLL_LOCAL_CLOSE           (1 << AFD_POLL_LOCAL_CLOSE_BIT)
#  define AFD_POLL_CONNECT_BIT           6
#  define AFD_POLL_CONNECT               (1 << AFD_POLL_CONNECT_BIT)
#  define AFD_POLL_ACCEPT_BIT            7
#  define AFD_POLL_ACCEPT                (1 << AFD_POLL_ACCEPT_BIT)
#  define AFD_POLL_CONNECT_FAIL_BIT      8
#  define AFD_POLL_CONNECT_FAIL          (1 << AFD_POLL_CONNECT_FAIL_BIT)
#  define AFD_POLL_QOS_BIT               9
#  define AFD_POLL_QOS                   (1 << AFD_POLL_QOS_BIT)
#  define AFD_POLL_GROUP_QOS_BIT         10
#  define AFD_POLL_GROUP_QOS             (1 << AFD_POLL_GROUP_QOS_BIT)

#  define AFD_NUM_POLL_EVENTS 11
#  define AFD_POLL_ALL        ((1 << AFD_NUM_POLL_EVENTS) - 1)

typedef struct _AFD_POLL_HANDLE_INFO {
  HANDLE   Handle;
  ULONG    Events;
  NTSTATUS Status;
} AFD_POLL_HANDLE_INFO, *PAFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
  LARGE_INTEGER        Timeout;
  ULONG                NumberOfHandles;
  ULONG                Exclusive;
  AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO, *PAFD_POLL_INFO;

/* Prototypes for dynamically loaded functions from ntdll.dll */
typedef NTSTATUS(NTAPI *NtCancelIoFileEx_t)(HANDLE           FileHandle,
                                            PIO_STATUS_BLOCK IoRequestToCancel,
                                            PIO_STATUS_BLOCK IoStatusBlock);
typedef NTSTATUS(NTAPI *NtDeviceIoControlFile_t)(
  HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
  PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer,
  ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);

typedef NTSTATUS(NTAPI *NtCreateFile_t)(
  PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
  POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock,
  PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
  ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength);

/* On UWP/Windows Store, these definitions aren't there for some reason */
#  ifndef SIO_BSP_HANDLE_POLL
#    define SIO_BSP_HANDLE_POLL 0x4800001D
#  endif

#  ifndef SIO_BASE_HANDLE
#    define SIO_BASE_HANDLE 0x48000022
#  endif

#  ifndef HANDLE_FLAG_INHERIT
#    define HANDLE_FLAG_INHERIT 0x00000001
#  endif

#endif /* _WIN32 */

#endif /* __ARES_EVENT_WIN32_H */
