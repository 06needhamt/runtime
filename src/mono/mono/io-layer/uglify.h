#ifndef _WAPI_UGLIFY_H_
#define _WAPI_UGLIFY_H_

/* Include this file if you insist on using the nasty Win32 typedefs */

#include <stdlib.h>

#include "mono/io-layer/wapi.h"

typedef const guchar *LPCTSTR;		/* replace this with gunichar */
typedef guint8 BYTE;
typedef guint16 WORD;
typedef guint32 DWORD;
typedef gpointer PVOID;
typedef gpointer LPVOID;
typedef gboolean BOOL;
typedef guint32 *LPDWORD;
typedef gint32 LONG;
typedef guint32 ULONG;
typedef gint32 *PLONG;
typedef guint64 LONGLONG;
typedef gunichar2 TCHAR;

typedef WapiHandle *HANDLE;
typedef WapiHandle **LPHANDLE;
typedef WapiHandle *SOCKET;	/* NB: w32 defines this to be int */
typedef WapiSecurityAttributes *LPSECURITY_ATTRIBUTES;
typedef WapiOverlapped *LPOVERLAPPED;
typedef WapiThreadStart LPTHREAD_START_ROUTINE;
typedef WapiCriticalSection CRITICAL_SECTION;
typedef WapiCriticalSection *LPCRITICAL_SECTION;
typedef WapiFileTime FILETIME;
typedef WapiFileTime *LPFILETIME;
typedef WapiSystemTime SYSTEMTIME;
typedef WapiSystemTime *LPSYSTEMTIME;
typedef WapiWSAData WSADATA;
typedef WapiWSAData *LDWSADATA;
typedef WapiLargeInteger LARGE_INTEGER;
typedef WapiLargeInteger *PLARGE_INTEGER;
typedef WapiSystemInfo SYSTEM_INFO;
typedef WapiSystemInfo *LPSYSTEM_INFO;
typedef WapiFloatingSaveArea FLOATING_SAVE_AREA;
typedef WapiFloatingSaveArea *PFLOATING_SAVE_AREA;
typedef WapiContext CONTEXT;
typedef WapiContext *PCONTEXT;
typedef WapiFindData WIN32_FIND_DATA;
typedef WapiFindData *LPWIN32_FIND_DATA;
typedef WapiFileAttributesData WIN32_FILE_ATTRIBUTE_DATA;
typedef WapiGetFileExInfoLevels GET_FILEEX_INFO_LEVELS;

#define CONST const
#define VOID void

#define IN
#define OUT
#define WINAPI

#endif /* _WAPI_UGLIFY_H_ */
