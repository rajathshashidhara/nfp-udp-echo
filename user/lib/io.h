#ifndef _USERSPACE_IO_H
#define _USERSPACE_IO_H

#include <linux/types.h>
#include <rte_io.h>

static inline uint8_t nn_readb(volatile const void *addr)
{
	return rte_read8(addr);
}

static inline void nn_writeb(uint8_t val, volatile void *addr)
{
	rte_write8(val, addr);
}

static inline uint32_t nn_readl(volatile const void *addr)
{
	return rte_read32(addr);
}

static inline void nn_writel(uint32_t val, volatile void *addr)
{
	rte_write32(val, addr);
}

static inline void nn_writew(uint16_t val, volatile void *addr)
{
	rte_write16(val, addr);
}

static inline uint64_t nn_readq(volatile void *addr)
{
	const volatile uint32_t *p = (uint32_t *)addr;
	uint32_t low, high;

	high = nn_readl((volatile const void *)(p + 1));
	low = nn_readl((volatile const void *)p);

	return low + ((uint64_t)high << 32);
}

static inline void nn_writeq(uint64_t val, volatile void *addr)
{
	nn_writel(val >> 32, (volatile char *)addr + 4);
	nn_writel(val, addr);
}

static __always_inline void rep_movs(void *to, const void *from, size_t n)
{
	unsigned long d0, d1, d2;
	asm volatile("rep ; movsl\n\t"
		     "testb $2,%b4\n\t"
		     "je 1f\n\t"
		     "movsw\n"
		     "1:\ttestb $1,%b4\n\t"
		     "je 2f\n\t"
		     "movsb\n"
		     "2:"
		     : "=&c" (d0), "=&D" (d1), "=&S" (d2)
		     : "0" (n / 4), "q" (n), "1" ((long)to), "2" ((long)from)
		     : "memory");
}

extern void memcpy_fromio_relaxed(void *to, const volatile void *from, size_t n);
extern void memcpy_toio_relaxed(volatile void *to, const void* from, size_t n);

#endif /* _USERSPACE_IO_H */
