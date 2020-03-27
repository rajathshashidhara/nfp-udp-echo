all: libnfpinterpose.so

libnfpinterpose.so: interpose.c ../user/nfpcore/nfp_ioctl.h
	$(CC) -shared -fPIC -Wall -g -o libnfpinterpose.so -I../user/nfpcore interpose.c -ldl

clean:
	rm -rfv libnfpinterpose.so interpose.o