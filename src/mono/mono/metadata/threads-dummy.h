/*
 * threads-dummy.h: System-specific thread support dummy routines
 *
 * Author:
 *	Dick Porter (dick@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 */

#ifndef _MONO_METADATA_THREADS_DUMMY_H_
#define _MONO_METADATA_THREADS_DUMMY_H_

#include <mono/metadata/object.h>

extern guint32 ves_icall_System_Threading_Thread_Thread_internal(MonoObject *this, MonoObject *start);
extern void ves_icall_System_Threading_Thread_Start_internal(MonoObject *this, guint32 id);
extern int ves_icall_System_Threading_Thread_Sleep_internal(int ms);
extern void ves_icall_System_Threading_Thread_Schedule_internal(void);
extern MonoObject *ves_icall_System_Threading_Thread_CurrentThread_internal(void);
extern gboolean ves_icall_System_Threading_Thread_Join_internal(MonoObject *this, int ms, guint32 id);
extern void ves_icall_System_Threading_Thread_DataSlot_register(MonoObject *slot);
extern void ves_icall_System_Threading_Thread_DataSlot_store(MonoObject *slot, MonoObject *data);
extern MonoObject *ves_icall_System_Threading_Thread_DataSlot_retrieve(MonoObject *slot);
extern void ves_icall_System_LocalDataStoreSlot_DataSlot_unregister(MonoObject *this);

#endif /* _MONO_METADATA_THREADS_DUMMY_H_ */
