DIR := $(shell pwd)

CFLAGS := -I$(DIR) \
			-I$(DIR)/../nfpcore \
			-I$(DIR)/..

SRCS-LIBS += memzone.c \
		driver.c

OBJS-LIBS := $(SRCS-LIBS:.c=.o)
DEPS-LIBS := $(SRCS-LIBS:.c=.d)

CFLAGS += -g3 -O2 -Wall -Werror -Wno-format-truncation -MD -MP -pthread

LIB := libdriver.a

all: $(LIB)

$(LIB): $(OBJS-LIBS)
	$(AR) rcu $@ $+
	ranlib $@

clean:
	rm -vf $(OBJS-LIBS) $(DEPS-LIBS) $(LIB)

-include $(DEPS-LIBS)

.PHONY: all clean