#ifndef _MONO_CLI_OBJECT_H_
#define _MONO_CLI_OBJECT_H_

#include <mono/metadata/class.h>
#include <mono/metadata/threads-types.h>

#define MONO_CHECK_ARG(arg, expr) 	G_STMT_START{			  \
     if (!(expr))							  \
       {								  \
              	MonoException *ex;                                        \
                char *msg = g_strdup_printf ("assertion `%s' failed",     \
		#expr);							  \
                if (arg) {} /* check if the name exists */                \
		ex = mono_get_exception_argument (#arg, msg);             \
		g_free (msg);                                             \
                mono_raise_exception (ex);                                \
       };				}G_STMT_END

#define MONO_CHECK_ARG_NULL(arg) 	 G_STMT_START{			  \
     if (arg == NULL)							  \
       {								  \
              	MonoException *ex;                                        \
		ex = mono_get_exception_argument_null (#arg);             \
                mono_raise_exception (ex);                                \
       };				}G_STMT_END

typedef guchar MonoBoolean;

typedef struct {
	MonoClass *klass;
	MonoThreadsSync *synchronisation;
} MonoObject;

typedef struct {
	guint32 length;
	guint32 lower_bound;
} MonoArrayBounds;

typedef struct {
	MonoObject obj;
	MonoArrayBounds *bounds;
	/* used by single dimensional arrays to speed up bound checking */
	guint32 max_length; 
	/* we use double to ensure proper alignment on platforms that need it */
	double vector [MONO_ZERO_LEN_ARRAY];
} MonoArray;

typedef struct {
	MonoObject obj;
	MonoArray *c_str;
	gint32 length;
} MonoString;

typedef struct {
	MonoObject object;
	MonoObject *inner_ex;
	MonoString *message;
	MonoString *help_link;
	MonoString *class_name;
	MonoString *stack_trace;
	gint32      hresult;
	MonoString *source;
} MonoException;

typedef struct {
	MonoException base;
} MonoSystemException;

typedef struct {
	MonoSystemException base;
	MonoString *param_name;
} MonoArgumentException;

typedef struct {
	MonoObject object;
	MonoObject *target_type;
	MonoObject *target;
	MonoString *method;
	gpointer    method_ptr;
} MonoDelegate;

typedef void   (*MonoRuntimeObjectInit) (MonoObject *o);
typedef gint32 (*MonoRuntimeExecMain)   (MonoMethod *method, MonoArray *args);

extern MonoRuntimeObjectInit mono_runtime_object_init;
extern MonoRuntimeExecMain mono_runtime_exec_main;

#define mono_array_length(array) ((array)->bounds->length)
#define mono_array_addr(array,type,index) ( ((char*)(array)->vector) + sizeof (type) * (index) )
#define mono_array_addr_with_size(array,size,index) ( ((char*)(array)->vector) + (size) * (index) )
#define mono_array_get(array,type,index) ( *(type*)mono_array_addr ((array), type, (index)) ) 
#define mono_array_set(array,type,index,value)	\
	do {	\
		type *__p = (type *) mono_array_addr ((array), type, (index));	\
		*__p = (value);	\
	} while (0)

#define mono_string_chars(s) ((gushort*)(s)->c_str->vector)

void *
mono_object_allocate        (size_t size);

MonoObject *
mono_object_new             (MonoClass *klass);

MonoObject *
mono_object_new_from_token  (MonoImage *image, guint32 token);

MonoArray*
mono_array_clone            (MonoArray *array);

MonoArray*
mono_array_new              (MonoClass *eclass, guint32 n);

MonoArray*
mono_array_new_full         (MonoClass *array_class, guint32 *lengths, guint32 *lower_bounds);

MonoString*
mono_string_new_utf16       (const guint16 *text, gint32 len);

MonoString*
mono_ldstr                  (MonoImage *image, guint32 index);

MonoString*
mono_string_is_interned     (MonoString *str);

MonoString*
mono_string_intern          (MonoString *str);

MonoString*
mono_string_new             (const char *text);

char *
mono_string_to_utf8         (MonoString *string_obj);

char *
mono_string_to_utf16        (MonoString *string_obj);

void       
mono_object_free            (MonoObject *o);

MonoObject *
mono_value_box              (MonoClass *klass, gpointer val);
		      
MonoObject *
mono_object_clone           (MonoObject *obj);

MonoObject *
mono_object_isinst          (MonoObject *obj, MonoClass *klass);

typedef void (*MonoExceptionFunc) (MonoException *ex);

void
mono_install_handler        (MonoExceptionFunc func);

void
mono_raise_exception        (MonoException *ex);

void
mono_install_runtime_object_init (MonoRuntimeObjectInit func);

void
mono_install_runtime_exec_main   (MonoRuntimeExecMain func);

#endif

