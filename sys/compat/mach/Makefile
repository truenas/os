# Makefile for syscall tables
#
# $FreeBSD$

all:
	@echo "make sysent only"

sysent:  mach_sysent.c mach_syscall.h mach_proto.h mach_syscalls.c mach_systrace_args.c

mach_sysent.c mach_syscall.h mach_proto.h mach_syscalls.c mach_systrace_args.c: \
		../../kern/makesyscalls.sh syscalls.master syscalls.conf
	-mv -f mach_sysent.c mach_sysent.c.bak
	-mv -f mach_syscall.h mach_syscall.h.bak
	-mv -f mach_proto.h mach_proto.h.bak
	-mv -f mach_syscalls.c mach_syscalls.c.bak
	-mv -f mach_systrace_args.c mach_systrace_args.c.bak
	sh ../../kern/makesyscalls.sh syscalls.master syscalls.conf
