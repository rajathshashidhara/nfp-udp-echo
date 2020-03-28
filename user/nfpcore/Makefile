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

%.o: $(NFPCOREDIR)/%.c
	$(CC) $(CFLAGS) -c $^ -o $@

DEPS += $(DEPS-NFPCORE)
OBJS += $(OBJS-NFPCORE)