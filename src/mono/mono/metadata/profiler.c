
#include "mono/metadata/profiler-private.h"
#include "mono/metadata/debug-helpers.h"
#include <string.h>

static MonoProfiler * current_profiler = NULL;

static MonoProfileAppDomainFunc   domain_start_load;
static MonoProfileAppDomainResult domain_end_load;
static MonoProfileAppDomainFunc   domain_start_unload;
static MonoProfileAppDomainFunc   domain_end_unload;

static MonoProfileAssemblyFunc   assembly_start_load;
static MonoProfileAssemblyResult assembly_end_load;
static MonoProfileAssemblyFunc   assembly_start_unload;
static MonoProfileAssemblyFunc   assembly_end_unload;

static MonoProfileModuleFunc   module_start_load;
static MonoProfileModuleResult module_end_load;
static MonoProfileModuleFunc   module_start_unload;
static MonoProfileModuleFunc   module_end_unload;

static MonoProfileClassFunc   class_start_load;
static MonoProfileClassResult class_end_load;
static MonoProfileClassFunc   class_start_unload;
static MonoProfileClassFunc   class_end_unload;

static MonoProfileMethodFunc   jit_start;
static MonoProfileMethodResult jit_end;
static MonoProfileMethodResult man_unman_transition;
static MonoProfileMethodFunc   method_enter;
static MonoProfileMethodFunc   method_leave;

static MonoProfileThreadFunc   thread_start;
static MonoProfileThreadFunc   thread_end;

static MonoProfileFunc shutdown_callback;

/* this is directly accessible to other mono libs. */
MonoProfileFlags mono_profiler_events;

void
mono_profiler_install (MonoProfiler *prof, MonoProfileFunc callback)
{
	if (current_profiler)
		g_error ("profiler already setup");
	current_profiler = prof;
	shutdown_callback = callback;
}

void
mono_profiler_set_events (MonoProfileFlags events)
{
	mono_profiler_events = events;
}

MonoProfileFlags
mono_profiler_get_events (void)
{
	return mono_profiler_events;
}

void
mono_profiler_install_enter_leave (MonoProfileMethodFunc enter, MonoProfileMethodFunc leave)
{
	method_enter = enter;
	method_leave = leave;
}

void 
mono_profiler_install_jit_compile (MonoProfileMethodFunc start, MonoProfileMethodResult end)
{
	jit_start = start;
	jit_end = end;
}

void 
mono_profiler_install_thread (MonoProfileThreadFunc start, MonoProfileThreadFunc end)
{
	thread_start = start;
	thread_end = end;
}

void 
mono_profiler_install_transition (MonoProfileMethodResult callback)
{
	man_unman_transition = callback;
}

void 
mono_profiler_install_appdomain   (MonoProfileAppDomainFunc start_load, MonoProfileAppDomainResult end_load,
                                   MonoProfileAppDomainFunc start_unload, MonoProfileAppDomainFunc end_unload)

{
	domain_start_load = start_load;
	domain_end_load = end_load;
	domain_start_unload = start_unload;
	domain_end_unload = end_unload;
}

void 
mono_profiler_install_assembly    (MonoProfileAssemblyFunc start_load, MonoProfileAssemblyResult end_load,
                                   MonoProfileAssemblyFunc start_unload, MonoProfileAssemblyFunc end_unload)
{
	assembly_start_load = start_load;
	assembly_end_load = end_load;
	assembly_start_unload = start_unload;
	assembly_end_unload = end_unload;
}

void 
mono_profiler_install_module      (MonoProfileModuleFunc start_load, MonoProfileModuleResult end_load,
                                   MonoProfileModuleFunc start_unload, MonoProfileModuleFunc end_unload)
{
	module_start_load = start_load;
	module_end_load = end_load;
	module_start_unload = start_unload;
	module_end_unload = end_unload;
}

void
mono_profiler_install_class       (MonoProfileClassFunc start_load, MonoProfileClassResult end_load,
                                   MonoProfileClassFunc start_unload, MonoProfileClassFunc end_unload)
{
	class_start_load = start_load;
	class_end_load = end_load;
	class_start_unload = start_unload;
	class_end_unload = end_unload;
}

void
mono_profiler_method_enter (MonoMethod *method)
{
	if ((mono_profiler_events & MONO_PROFILE_ENTER_LEAVE) && method_enter)
		method_enter (current_profiler, method);
}

void
mono_profiler_method_leave (MonoMethod *method)
{
	if ((mono_profiler_events & MONO_PROFILE_ENTER_LEAVE) && method_leave)
		method_leave (current_profiler, method);
}

void 
mono_profiler_method_jit (MonoMethod *method)
{
	if ((mono_profiler_events & MONO_PROFILE_JIT_COMPILATION) && jit_start)
		jit_start (current_profiler, method);
}

void 
mono_profiler_method_end_jit (MonoMethod *method, int result)
{
	if ((mono_profiler_events & MONO_PROFILE_JIT_COMPILATION) && jit_end)
		jit_end (current_profiler, method, result);
}

void 
mono_profiler_code_transition (MonoMethod *method, int result)
{
	if ((mono_profiler_events & MONO_PROFILE_TRANSITIONS) && man_unman_transition)
		man_unman_transition (current_profiler, method, result);
}

void
mono_profiler_thread_start (HANDLE thread)
{
	if ((mono_profiler_events & MONO_PROFILE_THREADS) && thread_start)
		thread_start (current_profiler, thread);
}

void 
mono_profiler_thread_end (HANDLE thread)
{
	if ((mono_profiler_events & MONO_PROFILE_THREADS) && thread_end)
		thread_end (current_profiler, thread);
}

void 
mono_profiler_assembly_event  (MonoAssembly *assembly, int code)
{
	if (!(mono_profiler_events & MONO_PROFILE_ASSEMBLY_EVENTS))
		return;
	
	switch (code) {
	case MONO_PROFILE_START_LOAD:
		if (assembly_start_load)
			assembly_start_load (current_profiler, assembly);
		break;
	case MONO_PROFILE_START_UNLOAD:
		if (assembly_start_unload)
			assembly_start_unload (current_profiler, assembly);
		break;
	case MONO_PROFILE_END_UNLOAD:
		if (assembly_end_unload)
			assembly_end_unload (current_profiler, assembly);
		break;
	default:
		g_assert_not_reached ();
	}
}

void 
mono_profiler_assembly_loaded (MonoAssembly *assembly, int result)
{
	if ((mono_profiler_events & MONO_PROFILE_ASSEMBLY_EVENTS) && assembly_end_load)
		assembly_end_load (current_profiler, assembly, result);
}

void 
mono_profiler_module_event  (MonoImage *module, int code)
{
	if (!(mono_profiler_events & MONO_PROFILE_MODULE_EVENTS))
		return;
	
	switch (code) {
	case MONO_PROFILE_START_LOAD:
		if (module_start_load)
			module_start_load (current_profiler, module);
		break;
	case MONO_PROFILE_START_UNLOAD:
		if (module_start_unload)
			module_start_unload (current_profiler, module);
		break;
	case MONO_PROFILE_END_UNLOAD:
		if (module_end_unload)
			module_end_unload (current_profiler, module);
		break;
	default:
		g_assert_not_reached ();
	}
}

void 
mono_profiler_module_loaded (MonoImage *module, int result)
{
	if ((mono_profiler_events & MONO_PROFILE_MODULE_EVENTS) && module_end_load)
		module_end_load (current_profiler, module, result);
}

void 
mono_profiler_class_event  (MonoClass *klass, int code)
{
	if (!(mono_profiler_events & MONO_PROFILE_CLASS_EVENTS))
		return;
	
	switch (code) {
	case MONO_PROFILE_START_LOAD:
		if (class_start_load)
			class_start_load (current_profiler, klass);
		break;
	case MONO_PROFILE_START_UNLOAD:
		if (class_start_unload)
			class_start_unload (current_profiler, klass);
		break;
	case MONO_PROFILE_END_UNLOAD:
		if (class_end_unload)
			class_end_unload (current_profiler, klass);
		break;
	default:
		g_assert_not_reached ();
	}
}

void 
mono_profiler_class_loaded (MonoClass *klass, int result)
{
	if ((mono_profiler_events & MONO_PROFILE_CLASS_EVENTS) && class_end_load)
		class_end_load (current_profiler, klass, result);
}

void 
mono_profiler_appdomain_event  (MonoDomain *domain, int code)
{
	if (!(mono_profiler_events & MONO_PROFILE_APPDOMAIN_EVENTS))
		return;
	
	switch (code) {
	case MONO_PROFILE_START_LOAD:
		if (domain_start_load)
			domain_start_load (current_profiler, domain);
		break;
	case MONO_PROFILE_START_UNLOAD:
		if (domain_start_unload)
			domain_start_unload (current_profiler, domain);
		break;
	case MONO_PROFILE_END_UNLOAD:
		if (domain_end_unload)
			domain_end_unload (current_profiler, domain);
		break;
	default:
		g_assert_not_reached ();
	}
}

void 
mono_profiler_appdomain_loaded (MonoDomain *domain, int result)
{
	if ((mono_profiler_events & MONO_PROFILE_APPDOMAIN_EVENTS) && domain_end_load)
		domain_end_load (current_profiler, domain, result);
}

void 
mono_profiler_shutdown (void)
{
	if (current_profiler && shutdown_callback)
		shutdown_callback (current_profiler);
}

/*
 * Small profiler extracted from mint: we should move it in a loadable module
 * and improve it to do graphs and more accurate timestamping with rdtsc.
 */

#define USE_X86TSC 0
#define USE_WIN32COUNTER 0
#if USE_X86TSC

typedef struct {
	unsigned int lows, highs, lowe, highe;
} MonoRdtscTimer;

#define rdtsc(low,high) \
        __asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high))

static int freq;

static double
rdtsc_elapsed (MonoRdtscTimer *t)
{
	unsigned long long diff;
	unsigned int highe = t->highe;
	if (t->lowe < t->lows)
		highe--;
	diff = (((unsigned long long) highe - t->highs) << 32) + (t->lowe - t->lows);
	return ((double)diff / freq) / 1000000; /* have to return the result in seconds */
}

static int 
have_rdtsc (void) {
	char buf[256];
	int have_freq = 0;
	int have_flag = 0;
	float val;
	FILE *cpuinfo;

	if (!(cpuinfo = fopen ("/proc/cpuinfo", "r")))
		return 0;
	while (fgets (buf, sizeof(buf), cpuinfo)) {
		if (sscanf (buf, "cpu MHz : %f", &val) == 1) {
			/*printf ("got mh: %f\n", val);*/
			have_freq = val;
		}
		if (strncmp (buf, "flags", 5) == 0) {
			if (strstr (buf, "tsc")) {
				have_flag = 1;
				/*printf ("have tsc\n");*/
			}
		}
	}
	fclose (cpuinfo);
	return have_flag? have_freq: 0;
}

#define MONO_TIMER_STARTUP 	\
	if (!(freq = have_rdtsc ())) g_error ("Compiled with TSC support, but none found");
#define MONO_TIMER_TYPE  MonoRdtscTimer
#define MONO_TIMER_INIT(t)
#define MONO_TIMER_DESTROY(t)
#define MONO_TIMER_START(t) rdtsc ((t).lows, (t).highs);
#define MONO_TIMER_STOP(t) rdtsc ((t).lowe, (t).highe);
#define MONO_TIMER_ELAPSED(t) rdtsc_elapsed (&(t))

#elif USE_WIN32COUNTER
#include <windows.h>

typedef struct {
	LARGE_INTEGER start, stop;
} MonoWin32Timer;

static int freq;

static double
win32_elapsed (MonoWin32Timer *t)
{
	LONGLONG diff = t->stop.QuadPart - t->start.QuadPart;
	return ((double)diff / freq) / 1000000; /* have to return the result in seconds */
}

static int 
have_win32counter (void) {
	LARGE_INTEGER f;

	if (!QueryPerformanceFrequency (&f))
		return 0;
	return f.LowPart;
}

#define MONO_TIMER_STARTUP 	\
	if (!(freq = have_win32counter ())) g_error ("Compiled with Win32 counter support, but none found");
#define MONO_TIMER_TYPE  MonoWin32Timer
#define MONO_TIMER_INIT(t)
#define MONO_TIMER_DESTROY(t)
#define MONO_TIMER_START(t) QueryPerformanceCounter (&(t).start)
#define MONO_TIMER_STOP(t) QueryPerformanceCounter (&(t).stop)
#define MONO_TIMER_ELAPSED(t) win32_elapsed (&(t))

#else

typedef struct {
	GTimeVal start, stop;
} MonoGLibTimer;

static double
timeval_elapsed (MonoGLibTimer *t)
{
	if (t->start.tv_usec > t->stop.tv_usec) {
		t->stop.tv_usec += G_USEC_PER_SEC;
		t->stop.tv_sec--;
	}
	return (t->stop.tv_sec - t->start.tv_sec) 
		+ ((double)(t->stop.tv_usec - t->start.tv_usec))/ G_USEC_PER_SEC;
}

#define MONO_TIMER_STARTUP
#define MONO_TIMER_TYPE MonoGLibTimer
#define MONO_TIMER_INIT(t)
#define MONO_TIMER_DESTROY(t)
#define MONO_TIMER_START(t) g_get_current_time (&(t).start)
#define MONO_TIMER_STOP(t) g_get_current_time (&(t).stop)
#define MONO_TIMER_ELAPSED(t) timeval_elapsed (&(t))
#endif

struct _MonoProfiler {
	GHashTable *methods;
	GHashTable *newobjs;
	MONO_TIMER_TYPE jit_timer;
	double      jit_time;
	int         methods_jitted;
};

typedef struct {
	union {
		MONO_TIMER_TYPE timer;
		MonoMethod *method;
	} u;
	guint64 count;
	double total;
} MethodProfile;

typedef struct _MethodCallProfile MethodCallProfile;

struct _MethodCallProfile {
	MethodCallProfile *next;
	MONO_TIMER_TYPE timer;
	MonoMethod *method;
};

static gint
compare_profile (MethodProfile *profa, MethodProfile *profb)
{
	return (gint)((profb->total - profa->total)*1000);
}

static void
build_profile (MonoMethod *m, MethodProfile *prof, GList **funcs)
{
	MONO_TIMER_DESTROY (prof->u.timer);
	prof->u.method = m;
	*funcs = g_list_insert_sorted (*funcs, prof, (GCompareFunc)compare_profile);
}

static void
output_profile (GList *funcs)
{
	GList *tmp;
	MethodProfile *p;
	char buf [256];
	char *sig;
	guint64 total_calls = 0;

	if (funcs)
		g_print ("Method name\t\t\t\t\tTotal (ms) Calls Per call (ms)\n");
	for (tmp = funcs; tmp; tmp = tmp->next) {
		p = tmp->data;
		total_calls += p->count;
		if (!(gint)(p->total*1000))
			continue;
		sig = mono_signature_get_desc (p->u.method->signature, FALSE);
		g_snprintf (buf, sizeof (buf), "%s.%s::%s(%s)",
			p->u.method->klass->name_space, p->u.method->klass->name,
			p->u.method->name, sig);
		g_free (sig);
		printf ("%-52s %7d %7llu %7d\n", buf,
			(gint)(p->total*1000), p->count, (gint)((p->total*1000)/p->count));
	}
	printf ("Total number of calls: %lld\n", total_calls);
}

typedef struct {
	MonoClass *klass;
	guint count;
} NewobjProfile;

static gint
compare_newobj_profile (NewobjProfile *profa, NewobjProfile *profb)
{
	return (gint)profb->count - (gint)profa->count;
}

static void
build_newobj_profile (MonoClass *class, gpointer count, GList **funcs)
{
	NewobjProfile *prof = g_new (NewobjProfile, 1);
	prof->klass = class;
	prof->count = GPOINTER_TO_UINT (count);
	*funcs = g_list_insert_sorted (*funcs, prof, (GCompareFunc)compare_newobj_profile);
}

static void
output_newobj_profile (GList *proflist)
{
	GList *tmp;
	NewobjProfile *p;
	MonoClass *klass;
	const char* isarray;
	char buf [256];

	if (proflist)
		g_print ("\n%-52s %9s\n", "Objects created:", "count");
	for (tmp = proflist; tmp; tmp = tmp->next) {
		p = tmp->data;
		klass = p->klass;
		if (klass->rank) {
			isarray = "[]";
			klass = klass->element_class;
		} else {
			isarray = "";
		}
		g_snprintf (buf, sizeof (buf), "%s.%s%s",
			klass->name_space, klass->name, isarray);
		g_print ("%-52s %9d\n", buf, p->count);
	}
}

static void
simple_method_enter (MonoProfiler *prof, MonoMethod *method)
{
	MethodProfile *profile_info;
	if (!(profile_info = g_hash_table_lookup (prof->methods, method))) {
		profile_info = g_new0 (MethodProfile, 1);
		MONO_TIMER_INIT (profile_info->u.timer);
		g_hash_table_insert (prof->methods, method, profile_info);
	}
	profile_info->count++;
	MONO_TIMER_START (profile_info->u.timer);
}

static void
simple_method_leave (MonoProfiler *prof, MonoMethod *method)
{
	MethodProfile *profile_info;
	if (!(profile_info = g_hash_table_lookup (prof->methods, method)))
		g_assert_not_reached ();

	MONO_TIMER_STOP (profile_info->u.timer);
	profile_info->total += MONO_TIMER_ELAPSED (profile_info->u.timer);
}

static void
simple_method_jit (MonoProfiler *prof, MonoMethod *method)
{
	prof->methods_jitted++;
	MONO_TIMER_START (prof->jit_timer);
}

static void
simple_method_end_jit (MonoProfiler *prof, MonoMethod *method, int result)
{
	MONO_TIMER_STOP (prof->jit_timer);
	prof->jit_time += MONO_TIMER_ELAPSED (prof->jit_timer);
}

static void
simple_shutdown (MonoProfiler *prof)
{
	GList *profile = NULL;

	printf("Total time spent compiling %d methods (sec): %.4g\n", prof->methods_jitted, prof->jit_time);
	g_hash_table_foreach (prof->methods, (GHFunc)build_profile, &profile);
	output_profile (profile);
	g_list_free (profile);
	profile = NULL;
		
	g_hash_table_foreach (prof->newobjs, (GHFunc)build_newobj_profile, &profile);
	output_newobj_profile (profile);
	g_list_free (profile);
}

void
mono_profiler_install_simple (void)
{
	MonoProfiler *prof = g_new0 (MonoProfiler, 1);

	MONO_TIMER_STARTUP;

	prof->methods = g_hash_table_new (g_direct_hash, g_direct_equal);
	prof->newobjs = g_hash_table_new (g_direct_hash, g_direct_equal);
	MONO_TIMER_INIT (prof->jit_timer);

	mono_profiler_install (prof, simple_shutdown);
	/* later do also object creation */
	mono_profiler_install_enter_leave (simple_method_enter, simple_method_leave);
	mono_profiler_install_jit_compile (simple_method_jit, simple_method_end_jit);
	mono_profiler_set_events (MONO_PROFILE_ENTER_LEAVE|MONO_PROFILE_JIT_COMPILATION);
}

