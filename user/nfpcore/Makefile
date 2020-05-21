DIR := $(shell pwd)

CFLAGS := -I$(DIR)

SRCS-NFPCORE += nfp_cppcore.c \
		nfp_cpp_pcie_ops.c \
		nfp_mutex.c \
		nfp_resource.c \
		nfp_crc.c \
		nfp_mip.c \
		nfp_nffw.c \
		nfp_hwinfo.c \
		nfp_nsp.c \
		nfp_nsp_cmds.c \
		nfp_nsp_eth.c \
		nfp_rtsym.c \
		pci.c \
		nfp_cpp_dev_ops.c \
		nfp_cpp_dev.c

OBJS-NFPCORE := $(SRCS-NFPCORE:.c=.o)
DEPS-NFPCORE := $(SRCS-NFPCORE:.c=.d)

CFLAGS += -g3 -O2 -Wall -Werror -Wno-format-truncation -MD -MP -pthread

LIB := libnfpcore.a

all: $(LIB)

$(LIB): $(OBJS-NFPCORE)
	$(AR) rcu $@ $+
	ranlib $@

clean:
	rm -vf $(OBJS-NFPCORE) $(DEPS-NFPCORE) $(LIB)

-include $(DEPS-NFPCORE)

.PHONY: all clean