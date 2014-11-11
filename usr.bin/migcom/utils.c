/*-
 * Copyright (c) 2014, Matthew Macy <kmacy@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * Copyright 1991-1998 by Open Software Foundation, Inc. 
 *              All Rights Reserved 
 *  
 * Permission to use, copy, modify, and distribute this software and 
 * its documentation for any purpose and without fee is hereby granted, 
 * provided that the above copyright notice appears in all copies and 
 * that both the copyright notice and this permission notice appear in 
 * supporting documentation. 
 *  
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE 
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE. 
 *  
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR 
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT, 
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION 
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
 */
/*
 * cmk1.1
 */
/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 * 92/03/03  16:25:39  jeffreyh
 * 	Changes from TRUNK
 * 	[92/02/26  12:33:02  jeffreyh]
 * 
 * 92/01/14  16:47:08  rpd
 * 	Modified WriteTypeDeclIn and WriteTypeDeclOut to disable
 * 	the deallocate flag on Indefinite arguments.
 * 	[92/01/09            rpd]
 * 
 * 92/01/03  20:30:51  dbg
 * 	Change argByReferenceUser and argByReferenceServer to fields in
 * 	argument_t.
 * 	[91/08/29            dbg]
 * 
 * 91/07/31  18:11:45  dbg
 * 	Accept new dealloc_t argument type in WriteStaticDecl,
 * 	WritePackMsgType.
 * 
 * 	Don't need to zero last character of C string.  Mig_strncpy does
 * 	the proper work.
 * 
 * 	Add SkipVFPrintf, so that WriteCopyType doesn't print fields in
 * 	comments.
 * 	[91/07/17            dbg]
 * 
 * 91/06/25  10:32:36  rpd
 * 	Changed WriteVarDecl to WriteUserVarDecl.
 * 	Added WriteServerVarDecl.
 * 	[91/05/23            rpd]
 * 
 * 91/02/05  17:56:28  mrt
 * 	Changed to new Mach copyright
 * 	[91/02/01  17:56:39  mrt]
 * 
 * 90/06/02  15:06:11  rpd
 * 	Created for new IPC.
 * 	[90/03/26  21:14:54  rpd]
 * 
 * 07-Apr-89  Richard Draves (rpd) at Carnegie-Mellon University
 *	Extensive revamping.  Added polymorphic arguments.
 *	Allow multiple variable-sized inline arguments in messages.
 *
 * 21-Aug-87  Mary Thompson (mrt) at Carnegie-Mellon University
 *	Added deallocflag to the WritePackMsg routines.
 *
 * 29-Jul-87  Mary Thompson (mrt) at Carnegie-Mellon University
 *	Changed WriteVarDecl to not automatically write
 *	semi-colons between items, so that it can be
 *	used to write C++ argument lists.
 *
 * 27-May-87  Richard Draves (rpd) at Carnegie-Mellon University
 *	Created.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <mach/message.h>
#include "routine.h"
#include "write.h"
#include "global.h"
#include "utils.h"
#include "alloc.h"


void
WriteIdentificationString(FILE *file)
{
    extern int IsKernelUser, IsKernelServer, UseMsgRPC;

    fprintf(file, "/*\n");
    fprintf(file, " * IDENTIFICATION:\n");
    fprintf(file, " * stub generated %s", GenerationDate);
    fprintf(file, " * with a MiG generated %s by %s\n", MigGenerationDate, MigMoreData);
    fprintf(file, " * OPTIONS: \n");
    if (IsKernelUser)
	fprintf(file, " *\tKernelUser\n");
    if (IsKernelServer)
	fprintf(file, " *\tKernelServer\n");
    if (!UseMsgRPC)
	fprintf(file, " *\t-R (no RPC calls)\n");
    fprintf(file, " */\n");
}

void
WriteImport(file, filename)
    FILE *file;
    string_t filename;
{
    fprintf(file, "#include %s\n", filename);
}

void 
WriteImplImports(file, stats, isuser)
    FILE *file;
    statement_t *stats;
    boolean_t isuser;
{
    register statement_t *stat;

    for (stat = stats; stat != stNULL; stat = stat->stNext)
	switch (stat->stKind)
	{
	  case skImport:
          case skIImport:
	    WriteImport(file, stat->stFileName);
	    break;
	  case skSImport:
	    if (!isuser)
	        WriteImport(file, stat->stFileName);
	    break;
	  case skUImport:
	    if (isuser)
	        WriteImport(file, stat->stFileName);
	    break;
	  case skRoutine:
	  case skDImport:
	    break;
	  default:
	    printf("WriteImplImport(): bad statement_kind_t (%d)",
			   (int) stat->stKind);
		abort();
	}
}

void
WriteRCSDecl(file, name, rcs)
    FILE *file;
    identifier_t name;
    string_t rcs;
{
    fprintf(file, "#ifndef\tlint\n");
    fprintf(file, "#if\tUseExternRCSId\n");
    fprintf(file, "%s char %s_rcsid[] = %s;\n", (BeAnsiC) ? "const" : "", name, rcs);
    fprintf(file, "#else\t/* UseExternRCSId */\n");
    fprintf(file, "static %s char rcsid[] = %s;\n", (BeAnsiC) ? "const" : "", rcs);
    fprintf(file, "#endif\t/* UseExternRCSId */\n");
    fprintf(file, "#endif\t/* lint */\n");
    fprintf(file, "\n");
}

static void
WriteOneApplDefault(FILE *file, const char *word1, const char *word2, const char *word3)
{
    char buf[50];

    sprintf(buf, "__%s%s%s", word1, word2, word3);
    fprintf(file, "#ifndef\t%s\n", buf);
    fprintf(file, "#define\t%s(_NUM_, _NAME_)\n", buf);
    fprintf(file, "#endif\t/* %s */\n", buf);
    fprintf(file, "\n");
}
    
void
WriteApplDefaults(FILE *file, const char *dir)
{
    WriteOneApplDefault(file, "Declare", dir, "Rpc");
    WriteOneApplDefault(file, "Before", dir, "Rpc");
    WriteOneApplDefault(file, "After", dir, "Rpc");
    WriteOneApplDefault(file, "Declare", dir, "Simple");
    WriteOneApplDefault(file, "Before", dir, "Simple");
    WriteOneApplDefault(file, "After", dir, "Simple");
}

void
WriteApplMacro(FILE *file, const char *dir, const char *when, routine_t *rt)
{
    const char *what = (rt->rtOneWay) ? "Simple" : "Rpc";

    fprintf(file, "\t__%s%s%s(%d, \"%s\")\n", 
	    when, dir, what, SubsystemBase + rt->rtNumber, rt->rtName);
}

void
WriteBogusDefines(FILE *file)
{
    fprintf(file, "#ifndef\tmig_internal\n");
    fprintf(file, "#define\tmig_internal\tstatic\n");
    fprintf(file, "#endif\t/* mig_internal */\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tmig_external\n");
    fprintf(file, "#define mig_external\n");
    fprintf(file, "#endif\t/* mig_external */\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tTypeCheck\n");
    fprintf(file, "#define\tTypeCheck 0\n");
    fprintf(file, "#endif\t/* TypeCheck */\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tLimitCheck\n");
    fprintf(file, "#define\tLimitCheck 0\n");
    fprintf(file, "#endif\t/* LimitCheck */\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tmin\n");
    fprintf(file, "#define\tmin(a,b)  ( ((a) < (b))? (a): (b) )\n");
    fprintf(file, "#endif\t/* min */\n");
    fprintf(file, "\n");

    fprintf(file, "#ifndef\tUseStaticTemplates\n");
    if (BeAnsiC) {
        fprintf(file, "#define\tUseStaticTemplates\t1\n");
    } else {
        fprintf(file, "#if\t%s\n", NewCDecl);
        fprintf(file, "#define\tUseStaticTemplates\t1\n");
        fprintf(file, "#endif\t/* %s */\n", NewCDecl);
    }    
    fprintf(file, "#endif\t/* UseStaticTemplates */\n");
    fprintf(file, "\n");

    fprintf(file, "#define _WALIGN_(x) (((x) + %ld) & ~%ld)\n",
	    itWordAlign - 1, itWordAlign - 1);
    fprintf(file, "#define _WALIGNSZ_(x) _WALIGN_(sizeof(x))\n");
}

void
WriteList(FILE *file, argument_t *args, void (*func)(), u_int mask, const char *between, const char *after)
{
    register argument_t *arg;
    register boolean_t sawone = FALSE;

    for (arg = args; arg != argNULL; arg = arg->argNext)
	if (akCheckAll(arg->argKind, mask))
	{
	    if (sawone)
		fprintf(file, "%s", between);
	    sawone = TRUE;

	    (*func)(file, arg);
	}

    if (sawone)
	fprintf(file, "%s", after);
}

static boolean_t
WriteReverseListPrim(FILE *file, argument_t *arg, void (*func)(), u_int mask, const char *between)
{
    boolean_t sawone = FALSE;

    if (arg != argNULL)
    {
	sawone = WriteReverseListPrim(file, arg->argNext, func, mask, between);

	if (akCheckAll(arg->argKind, mask))
	{
	    if (sawone)
		fprintf(file, "%s", between);
	    sawone = TRUE;

	    (*func)(file, arg);
	}
    }

    return sawone;
}

void
WriteReverseList(FILE *file, argument_t *args, void (*func)(), u_int mask, const char *between, const char *after)
{
    boolean_t sawone;

    sawone = WriteReverseListPrim(file, args, func, mask, between);

    if (sawone)
	fprintf(file, "%s", after);
}

void
WriteNameDecl(file, arg)
    FILE *file;
    argument_t *arg;
{
    fprintf(file, "%s", arg->argVarName);
}

void
WriteUserVarDecl(file, arg)
    FILE *file;
    argument_t *arg;
{
    const char *ref = (arg->argByReferenceUser ||
                 arg->argType->itNativePointer) ? "*" : "";

    fprintf(file, "\t%s %s%s", arg->argType->itUserType, ref, arg->argVarName);
}

void
WriteServerVarDecl(file, arg)
    FILE *file;
    argument_t *arg;
{
    const char *ref = (arg->argByReferenceServer ||
		 arg->argType->itNativePointer) ? "*" : "";
  
    fprintf(file, "\t%s %s%s",
	    arg->argType->itTransType, ref, arg->argVarName);
}

const char *
ReturnTypeStr(rt)
    routine_t *rt;
{
    return rt->rtRetCode->argType->itUserType;
}

const char *
FetchUserType(it)
    ipc_type_t *it;
{
    return it->itUserType;
}

const char *
FetchUserKPDType(it)
    ipc_type_t *it;
{
    return it->itUserKPDType;
}

const char *
FetchServerType(it)
    ipc_type_t *it;
{
    return it->itServerType;
}

const char *
FetchServerKPDType(it)
    ipc_type_t *it;
{
    /* do we really need to differentiate User and Server ?? */
    return it->itServerKPDType;
}

static void
WriteTrailerDecl(file, trailer)
    FILE *file;
    boolean_t trailer;
{
    if (trailer)
	fprintf(file, "\t\tmach_msg_format_0_trailer_t trailer;\n");
    else
	fprintf(file, "\t\tmach_msg_trailer_t trailer;\n");
}

void
WriteFieldDeclPrim(FILE *file, argument_t *arg, const char *(*tfunc)(ipc_type_t *))
{
    register ipc_type_t *it = arg->argType;

    if (IS_VARIABLE_SIZED_UNTYPED(it) || it->itNoOptArray) {
	register argument_t *count = arg->argCount;
	register ipc_type_t *btype = it->itElement;

	/*
	 *	Build our own declaration for a varying array:
	 *	use the element type and maximum size specified.
	 *	Note arg->argCount->argMultiplier == btype->itNumber.
	 */
	/*
	 * NDR encoded VarStrings requires the offset field.
	 * Since it is not used, it wasn't worthwhile to create an extra 
	 * parameter
	 */
	if (it->itString)
	    fprintf(file, "\t\t%s %sOffset; /* MiG doesn't use it */\n", 
		(*tfunc)(count->argType), arg->argName);

	if (!(arg->argFlags & flSameCount) && !it->itNoOptArray)
	        /* in these cases we would have a count, which we don't want */
		fprintf(file, "\t\t%s %s;\n", (*tfunc)(count->argType), 
		    count->argMsgField);
	fprintf(file, "\t\t%s %s[%d];",
			(*tfunc)(btype),
			arg->argMsgField,
			it->itNumber/btype->itNumber);
    }
    else if (IS_MULTIPLE_KPD(it))  
	fprintf(file, "\t\t%s %s[%d];", (*tfunc)(it), arg->argMsgField,
			it->itKPD_Number);
    else if (IS_OPTIONAL_NATIVE(it)) {
        fprintf(file, "\t\tboolean_t __Present__%s;\n",  arg->argMsgField);
        fprintf(file, "\t\tunion {\n");
	fprintf(file, "\t\t    %s __Real__%s;\n",
		(*tfunc)(it), arg->argMsgField);
	fprintf(file, "\t\t    char __Phony__%s[_WALIGNSZ_(%s)];\n",
		arg->argMsgField, (*tfunc)(it));
	fprintf(file, "\t\t} %s;", arg->argMsgField);
    } 
    else  {
	/* either simple KPD or simple in-line */
	fprintf(file, "\t\t%s %s;", (*tfunc)(it), arg->argMsgField);
    }

    /* Kernel Processed Data has always PadSize = 0 */
    if (it->itPadSize != 0)
	fprintf(file, "\n\t\tchar %s[%d];", arg->argPadName, it->itPadSize);
}


void
WriteStructDecl(FILE *file, argument_t *args, void (*func)(), u_int mask,
				const char *name, boolean_t simple, boolean_t trailer,
				boolean_t trailer_t, boolean_t template_only)
{
    fprintf(file, "\ttypedef struct {\n");
    fprintf(file, "\t\tmach_msg_header_t Head;\n");
    if (simple == FALSE) {
	fprintf(file, "\t\t/* start of the kernel processed data */\n");
	fprintf(file, "\t\tmach_msg_body_t msgh_body;\n");
	if (mask == akbRequest) 
    	    WriteList(file, args, func, mask | akbSendKPD, "\n", "\n");
	else 
    	    WriteList(file, args, func, mask | akbReturnKPD, "\n", "\n");
	fprintf(file, "\t\t/* end of the kernel processed data */\n");
    }
    if (!template_only) {
	if (mask == akbRequest) 
	    WriteList(file, args, func, mask | akbSendBody, "\n", "\n");

	else
	    WriteList(file, args, func, mask | akbReturnBody, "\n", "\n");
	if (trailer)
	    WriteTrailerDecl(file, trailer_t);    
    }
    fprintf(file, "\t} %s;\n", name);
    fprintf(file, "\n");
}

void
WriteTemplateDeclIn(file, arg)
    FILE *file;
    register argument_t *arg;
{
    (*arg->argKPD_Template)(file, arg, TRUE);
}

void
WriteTemplateDeclOut(file, arg)
    FILE *file;
    register argument_t *arg;
{
    (*arg->argKPD_Template)(file, arg, FALSE);
}

void
WriteTemplateKPD_port(FILE *file, argument_t *arg, boolean_t in)
{
    register ipc_type_t *it = arg->argType;

    fprintf(file, "#if\tUseStaticTemplates\n");
    fprintf(file, "\tstatic %s %s = {\n", it->itUserKPDType, arg->argTTName);

    fprintf(file, "\t\t/* name = */\t\tMACH_PORT_NULL,\n");
    fprintf(file, "\t\t/* pad1 = */\t\t0,\n");
    fprintf(file, "\t\t/* pad2 = */\t\t0,\n");
    fprintf(file, "\t\t/* disp = */\t\t%s,\n",
	in ? it->itInNameStr: it->itOutNameStr);
    fprintf(file, "\t\t/* type = */\t\tMACH_MSG_PORT_DESCRIPTOR,\n");

    fprintf(file, "\t};\n");
    fprintf(file, "#endif\t/* UseStaticTemplates */\n");
}

void
WriteTemplateKPD_ool(FILE *file, argument_t *arg, boolean_t in __unused)
{
    register ipc_type_t *it = arg->argType;

    fprintf(file, "#if\tUseStaticTemplates\n");
    fprintf(file, "\tstatic %s %s = {\n", it->itUserKPDType, arg->argTTName);

    if (IS_MULTIPLE_KPD(it))
	it = it->itElement;

    fprintf(file, "\t\t/* addr = */\t\t(void *)0,\n");
    if (it->itVarArray)
	fprintf(file, "\t\t/* size = */\t\t0,\n");
    else
	fprintf(file, "\t\t/* size = */\t\t%d,\n",
	    (it->itNumber * it->itSize + 7)/8);
    fprintf(file, "\t\t/* deal = */\t\t%s,\n",
	(arg->argDeallocate == d_YES) ? "TRUE" : "FALSE");
    /* the d_MAYBE case will be fixed runtime */
    fprintf(file, "\t\t/* copy = */\t\t%s,\n",
	(arg->argFlags & flPhysicalCopy) ? "MACH_MSG_PHYSICAL_COPY" : "MACH_MSG_VIRTUAL_COPY");
    /* the PHYSICAL COPY flag has not been established yet */
    fprintf(file, "\t\t/* pad2 = */\t\t0,\n");
    fprintf(file, "\t\t/* type = */\t\tMACH_MSG_OOL_DESCRIPTOR,\n");

    fprintf(file, "\t};\n");
    fprintf(file, "#endif\t/* UseStaticTemplates */\n");
}

void
WriteTemplateKPD_oolport(file, arg, in)
    FILE *file;
    argument_t *arg;
    boolean_t in;
{
    register ipc_type_t *it = arg->argType;

    fprintf(file, "#if\tUseStaticTemplates\n");
    fprintf(file, "\tstatic %s %s = {\n", it->itUserKPDType, arg->argTTName);

    if (IS_MULTIPLE_KPD(it))
	it = it->itElement;

    fprintf(file, "\t\t/* addr = */\t\t(void *)0,\n");
    if (!it->itVarArray)
	fprintf(file, "\t\t/* coun = */\t\t%d,\n",
	    it->itNumber);
    else
	fprintf(file, "\t\t/* coun = */\t\t0,\n");
    fprintf(file, "\t\t/* deal = */\t\t%s,\n",
        (arg->argDeallocate == d_YES) ? "TRUE" : "FALSE");
    fprintf(file, "\t\t/* copy is meaningful only in overwrite mode */\n");
    fprintf(file, "\t\t/* copy = */\t\tMACH_MSG_PHYSICAL_COPY,\n");
    fprintf(file, "\t\t/* disp = */\t\t%s,\n",
	in ? it->itInNameStr: it->itOutNameStr);
    fprintf(file, "\t\t/* type = */\t\tMACH_MSG_OOL_PORTS_DESCRIPTOR,\n");

    fprintf(file, "\t};\n");
    fprintf(file, "#endif\t/* UseStaticTemplates */\n");
}

/*
 * Like vfprintf, but omits a leading comment in the format string
 * and skips the items that would be printed by it.  Only %s, %d,
 * and %f are recognized.
 */
static void
SkipVFPrintf(FILE *file, const char *fmt, va_list pvar)
{
    if (*fmt == 0)
	return;	/* degenerate case */

    if (fmt[0] == '/' && fmt[1] == '*') {
	/* Format string begins with C comment.  Scan format
	   string until end-comment delimiter, skipping the
	   items in pvar that the enclosed format items would
	   print. */

	register int c;

	fmt += 2;
	for (;;) {
	    c = *fmt++;
	    if (c == 0)
		return;	/* nothing to format */
	    if (c == '*') {
		if (*fmt == '/') {
		    break;
		}
	    }
	    else if (c == '%') {
		/* Field to skip */
		c = *fmt++;
		switch (c) {
		    case 's':
			(void) va_arg(pvar, char *);
			break;
		    case 'd':
			(void) va_arg(pvar, int);
			break;
		    case 'f':
			(void) va_arg(pvar, double);
			break;
		    case '\0':
			return; /* error - fmt ends with '%' */
		    default:
			break;
		}
	    }
	}
	/* End of comment.  To be pretty, skip
	   the space that follows. */
	fmt++;
	if (*fmt == ' ')
	    fmt++;
    }

    /* Now format the string. */
    (void) vfprintf(file, fmt, pvar);
}

static void
vWriteCopyType(FILE *file, ipc_type_t *it, const char *left, const char *right, va_list pvar)
{
  va_list pv2;
  __va_copy(pv2,pvar);
    if (it->itStruct)
    {
	fprintf(file, "\t");
	(void) SkipVFPrintf(file, left, pvar);
	fprintf(file, " = ");
	(void) SkipVFPrintf(file, right, pv2);
	fprintf(file, ";\n");
    }
    else if (it->itString)
    {
	fprintf(file, "\t(void) mig_strncpy(");
	(void) SkipVFPrintf(file, left, pvar);
	fprintf(file, ", ");
	(void) SkipVFPrintf(file, right, pv2);
	fprintf(file, ", %d);\n", it->itTypeSize);
    }
    else
    {
	fprintf(file, "\t{   typedef struct { char data[%d]; } *sp;\n",
		it->itTypeSize);
	fprintf(file, "\t    * (sp) ");
	(void) SkipVFPrintf(file, left, pvar);
	fprintf(file, " = * (sp) ");
	(void) SkipVFPrintf(file, right, pv2);
	fprintf(file, ";\n\t}\n");
    }
}


/*ARGSUSED*/
/*VARARGS4*/
void
WriteCopyType(FILE *file, ipc_type_t *it, const char *left, const char *right, ...)
{
    va_list pvar;
    va_start(pvar, right);

    vWriteCopyType(file, it, left, right, pvar);

    va_end(pvar);
}


/*ARGSUSED*/
/*VARARGS4*/
void
WriteCopyArg(FILE *file, argument_t *arg, const char *left, const char *right, ...)
{
    va_list pvar;
	va_list pv2;
    va_start(pvar, right);

    {
	ipc_type_t *it = arg->argType;
	if (it->itVarArray && !it->itString) {
	  __va_copy(pv2,pvar);
	    fprintf(file, "\t    (void)memcpy(");
	    (void) SkipVFPrintf(file, left, pvar);
	    fprintf(file, ", ");
	    (void) SkipVFPrintf(file, right, pv2);
	    fprintf(file, ", %s);\n", arg->argCount->argVarName);
	} else
	    vWriteCopyType(file, it, left, right, pvar);
    }

    va_end(pvar);
}


/*
 * Global KPD disciplines 
 */
void
KPD_error(FILE *file __unused, argument_t *arg)
{
    printf("MiG internal error: argument is %s\n", arg->argVarName);
    exit(1);
}

void
KPD_noop(FILE *file __unused, argument_t *arg __unused)
{
}

static void
WriteStringDynArgs(args, mask, InPOutP, str_oolports, str_ool)
    argument_t *args;
    u_int	mask;
    string_t 	InPOutP;
    string_t 	*str_oolports, *str_ool;
{
    argument_t *arg;
    char loc[100], sub[20];
    string_t tmp_str1 = ""; 
    string_t tmp_str2 = "";
    int cnt, multiplier = 1;
    boolean_t test, complex = FALSE;

    for (arg = args; arg != argNULL; arg = arg->argNext) {
	ipc_type_t *it = arg->argType;

	if (IS_MULTIPLE_KPD(it)) {
	    test = it->itVarArray || it->itElement->itVarArray;
	    if (test) {
		multiplier = it->itKPD_Number;
	        it = it->itElement;
	        complex = TRUE;
	    }
	} else
	    test = it->itVarArray;

	cnt = multiplier;
	while (cnt) {
	    if (complex)
		sprintf(sub, "[%d]", multiplier - cnt);
	    if (akCheck(arg->argKind, mask) && 
		it->itPortType && !it->itInLine && test) {
		    sprintf(loc, " + %s->%s%s.count", InPOutP, arg->argMsgField,
		        complex ? sub : "");
		    tmp_str1 = strconcat(tmp_str1, loc);
	    }
	    if (akCheck(arg->argKind, mask) && 
		!it->itInLine && !it->itPortType && test) {
	 	    sprintf(loc, " + %s->%s%s.size", InPOutP, arg->argMsgField,
		        complex ? sub : "");
		    tmp_str2 = strconcat(tmp_str2, loc);
	    }
	    cnt--;
	}
    }
    *str_oolports = tmp_str1;
    *str_ool = tmp_str2;  
}

/*
 * Utilities for Logging Events that happen at the stub level
 */
void
WriteLogMsg(file, rt, where, what)
    FILE *file;
    routine_t *rt;
    int where, what;
{
    string_t ptr_str;
    string_t StringOolPorts = strNULL;
    string_t StringOOL = strNULL;
    u_int ports, oolports, ool;
    string_t event;

    fprintf(file, "\n#if  MIG_DEBUG\n");
    if (where == LOG_USER)
	fprintf(file, "\tLOG_TRACE(MACH_MSG_LOG_USER,\n");
    else
	fprintf(file, "\tLOG_TRACE(MACH_MSG_LOG_SERVER,\n");
    if (where == LOG_USER && what == LOG_REQUEST) {
	ptr_str = "InP";
	event = "MACH_MSG_REQUEST_BEING_SENT";
    } else if (where == LOG_USER && what == LOG_REPLY) {
	ptr_str = "Out0P";
	event = "MACH_MSG_REPLY_BEING_RCVD";
    } else if (where == LOG_SERVER && what == LOG_REQUEST) {
	ptr_str = "In0P";
	event = "MACH_MSG_REQUEST_BEING_RCVD";
    } else {
	ptr_str = "OutP";
	event = "MACH_MSG_REPLY_BEING_SENT";
    }
    WriteStringDynArgs(rt->rtArgs, 
	(what == LOG_REQUEST) ? akbSendKPD : akbReturnKPD, 
	ptr_str, &StringOolPorts, &StringOOL);
    fprintf(file, "\t\t%s,\n", event);
    fprintf(file, "\t\t%s->Head.msgh_id,\n", ptr_str);
    if (where == LOG_USER && what == LOG_REQUEST) {
	if (rt->rtNumRequestVar)
	    fprintf(file, "\t\tmsgh_size,\n");
	else
	    fprintf(file, "\t\tsizeof(Request),\n");
    } else 
	fprintf(file, "\t\t%s->Head.msgh_size,\n", ptr_str);
    if ((what == LOG_REQUEST && rt->rtSimpleRequest == FALSE) ||
	(what == LOG_REPLY && rt->rtSimpleReply == FALSE))
	    fprintf(file, "\t\t%s->msgh_body.msgh_descriptor_count,\n", ptr_str);
    else
	    fprintf(file, "\t\t0, /* Kernel Proc. Data entries */\n");
    if (what == LOG_REQUEST) {
	fprintf(file, "\t\t0, /* RetCode */\n");
	ports = rt->rtCountPortsIn;
        oolports = rt->rtCountOolPortsIn;
	ool = rt->rtCountOolIn;
    } else {
	if (akCheck(rt->rtRetCode->argKind, akbReply))
	    fprintf(file, "\t\t%s->RetCode,\n", ptr_str);
	else
	    fprintf(file, "\t\t0, /* RetCode */\n");
	ports = rt->rtCountPortsOut;
        oolports = rt->rtCountOolPortsOut;
	ool = rt->rtCountOolOut;
    }
    fprintf(file, "\t\t/* Ports */\n");
    fprintf(file, "\t\t%d,\n", ports);
    fprintf(file, "\t\t/* Out-of-Line Ports */\n");
    fprintf(file, "\t\t%d", oolports);
    if (StringOolPorts != strNULL)
	fprintf(file, "%s,\n", StringOolPorts);
    else
	fprintf(file, ",\n");
    fprintf(file, "\t\t/* Out-of-Line Bytes */\n");
    fprintf(file, "\t\t%d", ool);
    if (StringOOL != strNULL)
	fprintf(file, "%s,\n", StringOOL);
    else
	fprintf(file, ",\n");
    fprintf(file, "\t\t__FILE__, __LINE__);\n");
    fprintf(file, "#endif /* MIG_DEBUG */\n\n");
}

void
WriteLogDefines(FILE *file, string_t who)
{
    fprintf(file, "#if  MIG_DEBUG\n");
    fprintf(file, "#define LOG_W_E(X)\tLOG_ERRORS(%s, \\\n", who);
    fprintf(file, "\t\t\tMACH_MSG_ERROR_WHILE_PARSING, (void *)(X), __FILE__, __LINE__)\n");
    fprintf(file, "#else  /* MIG_DEBUG */\n");
    fprintf(file, "#define LOG_W_E(X)\n");
    fprintf(file, "#endif /* MIG_DEBUG */\n");
    fprintf(file, "\n");
}

/* common utility to report errors */
void
WriteReturnMsgError(file, rt, isuser, arg, error)
    FILE *file;
    routine_t *rt;
    boolean_t isuser;
    argument_t *arg;
    string_t error;
{
    char space[MAX_STR_LEN];
    char * string = &space[0];

    if (UseEventLogger && arg != argNULL) 
		sprintf(string, "LOG_W_E(\"%s\"); ", arg->argVarName);
    else
		string = (char *)"";

    fprintf(file, "\t\t{ ");

    if (isuser) {
   	if (! rtMessOnStack(rt))
		fprintf(file, "%s((char *) Mess, sizeof(*Mess)); ", MessFreeRoutine);

        fprintf(file, "%sreturn %s; }\n", string, error);
    }
    else
        fprintf(file, "%sMIG_RETURN_ERROR(OutP, %s); }\n", string, error);
}

/* executed iff elements are defined */
void
WriteCheckTrailerHead(file, rt, isuser)
    FILE *file;
    routine_t *rt;
    boolean_t isuser;
{
    string_t who = (isuser) ? "Out0P" : "In0P";

    fprintf(file, "\tTrailerP = (mach_msg_format_0_trailer_t *)((vm_offset_t)%s +\n", who);
    fprintf(file, "\t\tround_msg(%s->Head.msgh_size));\n", who);
    fprintf(file, "\tif (TrailerP->msgh_trailer_type != MACH_MSG_TRAILER_FORMAT_0)\n");

    WriteReturnMsgError(file, rt, isuser, argNULL, "MIG_TRAILER_ERROR");
    
    fprintf(file, "#if\tTypeCheck\n");
    fprintf(file, "\ttrailer_size = TrailerP->msgh_trailer_size -\n");
    fprintf(file, "\t\tsizeof(mach_msg_trailer_type_t) - sizeof(mach_msg_trailer_size_t);\n");
    fprintf(file, "#endif\t/* TypeCheck */\n");
}

/* executed iff elements are defined */
void
WriteCheckTrailerSize(file, isuser, arg)
    FILE *file;
    boolean_t isuser;
    register argument_t *arg;
{
    fprintf(file, "#if\tTypeCheck\n");
    if (akIdent(arg->argKind) == akeMsgSeqno) {
	fprintf(file, "\tif (trailer_size < sizeof(mach_port_seqno_t))\n");
	WriteReturnMsgError(file, arg->argRoutine, isuser, arg, "MIG_TRAILER_ERROR");
	fprintf(file, "\ttrailer_size -= sizeof(mach_port_seqno_t);\n");
    } else if (akIdent(arg->argKind) == akeSecToken) {
	fprintf(file, "\tif (trailer_size < sizeof(security_token_t))\n");
	WriteReturnMsgError(file, arg->argRoutine, isuser, arg, "MIG_TRAILER_ERROR");
	fprintf(file, "\ttrailer_size -= sizeof(security_token_t);\n");
    }
    fprintf(file, "#endif\t/* TypeCheck */\n");
}

