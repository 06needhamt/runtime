#ifndef __METADATA_REFLECTION_H__
#define __METADATA_REFLECTION_H__

#include <mono/metadata/image.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/class.h>
#include <mono/metadata/object.h>
#include <mono/utils/mono-hash.h>

typedef struct {
	GHashTable *hash;
	char *data;
	guint32 alloc_size; /* malloced bytes */
	guint32 index;
	guint32 offset; /* from start of metadata */
} MonoDynamicStream;

typedef struct {
	guint32 rows;
	guint32 row_size; /*  calculated later with column_sizes */
	guint32 columns;
	guint32 column_sizes [9]; 
	guint32 *values; /* rows * columns */
	guint32 next_idx;
} MonoDynamicTable;

/*
 * The followinbg structure must match the C# implementation in our corlib.
 */

struct _MonoReflectionMethod {
	MonoObject object;
	MonoMethod *method;
	MonoString *name;
};

typedef struct {
	MonoObject object;
	MonoObject *target_type;
	MonoObject *target;
	MonoString *method_name;
	gpointer method_ptr;
	gpointer delegate_trampoline;
	MonoReflectionMethod *method_info;
} MonoDelegate;

typedef struct _MonoMulticastDelegate MonoMulticastDelegate;
struct _MonoMulticastDelegate {
	MonoDelegate delegate;
	MonoMulticastDelegate *prev;
};

typedef struct {
	MonoObject object;
	MonoClass *klass;
	MonoClassField *field;
} MonoReflectionField;

typedef struct {
	MonoObject object;
	MonoClass *klass;
	MonoProperty *property;
} MonoReflectionProperty;

typedef struct {
	MonoObject object;
	MonoClass *klass;
	MonoEvent *event;
} MonoReflectionEvent;

typedef struct {
	MonoObject object;
	MonoReflectionType *ClassImpl;
	MonoObject *DefaultValueImpl;
	MonoObject *MemberImpl;
	MonoString *NameImpl;
	gint32 PositionImpl;
	guint32 AttrsImpl;
} MonoReflectionParameter;

typedef struct {
	MonoObject object;
	MonoAssembly *assembly;
} MonoReflectionAssembly;

typedef struct {
	MonoReflectionType *utype;
	MonoArray *values;
	MonoArray *names;
} MonoEnumInfo;

typedef struct {
	MonoReflectionType *parent;
	MonoReflectionType *ret;
	guint32 attrs;
	guint32 implattrs;
} MonoMethodInfo;

typedef struct {
	MonoReflectionType *parent;
	MonoString *name;
	MonoReflectionMethod *get;
	MonoReflectionMethod *set;
	guint32 attrs;
} MonoPropertyInfo;

typedef struct {
	MonoReflectionType *parent;
	MonoString *name;
	MonoReflectionMethod *add_method;
	MonoReflectionMethod *remove_method;
	MonoReflectionMethod *raise_method;
	guint32 attrs;
} MonoEventInfo;

typedef struct {
	MonoReflectionType *parent;
	MonoReflectionType *type;
	MonoString *name;
	guint32 attrs;
} MonoFieldInfo;

typedef struct {
	MonoString *name;
	MonoString *name_space;
	MonoReflectionType *parent;
	MonoReflectionType *etype;
	MonoReflectionAssembly *assembly;
	guint32 attrs;
	guint32 rank;
	MonoBoolean isbyref, ispointer, isprimitive;
} MonoTypeInfo;

typedef struct {
	MonoObject object;
	guint32 count;
	guint32 capacity;
	MonoArray *data_array;
} MonoArrayList;

typedef struct {
	MonoObject *member;
	gint32 code_pos;
} MonoReflectionILTokenInfo;

typedef struct {
	MonoObject object;
	MonoArray *code;
	MonoObject *mbuilder;
	gint32 code_len;
	gint32 max_stack;
	gint32 cur_stack;
	MonoArray *locals;
	MonoArray *ex_handlers;
	gint32 num_token_fixups;
	MonoArray *token_fixups;
} MonoReflectionILGen;

typedef struct {
	MonoArray *handlers;
	gint32 start;
	gint32 len;
	gint32 label;
} MonoILExceptionInfo;

typedef struct {
	MonoReflectionType *extype;
	gint32 type;
	gint32 start;
	gint32 len;
	gint32 filter_offset;
} MonoILExceptionBlock;

typedef struct {
	MonoObject object;
	MonoReflectionType *type;
	MonoString *name;
} MonoReflectionLocalBuilder;

typedef struct {
	MonoObject object;
	MonoObject* methodb;
	MonoString *name;
	MonoArray *cattrs;
	guint32 attrs;
	int position;
	guint32 table_idx;
} MonoReflectionParamBuilder;

typedef struct {
	MonoObject object;
	MonoReflectionILGen *ilgen;
	MonoArray *parameters;
	guint32 attrs;
	guint32 iattrs;
	guint32 table_idx;
	guint32 call_conv;
	MonoObject *type;
	MonoArray *pinfo;
	MonoArray *cattrs;
	MonoBoolean init_locals;
} MonoReflectionCtorBuilder;

typedef struct {
	MonoObject object;
	MonoMethod *mhandle;
	MonoReflectionType *rtype;
	MonoArray *parameters;
	guint32 attrs;
	guint32 iattrs;
	MonoString *name;
	guint32 table_idx;
	MonoArray *code;
	MonoReflectionILGen *ilgen;
	MonoObject *type;
	MonoArray *pinfo;
	MonoArray *cattrs;
	MonoReflectionMethod *override_method;
	MonoString *dll;
	MonoString *dllentry;
	guint32 charset;
	guint32 native_cc;
	guint32 call_conv;
	MonoBoolean init_locals;
} MonoReflectionMethodBuilder;

typedef struct {
	MonoAssembly assembly;
	guint32 meta_size;
	guint32 text_rva;
	guint32 metadata_rva;
	GHashTable *typeref;
	GHashTable *handleref;
	MonoGHashTable *token_fixups;
	MonoDynamicStream sheap;
	MonoDynamicStream code; /* used to store method headers and bytecode */
	MonoDynamicStream us;
	MonoDynamicStream blob;
	MonoDynamicStream tstream;
	MonoDynamicStream guid;
	MonoDynamicTable tables [64];
} MonoDynamicAssembly;

typedef struct {
	MonoReflectionAssembly assembly;
	MonoDynamicAssembly *dynamic_assembly;
	MonoReflectionMethodBuilder *entry_point;
	MonoArray *modules;
	MonoString *name;
	MonoString *dir;
	MonoArray *cattrs;
	MonoArray *table_indexes;
	MonoArrayList *methods;
} MonoReflectionAssemblyBuilder;

typedef struct {
	MonoObject object;
	guint32 attrs;
	MonoReflectionType *type;
	MonoString *name;
	MonoObject *def_value;
	gint32 offset;
	gint32 table_idx;
	MonoReflectionType *typeb;
	MonoArray *rva_data;
	MonoArray *cattrs;
} MonoReflectionFieldBuilder;

typedef struct {
	MonoObject object;
	guint32 attrs;
	MonoString *name;
	MonoReflectionType *type;
	MonoArray *parameters;
	MonoArray *cattrs;
	MonoObject *def_value;
	MonoReflectionMethodBuilder *set_method;
	MonoReflectionMethodBuilder *get_method;
	gint32 table_idx;
} MonoReflectionPropertyBuilder;

typedef struct {
	MonoObject	obj;
	MonoImage  *image;
	MonoObject *assembly;
	MonoString *fqname;
	MonoString *name;
	MonoString *scopename;
} MonoReflectionModule;

typedef struct {
	MonoReflectionModule module;
	MonoArray *types;
	MonoArray *cattrs;
	MonoArray *guid;
	guint32    table_idx;
	MonoReflectionAssemblyBuilder *assemblyb;
} MonoReflectionModuleBuilder;

typedef struct {
	MonoReflectionType type;
	MonoString *name;
	MonoString *nspace;
	MonoReflectionType *parent;
	MonoArray *interfaces;
	MonoArray *methods;
	MonoArray *ctors;
	MonoArray *properties;
	MonoArray *fields;
	MonoArray *events;
	MonoArray *cattrs;
	MonoArray *subtypes;
	guint32 attrs;
	guint32 table_idx;
	MonoReflectionModuleBuilder *module;
	gint32 class_size;
	gint32 packing_size;
} MonoReflectionTypeBuilder;

typedef struct {
	MonoObject  obj;
	MonoString *name;
	MonoString *codebase;
	gint32 major, minor, build, revision;
	/* FIXME: add missing stuff */
} MonoReflectionAssemblyName;

typedef struct {
	MonoObject  obj;
	MonoString *name;
	MonoReflectionType *type;
	MonoReflectionTypeBuilder *typeb;
	MonoArray *cattrs;
	MonoReflectionMethodBuilder *add_method;
	MonoReflectionMethodBuilder *remove_method;
	MonoReflectionMethodBuilder *raise_method;
	MonoArray *other_methods;
	guint32 attrs;
	guint32 table_idx;
} MonoReflectionEventBuilder;

typedef struct {
	MonoObject  obj;
	MonoReflectionMethod *ctor;
	MonoArray *data;
} MonoReflectionCustomAttr;

typedef struct {
	char *nest_name_space;
	char *nest_name;
	char *name_space;
	char *name;
	char *assembly;
	GList *modifiers; /* 0 -> byref, -1 -> pointer, > 0 -> array rank */
} MonoTypeNameParse;

typedef struct {
	MonoObject object;
	MonoReflectionModuleBuilder *module;
	MonoArray *arguments;
	guint32 type;
} MonoReflectionSigHelper;



char*         mono_type_get_name         (MonoType *type);
int           mono_reflection_parse_type (char *name, MonoTypeNameParse *info);
MonoType*     mono_reflection_get_type   (MonoImage* image, MonoTypeNameParse *info, gboolean ignorecase);

int           mono_image_get_header (MonoReflectionAssemblyBuilder *assembly, char *buffer, int maxsize);
void          mono_image_basic_init (MonoReflectionAssemblyBuilder *assembly);
guint32       mono_image_insert_string (MonoReflectionAssemblyBuilder *assembly, MonoString *str);
guint32       mono_image_create_token  (MonoDynamicAssembly *assembly, MonoObject *obj);

MonoReflectionAssembly* mono_assembly_get_object (MonoDomain *domain, MonoAssembly *assembly);
MonoReflectionType*     mono_type_get_object     (MonoDomain *domain, MonoType *type);
MonoReflectionMethod*   mono_method_get_object   (MonoDomain *domain, MonoMethod *method);
MonoReflectionField*    mono_field_get_object    (MonoDomain *domain, MonoClass *klass, MonoClassField *field);
MonoReflectionProperty* mono_property_get_object (MonoDomain *domain, MonoClass *klass, MonoProperty *property);
MonoReflectionEvent*    mono_event_get_object    (MonoDomain *domain, MonoClass *klass, MonoEvent *event);
/* note: this one is slightly different: we keep the whole array of params in the cache */
MonoReflectionParameter** mono_param_get_objects  (MonoDomain *domain, MonoMethod *method);

MonoArray*  mono_reflection_get_custom_attrs (MonoObject *obj);
MonoArray*  mono_reflection_get_custom_attrs_blob (MonoObject *ctor, MonoArray *ctorArgs, MonoArray *properties, MonoArray *porpValues, MonoArray *fields, MonoArray* fieldValues);

void        mono_reflection_setup_internal_class  (MonoReflectionTypeBuilder *tb);

void        mono_reflection_create_internal_class (MonoReflectionTypeBuilder *tb);

MonoArray  *mono_reflection_sighelper_get_signature_local (MonoReflectionSigHelper *sig);

MonoArray  *mono_reflection_sighelper_get_signature_field (MonoReflectionSigHelper *sig);

#endif /* __METADATA_REFLECTION_H__ */

