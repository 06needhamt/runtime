/*
 * assembly.c: Routines for loading assemblies.
 * 
 * Author:
 *   Miguel de Icaza (miguel@ximian.com)
 *
 * (C) 2001 Ximian, Inc.  http://www.ximian.com
 *
 * TODO:
 *   Implement big-endian versions of the reading routines.
 */
#include <config.h>
#include <stdio.h>
#include <glib.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "assembly.h"
#include "image.h"
#include "cil-coff.h"
#include "rawbuffer.h"

/* the default search path is just MONO_ASSEMBLIES */
static const char*
default_path [] = {
	MONO_ASSEMBLIES,
	NULL
};

static char **assemblies_path = NULL;
static int env_checked = 0;

static void
check_env (void) {
	const char *path;
	char **splitted;
	
	if (env_checked)
		return;
	env_checked = 1;

	path = getenv ("MONO_PATH");
	if (!path)
		return;
	splitted = g_strsplit (path, G_SEARCHPATH_SEPARATOR_S, 1000);
	if (assemblies_path)
		g_strfreev (assemblies_path);
	assemblies_path = splitted;
}

/*
 * keeps track of loaded assemblies
 */
static GList *loaded_assemblies = NULL;
static MonoAssembly *corlib;

static MonoAssembly*
search_loaded (MonoAssemblyName* aname)
{
	GList *tmp;
	MonoAssembly *ass;
	
	for (tmp = loaded_assemblies; tmp; tmp = tmp->next) {
		ass = tmp->data;
		/* we just compare the name, but later we'll do all the checks */
		/* g_print ("compare %s %s\n", aname->name, ass->aname.name); */
		if (strcmp (aname->name, ass->aname.name))
			continue;
		/* g_print ("success\n"); */
		return ass;
	}
	return NULL;
}

/**
 * g_concat_dir_and_file:
 * @dir:  directory name
 * @file: filename.
 *
 * returns a new allocated string that is the concatenation of dir and file,
 * takes care of the exact details for concatenating them.
 */
static char *
g_concat_dir_and_file (const char *dir, const char *file)
{
	g_return_val_if_fail (dir != NULL, NULL);
	g_return_val_if_fail (file != NULL, NULL);

        /*
	 * If the directory name doesn't have a / on the end, we need
	 * to add one so we get a proper path to the file
	 */
	if (dir [strlen(dir) - 1] != G_DIR_SEPARATOR)
		return g_strconcat (dir, G_DIR_SEPARATOR_S, file, NULL);
	else
		return g_strconcat (dir, file, NULL);
}

static MonoAssembly *
load_in_path (const char *basename, char** search_path, MonoImageOpenStatus *status)
{
	int i;
	char *fullpath;
	MonoAssembly *result;

	for (i = 0; search_path [i]; ++i) {
		fullpath = g_concat_dir_and_file (search_path [i], basename);
		result = mono_assembly_open (fullpath, status);
		g_free (fullpath);
		if (result)
			return result;
	}
	return NULL;
}

/**
 * mono_assembly_setrootdir:
 * @root_dir: The pathname of the root directory where we will locate assemblies
 *
 * This routine sets the internal default root directory for looking up
 * assemblies.  This is used by Windows installations to compute dynamically
 * the place where the Mono assemblies are located.
 *
 */
void
mono_assembly_setrootdir (const char *root_dir)
{
	/*
	 * Override the MONO_ASSEMBLIES directory configured at compile time.
	 */
	default_path [0] = g_strdup (root_dir);
}

/**
 * mono_assembly_open:
 * @filename: Opens the assembly pointed out by this name
 * @status: where a status code can be returned
 *
 * mono_assembly_open opens the PE-image pointed by @filename, and
 * loads any external assemblies referenced by it.
 *
 * NOTE: we could do lazy loading of the assemblies.  Or maybe not worth
 * it. 
 */
MonoAssembly *
mono_assembly_open (const char *filename, MonoImageOpenStatus *status)
{
	MonoAssembly *ass, *ass2;
	MonoImage *image;
	MonoTableInfo *t;
	guint32 cols [MONO_ASSEMBLY_SIZE];
	int i;
	char *base_dir;
	const char *hash;
	
	g_return_val_if_fail (filename != NULL, NULL);

	/* g_print ("file loading %s\n", filename); */
	image = mono_image_open (filename, status);

	if (!image){
		if (status)
			*status = MONO_IMAGE_ERROR_ERRNO;
		return NULL;
	}

	base_dir = g_path_get_dirname (filename);
	
	/*
	 * Create assembly struct, and enter it into the assembly cache
	 */
	ass = g_new0 (MonoAssembly, 1);
	ass->basedir = base_dir;
	ass->image = image;

	image->assembly = ass;

	t = &image->tables [MONO_TABLE_ASSEMBLY];
	mono_metadata_decode_row (t, 0, cols, MONO_ASSEMBLY_SIZE);
		
	ass->aname.hash_len = 0;
	ass->aname.hash_value = NULL;
	ass->aname.name = mono_metadata_string_heap (image, cols [MONO_ASSEMBLY_NAME]);
	ass->aname.culture = mono_metadata_string_heap (image, cols [MONO_ASSEMBLY_CULTURE]);
	ass->aname.flags = cols [MONO_ASSEMBLY_FLAGS];
	ass->aname.major = cols [MONO_ASSEMBLY_MAJOR_VERSION];
	ass->aname.minor = cols [MONO_ASSEMBLY_MINOR_VERSION];
	ass->aname.build = cols [MONO_ASSEMBLY_BUILD_NUMBER];
	ass->aname.revision = cols [MONO_ASSEMBLY_REV_NUMBER];

	/* avoid loading the same assembly twixe for now... */
	if ((ass2 = search_loaded (&ass->aname))) {
		g_free (ass);
		if (status)
			*status = MONO_IMAGE_OK;
		return ass2;
	}

	/* register right away to prevent loops */
	loaded_assemblies = g_list_prepend (loaded_assemblies, ass);

	t = &image->tables [MONO_TABLE_ASSEMBLYREF];

	image->references = g_new0 (MonoAssembly *, t->rows + 1);

	/*
	 * Load any assemblies this image references
	 */
	for (i = 0; i < t->rows; i++){
		MonoAssemblyName aname;

		mono_metadata_decode_row (t, i, cols, MONO_ASSEMBLYREF_SIZE);
		
		hash = mono_metadata_blob_heap (image, cols [MONO_ASSEMBLYREF_HASH_VALUE]);
		aname.hash_len = mono_metadata_decode_blob_size (hash, &hash);
		aname.hash_value = hash;
		aname.name = mono_metadata_string_heap (image, cols [MONO_ASSEMBLYREF_NAME]);
		aname.culture = mono_metadata_string_heap (image, cols [MONO_ASSEMBLYREF_CULTURE]);
		aname.flags = cols [MONO_ASSEMBLYREF_FLAGS];
		aname.major = cols [MONO_ASSEMBLYREF_MAJOR_VERSION];
		aname.minor = cols [MONO_ASSEMBLYREF_MINOR_VERSION];
		aname.build = cols [MONO_ASSEMBLYREF_BUILD_NUMBER];
		aname.revision = cols [MONO_ASSEMBLYREF_REV_NUMBER];

		image->references [i] = mono_assembly_load (&aname, base_dir, status);

		if (image->references [i] == NULL){
			int j;
			
			for (j = 0; j < i; j++)
				mono_assembly_close (image->references [j]);
			g_free (image->references);
			mono_image_close (image);

			g_warning ("Could not find assembly %s", aname.name);
			if (status)
				*status = MONO_IMAGE_MISSING_ASSEMBLYREF;
			g_free (ass);
			loaded_assemblies = g_list_remove (loaded_assemblies, ass);
			g_free (base_dir);
			return NULL;
		}
	}
	image->references [i] = NULL;

	t = &image->tables [MONO_TABLE_MODULEREF];
	ass->modules = g_new0 (MonoImage *, t->rows);
	for (i = 0; i < t->rows; i++){
		char *module_ref;
		const char *name;
		guint32 cols [MONO_MODULEREF_SIZE];

		mono_metadata_decode_row (t, i, cols, MONO_MODULEREF_SIZE);
		name = mono_metadata_string_heap (image, cols [MONO_MODULEREF_NAME]);
		module_ref = g_concat_dir_and_file (base_dir, name);
		ass->modules [i] = mono_image_open (module_ref, status);
		g_free (module_ref);
	}

	g_free (base_dir);
	return ass;
}

MonoAssembly*
mono_assembly_load (MonoAssemblyName *aname, const char *basedir, MonoImageOpenStatus *status)
{
	MonoAssembly *result;
	char *fullpath, *filename;

	check_env ();

	/* g_print ("loading %s\n", aname->name); */
	/* special case corlib */
	if ((strcmp (aname->name, "mscorlib") == 0) || (strcmp (aname->name, "corlib") == 0)) {
		if (corlib) {
			/* g_print ("corlib already loaded\n"); */
			return corlib;
		}
		/* g_print ("corlib load\n"); */
		if (assemblies_path) {
			corlib = load_in_path (CORLIB_NAME, assemblies_path, status);
			if (corlib)
				return corlib;
		}
		corlib = load_in_path (CORLIB_NAME, default_path, status);
		g_free (CORLIB_NAME);
		return corlib;
	}
	result = search_loaded (aname);
	if (result)
		return result;
	/* g_print ("%s not found in cache\n", aname->name); */
	if (strstr (aname->name, ".dll"))
		filename = g_strdup (aname->name);
	else
		filename = g_strconcat (aname->name, ".dll", NULL);
	if (basedir) {
		fullpath = g_concat_dir_and_file (basedir, filename);
		result = mono_assembly_open (fullpath, status);
		g_free (fullpath);
		if (result) {
			g_free (filename);
			return result;
		}
	}
	if (assemblies_path) {
		result = load_in_path (filename, assemblies_path, status);
		if (result) {
			g_free (filename);
			return result;
		}
	}
	result = load_in_path (filename, default_path, status);
	g_free (filename);
	return result;
}

void
mono_assembly_close (MonoAssembly *assembly)
{
	MonoImage *image;
	int i;
	
	g_return_if_fail (assembly != NULL);

	if (--assembly->ref_count != 0)
		return;
	
	loaded_assemblies = g_list_remove (loaded_assemblies, assembly);
	image = assembly->image;
	for (i = 0; image->references [i] != NULL; i++)
		mono_image_close (image->references [i]->image);
	g_free (image->references);
	     
	mono_image_close (assembly->image);

	g_free (assembly->basedir);
	g_free (assembly);
}

