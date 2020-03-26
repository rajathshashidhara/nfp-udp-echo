SRCS-LIBS += memzone.c \
		driver.c

OBJS-LIBS := $(SRCS-LIBS:.c=.o)
DEPS-LIBS := $(SRCS-LIBS:.c=.d)

%.o: $(DRIVERDIR)/%.c
	$(CC) $(CFLAGS) -c $^ -o $@

DEPS += $(DEPS-LIBS)
OBJS += $(OBJS-LIBS)