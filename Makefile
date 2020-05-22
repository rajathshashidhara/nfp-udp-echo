SUBDIRS = shim user firmware

.PHONY: subdirs $(SUBDIRS) all clean

all: subdirs

subdirs: $(SUBDIRS)

$(SUBDIRS):
		$(MAKE) -C $@

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
