/*
 * appdomain.h: AppDomain functions
 *
 * Author:
 *	Dietmar Maurer (dietmar@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 */

#ifndef _MONO_METADATA_APPDOMAIN_H_
#define _MONO_METADATA_APPDOMAIN_H_

#include <config.h>
#include <glib.h>

#include <mono/metadata/object.h>
#include <mono/metadata/reflection.h>

/* This is a copy of System.AppDomainSetup */
typedef struct {
	MonoObject object;
	MonoString *application_base;
	MonoString *application_name;
	MonoString *cache_path;
	MonoString *configuration_file;
	MonoString *dynamic_base;
	MonoString *license_file;
	MonoString *private_bin_path;
	MonoString *private_bin_path_probe;
	MonoString *shadow_copy_directories;
	MonoString *shadow_copy_files;
	MonoBoolean publisher_policy;
} MonoAppDomainSetup;

typedef struct _MonoAppDomain MonoAppDomain;

struct _MonoDomain {
	MonoAppDomain *domain;
	GHashTable *env;
	GHashTable *assemblies;
	MonoAppDomainSetup *setup;
	MonoString *friendly_name;
	GHashTable *ldstr_table;
	GHashTable *class_vtable_hash;
	GHashTable *jit_code_hash;
};

/* This is a copy of System.AppDomain */
struct _MonoAppDomain {
	MonoObject  object;
	MonoDomain *data;
};

MonoDomain *
mono_init (void);

inline MonoDomain *
mono_domain_get (void);

inline void
mono_domain_set (MonoDomain *domain);

MonoAssembly *
mono_domain_assembly_open (MonoDomain *domain, char *name);

void
mono_domain_unload (MonoDomain *domain);

void
ves_icall_System_AppDomainSetup_InitAppDomainSetup (MonoAppDomainSetup *setup);

MonoAppDomain *
ves_icall_System_AppDomain_getCurDomain            (void);

MonoAppDomain *
ves_icall_System_AppDomain_createDomain            (MonoString         *friendly_name,
						    MonoAppDomainSetup *setup);

MonoObject *
ves_icall_System_AppDomain_GetData                 (MonoAppDomain *ad, 
						    MonoString    *name);

void
ves_icall_System_AppDomain_SetData                 (MonoAppDomain *ad, 
						    MonoString    *name, 
						    MonoObject    *data);

MonoAppDomainSetup *
ves_icall_System_AppDomain_getSetup                (MonoAppDomain *ad);

MonoString *
ves_icall_System_AppDomain_getFriendlyName         (MonoAppDomain *ad);

MonoArray *
ves_icall_System_AppDomain_GetAssemblies           (MonoAppDomain *ad);

MonoReflectionAssembly *
ves_icall_System_AppDomain_LoadAssembly            (MonoAppDomain *ad, 
						    MonoReflectionAssemblyName *assRef,
						    MonoObject    *evidence);

void
ves_icall_System_AppDomain_Unload                  (MonoAppDomain *ad);

gint32
ves_icall_System_AppDomain_ExecuteAssembly         (MonoAppDomain *ad, 
						    MonoString    *file, 
						    MonoObject    *evidence,
						    MonoArray     *args);

#endif /* _MONO_METADATA_APPDOMAIN_H_ */
