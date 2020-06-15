DIR := $(shell pwd)
NFPCOREDIR := $(DIR)/nfpcore
DRIVERDIR := $(DIR)/lib

CFLAGS := -I$(DIR)\
			-I$(NFPCOREDIR)\
			-I$(DRIVERDIR)\
			-I$(DIR)/..

SRCS-SP := slowpath_main.c
OBJS-SP := $(SRCS-SP:.c=.o)
DEPS-SP := $(SRCS-SP:.c=.d)

SRCS-APP := app_main.c
OBJS-APP := $(SRCS-APP:.c=.o)
DEPS-APP := $(SRCS-APP:.c=.d)

SLOWPATH := slowpath-nfp.out
APP := nfp-user.out

all: $(SLOWPATH) $(APP)

CFLAGS += -g3 -Wall -Werror -Wno-format-truncation -pthread -MD -MP
LDFLAGS := -L$(NFPCOREDIR) -L$(DRIVERDIR)
LDLIBS := -ldriver -lnfpcore -lm -pthread

DEPS := $(DEPS-SP) $(DEPS-APP)
OBJS := $(OBJS-SP) $(OBJS-APP)

$(SLOWPATH): $(OBJS-SP)
	$(MAKE) -C nfpcore
	$(MAKE) -C lib
	$(CC) $(LDFLAGS) -o $(SLOWPATH) $(OBJS-SP) $(LDLIBS)

$(APP): $(OBJS-APP)
	$(MAKE) -C lib
	$(CC) $(LDFLAGS) -o $(APP) $(OBJS-APP) $(LDLIBS)

clean:
	$(MAKE) -C nfpcore clean
	$(MAKE) -C lib clean
	rm -rf $(DEPS) $(OBJS) $(APP) $(SLOWPATH) $(LIBS)

-include $(DEPS-SP)
-include $(DEPS-APP)

.PHONY: all clean