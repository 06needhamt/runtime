/*
 * object.c: Object creation for the Mono runtime
 *
 * Author:
 *   Miguel de Icaza (miguel@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 */
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <mono/metadata/loader.h>
#include <mono/metadata/object.h>

/**
 * mono_object_allocate:
 * @size: number of bytes to allocate
 *
 * This is a very simplistic routine until we have our GC-aware
 * memory allocator. 
 *
 * Returns: an allocated object of size @size, or NULL on failure.
 */
static void *
mono_object_allocate (size_t size)
{
	void *o = calloc (1, size);

	return o;
}

/**
 * mono_object_free:
 *
 * Frees the memory used by the object.  Debugging purposes
 * only, as we will have our GC system.
 */
void
mono_object_free (MonoObject *o)
{
	MonoClass *c = o->klass;
	
	memset (o, 0, c->instance_size);
	free (o);
}

/**
 * mono_new_object:
 * @klass: the class of the object that we want to create
 *
 * Returns: A newly created object whose definition is
 * looked up using @klass
 */
MonoObject *
mono_new_object (MonoClass *klass)
{
	MonoObject *o;

	if (!klass->metadata_inited)
		mono_class_metadata_init (klass);

	o = mono_object_allocate (klass->instance_size);
	o->klass = klass;

	mono_threads_synchronisation_init(&o->synchronisation);

	return o;
}

/**
 * mono_new_object_from_token:
 * @image: Context where the type_token is hosted
 * @token: a token of the type that we want to create
 *
 * Returns: A newly created object whose definition is
 * looked up using @token in the @image image
 */
MonoObject *
mono_new_object_from_token  (MonoImage *image, guint32 token)
{
	MonoClass *class;

	class = mono_class_get (image, token);

	return mono_new_object (class);
}


/**
 * mono_object_clone:
 * @obj: the object to clone
 *
 * Returns: A newly created object who is a shallow copy of @obj
 */
MonoObject *
mono_object_clone (MonoObject *obj)
{
	MonoObject *o;
	int size;
	
	size = obj->klass->instance_size;
	o = mono_object_allocate (size);
	
	memcpy (o, obj, size);

	return o;
}

/*
 * mono_new_szarray:
 * @image: image where the object is being referenced
 * @eclass: element class
 * @n: number of array elements
 *
 * This routine creates a new szarray with @n elements of type @token
 */
MonoObject *
mono_new_szarray (MonoClass *eclass, guint32 n)
{
	MonoClass *c;
	MonoObject *o;
	MonoArrayObject *ao;
	MonoArrayClass *ac;

	c = mono_array_class_get (eclass, 1);
	g_assert (c != NULL);

	o = mono_new_object (c);

	ao = (MonoArrayObject *)o;
	ac = (MonoArrayClass *)c;

	ao->bounds = g_malloc0 (sizeof (MonoArrayBounds));
	ao->bounds [0].length = n;
	ao->bounds [0].lower_bound = 0;

	ao->vector = g_malloc0 (n * mono_array_element_size (ac));

	return o;
}

/**
 * mono_new_utf16_string:
 * @text: a pointer to an utf16 string
 * @len: the length of the string
 *
 * Returns: A newly created string object which contains @text.
 */
MonoObject *
mono_new_utf16_string (const char *text, gint32 len)
{
	MonoObject *s;
	MonoArrayObject *ca;

	s = mono_new_object (mono_defaults.string_class);
	g_assert (s != NULL);

	ca = (MonoArrayObject *)mono_new_szarray (mono_defaults.string_class, len);
	g_assert (ca != NULL);
	
	((MonoStringObject *)s)->c_str = ca;
	((MonoStringObject *)s)->length = len;

	memcpy (ca->vector, text, len * 2);

	return s;
}

/**
 * mono_new_string:
 * @text: a pointer to an utf8 string
 *
 * Returns: A newly created string object which contains @text.
 */
MonoObject *
mono_new_string (const char *text)
{
	MonoObject *o;
	guint16 *ut;
	int i, l;

	/* fixme: use some kind of unicode library here */

	l = strlen (text);
	ut = g_malloc (l*2);

	for (i = 0; i < l; i++)
		ut [i] = text[i];
	
	o = mono_new_utf16_string ((char *)ut, l);

	g_free (ut);

	return o;
}

/**
 * mono_value_box:
 * @class: the class of the value
 * @value: a pointer to the unboxed data
 *
 * Returns: A newly created object which contains @value.
 */
MonoObject *
mono_value_box (MonoClass *class, gpointer value)
{
	MonoObject *res;
	int size;

	g_assert (class->valuetype);

	size = mono_class_instance_size (class);
	res = mono_object_allocate (size);
	res->klass = class;

	size = size - sizeof (MonoObject);

	memcpy ((char *)res + sizeof (MonoObject), value, size);

	return res;
}

/**
 * mono_object_isinst:
 * @obj: an object
 * @klass: a pointer to a class 
 *
 * Returns: #TRUE if @obj is derived from @klass
 */
gboolean
mono_object_isinst (MonoObject *obj, MonoClass *klass)
{
	MonoClass *oklass = obj->klass;

	while (oklass) {
		if (oklass == klass)
			return TRUE;
		oklass = oklass->parent;
	}
	return FALSE;
}

static GHashTable *ldstr_table = NULL;

static int
ldstr_hash (const char* str)
{
	guint len, h;
	const char *end;
	len = mono_metadata_decode_blob_size (str, &str);
	end = str + len;
	h = *str;
	/*
	 * FIXME: The distribution may not be so nice with lots of
	 * null chars in the string.
	 */
	for (str += 1; str < end; str++)
		h = (h << 5) - h + *str;
	return h;
}

static gboolean
ldstr_equal (const char *str1, const char *str2) {
	int len;
	if ((len=mono_metadata_decode_blob_size (str1, &str1)) !=
				mono_metadata_decode_blob_size (str2, &str2))
		return 0;
	return memcmp (str1, str2, len) == 0;
}

typedef struct {
	MonoObject *obj;
	MonoObject *found;
} InternCheck;

static void
check_interned (gpointer key, MonoObject *value, InternCheck *check)
{
	if (value == check->obj)
		check->found = value;
}

MonoObject*
mono_string_is_interned (MonoObject *o)
{
	InternCheck check;
	check.obj = o;
	check.found = NULL;
	/*
	 * Yes, this is slow. Our System.String implementation needs to be redone.
	 * And GLib needs foreach() methods that can be stopped halfway.
	 */
	g_hash_table_foreach (ldstr_table, (GHFunc)check_interned, &check);
	return check.found;
}

MonoObject*
mono_string_intern (MonoObject *o)
{
	MonoObject *res;
	MonoStringObject *str = (MonoStringObject*) o;
	char *ins = g_malloc (4 + str->length * 2);
	char *p;
	
	/* Encode the length */
	p = ins;
	mono_metadata_encode_value (str->length, p, &p);
	memcpy (p, str->c_str->vector, str->length * 2);
	
	if ((res = g_hash_table_lookup (ldstr_table, str))) {
		g_free (ins);
		return res;
	}
	g_hash_table_insert (ldstr_table, ins, str);
	return (MonoObject*)str;
}

MonoObject*
mono_ldstr (MonoImage *image, guint32 index)
{
	const char *str, *sig;
	MonoObject *o;
	guint len;
	
	if (!ldstr_table)
		ldstr_table = g_hash_table_new ((GHashFunc)ldstr_hash, (GCompareFunc)ldstr_equal);
	
	sig = str = mono_metadata_user_string (image, index);
	
	if ((o = g_hash_table_lookup (ldstr_table, str)))
		return o;
	
	len = mono_metadata_decode_blob_size (str, &str);
	o = mono_new_utf16_string (str, len >> 1);
	g_hash_table_insert (ldstr_table, sig, o);

	return o;
}

char *
mono_string_to_utf8 (MonoObject *o)
{
	MonoStringObject *s = (MonoStringObject *)o;
	char *as, *vector;
	int i;

	g_assert (o != NULL);

	if (!s->length)
		return g_strdup ("");

	vector = s->c_str->vector;

	g_assert (vector != NULL);

	as = g_malloc (s->length + 1);

	/* fixme: replace with a real unicode/ansi conversion */
	for (i = 0; i < s->length; i++) {
		as [i] = vector [i*2];
	}

	as [i] = '\0';

	return as;
}

