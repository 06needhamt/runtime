/*
 * loader.c: Image Loader 
 *
 * Authors:
 *   Paolo Molaro (lupus@ximian.com)
 *   Miguel de Icaza (miguel@ximian.com)
 *   Patrik Torstensson (patrik.torstensson@labs2.com)
 *
 * (C) 2001 Ximian, Inc.
 *
 * This file is used by the interpreter and the JIT engine to locate
 * assemblies.  Used to load AssemblyRef and later to resolve various
 * kinds of `Refs'.
 *
 * TODO:
 *   This should keep track of the assembly versions that we are loading.
 *
 */
#include <config.h>
#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/image.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/tokentype.h>
#include <mono/metadata/cil-coff.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/loader.h>
#include <mono/metadata/class.h>
#include <mono/metadata/debug-helpers.h>

static gboolean dummy_icall = TRUE;

MonoDefaults mono_defaults;

#ifdef __CYGWIN__
#define mono_map_dll(name) (name)
#else
static const char *dll_map[] = {
	"libc", "libc.so.6",
	"libm", "libm.so.6",
	"cygwin1.dll", "libc.so.6", 
	NULL, NULL
};

static const char *
mono_map_dll (const char *name)
{
	int i = 0;

	while (dll_map [i]) {
		if (!strcmp (dll_map [i], name))
			return  dll_map [i + 1];
		i += 2;
	}

	return name;
}
#endif

static GHashTable *icall_hash = NULL;

void
mono_add_internal_call (const char *name, gconstpointer method)
{
	if (!icall_hash) {
		dummy_icall = FALSE;
		icall_hash = g_hash_table_new (g_str_hash , g_str_equal);
	}

	g_hash_table_insert (icall_hash, g_strdup (name), method);
}

static void
ves_icall_dummy (void)
{
	g_warning ("the mono runtime is not initialized");
	g_assert_not_reached ();
}

gpointer
mono_lookup_internal_call (MonoMethod *method)
{
	char *name;
	char *tmpsig;
	gpointer res;

	if (dummy_icall)
		return ves_icall_dummy;

	if (!method) {
		g_warning ("can't resolve internal call, method is null");
	}

	if (!icall_hash) {
		g_warning ("icall_hash not initialized");
		g_assert_not_reached ();
	}

	name = g_strconcat (method->klass->name_space, ".", method->klass->name, "::", method->name, NULL);
	if (!(res = g_hash_table_lookup (icall_hash, name))) {
		// trying to resolve with full signature
		g_free (name);
	
		tmpsig = mono_signature_get_desc(method->signature, TRUE);
		name = g_strconcat (method->klass->name_space, ".", method->klass->name, "::", method->name, "(", tmpsig, ")", NULL);
		if (!(res = g_hash_table_lookup (icall_hash, name))) {
			g_warning ("cant resolve internal call to \"%s\" (tested without signature also)", name);
			g_print ("\nYour mono runtime and corlib are out of sync.\n");
			g_print ("When you update one from cvs you need to update, compile and install\nthe other too.\n");
			g_print ("Do not report this as a bug unless you're sure you have updated correctly:\nyou probably have a broken mono install.\n");
			g_print ("If you see other errors or faults after this message they are probably related\n");
			g_print ("and you need to fix your mono install first.\n");

			g_free (name);
			g_free (tmpsig);

			return NULL;
		}

		g_free(tmpsig);
	}

	g_free (name);

	return res;
}

MonoClassField*
mono_field_from_memberref (MonoImage *image, guint32 token, MonoClass **retklass)
{
	MonoClass *klass;
	MonoTableInfo *tables = image->tables;
	guint32 cols[6];
	guint32 nindex, class;
	const char *fname;
	const char *ptr;
	guint32 idx = mono_metadata_token_index (token);

	mono_metadata_decode_row (&tables [MONO_TABLE_MEMBERREF], idx-1, cols, MONO_MEMBERREF_SIZE);
	nindex = cols [MONO_MEMBERREF_CLASS] >> MEMBERREF_PARENT_BITS;
	class = cols [MONO_MEMBERREF_CLASS] & MEMBERREF_PARENT_MASK;

	fname = mono_metadata_string_heap (image, cols [MONO_MEMBERREF_NAME]);
	
	ptr = mono_metadata_blob_heap (image, cols [MONO_MEMBERREF_SIGNATURE]);
	mono_metadata_decode_blob_size (ptr, &ptr);
	/* we may want to check the signature here... */

	switch (class) {
	case MEMBERREF_PARENT_TYPEREF:
		klass = mono_class_from_typeref (image, MONO_TOKEN_TYPE_REF | nindex);
		if (!klass) {
			g_warning ("Missing field %s in typeref index %d", fname, nindex);
			return NULL;
		}
		mono_class_init (klass);
		if (retklass)
			*retklass = klass;
		return mono_class_get_field_from_name (klass, fname);
	default:
		return NULL;
	}
}

static MonoMethod *
find_method (MonoClass *klass, const char* name, MonoMethodSignature *sig)
{
	int i;
	while (klass) {
		/* mostly dumb search for now */
		for (i = 0; i < klass->method.count; ++i) {
			MonoMethod *m = klass->methods [i];
			if (!strcmp (name, m->name)) {
				if (mono_metadata_signature_equal (sig, m->signature))
					return m;
			}
		}
		klass = klass->parent;
	}
	return NULL;

}

static MonoMethod *
method_from_memberref (MonoImage *image, guint32 idx)
{
	MonoClass *klass;
	MonoMethod *method;
	MonoTableInfo *tables = image->tables;
	guint32 cols[6];
	guint32 nindex, class;
	const char *mname;
	MonoMethodSignature *sig;
	const char *ptr;

	mono_metadata_decode_row (&tables [MONO_TABLE_MEMBERREF], idx-1, cols, 3);
	nindex = cols [MONO_MEMBERREF_CLASS] >> MEMBERREF_PARENT_BITS;
	class = cols [MONO_MEMBERREF_CLASS] & MEMBERREF_PARENT_MASK;
	/*g_print ("methodref: 0x%x 0x%x %s\n", class, nindex,
		mono_metadata_string_heap (m, cols [MONO_MEMBERREF_NAME]));*/

	mname = mono_metadata_string_heap (image, cols [MONO_MEMBERREF_NAME]);
	
	ptr = mono_metadata_blob_heap (image, cols [MONO_MEMBERREF_SIGNATURE]);
	mono_metadata_decode_blob_size (ptr, &ptr);
	sig = mono_metadata_parse_method_signature (image, 0, ptr, NULL);

	switch (class) {
	case MEMBERREF_PARENT_TYPEREF:
		klass = mono_class_from_typeref (image, MONO_TOKEN_TYPE_REF | nindex);
		if (!klass) {
			g_warning ("Missing method %s in typeref index %d", mname, nindex);
			mono_metadata_free_method_signature (sig);
			return NULL;
		}
		mono_class_init (klass);
		method = find_method (klass, mname, sig);
		if (!method)
			g_warning ("Missing method %s in typeref index %d", mname, nindex);
		mono_metadata_free_method_signature (sig);
		return method;
	case MEMBERREF_PARENT_TYPESPEC: {
		guint32 bcols [MONO_TYPESPEC_SIZE];
		guint32 len;
		MonoType *type;
		MonoMethod *result;

		mono_metadata_decode_row (&tables [MONO_TABLE_TYPESPEC], nindex - 1, 
					  bcols, MONO_TYPESPEC_SIZE);
		ptr = mono_metadata_blob_heap (image, bcols [MONO_TYPESPEC_SIGNATURE]);
		len = mono_metadata_decode_value (ptr, &ptr);	
		type = mono_metadata_parse_type (image, MONO_PARSE_TYPE, 0, ptr, &ptr);

		if (type->type != MONO_TYPE_ARRAY && type->type != MONO_TYPE_SZARRAY)
			g_assert_not_reached ();		

		result = (MonoMethod *)g_new0 (MonoMethodPInvoke, 1);
		result->klass = mono_class_get (image, MONO_TOKEN_TYPE_SPEC | nindex);
		result->iflags = METHOD_IMPL_ATTRIBUTE_INTERNAL_CALL;
		result->signature = sig;
		result->name = mname;

		if (!strcmp (mname, ".ctor")) {
			/* we special-case this in the runtime. */
			result->addr = NULL;
			return result;
		}
		
		if (!strcmp (mname, "Set")) {
			g_assert (sig->hasthis);
			g_assert (type->data.array->rank + 1 == sig->param_count);

			result->addr = mono_lookup_internal_call(result);
			return result;
		}

		if (!strcmp (mname, "Get")) {
			g_assert (sig->hasthis);
			g_assert (type->data.array->rank == sig->param_count);

			result->addr = mono_lookup_internal_call(result);
			return result;
		}

		if (!strcmp (mname, "Address")) {
			g_assert (sig->hasthis);
			g_assert (type->data.array->rank == sig->param_count);

			result->addr = mono_lookup_internal_call(result);
			return result;
		}

		g_assert_not_reached ();
		break;
	}
	default:
		g_assert_not_reached ();
	}

	return NULL;
}

static void
fill_pinvoke_info (MonoImage *image, MonoMethodPInvoke *piinfo, int idx)
{
	MonoMethod *mh = &piinfo->method;
	MonoTableInfo *tables = image->tables;
	MonoTableInfo *im = &tables [MONO_TABLE_IMPLMAP];
	MonoTableInfo *mr = &tables [MONO_TABLE_MODULEREF];
	guint32 im_cols [4];
	guint32 mr_cols [1];
	const char *import = NULL;
	const char *scope = NULL;
	char *full_name;
	GModule *gmodule;
	int i;

	for (i = 0; i < im->rows; i++) {
			
		mono_metadata_decode_row (im, i, im_cols, 4);

		if ((im_cols[1] >> 1) == idx + 1) {

			import = mono_metadata_string_heap (image, im_cols [2]);

			mono_metadata_decode_row (mr, im_cols [3] - 1, mr_cols,
						  1);
			
			scope = mono_metadata_string_heap (image, mr_cols [0]);
		}
	}

	piinfo->piflags = im_cols [0];

	g_assert (import && scope);

	scope = mono_map_dll (scope);
	full_name = g_module_build_path (NULL, scope);
	gmodule = g_module_open (full_name, G_MODULE_BIND_LAZY);

	mh->addr = NULL;
	if (!gmodule) {
		if (!(gmodule=g_module_open (scope, G_MODULE_BIND_LAZY))) {
			g_warning ("Failed to load library %s (%s)", full_name, scope);
			g_free (full_name);
			return;
		}
	}
	g_free (full_name);

	g_module_symbol (gmodule, import, &mh->addr); 

	if (!mh->addr) {
		g_warning ("Failed to load function %s from %s", import, scope);
		return;
	}

	mh->flags |= METHOD_ATTRIBUTE_PINVOKE_IMPL;
}

MonoMethod *
mono_get_method (MonoImage *image, guint32 token, MonoClass *klass)
{
	MonoMethod *result;
	int table = mono_metadata_token_table (token);
	int idx = mono_metadata_token_index (token);
	MonoTableInfo *tables = image->tables;
	const char *loc, *sig = NULL;
	int size;
	guint32 cols [MONO_TYPEDEF_SIZE];

	if ((result = g_hash_table_lookup (image->method_cache, GINT_TO_POINTER (token))))
			return result;

	if (table != MONO_TABLE_METHOD) {
		if (table != MONO_TABLE_MEMBERREF)
			g_print("got wrong token: 0x%08x\n", token);
		g_assert (table == MONO_TABLE_MEMBERREF);
		result = method_from_memberref (image, idx);
		g_hash_table_insert (image->method_cache, GINT_TO_POINTER (token), result);
		return result;
	}

	mono_metadata_decode_row (&tables [table], idx - 1, cols, 6);

	if ((cols [2] & METHOD_ATTRIBUTE_PINVOKE_IMPL) ||
	    (cols [1] & METHOD_IMPL_ATTRIBUTE_INTERNAL_CALL))
		result = (MonoMethod *)g_new0 (MonoMethodPInvoke, 1);
	else 
		result = (MonoMethod *)g_new0 (MonoMethodNormal, 1);
	
	result->slot = -1;
	result->klass = klass;
	result->flags = cols [2];
	result->iflags = cols [1];
	result->name = mono_metadata_string_heap (image, cols [3]);

	if (!sig) /* already taken from the methodref */
		sig = mono_metadata_blob_heap (image, cols [4]);
	size = mono_metadata_decode_blob_size (sig, &sig);
	result->signature = mono_metadata_parse_method_signature (image, 0, sig, NULL);

	if (!result->klass) {
		guint32 type = mono_metadata_typedef_from_method (image, token);
		result->klass = mono_class_get (image, MONO_TOKEN_TYPE_DEF | type);
	}

	if (cols [1] & METHOD_IMPL_ATTRIBUTE_INTERNAL_CALL) {
		result->addr = mono_lookup_internal_call (result);
		result->flags |= METHOD_ATTRIBUTE_PINVOKE_IMPL;
	} else if (cols [2] & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
		fill_pinvoke_info (image, (MonoMethodPInvoke *)result, idx - 1);
	} else {
		/* if this is a methodref from another module/assembly, this fails */
		loc = mono_cli_rva_map ((MonoCLIImageInfo *)image->image_info, cols [0]);

		if (!result->klass->dummy && !(result->flags & METHOD_ATTRIBUTE_ABSTRACT) &&
					!(result->iflags & METHOD_IMPL_ATTRIBUTE_RUNTIME)) {
			g_assert (loc);
			((MonoMethodNormal *)result)->header = mono_metadata_parse_mh (image, loc);
		}
	}

	g_hash_table_insert (image->method_cache, GINT_TO_POINTER (token), result);

	return result;
}

void
mono_free_method  (MonoMethod *method)
{
	mono_metadata_free_method_signature (method->signature);
	if (method->flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
		MonoMethodPInvoke *piinfo = (MonoMethodPInvoke *)method;
		g_free (piinfo->code);
	} else if (!(method->iflags & METHOD_IMPL_ATTRIBUTE_INTERNAL_CALL)) {
		mono_metadata_free_mh (((MonoMethodNormal *)method)->header);
	}

	g_free (method);
}

void
mono_method_get_param_names (MonoMethod *method, const char **names)
{
	int i, lastp;
	MonoClass *klass = method->klass;
	MonoTableInfo *methodt = &klass->image->tables [MONO_TABLE_METHOD];
	MonoTableInfo *paramt = &klass->image->tables [MONO_TABLE_PARAM];

	if (!method->signature->param_count)
		return;
	for (i = 0; i < method->signature->param_count; ++i)
		names [i] = "";

	mono_class_init (klass);
	if (!klass->methods)
		return;

	for (i = 0; i < klass->method.count; ++i) {
		if (method == klass->methods [i]) {
			guint32 idx = klass->method.first + i;
			guint32 cols [MONO_PARAM_SIZE];
			guint param_index = mono_metadata_decode_row_col (methodt, idx, MONO_METHOD_PARAMLIST);

			if (idx + 1 < methodt->rows)
				lastp = mono_metadata_decode_row_col (methodt, idx + 1, MONO_METHOD_PARAMLIST);
			else
				lastp = paramt->rows;
			for (i = param_index; i < lastp; ++i) {
				mono_metadata_decode_row (paramt, i -1, cols, MONO_PARAM_SIZE);
				if (cols [MONO_PARAM_SEQUENCE]) /* skip return param spec */
					names [cols [MONO_PARAM_SEQUENCE] - 1] = mono_metadata_string_heap (klass->image, cols [MONO_PARAM_NAME]);
			}
			return;
		}
	}
}
