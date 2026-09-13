/* i386 process entry point for muxOS.
 *
 * The kernel enters here with the initial stack laid out the usual way:
 *   [esp+0] argc, [esp+4] argv[], ... We pass that stack pointer (as the
 * "entry stack") plus the address of main() to __mlibc_entry. */
.section .text
.global _start
.type _start, @function
_start:
	xorl %ebp, %ebp
	movl %esp, %eax
	pushl $main
	pushl %eax
	call __mlibc_entry
	hlt
.size _start, . - _start

/* Normally emitted by crtbegin.o; mlibc/crt references it. */
.section .data
.global __dso_handle
.hidden __dso_handle
.type __dso_handle, @object
.size __dso_handle, 4
__dso_handle:
	.long 0

.section .note.GNU-stack,"",%progbits
