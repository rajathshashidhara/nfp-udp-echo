DIR := $(shell pwd)
NFPCOREDIR := $(DIR)/nfpcore
DRIVERDIR := $(DIR)/lib

SRCS-MAIN := app_main.c
OBJS-MAIN := $(SRCS-MAIN:.c=.o)
DEPS-MAIN := $(SRCS-MAIN:.c=.d)

APP := nfp-user

all: $(APP)

CFLAGS := -O3 -g -Wall -Werror -Wno-format-truncation -I$(DIR) -I$(DIR)/lib/ -I$(DIR)/nfpcore/ -MMD -MP
LDLIBS := -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

-include $(DEPS-MAIN)

DEPS := $(DEPS-MAIN)
OBJS := $(OBJS-MAIN)

include $(DRIVERDIR)/Makefile
include $(NFPCOREDIR)/Makefile

$(APP): $(OBJS)
	$(CC) -o $(APP) $(OBJS) $(LDLIBS)

clean:
	rm -rf $(DEPS) $(OBJS) $(APP) $(LIBS)
