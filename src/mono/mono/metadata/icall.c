/*
 * icall.c:
 *
 * Authors:
 *   Dietmar Maurer (dietmar@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 */

#include <config.h>
#include <glib.h>
#include <stdarg.h>
#include <string.h>

#include <mono/metadata/object.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/reflection.h>
#include <mono/metadata/assembly.h>
#include "decimal.h"

static MonoObject *
ves_icall_System_Array_GetValue (MonoObject *this, MonoObject *idxs)
{
	MonoClass *ac, *ic;
	MonoArray *ao, *io;
	gint32 i, pos, *ind, esize;
	gpointer *ea;

	io = (MonoArray *)idxs;
	ic = (MonoClass *)io->obj.klass;
	
	ao = (MonoArray *)this;
	ac = (MonoClass *)ao->obj.klass;

	g_assert (ic->rank == 1);
	g_assert (io->bounds [0].length == ac->rank);

	ind = (guint32 *)io->vector;

	pos = ind [0] - ao->bounds [0].lower_bound;
	for (i = 1; i < ac->rank; i++)
		pos = pos*ao->bounds [i].length + ind [i] - 
			ao->bounds [i].lower_bound;

	esize = mono_array_element_size (ac);
	ea = (gpointer*)((char*)ao->vector + (pos * esize));

	if (ac->element_class->valuetype)
		return mono_value_box (ac->element_class, ea);
	else
		return *ea;
}

static void 
ves_icall_System_Array_SetValue (MonoObject *this, MonoObject *value,
				 MonoObject *idxs)
{
	MonoArray *ao, *io, *vo;
	MonoClass *ac, *ic, *vc;
	gint32 i, pos, *ind, esize;
	gpointer *ea;

	vo = (MonoArray *)value;
	vc = (MonoClass *)vo->obj.klass;

	io = (MonoArray *)idxs;
	ic = (MonoClass *)io->obj.klass;
	
	ao = (MonoArray *)this;
	ac = (MonoClass *)ao->obj.klass;

	g_assert (ic->rank == 1);
	g_assert (io->bounds [0].length == ac->rank);
	g_assert (ac->element_class == vo->obj.klass);

	ind = (guint32 *)io->vector;

	pos = ind [0] - ao->bounds [0].lower_bound;
	for (i = 1; i < ac->rank; i++)
		pos = pos*ao->bounds [i].length + ind [i] - 
			ao->bounds [i].lower_bound;

	esize = mono_array_element_size (ac);
	ea = (gpointer*)((char*)ao->vector + (pos * esize));

	if (ac->element_class->valuetype) {
		g_assert (vc->valuetype);

		memcpy (ea, (char *)vo + sizeof (MonoObject), esize);
	} else
		*ea = (gpointer)vo;

}

static void
ves_icall_System_Array_CreateInstance ()
{
	g_warning ("not implemented");
	g_assert_not_reached ();
}


static gint32 
ves_icall_System_Array_GetRank (MonoObject *this)
{
	return this->klass->rank;
}

static gint32
ves_icall_System_Array_GetLength (MonoObject *this, gint32 dimension)
{
	return ((MonoArray *)this)->bounds [dimension].length;
}

static gint32
ves_icall_System_Array_GetLowerBound (MonoObject *this, gint32 dimension)
{
	return ((MonoArray *)this)->bounds [dimension].lower_bound;
}

static void
ves_icall_InitializeArray (MonoArray *array, MonoClassField *field_handle)
{
		guint32 size = mono_array_element_size (((MonoObject*) array)->klass) * mono_array_length (array);
		/*
		 * FIXME: ENOENDIAN: we need to byteswap as needed.
		 */
		memcpy (mono_array_addr (array, char, 0), field_handle->data, size);
}

static MonoObject *
ves_icall_System_Object_MemberwiseClone (MonoObject *this)
{
	return mono_object_clone (this);
}

static MonoObject *
ves_icall_app_get_cur_domain ()
{
	MonoClass *klass = mono_class_from_name (mono_defaults.corlib, "System", "AppDomain");

	return mono_object_new (klass);
}

static MonoReflectionType *
my_mono_new_object (MonoClass *klass, gpointer data)
{
	MonoReflectionType *res = (MonoReflectionType *)mono_object_new (klass);

	res->type = (MonoType *)data;

	return res;
}

static gint32
ves_icall_get_data_chunk (MonoReflectionAssemblyBuilder *assb, gint32 type, MonoArray *buf)
{
	int count;

	if (type == 0) { /* get the header */
		count = mono_image_get_header (assb, (char*)buf->vector, buf->bounds->length);
		if (count != -1)
			return count;
	} else {
		MonoDynamicAssembly *ass = assb->dynamic_assembly;
		count = ass->code.index + ass->meta_size;
		if (count > buf->bounds->length)
			return 0;
		memcpy (buf->vector, ass->code.data, ass->code.index);
		memcpy (buf->vector + ass->code.index, ass->assembly.image->raw_metadata, ass->meta_size);
		return count;
	}
	
	return 0;
}

static MonoObject*
ves_icall_type_from_name (MonoObject *name)
{
	return NULL;
}

static MonoReflectionType*
ves_icall_type_from_handle (gpointer handle)
{
	return my_mono_new_object (mono_defaults.monotype_class, handle);
}

static gpointer
ves_icall_System_Runtime_InteropServices_Marshal_ReadIntPtr (gpointer ptr)
{
	return (gpointer)(*(int *)ptr);
}

static MonoString*
ves_icall_System_Runtime_InteropServices_Marshal_PtrToStringAuto (gpointer ptr)
{
	return mono_string_new ((char *)ptr);
}

static MonoReflectionAssembly*
ves_icall_System_Reflection_Assembly_LoadFrom (MonoString *assName, MonoObject *evidence)
{
	/* FIXME : examine evidence? */
	char *name = mono_string_to_utf8 (assName);
	enum MonoImageOpenStatus status = MONO_IMAGE_OK;
	MonoClass *klass = mono_class_from_name (mono_defaults.corlib, "System.Reflection", "Assembly");
	MonoAssembly *ass = mono_assembly_open (name, NULL, &status);
	MonoReflectionAssembly *res;

	g_assert (ass != NULL);
	g_assert (status == MONO_IMAGE_OK);

	res = (MonoReflectionAssembly *)mono_object_new (klass);
	res->assembly = ass;

	g_free (name);

	return res;
}

static MonoReflectionType*
ves_icall_System_Reflection_Assembly_GetType (MonoReflectionAssembly *assembly, MonoString *type) /* , char throwOnError, char ignoreCase) */
{
	/* FIXME : use throwOnError and ignoreCase */
	gchar *name, *namespace, **parts;
	MonoClass *klass;
	int j = 0;

	name = mono_string_to_utf8 (type);

	parts = g_strsplit (name, ".", 0);
	g_free (name);

	while (parts[j])
		j++;

	name = parts[j-1];
	parts[j-1] = NULL;
	namespace = g_strjoinv (".", parts);
	g_strfreev (parts);

	klass = mono_class_from_name (assembly->assembly->image, namespace, name);
	if (!klass->metadata_inited)
		mono_class_metadata_init (klass);

	g_free (name);
	g_free (namespace);

	return my_mono_new_object (mono_defaults.monotype_class, &klass->byval_arg);
}

static MonoString *
ves_icall_System_MonoType_assQualifiedName (MonoReflectionType *object)
{
	/* FIXME : real rules are more complicated (internal classes,
	  reference types, array types, etc. */


	MonoString *res;
	gchar *fullname;
	MonoClass *klass;

	klass = object->type->data.klass;

	fullname = g_strconcat (klass->name_space, ".",
	                           klass->name, ",",
	                           klass->image->assembly_name);
  res = mono_string_new (fullname);
	g_free (fullname);

	return res;
}

static MonoString *
ves_icall_System_PAL_GetCurrentDirectory (MonoObject *object)
{
	MonoString *res;
	gchar *path = g_get_current_dir ();
	res = mono_string_new (path);
	g_free (path);
	return res;
}

static gpointer icall_map [] = {
	/*
	 * System.Array
	 */
	"System.Array::GetValue",         ves_icall_System_Array_GetValue,
	"System.Array::SetValue",         ves_icall_System_Array_SetValue,
	"System.Array::GetRank",          ves_icall_System_Array_GetRank,
	"System.Array::GetLength",        ves_icall_System_Array_GetLength,
	"System.Array::GetLowerBound",    ves_icall_System_Array_GetLowerBound,
	"System.Array::CreateInstance",   ves_icall_System_Array_CreateInstance,

	/*
	 * System.Object
	 */
	"System.Object::MemberwiseClone", ves_icall_System_Object_MemberwiseClone,

	/*
	 * System.String
	 */
	"System.String::IsInterned", mono_string_is_interned,
	"System.String::Intern", mono_string_intern,

	/*
	 * System.AppDomain
	 */
	"System.AppDomain::getCurDomain", ves_icall_app_get_cur_domain,

	/*
	 * System.Decimal
	 */
	"System.Decimal::decimal2UInt64", mono_decimal2UInt64,
	"System.Decimal::decimal2Int64", mono_decimal2Int64,
	"System.Decimal::double2decimal", mono_double2decimal, /* FIXME: wrong signature. */
	"System.Decimal::decimalIncr", mono_decimalIncr,
	"System.Decimal::decimalSetExponent", mono_decimalSetExponent,
	"System.Decimal::decimal2double", mono_decimal2double,
	"System.Decimal::decimalFloorAndTrunc", mono_decimalFloorAndTrunc,
	"System.Decimal::decimalRound", mono_decimalRound,
	"System.Decimal::decimalMult", mono_decimalMult,
	"System.Decimal::decimalDiv", mono_decimalDiv,
	"System.Decimal::decimalIntDiv", mono_decimalIntDiv,
	"System.Decimal::decimalCompare", mono_decimalCompare,
	"System.Decimal::string2decimal", mono_string2decimal,
	"System.Decimal::decimal2string", mono_decimal2string,

	/*
	 * ModuleBuilder
	 */
	
	/*
	 * AssemblyBuilder
	 */
	"System.Reflection.Emit.AssemblyBuilder::getDataChunk", ves_icall_get_data_chunk,
	
	/*
	 * TypeBuilder
	 */
	
	/*
	 * MethodBuilder
	 */
	
	/*
	 * System.Type
	 */
	"System.Type::internal_from_name", ves_icall_type_from_name,
	"System.Type::internal_from_handle", ves_icall_type_from_handle,

	/*
	 * System.Runtime.CompilerServices.RuntimeHelpers
	 */
	"System.Runtime.CompilerServices.RuntimeHelpers::InitializeArray", ves_icall_InitializeArray,
	
	/*
	 * System.Threading
	 */
	"System.Threading.Thread::Thread_internal", ves_icall_System_Threading_Thread_Thread_internal,
	"System.Threading.Thread::Start_internal", ves_icall_System_Threading_Thread_Start_internal,
	"System.Threading.Thread::Sleep_internal", ves_icall_System_Threading_Thread_Sleep_internal,
	"System.Threading.Thread::Schedule_internal", ves_icall_System_Threading_Thread_Schedule_internal,
	"System.Threading.Thread::CurrentThread_internal", ves_icall_System_Threading_Thread_CurrentThread_internal,
	"System.Threading.Thread::Join_internal", ves_icall_System_Threading_Thread_Join_internal,
	"System.Threading.Thread::DataSlot_register", ves_icall_System_Threading_Thread_DataSlot_register,
	"System.Threading.Thread::DataSlot_store", ves_icall_System_Threading_Thread_DataSlot_store,
	"System.Threading.Thread::DataSlot_retrieve", ves_icall_System_Threading_Thread_DataSlot_retrieve,
	/* Not in the System.Threading namespace, but part of the same code */
	"System.LocalDataStoreSlot::DataSlot_unregister", ves_icall_System_LocalDataStoreSlot_DataSlot_unregister,
	"System.Threading.Monitor::Monitor_enter", ves_icall_System_Threading_Monitor_Monitor_enter,
	"System.Threading.Monitor::Monitor_exit", ves_icall_System_Threading_Monitor_Monitor_exit,
	"System.Threading.Monitor::Monitor_test_owner", ves_icall_System_Threading_Monitor_Monitor_test_owner,
	"System.Threading.Monitor::Monitor_test_synchronised", ves_icall_System_Threading_Monitor_Monitor_test_synchronised,
	"System.Threading.Monitor::Monitor_pulse", ves_icall_System_Threading_Monitor_Monitor_pulse,
	"System.Threading.Monitor::Monitor_pulse_all", ves_icall_System_Threading_Monitor_Monitor_pulse_all,
	"System.Threading.Monitor::Monitor_try_enter", ves_icall_System_Threading_Monitor_Monitor_try_enter,
	"System.Threading.Monitor::Monitor_wait", ves_icall_System_Threading_Monitor_Monitor_wait,

	"System.Runtime.InteropServices.Marshal::ReadIntPtr", ves_icall_System_Runtime_InteropServices_Marshal_ReadIntPtr,
	"System.Runtime.InteropServices.Marshal::PtrToStringAuto", ves_icall_System_Runtime_InteropServices_Marshal_PtrToStringAuto,

	"System.Reflection.Assembly::LoadFrom", ves_icall_System_Reflection_Assembly_LoadFrom,
	"System.Reflection.Assembly::GetType", ves_icall_System_Reflection_Assembly_GetType,

	"System.MonoType::assQualifiedName", ves_icall_System_MonoType_assQualifiedName,

	"System.PAL.OpSys::GetCurrentDirectory", ves_icall_System_PAL_GetCurrentDirectory,
	/*
	 * add other internal calls here
	 */
	NULL, NULL
};

void
mono_init_icall ()
{
	char *n;
	int i = 0;

	while ((n = icall_map [i])) {
		mono_add_internal_call (n, icall_map [i+1]);
		i += 2;
	}
       
}


