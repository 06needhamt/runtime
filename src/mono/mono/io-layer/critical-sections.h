#ifndef _WAPI_CRITICAL_SECTIONS_H_
#define _WAPI_CRITICAL_SECTIONS_H_

#include <glib.h>
#include <pthread.h>

typedef struct _WapiCriticalSection WapiCriticalSection;

struct _WapiCriticalSection
{
	guint32 depth;
	pthread_mutex_t mutex;
};

extern void InitializeCriticalSection(WapiCriticalSection *section);
extern gboolean InitializeCriticalSectionAndSpinCount(WapiCriticalSection *section, guint32 spincount);
extern void DeleteCriticalSection(WapiCriticalSection *section);
extern guint32 SetCriticalSectionSpinCount(WapiCriticalSection *section, guint32 spincount);
extern gboolean TryEnterCriticalSection(WapiCriticalSection *section);
extern void EnterCriticalSection(WapiCriticalSection *section);
extern void LeaveCriticalSection(WapiCriticalSection *section);

#endif /* _WAPI_CRITICAL_SECTIONS_H_ */
