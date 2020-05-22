DIR := $(shell pwd)
NFPCOREDIR := $(DIR)/nfpcore
DRIVERDIR := $(DIR)/lib

CFLAGS := -I$(DIR)\
			-I$(NFPCOREDIR)\
			-I$(DRIVERDIR)\
			-I$(DIR)/..

SRCS-MAIN := app_main.c
OBJS-MAIN := $(SRCS-MAIN:.c=.o)
DEPS-MAIN := $(SRCS-MAIN:.c=.d)

APP := nfp-user.out

all: $(APP)

CFLAGS += -g3 -Wall -Werror -Wno-format-truncation -pthread -MD -MP
LDFLAGS := -L$(NFPCOREDIR) -L$(DRIVERDIR)
LDLIBS := -ldriver -lnfpcore -lm -pthread

$(LIBSDIR)

DEPS := $(DEPS-MAIN)
OBJS := $(OBJS-MAIN)

$(APP): $(OBJS)
	$(MAKE) -C nfpcore
	$(MAKE) -C lib
	$(CC) $(LDFLAGS) -o $(APP) $(OBJS) $(LDLIBS)

clean:
	$(MAKE) -C nfpcore clean
	$(MAKE) -C lib clean
	rm -rf $(DEPS) $(OBJS) $(APP) $(LIBS)

-include $(DEPS-MAIN)

.PHONY: all clean