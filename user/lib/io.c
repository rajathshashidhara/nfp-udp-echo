#include "io.h"

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

#define movs(type,to,from) \
	asm volatile("movs" type:"=&D" (to), "=&S" (from):"0" (to), "1" (from):"memory")

void memcpy_fromio_relaxed(void *to, const volatile void *from, size_t n)
{
	if (unlikely(!n))
		return;

	/* Align any unaligned source IO */
	if (unlikely(1 & (unsigned long)from)) {
		movs("b", to, from);
		n--;
	}
	if (n > 1 && unlikely(2 & (unsigned long)from)) {
		movs("w", to, from);
		n-=2;
	}
	rep_movs(to, (const void *)from, n);
}

void memcpy_toio_relaxed(volatile void *to, const void *from, size_t n)
{
	if (unlikely(!n))
		return;

	/* Align any unaligned destination IO */
	if (unlikely(1 & (unsigned long)to)) {
		movs("b", to, from);
		n--;
	}
	if (n > 1 && unlikely(2 & (unsigned long)to)) {
		movs("w", to, from);
		n-=2;
	}
	rep_movs((void *)to, (const void *) from, n);
}
