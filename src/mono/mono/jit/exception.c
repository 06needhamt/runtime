/*
 * exception.c: exception support
 *
 * Authors:
 *   Dietmar Maurer (dietmar@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 */

#include <config.h>
#include <glib.h>

#include <mono/arch/x86/x86-codegen.h>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/tabledefs.h>

#include "jit.h"
#include "codegen.h"

/*
 * arch_get_restore_context:
 *
 * Returns a pointer to a method which restores a previously saved sigcontext.
 */
static gpointer
arch_get_restore_context (void)
{
	static guint8 *start = NULL;
	guint8 *code;

	if (start)
		return start;

	/* restore_contect (struct sigcontext *ctx) */
	/* we do not restore X86_EAX, X86_EDX */

	start = code = g_malloc (1024);
	
	/* load ctx */
	x86_mov_reg_membase (code, X86_EAX, X86_ESP, 4, 4);

	/* get return address, stored in EDX */
	x86_mov_reg_membase (code, X86_EDX, X86_EAX,  G_STRUCT_OFFSET (struct sigcontext, eip), 4);
	/* restore EBX */
	x86_mov_reg_membase (code, X86_EBX, X86_EAX,  G_STRUCT_OFFSET (struct sigcontext, ebx), 4);
	/* restore EDI */
	x86_mov_reg_membase (code, X86_EDI, X86_EAX,  G_STRUCT_OFFSET (struct sigcontext, edi), 4);
	/* restore ESI */
	x86_mov_reg_membase (code, X86_ESI, X86_EAX,  G_STRUCT_OFFSET (struct sigcontext, esi), 4);
	/* restore ESP */
	x86_mov_reg_membase (code, X86_ESP, X86_EAX,  G_STRUCT_OFFSET (struct sigcontext, esp), 4);
	/* restore EBP */
	x86_mov_reg_membase (code, X86_EBP, X86_EAX,  G_STRUCT_OFFSET (struct sigcontext, ebp), 4);
	/* restore ECX. the exception object is passed here to the catch handler */
	x86_mov_reg_membase (code, X86_ECX, X86_EAX,  G_STRUCT_OFFSET (struct sigcontext, ecx), 4);

	/* jump to the saved IP */
	x86_jump_reg (code, X86_EDX);

	return start;
}

/*
 * arch_get_call_finally:
 *
 * Returns a pointer to a method which calls a finally handler.
 */
static gpointer
arch_get_call_finally (void)
{
	static guint8 start [28];
	static int inited = 0;
	guint8 *code;

	if (inited)
		return start;

	inited = 1;
	/* call_finally (struct sigcontext *ctx, unsigned long eip) */
	code = start;

	x86_push_reg (code, X86_EBP);
	x86_mov_reg_reg (code, X86_EBP, X86_ESP, 4);
	x86_push_reg (code, X86_EBX);
	x86_push_reg (code, X86_EDI);
	x86_push_reg (code, X86_ESI);

	/* load ctx */
	x86_mov_reg_membase (code, X86_EAX, X86_EBP, 8, 4);
	/* load eip */
	x86_mov_reg_membase (code, X86_ECX, X86_EBP, 12, 4);
	/* save EBP */
	x86_push_reg (code, X86_EBP);
	/* set new EBP */
	x86_mov_reg_membase (code, X86_EBP, X86_EAX,  G_STRUCT_OFFSET (struct sigcontext, ebp), 4);
	/* call the handler */
	x86_call_reg (code, X86_ECX);
	/* restore EBP */
	x86_pop_reg (code, X86_EBP);
	/* restore saved regs */
	x86_pop_reg (code, X86_ESI);
	x86_pop_reg (code, X86_EDI);
	x86_pop_reg (code, X86_EBX);
	x86_leave (code);
	x86_ret (code);

	g_assert ((code - start) < 28);
	return start;
}


/**
 * arch_handle_exception:
 * @ctx: saved processor state
 * @obj:
 */
void
arch_handle_exception (struct sigcontext *ctx, gpointer obj)
{
	MonoDomain *domain = mono_domain_get ();
	MonoJitInfo *ji;
	gpointer ip = (gpointer)ctx->eip;
	static void (*restore_context) (struct sigcontext *);
	static void (*call_finally) (struct sigcontext *, unsigned long);
	void (*cleanup) (MonoObject *exc);

	g_assert (ctx != NULL);
	g_assert (obj != NULL);

	ji = mono_jit_info_table_find (domain, ip);

	if (!restore_context)
		restore_context = arch_get_restore_context ();
	
	if (!call_finally)
		call_finally = arch_get_call_finally ();

	cleanup = TlsGetValue (exc_cleanup_id);

	if (ji) { /* we are inside managed code */
		MonoMethod *m = ji->method;
		int offset = 2;

		if (ji->num_clauses) {
			int i;

			g_assert (ji->clauses);
			
			for (i = 0; i < ji->num_clauses; i++) {
				MonoJitExceptionInfo *ei = &ji->clauses [i];

				if (ei->try_start <= ip && ip <= (ei->try_end)) { 
					/* catch block */
					if (ei->flags == 0 && mono_object_isinst (obj, 
					        mono_class_get (m->klass->image, ei->token_or_filter))) {
					
						ctx->eip = (unsigned long)ei->handler_start;
						ctx->ecx = (unsigned long)obj;
						restore_context (ctx);
						g_assert_not_reached ();
					}
				}
			}

			/* no handler found - we need to call all finally handlers */
			for (i = 0; i < ji->num_clauses; i++) {
				MonoJitExceptionInfo *ei = &ji->clauses [i];

				if (ei->try_start <= ip && ip < (ei->try_end) &&
				    (ei->flags & MONO_EXCEPTION_CLAUSE_FINALLY)) {
					call_finally (ctx, (unsigned long)ei->handler_start);
				}
			}
		}

		if (mono_object_isinst (obj, mono_defaults.exception_class)) {
			char  *strace = mono_string_to_utf8 (((MonoException*)obj)->stack_trace);
			char  *tmp;

			if (!strcmp (strace, "TODO: implement stack traces")){
				g_free (strace);
				strace = g_strdup ("");
			}

			tmp = g_strdup_printf ("%sin %s.%s:%s ()\n", strace, m->klass->name_space,  
					       m->klass->name, m->name);

			g_free (strace);

			((MonoException*)obj)->stack_trace = mono_string_new (domain, tmp);
			g_free (tmp);
		}

		/* continue unwinding */

		/* restore caller saved registers */
		if (ji->used_regs & X86_ESI_MASK) {
			ctx->esi = *((int *)ctx->ebp + offset);
			offset++;
		}
		if (ji->used_regs & X86_EDI_MASK) {
			ctx->edi = *((int *)ctx->ebp + offset);
			offset++;
		}
		if (ji->used_regs & X86_EBX_MASK) {
			ctx->ebx = *((int *)ctx->ebp + offset);
		}

		ctx->esp = ctx->ebp;
		ctx->eip = *((int *)ctx->ebp + 1);
		ctx->ebp = *((int *)ctx->ebp);
		
		if (ctx->ebp < (unsigned)mono_end_of_stack)
			arch_handle_exception (ctx, obj);
		else {
			g_assert (cleanup);
			cleanup (obj);
		}
	
	} else {
		gpointer *lmf_addr = TlsGetValue (lmf_thread_id);
		MonoLMF *lmf;
		MonoMethod *m;

		g_assert (lmf_addr);
		lmf = *((MonoLMF **)lmf_addr);

		if (!lmf) {
			g_assert (cleanup);
			cleanup (obj);
		}
		m = lmf->method;

		*lmf_addr = lmf->previous_lmf;

		ctx->esi = lmf->esi;
		ctx->edi = lmf->edi;
		ctx->ebx = lmf->ebx;
		ctx->ebp = lmf->ebp;
		ctx->eip = lmf->eip;
		ctx->esp = (unsigned long)&lmf->eip;

		if (mono_object_isinst (obj, mono_defaults.exception_class)) {
			char  *strace = mono_string_to_utf8 (((MonoException*)obj)->stack_trace);
			char  *tmp;

			if (!strcmp (strace, "TODO: implement stack traces"))
				strace = g_strdup ("");

			tmp = g_strdup_printf ("%sin (unmanaged) %s.%s:%s ()\n", strace, m->klass->name_space,  
					       m->klass->name, m->name);

			g_free (strace);

			((MonoException*)obj)->stack_trace = mono_string_new (domain, tmp);
			g_free (tmp);
		}

		if (ctx->eip < (unsigned)mono_end_of_stack)
			arch_handle_exception (ctx, obj);
		else {
			g_assert (cleanup);
			cleanup (obj);
		}
	}

	g_assert_not_reached ();
}

static void
throw_exception (unsigned long eax, unsigned long ecx, unsigned long edx, unsigned long ebx,
		 unsigned long esi, unsigned long edi, unsigned long ebp, MonoObject *exc,
		 unsigned long eip,  unsigned long esp)
{
	struct sigcontext ctx;

	ctx.esp = esp;
	ctx.eip = eip;
	ctx.ebp = ebp;
	ctx.edi = edi;
	ctx.esi = esi;
	ctx.ebx = ebx;
	ctx.edx = edx;
	ctx.ecx = ecx;
	ctx.eax = eax;
	
	arch_handle_exception (&ctx, exc);

	g_assert_not_reached ();
}

/**
 * arch_get_throw_exception:
 *
 * Returns a function pointer which can be used to raise 
 * exceptions. The returned function has the following 
 * signature: void (*func) (MonoException *exc); 
 * For example to raise an arithmetic exception you can use:
 *
 * x86_push_imm (code, mono_get_exception_arithmetic ()); 
 * x86_call_code (code, arch_get_throw_exception ()); 
 *
 */
gpointer 
arch_get_throw_exception (void)
{
	static guint8 start [24];
	static int inited = 0;
	guint8 *code;

	if (inited)
		return start;

	inited = 1;
	code = start;

	x86_push_reg (code, X86_ESP);
	x86_push_membase (code, X86_ESP, 4); /* IP */
	x86_push_membase (code, X86_ESP, 12); /* exception */
	x86_push_reg (code, X86_EBP);
	x86_push_reg (code, X86_EDI);
	x86_push_reg (code, X86_ESI);
	x86_push_reg (code, X86_EBX);
	x86_push_reg (code, X86_EDX);
	x86_push_reg (code, X86_ECX);
	x86_push_reg (code, X86_EAX);
	x86_call_code (code, throw_exception);
	/* we should never reach this breakpoint */
	x86_breakpoint (code);

	g_assert ((code - start) < 24);
	return start;
}

/**
 * arch_get_throw_exception_by_name:
 *
 * Returns a function pointer which can be used to raise 
 * corlib exceptions. The returned function has the following 
 * signature: void (*func) (char *exc_name); 
 * For example to raise an arithmetic exception you can use:
 *
 * x86_push_imm (code, "ArithmeticException"); 
 * x86_call_code (code, arch_get_throw_exception ()); 
 *
 */
gpointer 
arch_get_throw_exception_by_name ()
{
	static guint8 start [32];
	static int inited = 0;
	guint8 *code;

	if (inited)
		return start;

	inited = 1;
	code = start;

	/* fixme: we do not save EAX, EDX, ECD - unsure if we need that */

	x86_push_membase (code, X86_ESP, 4); /* exception name */
	x86_push_imm (code, "System");
	x86_push_imm (code, mono_defaults.exception_class->image);
	x86_call_code (code, mono_exception_from_name);
	x86_alu_reg_imm (code, X86_ADD, X86_ESP, 12);
	/* save the newly create object (overwrite exception name)*/
	x86_mov_membase_reg (code, X86_ESP, 4, X86_EAX, 4);
	x86_jump_code (code, arch_get_throw_exception ());

	g_assert ((code - start) < 32);

	return start;
}	
