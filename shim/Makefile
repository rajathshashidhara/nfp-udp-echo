all: libnfpinterpose.so

libnfpinterpose.so: interpose.o
	$(CC) -shared -o libnfpinterpose.so -lm -lrt -lpthread interpose.o

interpose.o: interpose.c nfp_ioctl.h
	$(CC) -c -Wall -g -fpic -o interpose.o interpose.c

clean:
	rm -rfv libnfpinterpose.so interpose.o