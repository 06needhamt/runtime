/*
 * threads-pthread.h: System-specific thread support
 *
 * Author:
 *	Dick Porter (dick@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 */

#ifndef _MONO_METADATA_THREADS_PTHREAD_H_
#define _MONO_METADATA_THREADS_PTHREAD_H_

#include <pthread.h>

#include <mono/metadata/object.h>

extern pthread_t ves_icall_System_Threading_Thread_Start_internal(MonoObject *this, MonoObject *start);
extern int ves_icall_System_Threading_Thread_Sleep_internal(int ms);
extern void ves_icall_System_Threading_Thread_Schedule_internal(void);
extern MonoObject *ves_icall_System_Threading_Thread_CurrentThread_internal(void);

#endif /* _MONO_METADATA_THREADS_PTHREAD_H_ */
