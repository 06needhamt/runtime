/*
 * loader.c: Image Loader 
 *
 * Authors:
 *   Paolo Molaro (lupus@ximian.com)
 *   Miguel de Icaza (miguel@ximian.com)
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
#include <stdio.h>
#include <string.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/image.h>
#include <mono/metadata/tokentype.h>
#include <mono/metadata/cil-coff.h>
#include "cli.h"

static guint32
typedef_from_name (MonoImage *image, const char *name, const char *nspace, guint32 *mlist)
{
	metadata_t *m = &image->metadata;
	metadata_tableinfo_t *t = &m->tables [META_TABLE_TYPEDEF];
	guint32 i;
	guint32 cols [META_TYPEDEF_SIZE];

	for (i=0; i < t->rows; ++i) {
		mono_metadata_decode_row (t, i, cols, META_TYPEDEF_SIZE);
		if (strcmp (name, mono_metadata_string_heap (m, cols [META_TYPEDEF_NAME])) == 0 
				&& strcmp (nspace, mono_metadata_string_heap (m, cols [META_TYPEDEF_NAMESPACE])) == 0) {
			*mlist = cols [META_TYPEDEF_METHOD_LIST];
			return i + 1;
		}
	}
	g_assert_not_reached ();
	return 0;
}

static void
methoddef_from_memberref (MonoImage *image, guint32 index, MonoImage **rimage, guint32 *rindex)
{
	metadata_t *m = &image->metadata;
	metadata_tableinfo_t *tables = m->tables;
	guint32 cols[6];
	guint32 nindex, sig_len, msig_len, class, i;
	const char *sig, *msig, *mname, *name, *nspace;
	
	mono_metadata_decode_row (&tables [META_TABLE_MEMBERREF], index-1, cols, 3);
	nindex = cols [META_MEMBERREF_CLASS] >> MEMBERREF_PARENT_BITS;
	class = cols [META_MEMBERREF_CLASS] & MEMBERREF_PARENT_MASK;
	/*g_print ("methodref: 0x%x 0x%x %s\n", class, nindex,
		mono_metadata_string_heap (m, cols [META_MEMBERREF_NAME]));*/
	sig = mono_metadata_blob_heap (m, cols [META_MEMBERREF_SIGNATURE]);
	sig_len = mono_metadata_decode_blob_size (sig, &sig);
	mname = mono_metadata_string_heap (m, cols [META_MEMBERREF_NAME]);

	switch (class) {
	case MEMBERREF_PARENT_TYPEREF: {
		guint32 scopeindex, scopetable;

		mono_metadata_decode_row (&tables [META_TABLE_TYPEREF], nindex-1, cols, META_TYPEREF_SIZE);
		scopeindex = cols [META_TYPEREF_SCOPE] >> RESOLTION_SCOPE_BITS;
		scopetable = cols [META_TYPEREF_SCOPE] & RESOLTION_SCOPE_MASK;
		/*g_print ("typeref: 0x%x 0x%x %s.%s\n", scopetable, scopeindex,
			mono_metadata_string_heap (m, cols [META_TYPEREF_NAMESPACE]),
			mono_metadata_string_heap (m, cols [META_TYPEREF_NAME]));*/
		switch (scopetable) {
		case RESOLTION_SCOPE_ASSEMBLYREF:
			/*
			 * To find the method we have the following info:
			 * *) name and namespace of the class from the TYPEREF table
			 * *) name and signature of the method from the MEMBERREF table
			 */
			nspace = mono_metadata_string_heap (m, cols [META_TYPEREF_NAMESPACE]);
			name = mono_metadata_string_heap (m, cols [META_TYPEREF_NAME]);
			
			image = image->references [scopeindex-1]->image;
			m = &image->metadata;
			tables = &m->tables [META_TABLE_METHOD];
			typedef_from_name (image, name, nspace, &i);
			/* mostly dumb search for now */
			for (;i < tables->rows; ++i) {
				mono_metadata_decode_row (tables, i, cols, META_METHOD_SIZE);
				msig = mono_metadata_blob_heap (m, cols [META_METHOD_SIGNATURE]);
				msig_len = mono_metadata_decode_blob_size (msig, &msig);
				
				if (strcmp (mname, mono_metadata_string_heap (m, cols [META_METHOD_NAME])) == 0 
						&& sig_len == msig_len
						&& strncmp (sig, msig, sig_len) == 0) {
					*rimage = image;
					*rindex = i + 1;
					return;
				}
			}
			g_assert_not_reached ();
			break;
		default:
			g_assert_not_reached ();
		}
		break;
	}
	default:
		g_assert_not_reached ();
	}
}

MonoMethod *
mono_get_method (MonoImage *image, guint32 token)
{
	MonoMethod *result;
	int table = mono_metadata_token_table (token);
	int index = mono_metadata_token_index (token);
	metadata_tableinfo_t *tables = image->metadata.tables;
	const char *loc;
	const char *sig = NULL;
	int size;
	guint32 cols[6];

	if (table == META_TABLE_METHOD && (result = g_hash_table_lookup (image->method_cache, GINT_TO_POINTER (token))))
			return result;
	
	if (table != META_TABLE_METHOD) {
		g_assert (table == META_TABLE_MEMBERREF);
		methoddef_from_memberref (image, index, &image, &token);
		return mono_get_method (image, TOKEN_TYPE_METHOD_DEF | token);
	}

	result = g_new0 (MonoMethod, 1);
	result->image = image;
	
	mono_metadata_decode_row (&tables [table], index - 1, cols, 6);
	result->name_idx = cols [3];
	/* if this is a methodref from another module/assembly, this fails */
	loc = cli_rva_map ((cli_image_info_t *)image->image_info, cols [0]);
	g_assert (loc);
	result->header = mono_metadata_parse_mh (&image->metadata, loc);
	if (!sig) /* already taken from the methodref */
		sig = mono_metadata_blob_heap (&image->metadata, cols [4]);
	size = mono_metadata_decode_blob_size (sig, &sig);
	result->signature = mono_metadata_parse_method_signature (&image->metadata, 0, sig, NULL);

	g_hash_table_insert (image->method_cache, GINT_TO_POINTER (token), result);

	return result;
}

void
mono_free_method  (MonoMethod *method)
{
	mono_metadata_free_method_signature (method->signature);
	mono_metadata_free_mh (method->header);
	g_free (method);
}
