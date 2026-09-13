#include <bits/syscall.h>

extern "C" long __do_syscall_ret(unsigned long ret) {
	return (long)ret;
}

using sc_word_t = long;

/*
 * muxOS syscall ABI: int $0x80, number in eax, args 1..5 in
 * ebx/ecx/edx/esi/edi.  We deliberately do not touch ebp (the kernel accepts
 * a 6th argument there, but nothing we need uses it and clobbering ebp breaks
 * frame-pointer builds).
 */
static inline sc_word_t muxos_syscall(long sc, sc_word_t a1, sc_word_t a2,
		sc_word_t a3, sc_word_t a4, sc_word_t a5) {
	long ret;
	register long r_ax asm("eax") = sc;
	register long r_bx asm("ebx") = a1;
	register long r_cx asm("ecx") = a2;
	register long r_dx asm("edx") = a3;
	register long r_si asm("esi") = a4;
	register long r_di asm("edi") = a5;
	asm volatile("int $0x80"
			: "=a"(ret)
			: "a"(r_ax), "b"(r_bx), "c"(r_cx), "d"(r_dx), "S"(r_si), "D"(r_di)
			: "memory");
	return (sc_word_t)ret;
}

sc_word_t __do_syscall0(long sc) {
	return muxos_syscall(sc, 0, 0, 0, 0, 0);
}

sc_word_t __do_syscall1(long sc, sc_word_t a1) {
	return muxos_syscall(sc, a1, 0, 0, 0, 0);
}

sc_word_t __do_syscall2(long sc, sc_word_t a1, sc_word_t a2) {
	return muxos_syscall(sc, a1, a2, 0, 0, 0);
}

sc_word_t __do_syscall3(long sc, sc_word_t a1, sc_word_t a2, sc_word_t a3) {
	return muxos_syscall(sc, a1, a2, a3, 0, 0);
}

sc_word_t __do_syscall4(long sc, sc_word_t a1, sc_word_t a2, sc_word_t a3,
		sc_word_t a4) {
	return muxos_syscall(sc, a1, a2, a3, a4, 0);
}

sc_word_t __do_syscall5(long sc, sc_word_t a1, sc_word_t a2, sc_word_t a3,
		sc_word_t a4, sc_word_t a5) {
	return muxos_syscall(sc, a1, a2, a3, a4, a5);
}

sc_word_t __do_syscall6(long sc, sc_word_t a1, sc_word_t a2, sc_word_t a3,
		sc_word_t a4, sc_word_t a5, sc_word_t) {
	return muxos_syscall(sc, a1, a2, a3, a4, a5);
}

sc_word_t __do_syscall7(long sc, sc_word_t a1, sc_word_t a2, sc_word_t a3,
		sc_word_t a4, sc_word_t a5, sc_word_t) {
	return muxos_syscall(sc, a1, a2, a3, a4, a5);
}
