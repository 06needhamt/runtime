#ifndef _WAPI_IO_H_
#define _WAPI_IO_H_

#include <stdlib.h>

#include "mono/io-layer/wapi.h"

typedef struct _WapiSecurityAttributes WapiSecurityAttributes;

struct _WapiSecurityAttributes 
{
	guint32 nLength;
	gpointer lpSecurityDescriptor;
	gboolean bInheritHandle;
};

typedef struct _WapiOverlapped WapiOverlapped;

struct _WapiOverlapped
{
	guint32 Internal;
	guint32 InternalHigh;
	guint32 Offset;
	guint32 OffsetHigh;
	WapiHandle *hEvent;
};

#define GENERIC_READ	0x80000000
#define GENERIC_WRITE	0x40000000
#define GENERIC_EXECUTE	0x20000000
#define GENERIC_ALL	0x10000000

#define FILE_SHARE_READ		0x00000001
#define FILE_SHARE_WRITE	0x00000002
#define FILE_SHARE_DELETE	0x00000004

#define CREATE_NEW		1
#define CREATE_ALWAYS		2
#define OPEN_EXISTING		3
#define OPEN_ALWAYS		4
#define TRUNCATE_EXISTING	5


#define FILE_ATTRIBUTE_READONLY			0x00000001
#define FILE_ATTRIBUTE_HIDDEN			0x00000002
#define FILE_ATTRIBUTE_SYSTEM			0x00000004
#define FILE_ATTRIBUTE_DIRECTORY		0x00000010
#define FILE_ATTRIBUTE_ARCHIVE			0x00000020
#define FILE_ATTRIBUTE_ENCRYPTED		0x00000040
#define FILE_ATTRIBUTE_NORMAL			0x00000080
#define FILE_ATTRIBUTE_TEMPORARY		0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE		0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT		0x00000400
#define FILE_ATTRIBUTE_COMPRESSED		0x00000800
#define FILE_ATTRIBUTE_OFFLINE			0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED	0x00002000
#define FILE_FLAG_OPEN_NO_RECALL		0x00100000
#define FILE_FLAG_OPEN_REPARSE_POINT		0x00200000
#define FILE_FLAG_POSIX_SEMANTICS		0x01000000
#define FILE_FLAG_BACKUP_SEMANTICS		0x02000000
#define FILE_FLAG_DELETE_ON_CLOSE		0x04000000
#define FILE_FLAG_SEQUENTIAL_SCAN		0x08000000
#define FILE_FLAG_RANDOM_ACCESS			0x10000000
#define FILE_FLAG_NO_BUFFERING			0x20000000
#define FILE_FLAG_OVERLAPPED			0x40000000
#define FILE_FLAG_WRITE_THROUGH			0x80000000

#define MAX_PATH	260

typedef enum {
	STD_INPUT_HANDLE=-10,
	STD_OUTPUT_HANDLE=-11,
	STD_ERROR_HANDLE=-12,
} WapiStdHandle;

typedef enum {
	FILE_BEGIN=0,
	FILE_CURRENT=1,
	FILE_END=2,
} WapiSeekMethod;

typedef enum {
	FILE_TYPE_UNKNOWN=0x0000,
	FILE_TYPE_DISK=0x0001,
	FILE_TYPE_CHAR=0x0002,
	FILE_TYPE_PIPE=0x0003,
	FILE_TYPE_REMOTE=0x8000,
} WapiFileType;

typedef enum {
	GetFileExInfoStandard=0x0000,
	GetFileExMaxInfoLevel=0x0001
} WapiGetFileExInfoLevels;

typedef struct 
{
	guint32 dwLowDateTime;
	guint32 dwHighDateTime;
} WapiFileTime;

typedef struct 
{
	guint16 wYear;
	guint16 wMonth;
	guint16 wDayOfWeek;
	guint16 wDay;
	guint16 wHour;
	guint16 wMinute;
	guint16 wSecond;
	guint16 wMilliseconds;
} WapiSystemTime;

typedef struct
{
	guint32 dwFileAttributes;
	WapiFileTime ftCreationTime;
	WapiFileTime ftLastAccessTime;
	WapiFileTime ftLastWriteTime;
	guint32 nFileSizeHigh;
	guint32 nFileSizeLow;
	guint32 dwReserved0;
	guint32 dwReserved1;
	guchar cFileName [MAX_PATH];
	guchar cAlternateFileName [14];
} WapiFindData;

typedef struct
{
	guint32 dwFileAttributes;
	WapiFileTime ftCreationTime;
	WapiFileTime ftLastAccessTime;
	WapiFileTime ftLastWriteTime;
	guint32 nFileSizeHigh;
	guint32 nFileSizeLow;
} WapiFileAttributesData;

#define INVALID_SET_FILE_POINTER ((guint32)-1)
#define INVALID_FILE_SIZE ((guint32)0xFFFFFFFF)

extern WapiHandle *CreateFile(const guchar *name, guint32 fileaccess,
			      guint32 sharemode,
			      WapiSecurityAttributes *security,
			      guint32 createmode,
			      guint32 attrs, WapiHandle *template);
extern gboolean DeleteFile(const guchar *name);
extern WapiHandle *GetStdHandle(WapiStdHandle stdhandle);
extern gboolean ReadFile(WapiHandle *handle, gpointer buffer, guint32 numbytes,
			 guint32 *bytesread, WapiOverlapped *overlapped);
extern gboolean WriteFile(WapiHandle *handle, gconstpointer buffer,
			  guint32 numbytes, guint32 *byteswritten,
			  WapiOverlapped *overlapped);
extern gboolean FlushFileBuffers(WapiHandle *handle);
extern gboolean SetEndOfFile(WapiHandle *handle);
extern guint32 SetFilePointer(WapiHandle *handle, gint32 movedistance,
			      gint32 *highmovedistance, WapiSeekMethod method);
extern WapiFileType GetFileType(WapiHandle *handle);
extern guint32 GetFileSize(WapiHandle *handle, guint32 *highsize);
extern gboolean GetFileTime(WapiHandle *handle, WapiFileTime *create_time, WapiFileTime *last_access, WapiFileTime *last_write);
extern gboolean SetFileTime(WapiHandle *handle, const WapiFileTime *create_time, const WapiFileTime *last_access, const WapiFileTime *last_write);
extern gboolean FileTimeToSystemTime(const WapiFileTime *file_time, WapiSystemTime *system_time);
extern WapiHandle *FindFirstFile (const guchar *pattern, WapiFindData *find_data);
extern gboolean FindNextFile (WapiHandle *handle, WapiFindData *find_data);
extern gboolean FindClose (WapiHandle *handle);
extern gboolean CreateDirectory (const guchar *name, WapiSecurityAttributes *security);
extern gboolean RemoveDirectory (const guchar *name);
extern gboolean MoveFile (const guchar *name, const guchar *dest_name);
extern gboolean CopyFile (const guchar *name, const guchar *dest_name, gboolean fail_if_exists);
extern guint32 GetFileAttributes (const guchar *name);
extern gboolean GetFileAttributesEx (const guchar *name, WapiGetFileExInfoLevels level, gpointer info);

#endif /* _WAPI_IO_H_ */
