
/*
 * reflection.c: Routines for creating an image at runtime.
 * 
 * Author:
 *   Paolo Molaro (lupus@ximian.com)
 *
 * (C) 2001 Ximian, Inc.  http://www.ximian.com
 *
 */
#include <config.h>
#include "mono/metadata/reflection.h"
#include "mono/metadata/tabledefs.h"
#include "mono/metadata/tokentype.h"
#include <stdio.h>
#include <glib.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include "image.h"
#include "cil-coff.h"
#include "rawbuffer.h"
#include "mono-endian.h"
#include "private.h"

#define TEXT_OFFSET 512
#define CLI_H_SIZE 136
#define FILE_ALIGN 512

const unsigned char table_sizes [64] = {
	MONO_MODULE_SIZE,
	MONO_TYPEREF_SIZE,
	MONO_TYPEDEF_SIZE,
	0,
	MONO_FIELD_SIZE,
	0,
	MONO_METHOD_SIZE,
	0,
	MONO_PARAM_SIZE,
	MONO_INTERFACEIMPL_SIZE,
	MONO_MEMBERREF_SIZE,	/* 0x0A */
	MONO_CONSTANT_SIZE,
	MONO_CUSTOM_ATTR_SIZE,
	MONO_FIELD_MARSHAL_SIZE,
	MONO_DECL_SECURITY_SIZE,
	MONO_CLASS_LAYOUT_SIZE,
	MONO_FIELD_LAYOUT_SIZE,	/* 0x10 */
	MONO_STAND_ALONE_SIGNATURE_SIZE,
	MONO_EVENT_MAP_SIZE,
	0,
	MONO_EVENT_SIZE,
	MONO_PROPERTY_MAP_SIZE,
	0,
	MONO_PROPERTY_SIZE,
	MONO_METHOD_SEMA_SIZE,
	MONO_MTHODIMPL_SIZE,
	MONO_MODULEREF_SIZE,	/* 0x1A */
	MONO_TYPESPEC_SIZE,
	MONO_IMPLMAP_SIZE,	
	MONO_FIELD_RVA_SIZE,
	0,
	0,
	MONO_ASSEMBLY_SIZE,	/* 0x20 */
	MONO_ASSEMBLY_PROCESSOR_SIZE,
	MONO_ASSEMBLYOS_SIZE,
	MONO_ASSEMBLYREF_SIZE,
	MONO_ASSEMBLYREFPROC_SIZE,
	MONO_ASSEMBLYREFOS_SIZE,
	MONO_FILE_SIZE,
	MONO_EXP_TYPE_SIZE,
	MONO_MANIFEST_SIZE,
	MONO_NESTED_CLASS_SIZE,
	0	/* 0x2A */
};

static void
alloc_table (MonoDynamicTable *table, guint nrows)
{
	table->rows = nrows;
	g_assert (table->columns);
	table->values = g_realloc (table->values, (1 + table->rows) * table->columns * sizeof (guint32));
}

static guint32
string_heap_insert (MonoStringHeap *sh, const char *str)
{
	guint32 idx;
	guint32 len;
	gpointer oldkey, oldval;

	if (g_hash_table_lookup_extended (sh->hash, str, &oldkey, &oldval))
		return GPOINTER_TO_UINT (oldval);

	len = strlen (str) + 1;
	idx = sh->index;
	if (idx + len > sh->alloc_size) {
		sh->alloc_size += len + 4096;
		sh->data = g_realloc (sh->data, sh->alloc_size);
	}
	/*
	 * We strdup the string even if we already copy them in sh->data
	 * so that the string pointers in the hash remain valid even if
	 * we need to realloc sh->data. We may want to avoid that later.
	 */
	g_hash_table_insert (sh->hash, g_strdup (str), GUINT_TO_POINTER (idx));
	memcpy (sh->data + idx, str, len);
	sh->index += len;
	return idx;
}

static void
string_heap_init (MonoStringHeap *sh)
{
	sh->index = 0;
	sh->alloc_size = 4096;
	sh->data = g_malloc (4096);
	sh->hash = g_hash_table_new (g_str_hash, g_str_equal);
	string_heap_insert (sh, "");
}

static void
string_heap_free (MonoStringHeap *sh)
{
	g_free (sh->data);
	g_hash_table_foreach (sh->hash, g_free, NULL);
	g_hash_table_destroy (sh->hash);
}

static guint32
mono_image_add_stream_data (MonoDynamicStream *stream, char *data, guint32 len)
{
	guint32 idx;
	if (stream->alloc_size < stream->index + len) {
		stream->alloc_size += len + 4096;
		stream->data = g_realloc (stream->data, stream->alloc_size);
	}
	memcpy (stream->data + stream->index, data, len);
	idx = stream->index;
	stream->index += len;
	/* 
	 * align index? Not without adding an additional param that controls it since
	 * we may store a blob value in pieces.
	 */
	return idx;
}

static void
encode_type (MonoType *type, char *p, char **endbuf)
{
	switch (type->type){
	case MONO_TYPE_VOID:
	case MONO_TYPE_BOOLEAN:
	case MONO_TYPE_CHAR:
	case MONO_TYPE_I1:
	case MONO_TYPE_U1:
	case MONO_TYPE_I2:
	case MONO_TYPE_U2:
	case MONO_TYPE_I4:
	case MONO_TYPE_U4:
	case MONO_TYPE_I8:
	case MONO_TYPE_U8:
	case MONO_TYPE_R4:
	case MONO_TYPE_R8:
	case MONO_TYPE_I:
	case MONO_TYPE_U:
	case MONO_TYPE_STRING:
	case MONO_TYPE_OBJECT:
	case MONO_TYPE_TYPEDBYREF:
		mono_metadata_encode_value (type->type, p, endbuf);
		break;
	default:
		g_error ("need to encode type %d", type->type);
	}
}

static guint32
method_encode_signature (MonoDynamicAssembly *assembly, MonoMethodBuilder *mb)
{
	char *buf;
	char *p;
	int i;
	guint32 size = 10 + mb->nparams * 10;
	guint32 idx;
	char blob_size [6];
	char *b = blob_size;
	
	p = buf = g_malloc (size);
	if (!(mb->attrs & METHOD_ATTRIBUTE_STATIC))
		*p |= 0x20; /* hasthis */
	/* 
	 * FIXME: set also call convention and explict_this if needed.
	 */
	p++;
	mono_metadata_encode_value (mb->nparams, p, &p);
	encode_type (mb->ret, p, &p);
	for (i = 0; i < mb->nparams; ++i) {
		encode_type (mb->params [i], p, &p);
	}
	/* store length */
	mono_metadata_encode_value (p-buf, b, &b);
	idx = mono_image_add_stream_data (&assembly->blob, blob_size, b-blob_size);
	mono_image_add_stream_data (&assembly->blob, buf, p-buf);
	g_free (buf);
	return idx;
}

static guint32
method_encode_code (MonoDynamicAssembly *assembly, MonoMethodBuilder *mb)
{
	/* we use only tiny formats now: need  to implement ILGenerator */
	char flags = 0;
	guint32 idx;
	/* check for exceptions, maxstack, locals */
	if (mb->code_size < 64 && !(mb->code_size & 1)) {
		flags = (mb->code_size << 2) | 0x2;
	} else if (mb->code_size < 32 && (mb->code_size & 1)) {
		flags = (mb->code_size << 2) | 0x6; /* LAMESPEC: see metadata.c */
	} else {
		g_error ("fat method headers not yet supported");
	}
	idx = mono_image_add_stream_data (&assembly->code, &flags, 1);
	mono_image_add_stream_data (&assembly->code, mb->code, mb->code_size);
	return assembly->text_rva + idx + CLI_H_SIZE;
}

static void
mono_image_get_method_info (MonoMethodBuilder *mb, MonoDynamicAssembly *assembly)
{
	MonoDynamicTable *table;
	guint32 *values;

	table = &assembly->tables [MONO_TABLE_METHOD];
	mb->table_idx = table->next_idx ++;
	values = table->values + mb->table_idx * MONO_METHOD_SIZE;
	values [MONO_METHOD_NAME] = string_heap_insert (&assembly->sheap, mb->name);
	values [MONO_METHOD_FLAGS] = mb->attrs;
	values [MONO_METHOD_IMPLFLAGS] = 0;
	values [MONO_METHOD_SIGNATURE] = method_encode_signature (assembly, mb);
	values [MONO_METHOD_PARAMLIST] = 1; /* FIXME: add support later */
	values [MONO_METHOD_RVA] = method_encode_code (assembly, mb);
}

static guint32
mono_image_typedef_or_ref (MonoDynamicAssembly *assembly, MonoClass *klass)
{
	MonoDynamicTable *table;
	guint32 *values;
	guint32 token;

	if (!assembly->typeref)
		assembly->typeref = g_hash_table_new (g_direct_hash, g_direct_equal);
	
	token = GPOINTER_TO_UINT (g_hash_table_lookup (assembly->typeref, klass));
	if (token)
		return token;
	if (klass->image != mono_defaults.corlib)
		g_error ("multiple assemblyref not yet supported");

	table = &assembly->tables [MONO_TABLE_TYPEREF];
	alloc_table (table, table->rows + 1);
	values = table->values + table->next_idx * MONO_TYPEREF_SIZE;
	values [MONO_TYPEREF_SCOPE] = (1 << 2) | 2; /* first row in assemblyref LAMESPEC, see get.c */
	values [MONO_TYPEREF_NAME] = string_heap_insert (&assembly->sheap, klass->name);
	values [MONO_TYPEREF_NAMESPACE] = string_heap_insert (&assembly->sheap, klass->name_space);
	token = 1 | (table->next_idx << 2); /* typeref */
	g_hash_table_insert (assembly->typeref, klass, GUINT_TO_POINTER(token));
	table->next_idx ++;
	return token;
}

static void
mono_image_get_type_info (MonoTypeBuilder *tb, MonoDynamicAssembly *assembly)
{
	MonoDynamicTable *table;
	guint *values;

	table = &assembly->tables [MONO_TABLE_TYPEDEF];
	tb->table_idx = table->next_idx ++;
	values = table->values + tb->table_idx * MONO_TYPEDEF_SIZE;
	values [MONO_TYPEDEF_FLAGS] = tb->attrs;
	/* use tb->base later */
	values [MONO_TYPEDEF_EXTENDS] = mono_image_typedef_or_ref (assembly, mono_defaults.object_class);
	values [MONO_TYPEDEF_NAME] = string_heap_insert (&assembly->sheap, tb->name);
	values [MONO_TYPEDEF_NAMESPACE] = string_heap_insert (&assembly->sheap, tb->nspace);
	values [MONO_TYPEDEF_FIELD_LIST] = assembly->tables [MONO_TABLE_FIELD].next_idx;
	values [MONO_TYPEDEF_METHOD_LIST] = assembly->tables [MONO_TABLE_METHOD].next_idx;

	table = &assembly->tables [MONO_TABLE_METHOD];
	table->rows += g_list_length (tb->methods);
	/*if (!tb->has_default_ctor)
		table->rows++;*/
	alloc_table (table, table->rows);
	g_list_foreach (tb->methods, mono_image_get_method_info, assembly);

	/* Do the same with fields, properties etc.. */
}

static void
mono_image_fill_module_table (MonoModuleBuilder *mb, MonoDynamicAssembly *assembly)
{
	MonoDynamicTable *table;

	table = &assembly->tables [MONO_TABLE_MODULE];
	mb->table_idx = table->next_idx ++;
	table->values [mb->table_idx * MONO_MODULE_SIZE + MONO_MODULE_NAME] = string_heap_insert (&assembly->sheap, mb->name);
	/* need to set mvid? */

	/*
	 * fill-in info in other tables as well.
	 */
	table = &assembly->tables [MONO_TABLE_TYPEDEF];
	table->rows += g_list_length (mb->types);
	alloc_table (table, table->rows);
	g_list_foreach (mb->types, mono_image_get_type_info, assembly);
}

static void
build_compressed_metadata (MonoDynamicAssembly *assembly)
{
	int i;
	guint64 valid_mask = 0;
	guint32 heapt_size = 0;
	guint32 meta_size = 256; /* allow for header and other stuff */
	guint32 table_offset;
	guint32 ntables = 0;
	guint64 *int64val;
	guint32 *int32val;
	guint16 *int16val;
	MonoImage *meta;
	unsigned char *p;
	char *version = "mono" VERSION;
	
	/* Compute table sizes */
	meta = assembly->assembly.image = g_new0 (MonoImage, 1);
	
	/* Setup the info used by compute_sizes () */
	meta->idx_blob_wide = assembly->blob.index >= 65536 ? 1 : 0;
	meta->idx_guid_wide = assembly->guid.index >= 65536 ? 1 : 0;
	meta->idx_string_wide = assembly->sheap.index >= 65536 ? 1 : 0;

	meta_size += assembly->blob.index;
	meta_size += assembly->guid.index;
	meta_size += assembly->sheap.index;
	meta_size += assembly->us.index;

	for (i=0; i < 64; ++i)
		meta->tables [i].rows = assembly->tables [i].rows;
	
	for (i = 0; i < 64; i++){
		if (meta->tables [i].rows == 0)
			continue;
		valid_mask |= (guint64)1 << i;
		ntables ++;
		meta->tables [i].row_size = mono_metadata_compute_size (
			meta, i, &meta->tables [i].size_bitfield);
		heapt_size += meta->tables [i].row_size * meta->tables [i].rows;
	}
	heapt_size += 24; /* #~ header size */
	heapt_size += ntables * 4;
	meta_size += heapt_size;
	meta->raw_metadata = g_malloc0 (meta_size);
	p = meta->raw_metadata;
	/* the metadata signature */
	*p++ = 'B'; *p++ = 'S'; *p++ = 'J'; *p++ = 'B';
	/* version numbers and 4 bytes reserved */
	int16val = (guint16*)p;
	*int16val++ = 1;
	*int16val = 1;
	p += 8;
	/* version string */
	int32val = (guint32*)p;
	*int32val = strlen (version);
	p += 4;
	memcpy (p, version, *int32val);
	p += *int32val;
	p += 3; p = (guint32)p & ~3; /* align */
	int16val = (guint16*)p;
	*int16val++ = 0; /* flags must be 0 */
	*int16val = 5; /* number of streams */
	p += 4;

	/*
	 * write the stream info.
	 */
	table_offset = (p - (unsigned char*)meta->raw_metadata) + 5 * 8 + 40; /* room needed for stream headers */
	
	int32val = (guint32*)p;
	*int32val++ = assembly->tstream.offset = table_offset;
	*int32val = heapt_size;
	table_offset += *int32val;
	p += 8;
	strcpy (p, "#~");
	/* 
	 * FIXME: alignment not 64 bit safe: same problem in metadata/image.c 
	 */
	p += 3 + 3; p = (guint32)p & ~3;

	int32val = (guint32*)p;
	*int32val++ = assembly->sheap.offset = table_offset;
	*int32val = assembly->sheap.index;
	table_offset += *int32val;
	p += 8;
	strcpy (p, "#Strings");
	p += 9 + 3; p = (guint32)p & ~3;

	int32val = (guint32*)p;
	*int32val++ = assembly->us.offset = table_offset;
	*int32val = assembly->us.index;
	table_offset += *int32val;
	p += 8;
	strcpy (p, "#US");
	p += 4 + 3; p = (guint32)p & ~3;

	int32val = (guint32*)p;
	*int32val++ = assembly->blob.offset = table_offset;
	*int32val = assembly->blob.index;
	table_offset += *int32val;
	p += 8;
	strcpy (p, "#Blob");
	p += 6 + 3; p = (guint32)p & ~3;

	int32val = (guint32*)p;
	*int32val++ = assembly->guid.offset = table_offset;
	*int32val = assembly->guid.index;
	table_offset += *int32val;
	p += 8;
	strcpy (p, "#GUID");
	p += 6 + 3; p = (guint32)p & ~3;

	/* 
	 * now copy the data, the table stream header and contents goes first.
	 */
	g_assert ((p - (unsigned char*)meta->raw_metadata) < assembly->tstream.offset);
	p = meta->raw_metadata + assembly->tstream.offset;
	int32val = (guint32*)p;
	*int32val = 0; /* reserved */
	p += 4;
	*p++ = 1; /* version */
	*p++ = 0;
	if (meta->idx_string_wide)
		*p |= 0x01;
	if (meta->idx_guid_wide)
		*p |= 0x02;
	if (meta->idx_blob_wide)
		*p |= 0x04;
	++p;
	*p++ = 0; /* reserved */
	int64val = (guint64*)p;
	*int64val++ = valid_mask;
	*int64val++ = 0; /* bitvector of sorted tables, set to 0 for now  */
	p += 16;
	int32val = (guint32*)p;
	for (i = 0; i < 64; i++){
		if (meta->tables [i].rows == 0)
			continue;
		*int32val++ = meta->tables [i].rows;
	}
	p = (unsigned char*)int32val;
	/* compress the tables */
	for (i = 0; i < 64; i++){
		int row, col;
		guint32 *values;
		guint32 bitfield = meta->tables [i].size_bitfield;
		if (!meta->tables [i].rows)
			continue;
		if (assembly->tables [i].columns != mono_metadata_table_count (bitfield))
			g_error ("col count mismatch in %d: %d %d", i, assembly->tables [i].columns, mono_metadata_table_count (bitfield));
		meta->tables [i].base = p;
		for (row = 1; row <= meta->tables [i].rows; ++row) {
			values = assembly->tables [i].values + row * assembly->tables [i].columns;
			for (col = 0; col < assembly->tables [i].columns; ++col) {
				switch (mono_metadata_table_size (bitfield, col)) {
				case 1:
					*p++ = values [col];
					break;
				case 2:
					int16val = (guint16*)p;
					*int16val = values [col];
					p += 2;
					break;
				case 4:
					int32val = (guint32*)p;
					*int32val = values [col];
					p += 4;
					break;
				default:
					g_assert_not_reached ();
				}
			}
		}
		g_assert ((p - (unsigned char*)meta->tables [i].base) == (meta->tables [i].rows * meta->tables [i].row_size));
	}
	
	g_assert (assembly->guid.offset + assembly->guid.index < meta_size);
	memcpy (meta->raw_metadata + assembly->sheap.offset, assembly->sheap.data, assembly->sheap.index);
	memcpy (meta->raw_metadata + assembly->us.offset, assembly->us.data, assembly->us.index);
	memcpy (meta->raw_metadata + assembly->blob.offset, assembly->blob.data, assembly->blob.index);
	memcpy (meta->raw_metadata + assembly->guid.offset, assembly->guid.data, assembly->guid.index);

	assembly->meta_size = assembly->guid.offset + assembly->guid.index;
}

static void
mono_image_build_metadata (MonoDynamicAssembly *assembly)
{
	char *meta;
	MonoDynamicTable *table;
	GList *type;
	guint32 len;
	guint32 *values;
	int i;
	
	/*
	 * FIXME: check if metadata was already built. 
	 */
	string_heap_init (&assembly->sheap);
	mono_image_add_stream_data (&assembly->us, "", 1);
	mono_image_add_stream_data (&assembly->blob, "", 1);

	assembly->text_rva =  0x00002000;

	for (i=0; i < 64; ++i) {
		assembly->tables [i].next_idx = 1;
		assembly->tables [i].columns = table_sizes [i];
	}
	
	table = &assembly->tables [MONO_TABLE_ASSEMBLY];
	alloc_table (table, 1);
	values = table->values + MONO_ASSEMBLY_SIZE;
	values [MONO_ASSEMBLY_HASH_ALG] = 0x8004;
	values [MONO_ASSEMBLY_NAME] = string_heap_insert (&assembly->sheap, assembly->name);
	values [MONO_ASSEMBLY_CULTURE] = string_heap_insert (&assembly->sheap, "");

	assembly->tables [MONO_TABLE_TYPEDEF].rows = 1; /* .<Module> */
	assembly->tables [MONO_TABLE_TYPEDEF].next_idx++;

	len = g_list_length (assembly->modules);
	table = &assembly->tables [MONO_TABLE_MODULE];
	alloc_table (table, len);
	g_list_foreach (assembly->modules, mono_image_fill_module_table, assembly);

	table = &assembly->tables [MONO_TABLE_TYPEDEF];
	/* 
	 * table->rows is already set above and in mono_image_fill_module_table.
	 */
	alloc_table (table, table->rows);
	/*
	 * Set the first entry.
	 */
	values = table->values + table->columns;
	values [MONO_TYPEDEF_FLAGS] = 0;
	values [MONO_TYPEDEF_NAME] = string_heap_insert (&assembly->sheap, "<Module>") ;
	values [MONO_TYPEDEF_NAMESPACE] = string_heap_insert (&assembly->sheap, "") ;
	values [MONO_TYPEDEF_EXTENDS] = 0;
	values [MONO_TYPEDEF_FIELD_LIST] = 1;
	values [MONO_TYPEDEF_METHOD_LIST] = 1;

	/* later include all the assemblies referenced */
	table = &assembly->tables [MONO_TABLE_ASSEMBLYREF];
	alloc_table (table, 1);
	values = table->values + table->columns;
	values [MONO_ASSEMBLYREF_NAME] = string_heap_insert (&assembly->sheap, "corlib");

	build_compressed_metadata (assembly);
}

int
mono_image_get_header (MonoDynamicAssembly *assembly, char *buffer, int maxsize)
{
	MonoMSDOSHeader *msdos;
	MonoDotNetHeader *header;
	MonoSectionTable *section;
	MonoCLIHeader *cli_header;
	guint32 header_size =  TEXT_OFFSET + CLI_H_SIZE;

	static const unsigned char msheader[] = {
		0x4d, 0x5a, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00,  0x04, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
		0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
		0x0e, 0x1f, 0xba, 0x0e, 0x00, 0xb4, 0x09, 0xcd,  0x21, 0xb8, 0x01, 0x4c, 0xcd, 0x21, 0x54, 0x68,
		0x69, 0x73, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72,  0x61, 0x6d, 0x20, 0x63, 0x61, 0x6e, 0x6e, 0x6f,
		0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6e,  0x20, 0x69, 0x6e, 0x20, 0x44, 0x4f, 0x53, 0x20,
		0x6d, 0x6f, 0x64, 0x65, 0x2e, 0x0d, 0x0d, 0x0a,  0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	if (maxsize < header_size)
		return -1;

	mono_image_build_metadata (assembly);

	memset (buffer, 0, header_size);
	memcpy (buffer, msheader, sizeof (MonoMSDOSHeader));

	msdos = (MonoMSDOSHeader *)buffer;
	header = (MonoDotNetHeader *)(buffer + sizeof (MonoMSDOSHeader));
	section = (MonoSectionTable*) (buffer + sizeof (MonoMSDOSHeader) + sizeof (MonoDotNetHeader));

	/* FIXME: ENDIAN problem: byteswap as needed */
	msdos->pe_offset = sizeof (MonoMSDOSHeader);

	header->pesig [0] = 'P';
	header->pesig [1] = 'E';
	header->pesig [2] = header->pesig [3] = 0;

	header->coff.coff_machine = 0x14c;
	header->coff.coff_sections = 1; /* only .text supported now */
	header->coff.coff_time = time (NULL);
	header->coff.coff_opt_header_size = sizeof (MonoDotNetHeader) - sizeof (MonoCOFFHeader) - 4;
	/* it's an exe */
	header->coff.coff_attributes = 0x010e;
	/* it's a dll */
	//header->coff.coff_attributes = 0x210e;
	header->pe.pe_magic = 0x10B;
	header->pe.pe_major = 6;
	header->pe.pe_minor = 0;
	/* need to set: pe_code_size pe_data_size pe_rva_entry_point pe_rva_code_base pe_rva_data_base */

	header->nt.pe_image_base = 0x400000;
	header->nt.pe_section_align = 8192;
	header->nt.pe_file_alignment = FILE_ALIGN;
	header->nt.pe_os_major = 4;
	header->nt.pe_os_minor = 0;
	header->nt.pe_subsys_major = 4;
	/* need to set pe_image_size, pe_header_size */
	header->nt.pe_subsys_required = 3; /* 3 -> cmdline app, 2 -> GUI app */
	header->nt.pe_stack_reserve = 0x00100000;
	header->nt.pe_stack_commit = 0x00001000;
	header->nt.pe_heap_reserve = 0x00100000;
	header->nt.pe_heap_commit = 0x00001000;
	header->nt.pe_loader_flags = 1;
	header->nt.pe_data_dir_count = 16;

#if 0
	/* set: */
	header->datadir.pe_import_table
	pe_resource_table
	pe_reloc_table
	pe_iat	
#endif
	header->datadir.pe_cli_header.size = CLI_H_SIZE;
	header->datadir.pe_cli_header.rva = assembly->text_rva; /* we put it always at the beginning */

	/* Write section tables */
	strcpy (section->st_name, ".text");
	section->st_virtual_size = 1024; /* FIXME */
	section->st_virtual_address = assembly->text_rva;
	section->st_raw_data_size = 1024; /* FIXME */
	section->st_raw_data_ptr = TEXT_OFFSET;
	section->st_flags = SECT_FLAGS_HAS_CODE | SECT_FLAGS_MEM_EXECUTE | SECT_FLAGS_MEM_READ;

	/* 
	 * align: build_compressed_metadata () assumes metadata is aligned 
	 * see below:
	 * cli_header->ch_metadata.rva = assembly->text_rva + assembly->code.index + CLI_H_SIZE;
	 */
	assembly->code.index += 3;
	assembly->code.index &= ~3;

	/*
	 * Write the MonoCLIHeader header 
	 */
	cli_header = (MonoCLIHeader*)(buffer + TEXT_OFFSET);
	cli_header->ch_size = CLI_H_SIZE;
	cli_header->ch_runtime_major = 2;
	cli_header->ch_flags = CLI_FLAGS_ILONLY;
	if (assembly->entry_point) 
		cli_header->ch_entry_point = assembly->entry_point->table_idx | MONO_TOKEN_METHOD_DEF;
	else
		cli_header->ch_entry_point = 0;
	cli_header->ch_metadata.rva = assembly->text_rva + assembly->code.index + CLI_H_SIZE;
	cli_header->ch_metadata.size = assembly->meta_size;
	
	return header_size;
}

