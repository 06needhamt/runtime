#ifndef _WAPI_THREADS_H_
#define _WAPI_THREADS_H_

#include <glib.h>

#include "mono/io-layer/handles.h"
#include "mono/io-layer/io.h"
#include "mono/io-layer/status.h"

#define TLS_MINIMUM_AVAILABLE 64
#define TLS_OUT_OF_INDEXES 0xFFFFFFFF

#define STILL_ACTIVE STATUS_PENDING

typedef guint32 (*WapiThreadStart)(gpointer);

extern WapiHandle *CreateThread(WapiSecurityAttributes *security, guint32 stacksize, WapiThreadStart start, gpointer param, guint32 create, guint32 *tid);
extern void ExitThread(guint32 exitcode) G_GNUC_NORETURN;
extern gboolean GetExitCodeThread(WapiHandle *handle, guint32 *exitcode);
extern guint32 ResumeThread(WapiHandle *handle);
extern guint32 SuspendThread(WapiHandle *handle);
extern guint32 TlsAlloc(void);
extern gboolean TlsFree(guint32 idx);
extern gpointer TlsGetValue(guint32 idx);
extern gboolean TlsSetValue(guint32 idx, gpointer value);
extern void Sleep(guint32 ms);

#endif /* _WAPI_THREADS_H_ */
