/*
 * class.c: Class management for the Mono runtime
 *
 * Author:
 *   Miguel de Icaza (miguel@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 *
 * Possible Optimizations:
 *     in mono_class_create, do not allocate the class right away,
 *     but wait until you know the size of the FieldMap, so that
 *     the class embeds directly the FieldMap after the vtable.
 *
 * 
 */
#include <config.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <mono/metadata/image.h>
#include <mono/metadata/cil-coff.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/tokentype.h>
#include <mono/metadata/class.h>
#include <mono/metadata/object.h>
#include <mono/metadata/appdomain.h>

#define CSIZE(x) (sizeof (x) / 4)

static MonoClass * mono_class_create_from_typedef (MonoImage *image, guint32 type_token);

static gpointer
default_trampoline (MonoMethod *method)
{
	return method;
}

static void
default_runtime_class_init (MonoClass *klass)
{
	return;
}

static MonoTrampoline arch_create_jit_trampoline = default_trampoline;
static MonoRuntimeClassInit mono_runtime_class_init = default_runtime_class_init;

void
mono_install_trampoline (MonoTrampoline func) 
{
	arch_create_jit_trampoline = func? func: default_trampoline;
}

void
mono_install_runtime_class_init (MonoRuntimeClassInit func)
{
	mono_runtime_class_init = func? func: default_runtime_class_init;
}

static MonoClass *
mono_class_create_from_typeref (MonoImage *image, guint32 type_token)
{
	guint32 cols [MONO_TYPEREF_SIZE];
	MonoTableInfo  *t = &image->tables [MONO_TABLE_TYPEREF];
	guint32 idx;
	const char *name, *nspace;
	MonoClass *res;

	mono_metadata_decode_row (t, (type_token&0xffffff)-1, cols, MONO_TYPEREF_SIZE);
	g_assert ((cols [MONO_TYPEREF_SCOPE] & 0x3) == 2);
	idx = cols [MONO_TYPEREF_SCOPE] >> 2;

	if (!image->references ||  !image->references [idx-1]) {
		/* 
		 * detected a reference to mscorlib, we simply return a reference to a dummy 
		 * until we have a better solution.
		 */
		fprintf(stderr, "Sending dummy where %s.%s expected\n", mono_metadata_string_heap (image, cols [MONO_TYPEREF_NAMESPACE]), mono_metadata_string_heap (image, cols [MONO_TYPEREF_NAME])); 
		
		res = mono_class_from_name (image, "System", "MonoDummy");
		/* prevent method loading */
		res->dummy = 1;
		/* some storage if the type is used  - very ugly hack */
		res->instance_size = 2*sizeof (gpointer);
		return res;
	}	

	name = mono_metadata_string_heap (image, cols [MONO_TYPEREF_NAME]);
	nspace = mono_metadata_string_heap (image, cols [MONO_TYPEREF_NAMESPACE]);
	
	/* load referenced assembly */
	image = image->references [idx-1]->image;

	return mono_class_from_name (image, nspace, name);
}

/** 
 * class_compute_field_layout:
 * @m: pointer to the metadata.
 * @class: The class to initialize
 *
 * Initializes the class->fields.
 *
 * Currently we only support AUTO_LAYOUT, and do not even try to do
 * a good job at it.  This is temporary to get the code for Paolo.
 */
static void
class_compute_field_layout (MonoClass *class)
{
	MonoImage *m = class->image; 
	const int top = class->field.count;
	guint32 layout = class->flags & TYPE_ATTRIBUTE_LAYOUT_MASK;
	MonoTableInfo *t = &m->tables [MONO_TABLE_FIELD];
	int i;

	/*
	 * Fetch all the field information.
	 */
	for (i = 0; i < top; i++){
		const char *sig;
		guint32 cols [MONO_FIELD_SIZE];
		int idx = class->field.first + i;
		
		mono_metadata_decode_row (t, idx, cols, CSIZE (cols));
		/* The name is needed for fieldrefs */
		class->fields [i].name = mono_metadata_string_heap (m, cols [MONO_FIELD_NAME]);
		sig = mono_metadata_blob_heap (m, cols [MONO_FIELD_SIGNATURE]);
		mono_metadata_decode_value (sig, &sig);
		/* FIELD signature == 0x06 */
		g_assert (*sig == 0x06);
		class->fields [i].type = mono_metadata_parse_field_type (
			m, cols [MONO_FIELD_FLAGS], sig + 1, &sig);
		if (cols [MONO_FIELD_FLAGS] & FIELD_ATTRIBUTE_HAS_FIELD_RVA) {
			mono_metadata_field_info (m, idx, NULL, &class->fields [i].data, NULL);
			if (!class->fields [i].data)
				g_warning ("field %s in %s should have RVA data, but hasn't", class->fields [i].name, class->name);
		}
		if (class->enumtype && !(cols [MONO_FIELD_FLAGS] & FIELD_ATTRIBUTE_STATIC)) {
			class->enum_basetype = class->fields [i].type;
			class->element_class = mono_class_from_mono_type (class->enum_basetype);
		}
	}
	if (class->enumtype && !class->enum_basetype) {
		if (!((strcmp (class->name, "Enum") == 0) && (strcmp (class->name_space, "System") == 0)))
			G_BREAKPOINT ();
	}
	/*
	 * Compute field layout and total size.
	 */
	switch (layout){
	case TYPE_ATTRIBUTE_AUTO_LAYOUT:
	case TYPE_ATTRIBUTE_SEQUENTIAL_LAYOUT:
		for (i = 0; i < top; i++){
			int size, align;
			
			size = mono_type_size (class->fields [i].type, &align);
			if (class->fields [i].type->attrs & FIELD_ATTRIBUTE_STATIC) {
				class->fields [i].offset = class->class_size;
				class->class_size += (class->class_size % align);
				class->class_size += size;
			} else {
				class->min_align = MAX (align, class->min_align);
				class->fields [i].offset = class->instance_size;
				class->instance_size += (class->instance_size % align);
				class->instance_size += size;
			}
		}
		if (class->instance_size & (class->min_align - 1)) {
			class->instance_size += class->min_align - 1;
			class->instance_size &= ~(class->min_align - 1);
		}
		break;
	case TYPE_ATTRIBUTE_EXPLICIT_LAYOUT:
		for (i = 0; i < top; i++){
			int size, align;
			int idx = class->field.first + i;

			/*
			 * There must be info about all the fields in a type if it
			 * uses explicit layout.
			 */

			size = mono_type_size (class->fields [i].type, &align);
			if (class->fields [i].type->attrs & FIELD_ATTRIBUTE_STATIC) {
				class->fields [i].offset = class->class_size;
				class->class_size += (class->class_size % align);
				class->class_size += size;
			} else {
				mono_metadata_field_info (m, idx, &class->fields [i].offset, NULL, NULL);
				if (class->fields [i].offset == (guint32)-1)
						g_warning ("%s not initialized correctly (missing field layout info for %s)", class->name, class->fields [i].name);
				/*
				 * The offset is from the start of the object: this works for both
				 * classes and valuetypes.
				 */
				class->fields [i].offset += sizeof (MonoObject);
				/*
				 * Calc max size.
				 */
				size += class->fields [i].offset;
				class->instance_size = MAX (class->instance_size, size);
			}
		}
		break;
	}
}

static void
init_properties (MonoClass *class)
{
	guint startm, endm, i, j;
	guint32 cols [MONO_PROPERTY_SIZE];
	MonoTableInfo *pt = &class->image->tables [MONO_TABLE_PROPERTY];
	MonoTableInfo *msemt = &class->image->tables [MONO_TABLE_METHODSEMANTICS];

	class->property.first = mono_metadata_properties_from_typedef (class->image, mono_metadata_token_index (class->type_token) - 1, &class->property.last);
	class->property.count = class->property.last - class->property.first;

	class->properties = g_new0 (MonoProperty, class->property.count);
	for (i = class->property.first; i < class->property.last; ++i) {
		mono_metadata_decode_row (pt, i, cols, MONO_PROPERTY_SIZE);
		class->properties [i - class->property.first].attrs = cols [MONO_PROPERTY_FLAGS];
		class->properties [i - class->property.first].name = mono_metadata_string_heap (class->image, cols [MONO_PROPERTY_NAME]);

		startm = mono_metadata_methods_from_property (class->image, i, &endm);
		for (j = startm; j < endm; ++j) {
			mono_metadata_decode_row (msemt, j, cols, MONO_METHOD_SEMA_SIZE);
			switch (cols [MONO_METHOD_SEMA_SEMANTICS]) {
			case 1: /* set */
				class->properties [i - class->property.first].set = class->methods [cols [MONO_METHOD_SEMA_METHOD] - 1 - class->method.first];
				break;
			case 2: /* get */
				class->properties [i - class->property.first].get = class->methods [cols [MONO_METHOD_SEMA_METHOD] - 1 - class->method.first];
				break;
			default:
				break;
			}
		}
	}
}

static guint
mono_get_unique_iid (MonoClass *class)
{
	static GHashTable *iid_hash = NULL;
	static guint iid = 0;

	char *str;
	gpointer value;
	
	g_assert (class->flags & TYPE_ATTRIBUTE_INTERFACE);

	if (!iid_hash)
		iid_hash = g_hash_table_new (g_str_hash, g_str_equal);

	str = g_strdup_printf ("%s|%s.%s\n", class->image->name, class->name_space, class->name);

	if (g_hash_table_lookup_extended (iid_hash, str, NULL, &value)) {
		g_free (str);
		return (guint)value;
	} else {
		g_hash_table_insert (iid_hash, str, (gpointer)iid);
		++iid;
	}

	g_free (str);
	return iid - 1;
}

/**
 * mono_class_init:
 * @class: the class to initialize
 *
 * compute the instance_size, class_size and other infos that cannot be 
 * computed at mono_class_get() time. Also compute a generic vtable and 
 * the method slot numbers. We use this infos later to create a domain
 * specific vtable.  
 */
void
mono_class_init (MonoClass *class)
{
	MonoClass *k, *ic;
	MonoMethod **vtable = class->vtable;
	int i, max_iid, cur_slot = 0;

	g_assert (class);

	if (class->inited)
		return;
	class->inited = 1;

	if (class->parent) {
		if (!class->parent->inited)
			mono_class_init (class->parent);
		class->instance_size += class->parent->instance_size;
		class->class_size += class->parent->class_size;
		class->min_align = class->parent->min_align;
		cur_slot = class->parent->vtable_size;
	} else
		class->min_align = 1;

	/*
	 * Computes the size used by the fields, and their locations
	 */
	if (class->field.count > 0){
		class->fields = g_new0 (MonoClassField, class->field.count);
		class_compute_field_layout (class);
	}

	/* initialize method pointers */
	class->methods = g_new (MonoMethod*, class->method.count);
	for (i = 0; i < class->method.count; ++i)
		class->methods [i] = mono_get_method (class->image,
		        MONO_TOKEN_METHOD_DEF | (i + class->method.first + 1), class);

	if (class->flags & TYPE_ATTRIBUTE_INTERFACE) {
		for (i = 0; i < class->method.count; ++i)
			class->methods [i]->slot = i;
		return;
	}

	//printf ("METAINIT %s.%s\n", class->name_space, class->name);

	/* compute maximum number of slots and maximum interface id */
	max_iid = 0;
	for (k = class; k ; k = k->parent) {
		for (i = 0; i < k->interface_count; i++) {
			ic = k->interfaces [i];

			if (!ic->inited)
				mono_class_init (ic);

			if (max_iid < ic->interface_id)
				max_iid = ic->interface_id;
		}
	}
	
	class->max_interface_id = max_iid;
	/* compute vtable offset for interfaces */
	class->interface_offsets = g_malloc (sizeof (gpointer) * (max_iid + 1));

	for (i = 0; i <= max_iid; i++)
		class->interface_offsets [i] = -1;

	for (i = 0; i < class->interface_count; i++) {
		ic = class->interfaces [i];
		class->interface_offsets [ic->interface_id] = cur_slot;
		cur_slot += ic->method.count;
	}

	for (k = class->parent; k ; k = k->parent) {
		for (i = 0; i < k->interface_count; i++) {
			ic = k->interfaces [i]; 
			if (class->interface_offsets [ic->interface_id] == -1) {
				int io = k->interface_offsets [ic->interface_id];

				g_assert (io >= 0);
				g_assert (io <= class->vtable_size);

				class->interface_offsets [ic->interface_id] = io;
			}
		}
	}

	if (class->parent && class->parent->vtable_size)
		memcpy (class->vtable, class->parent->vtable,  sizeof (gpointer) * class->parent->vtable_size);
 
	for (k = class; k ; k = k->parent) {
		for (i = 0; i < k->interface_count; i++) {
			int j, l, io;

			ic = k->interfaces [i];
			io = k->interface_offsets [ic->interface_id];
			
			g_assert (io >= 0);
			g_assert (io <= class->vtable_size);

			if (k == class) {
				for (l = 0; l < ic->method.count; l++) {
					MonoMethod *im = ic->methods [l];						
					for (j = 0; j < class->method.count; ++j) {
						MonoMethod *cm = class->methods [j];
						if (!(cm->flags & METHOD_ATTRIBUTE_VIRTUAL) ||
						    !(cm->flags & METHOD_ATTRIBUTE_PUBLIC) ||
						    !(cm->flags & METHOD_ATTRIBUTE_NEW_SLOT))
							continue;
						if (!strcmp(cm->name, im->name) && 
						    mono_metadata_signature_equal (cm->signature, im->signature)) {
							g_assert (io + l <= class->vtable_size);
							vtable [io + l] = cm;
						}
					}
				}
			} else {
				/* already implemented */
				if (io >= k->vtable_size)
					continue;
			}
				
			for (l = 0; l < ic->method.count; l++) {
				MonoMethod *im = ic->methods [l];						
				MonoClass *k1;

				g_assert (io + l <= class->vtable_size);

				if (vtable [io + l])
					continue;
					
				for (k1 = class; k1; k1 = k1->parent) {
					for (j = 0; j < k1->method.count; ++j) {
						MonoMethod *cm = k1->methods [j];

						if (!(cm->flags & METHOD_ATTRIBUTE_VIRTUAL) ||
						    !(cm->flags & METHOD_ATTRIBUTE_PUBLIC))
							continue;
						
						if (!strcmp(cm->name, im->name) && 
						    mono_metadata_signature_equal (cm->signature, im->signature)) {
							g_assert (io + l <= class->vtable_size);
							vtable [io + l] = cm;
							break;
						}
						
					}
					g_assert (io + l <= class->vtable_size);
					if (vtable [io + l])
						break;
				}
			}

			for (l = 0; l < ic->method.count; l++) {
				MonoMethod *im = ic->methods [l];						
				char *qname;
				if (ic->name_space && ic->name_space [0])
					qname = g_strconcat (ic->name_space, ".", ic->name, ".", im->name, NULL);
				else
					qname = g_strconcat (ic->name, ".", im->name, NULL); 

				for (j = 0; j < class->method.count; ++j) {
					MonoMethod *cm = class->methods [j];

					if (!(cm->flags & METHOD_ATTRIBUTE_VIRTUAL))
						continue;
					
					if (!strcmp (cm->name, qname) &&
					    mono_metadata_signature_equal (cm->signature, im->signature)) {
						g_assert (io + l <= class->vtable_size);
						vtable [io + l] = cm;
						break;
					}
				}
				g_free (qname);
			}

			
			if (!(class->flags & TYPE_ATTRIBUTE_ABSTRACT)) {
				for (l = 0; l < ic->method.count; l++) {
					MonoMethod *im = ic->methods [l];						
					g_assert (io + l <= class->vtable_size);
					if (!(vtable [io + l])) {
						printf ("no implementation for interface method %s.%s::%s in class %s.%s\n",
							ic->name_space, ic->name, im->name, class->name_space, class->name);
						
						for (j = 0; j < class->method.count; ++j) {
							MonoMethod *cm = class->methods [j];
							
							printf ("METHOD %s\n", cm->name);
						}
						g_assert_not_reached ();
					}
				}
			}
		
			for (l = 0; l < ic->method.count; l++) {
				MonoMethod *im = vtable [io + l];

				if (im) {
					g_assert (io + l <= class->vtable_size);
					if (im->slot < 0) {
						// fixme: why do we need this ?
						im->slot = io + l;
						// g_assert_not_reached ();
					}
				}
			}
		}
	} 

	for (i = 0; i < class->method.count; ++i) {
		MonoMethod *cm;
	       
		cm = class->methods [i];

		if (!(cm->flags & METHOD_ATTRIBUTE_VIRTUAL) ||
		    (cm->slot >= 0))
			continue;
		
		if (!(cm->flags & METHOD_ATTRIBUTE_NEW_SLOT)) {
			for (k = class->parent; k ; k = k->parent) {
				int j;
				for (j = 0; j < k->method.count; ++j) {
					MonoMethod *m1 = k->methods [j];
					if (!(m1->flags & METHOD_ATTRIBUTE_VIRTUAL))
						continue;
					if (!strcmp(cm->name, m1->name) && 
					    mono_metadata_signature_equal (cm->signature, m1->signature)) {
						cm->slot = k->methods [j]->slot;
						g_assert (cm->slot < class->vtable_size);
						break;
					}
				}
				if (cm->slot >= 0) 
					break;
			}
		}

		if (cm->slot < 0)
			cm->slot = cur_slot++;

		if (!(cm->flags & METHOD_ATTRIBUTE_ABSTRACT))
			vtable [cm->slot] = cm;
	}

	init_properties (class);

	i = mono_metadata_nesting_typedef (class->image, class->type_token);
	while (i) {
		MonoClass* nclass;
		guint32 cols [MONO_NESTED_CLASS_SIZE];
		mono_metadata_decode_row (&class->image->tables [MONO_TABLE_NESTEDCLASS], i - 1, cols, MONO_NESTED_CLASS_SIZE);
		if (cols [MONO_NESTED_CLASS_ENCLOSING] != mono_metadata_token_index (class->type_token))
			break;
		nclass = mono_class_create_from_typedef (class->image, MONO_TOKEN_TYPE_DEF | cols [MONO_NESTED_CLASS_NESTED]);
		class->nested_classes = g_list_prepend (class->nested_classes, nclass);
		++i;
	}

	mono_runtime_class_init (class);

	/*
	printf ("VTABLE %s.%s\n", class->name_space, class->name); 

	for (i = 0; i < class->vtable_size; ++i) {
		MonoMethod *cm;
	       
		cm = vtable [i];
		if (cm) {
			printf ("  METH%d %p %s %d\n", i, cm, cm->name, cm->slot);
		}
	}
	
	printf ("METAEND %s.%s\n", class->name_space, class->name); 
	*/
}

/**
 * mono_class_vtable:
 * @domain: the application domain
 * @class: the class to initialize
 *
 * VTables are domain specific because we create domain specific code, and 
 * they contain the domain specific static class data.
 */
MonoVTable *
mono_class_vtable (MonoDomain *domain, MonoClass *class)
{
	MonoClass *k;
	MonoVTable *vt;
	int i;

	g_assert (class);

	if (class->flags & TYPE_ATTRIBUTE_INTERFACE)
		g_assert_not_reached ();

	if ((vt = g_hash_table_lookup (domain->class_vtable_hash, class)))
		return vt;
	
	if (!class->inited)
		mono_class_init (class);
		
	vt = g_malloc0 (sizeof (MonoVTable) + class->vtable_size * sizeof (gpointer));
	vt->klass = class;
	vt->domain = domain;

	if (class->class_size)
		vt->data = g_malloc0 (class->class_size);

	vt->interface_offsets = g_malloc0 (sizeof (gpointer) * (class->max_interface_id + 1));

	/* initialize interface offsets */
	for (k = class; k ; k = k->parent) {
		for (i = 0; i < k->interface_count; i++) {
			int slot;
			MonoClass *ic = k->interfaces [i];
			slot = class->interface_offsets [ic->interface_id];
			vt->interface_offsets [ic->interface_id] = &vt->vtable [slot];
		}
	}

	/* initialize vtable */
	for (i = 0; i < class->vtable_size; ++i) {
		MonoMethod *cm;
	       
		if ((cm = class->vtable [i]))
			vt->vtable [i] = arch_create_jit_trampoline (cm);
	}

	g_hash_table_insert (domain->class_vtable_hash, class, vt);

	return vt;
}

/*
 * Compute a relative numbering of the class hierarchy as described in
 * "Java for Large-Scale Scientific Computations?"
 */
static void
mono_compute_relative_numbering (MonoClass *class, int *c)
{
	GList *s;

	(*c)++;

	class->baseval = *c;

	for (s = class->subclasses; s; s = s->next)
		mono_compute_relative_numbering ((MonoClass *)s->data, c); 
	
	class->diffval = *c -  class->baseval;
}

/**
 * @image: context where the image is created
 * @type_token:  typedef token
 */
static MonoClass *
mono_class_create_from_typedef (MonoImage *image, guint32 type_token)
{
	MonoTableInfo *tt = &image->tables [MONO_TABLE_TYPEDEF];
	MonoClass *class, *parent = NULL;
	guint32 cols [MONO_TYPEDEF_SIZE];
	guint32 cols_next [MONO_TYPEDEF_SIZE];
	guint tidx = mono_metadata_token_index (type_token);
	const char *name, *nspace;
	guint vtsize = 0, icount = 0; 
	MonoClass **interfaces;
	int i;

	if ((class = g_hash_table_lookup (image->class_cache, GUINT_TO_POINTER (type_token))))
		return class;

	g_assert (mono_metadata_token_table (type_token) == MONO_TABLE_TYPEDEF);
	
	mono_metadata_decode_row (tt, tidx - 1, cols, CSIZE (cols));
	
	if (tt->rows > tidx) {		
		mono_metadata_decode_row (tt, tidx, cols_next, CSIZE (cols_next));
		vtsize += cols_next [MONO_TYPEDEF_METHOD_LIST] - cols [MONO_TYPEDEF_METHOD_LIST];
	} else {
		vtsize += image->tables [MONO_TABLE_METHOD].rows - cols [MONO_TYPEDEF_METHOD_LIST] + 1;
	}

	name = mono_metadata_string_heap (image, cols[1]);
	nspace = mono_metadata_string_heap (image, cols[2]);

	if (!(!strcmp (nspace, "System") && !strcmp (name, "Object")) &&
	    !(cols [0] & TYPE_ATTRIBUTE_INTERFACE)) {
		parent = mono_class_get (image, mono_metadata_token_from_dor (cols [3]));
	}
	interfaces = mono_metadata_interfaces_from_typedef (image, type_token, &icount);

	for (i = 0; i < icount; i++) 
		vtsize += interfaces [i]->method.count;
	
	if (parent)
		vtsize += parent->vtable_size;

	if (cols [0] & TYPE_ATTRIBUTE_INTERFACE)
		vtsize = 0;

	class = g_malloc0 (sizeof (MonoClass) + vtsize * sizeof (gpointer));
	
	g_hash_table_insert (image->class_cache, GUINT_TO_POINTER (type_token), class);

	class->parent = parent;
	class->interfaces = interfaces;
	class->interface_count = icount;
	class->vtable_size = vtsize;

	class->this_arg.byref = 1;
	class->this_arg.data.klass = class;
	class->this_arg.type = MONO_TYPE_CLASS;
	class->byval_arg.data.klass = class;
	class->byval_arg.type = MONO_TYPE_CLASS;

	class->name = name;
	class->name_space = nspace;

	class->image = image;
	class->type_token = type_token;
	class->flags = cols [0];

	class->element_class = class;

	/*g_print ("Init class %s\n", name);*/

	/* if root of the hierarchy */
	if (!strcmp (nspace, "System") && !strcmp (name, "Object")) {
		class->parent = NULL;
		class->instance_size = sizeof (MonoObject);
	} else if (!(cols [0] & TYPE_ATTRIBUTE_INTERFACE)) {
		int rnum = 0;
		class->parent = mono_class_get (image,  mono_metadata_token_from_dor (cols [3]));
		if (class->parent->enumtype || ((strcmp (class->parent->name, "ValueType") == 0) && (strcmp (class->parent->name_space, "System") == 0)))
			class->valuetype = 1;
		class->enumtype = class->parent->enumtype;
		class->parent->subclasses = g_list_prepend (class->parent->subclasses, class);
		mono_compute_relative_numbering (mono_defaults.object_class, &rnum);
	}

	if (!strcmp (nspace, "System")) {
		if (!strcmp (name, "ValueType")) {
			/*
			 * do not set the valuetype bit for System.ValueType.
			 * class->valuetype = 1;
			 */
		} else if (!strcmp (name, "Enum")) {
			/*
			 * do not set the valuetype bit for System.Enum.
			 * class->valuetype = 1;
			 */
			class->valuetype = 0;
			class->enumtype = 1;
		} else if (!strcmp (name, "Object")) {
			class->this_arg.type = class->byval_arg.type = MONO_TYPE_OBJECT;
		} else if (!strcmp (name, "String")) {
			class->this_arg.type = class->byval_arg.type = MONO_TYPE_STRING;
		}
	}
	
	if (class->valuetype) {
		int t = MONO_TYPE_VALUETYPE;
		if (!strcmp (nspace, "System")) {
			switch (*name) {
			case 'B':
				if (!strcmp (name, "Boolean")) {
					t = MONO_TYPE_BOOLEAN;
				} else if (!strcmp(name, "Byte")) {
					t = MONO_TYPE_U1;
				}
				break;
			case 'C':
				if (!strcmp (name, "Char")) {
					t = MONO_TYPE_CHAR;
				}
				break;
			case 'D':
				if (!strcmp (name, "Double")) {
					t = MONO_TYPE_R8;
				}
				break;
			case 'I':
				if (!strcmp (name, "Int32")) {
					t = MONO_TYPE_I4;
				} else if (!strcmp(name, "Int16")) {
					t = MONO_TYPE_I2;
				} else if (!strcmp(name, "Int64")) {
					t = MONO_TYPE_I8;
				} else if (!strcmp(name, "IntPtr")) {
					t = MONO_TYPE_I;
				}
				break;
			case 'S':
				if (!strcmp (name, "Single")) {
					t = MONO_TYPE_R4;
				} else if (!strcmp(name, "SByte")) {
					t = MONO_TYPE_I1;
				}
				break;
			case 'U':
				if (!strcmp (name, "UInt32")) {
					t = MONO_TYPE_U4;
				} else if (!strcmp(name, "UInt16")) {
					t = MONO_TYPE_U2;
				} else if (!strcmp(name, "UInt64")) {
					t = MONO_TYPE_U8;
				} else if (!strcmp(name, "UIntPtr")) {
					t = MONO_TYPE_U;
				}
				break;
			case 'V':
				if (!strcmp (name, "Void")) {
					t = MONO_TYPE_VOID;
				}
				break;
			default:
				break;
			}
		}
		class->this_arg.type = class->byval_arg.type = t;
	}

	/*
	 * Compute the field and method lists
	 */
	class->field.first  = cols [MONO_TYPEDEF_FIELD_LIST] - 1;
	class->method.first = cols [MONO_TYPEDEF_METHOD_LIST] - 1;

	if (tt->rows > tidx){		
		mono_metadata_decode_row (tt, tidx, cols_next, CSIZE (cols_next));
		class->field.last  = cols_next [MONO_TYPEDEF_FIELD_LIST] - 1;
		class->method.last = cols_next [MONO_TYPEDEF_METHOD_LIST] - 1;
	} else {
		class->field.last  = image->tables [MONO_TABLE_FIELD].rows;
		class->method.last = image->tables [MONO_TABLE_METHOD].rows;
	}

	if (cols [MONO_TYPEDEF_FIELD_LIST] && 
	    cols [MONO_TYPEDEF_FIELD_LIST] <= image->tables [MONO_TABLE_FIELD].rows)
		class->field.count = class->field.last - class->field.first;
	else
		class->field.count = 0;

	if (cols [MONO_TYPEDEF_METHOD_LIST] <= image->tables [MONO_TABLE_METHOD].rows)
		class->method.count = class->method.last - class->method.first;
	else
		class->method.count = 0;

	/* reserve space to store vector pointer in arrays */
	if (!strcmp (nspace, "System") && !strcmp (name, "Array")) {
		class->instance_size += 2 * sizeof (gpointer);
		g_assert (class->field.count == 0);
	}

	if (class->flags & TYPE_ATTRIBUTE_INTERFACE)
		class->interface_id = mono_get_unique_iid (class);

	//class->interfaces = mono_metadata_interfaces_from_typedef (image, type_token, &class->interface_count);

	if (class->enumtype)
		mono_class_init (class);

	if ((type_token = mono_metadata_nested_in_typedef (image, type_token)))
		class->nested_in = mono_class_create_from_typedef (image, type_token);
	return class;
}

MonoClass *
mono_class_from_mono_type (MonoType *type)
{
	switch (type->type) {
	case MONO_TYPE_OBJECT:
		return mono_defaults.object_class;
	case MONO_TYPE_VOID:
		return mono_defaults.void_class;
	case MONO_TYPE_BOOLEAN:
		return mono_defaults.boolean_class;
	case MONO_TYPE_CHAR:
		return mono_defaults.char_class;
	case MONO_TYPE_I1:
		return mono_defaults.sbyte_class;
	case MONO_TYPE_U1:
		return mono_defaults.byte_class;
	case MONO_TYPE_I2:
		return mono_defaults.int16_class;
	case MONO_TYPE_U2:
		return mono_defaults.uint16_class;
	case MONO_TYPE_I4:
		return mono_defaults.int32_class;
	case MONO_TYPE_U4:
		return mono_defaults.uint32_class;
	case MONO_TYPE_I:
		return mono_defaults.int_class;
	case MONO_TYPE_U:
		return mono_defaults.uint_class;
	case MONO_TYPE_I8:
		return mono_defaults.int64_class;
	case MONO_TYPE_U8:
		return mono_defaults.uint64_class;
	case MONO_TYPE_R4:
		return mono_defaults.single_class;
	case MONO_TYPE_R8:
		return mono_defaults.double_class;
	case MONO_TYPE_STRING:
		return mono_defaults.string_class;
	case MONO_TYPE_ARRAY:
		return mono_defaults.array_class;
	case MONO_TYPE_PTR:
		/* FIXME: this is wrong. Need to handle PTR types */
		g_assert_not_reached ();
		return mono_class_from_mono_type (type->data.type);
	case MONO_TYPE_SZARRAY:
		return mono_array_class_get (mono_class_from_mono_type (type->data.type), 1);
	case MONO_TYPE_CLASS:
	case MONO_TYPE_VALUETYPE:
		return type->data.klass;
	default:
		g_warning ("implement me %02x\n", type->type);
		g_assert_not_reached ();
	}
	
	return NULL;
}

/**
 * @image: context where the image is created
 * @type_spec:  typespec token
 */
static MonoClass *
mono_class_create_from_typespec (MonoImage *image, guint32 type_spec)
{
	MonoType *type;
	MonoClass *class, *eclass;

	type = mono_type_create_from_typespec (image, type_spec);

	switch (type->type) {
	case MONO_TYPE_ARRAY:
		eclass = mono_class_from_mono_type (type->data.array->type);
		class = mono_array_class_get (eclass, type->data.array->rank);
		break;
	case MONO_TYPE_SZARRAY:
		eclass = mono_class_from_mono_type (type->data.type);
		class = mono_array_class_get (eclass, 1);
		break;
	case MONO_TYPE_PTR:
		class = mono_class_from_mono_type (type->data.type);
		break;
	default:
		g_warning ("implement me: %08x", type->type);
		g_assert_not_reached ();		
	}

	mono_metadata_free_type (type);
	
	return class;
}

/**
 * mono_array_class_get:
 * @eclass: element type class
 * @rank: the dimension of the array class
 *
 * Returns: a class object describing the array with element type @etype and 
 * dimension @rank. 
 */
MonoClass *
mono_array_class_get (MonoClass *eclass, guint32 rank)
{
	MonoImage *image;
	MonoClass *class;
	static MonoClass *parent = NULL;
	guint32 key;
	int rnum = 0;

	g_assert (rank <= 255);

	if (!parent)
		parent = mono_defaults.array_class;

	if (!parent->inited)
		mono_class_init (parent);

	image = eclass->image;

	g_assert (!eclass->type_token ||
		  mono_metadata_token_table (eclass->type_token) == MONO_TABLE_TYPEDEF);

	key = ((rank & 0xff) << 24) | (eclass->type_token & 0xffffff);
	if ((class = g_hash_table_lookup (image->array_cache, GUINT_TO_POINTER (key))))
		return class;
	
	class = g_malloc0 (sizeof (MonoClass) + parent->vtable_size * sizeof (gpointer));

	class->image = image;
	class->name_space = "System";
	class->name = "Array";
	class->type_token = 0;
	class->flags = TYPE_ATTRIBUTE_CLASS;
	class->parent = parent;
	class->instance_size = mono_class_instance_size (class->parent);
	class->class_size = 0;
	class->vtable_size = parent->vtable_size;
	class->parent->subclasses = g_list_prepend (class->parent->subclasses, class);
	mono_compute_relative_numbering (mono_defaults.object_class, &rnum);

	class->rank = rank;
	class->element_class = eclass;
	if (rank > 1) {
		class->byval_arg.type = MONO_TYPE_ARRAY;
		/* FIXME: complete.... */
	} else {
		class->byval_arg.type = MONO_TYPE_SZARRAY;
		class->byval_arg.data.type = &eclass->byval_arg;
	}
	class->this_arg = class->byval_arg;
	class->this_arg.byref = 1;
	
	g_hash_table_insert (image->array_cache, GUINT_TO_POINTER (key), class);
	return class;
}

/**
 * mono_class_instance_size:
 * @klass: a class 
 * 
 * Returns: the size of an object instance
 */
gint32
mono_class_instance_size (MonoClass *klass)
{
	
	if (!klass->inited)
		mono_class_init (klass);

	return klass->instance_size;
}

/**
 * mono_class_value_size:
 * @klass: a class 
 *
 * This function is used for value types, and return the
 * space and the alignment to store that kind of value object.
 *
 * Returns: the size of a value of kind @klass
 */
gint32
mono_class_value_size      (MonoClass *klass, guint32 *align)
{
	gint32 size;

	/* fixme: check disable, because we still have external revereces to
	 * mscorlib and Dummy Objects 
	 */
	/*g_assert (klass->valuetype);*/

	size = mono_class_instance_size (klass) - sizeof (MonoObject);

	if (align)
		*align = klass->min_align;

	return size;
}

/**
 * mono_class_data_size:
 * @klass: a class 
 * 
 * Returns: the size of the static class data
 */
gint32
mono_class_data_size (MonoClass *klass)
{
	
	if (!klass->inited)
		mono_class_init (klass);

	return klass->class_size;
}

/*
 * Auxiliary routine to mono_class_get_field
 *
 * Takes a field index instead of a field token.
 */
static MonoClassField *
mono_class_get_field_idx (MonoClass *class, int idx)
{
	if (class->field.count){
		if ((idx >= class->field.first) && (idx < class->field.last)){
			return &class->fields [idx - class->field.first];
		}
	}

	if (!class->parent)
		return NULL;
	
	return mono_class_get_field_idx (class->parent, idx);
}

/**
 * mono_class_get_field:
 * @class: the class to lookup the field.
 * @field_token: the field token
 *
 * Returns: A MonoClassField representing the type and offset of
 * the field, or a NULL value if the field does not belong to this
 * class.
 */
MonoClassField *
mono_class_get_field (MonoClass *class, guint32 field_token)
{
	int idx = mono_metadata_token_index (field_token);

	g_assert (mono_metadata_token_code (field_token) == MONO_TOKEN_FIELD_DEF);

	return mono_class_get_field_idx (class, idx - 1);
}

MonoClassField *
mono_class_get_field_from_name (MonoClass *klass, const char *name)
{
	int i;
	guint32 token;
	MonoTableInfo *t = &klass->image->tables [MONO_TABLE_FIELD];

	for (i = 0; i < klass->field.count; ++i) {
		token = mono_metadata_decode_row_col (t, klass->field.first + i, MONO_FIELD_NAME);
		if (strcmp (name, mono_metadata_string_heap (klass->image, token)) == 0)
			return &klass->fields [i];
	}
	return NULL;
}

/**
 * mono_class_get:
 * @image: the image where the class resides
 * @type_token: the token for the class
 * @at: an optional pointer to return the array element type
 *
 * Returns: the MonoClass that represents @type_token in @image
 */
MonoClass *
mono_class_get (MonoImage *image, guint32 type_token)
{
	MonoClass *class;

	switch (type_token & 0xff000000){
	case MONO_TOKEN_TYPE_DEF:
		class = mono_class_create_from_typedef (image, type_token);
		break;		
	case MONO_TOKEN_TYPE_REF:
		return mono_class_create_from_typeref (image, type_token);
	case MONO_TOKEN_TYPE_SPEC:
		class = mono_class_create_from_typespec (image, type_token);
		break;
	default:
		g_warning ("unknown token type %x", type_token & 0xff000000);
		g_assert_not_reached ();
	}

	return class;
}

MonoClass *
mono_class_from_name (MonoImage *image, const char* name_space, const char *name)
{
	GHashTable *nspace_table;
	guint32 token;

	nspace_table = g_hash_table_lookup (image->name_cache, name_space);
	if (!nspace_table)
		return 0;
	token = GPOINTER_TO_UINT (g_hash_table_lookup (nspace_table, name));
	
	if (!token) {
		/*g_warning ("token not found for %s.%s in image %s", name_space, name, image->name);*/
		return NULL;
	}

	token = MONO_TOKEN_TYPE_DEF | token;

	return mono_class_get (image, token);
}

/**
 * mono_array_element_size:
 * @ac: pointer to a #MonoArrayClass
 *
 * Returns: the size of single array element.
 */
gint32
mono_array_element_size (MonoClass *ac)
{
	if (ac->element_class->valuetype)
		return mono_class_instance_size (ac->element_class) - sizeof (MonoObject);
	else
		return sizeof (gpointer);
}

gpointer
mono_ldtoken (MonoImage *image, guint32 token, MonoClass **handle_class)
{
	switch (token & 0xff000000) {
	case MONO_TOKEN_TYPE_DEF:
	case MONO_TOKEN_TYPE_REF: {
		MonoClass *class;
		if (handle_class)
			*handle_class = mono_defaults.typehandle_class;
		class = mono_class_get (image, token);
		/* We return a MonoType* as handle */
		return &class->byval_arg;
	}
	case MONO_TOKEN_TYPE_SPEC: {
		MonoClass *class;
		if (handle_class)
			*handle_class = mono_defaults.typehandle_class;
		class = mono_class_create_from_typespec (image, token);
		return &class->byval_arg;
	}
	case MONO_TOKEN_FIELD_DEF: {
		MonoClass *class;
		guint32 type = mono_metadata_typedef_from_field (image, mono_metadata_token_index (token));
		class = mono_class_get (image, MONO_TOKEN_TYPE_DEF | type);
		mono_class_init (class);
		if (handle_class)
				*handle_class = mono_class_from_name (mono_defaults.corlib, "System", "RuntimeFieldHandle");
		return mono_class_get_field (class, token);
	}
	case MONO_TOKEN_METHOD_DEF:
	case MONO_TOKEN_MEMBER_REF:
	default:
		g_warning ("Unknown token 0x%08x in ldtoken", token);
		break;
	}
	return NULL;
}

