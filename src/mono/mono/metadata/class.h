#ifndef _MONO_CLI_CLASS_H_
#define _MONO_CLI_CLASS_H_

#include <mono/metadata/metadata.h>
#include <mono/metadata/image.h>
#include <mono/metadata/loader.h>

#define MONO_CLASS_IS_ARRAY(c) (c->type_token == 0)

typedef struct {
	MonoFieldType *type;
	int            offset;
	guint32        flags;
} MonoClassField;

typedef struct _MonoClass MonoClass;

struct _MonoClass {
	MonoImage *image;
	guint32    type_token;

	guint inited : 1;
	guint valuetype : 1; /* derives from System.ValueType */

	MonoClass *parent;
	
	/*
	 * Computed object instance size, total.
	 */
	int        instance_size;
	int        class_size;

	/*
	 * From the TypeDef table
	 */
	guint32    flags;
	struct {
		guint32 first, last;
		int count;
	} field, method;

	/*
	 * Field information: Type and location from object base
	 */
	MonoClassField *fields;

	MonoMethod **methods;

	/*
	 * After the methods, there is room for the static fields...
	 */
};

typedef struct {
	MonoClass  class;
	MonoClass *element_class; /* element class */
	guint32    rank;          /* array dimension */
} MonoArrayClass;

MonoClass *
mono_class_get          (MonoImage *image, guint32 type_token);

MonoClass *
mono_array_class_get    (MonoImage *image, guint32 etype, guint32 rank);

MonoClassField *
mono_class_get_field    (MonoClass *class, guint32 field_token);

gint32
mono_array_element_size (MonoArrayClass *ac);

#endif /* _MONO_CLI_CLASS_H_ */
