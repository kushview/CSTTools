# $Id: Makefile.distrib,v 1.1 2007/02/19 21:10:37 rip Exp $ 
#
# need to modify this variable to point to where your tiff include files are
#
CINCLUDE	=-I/usr/local/include -I. 
CFLAGS  	= -DHAVE_UNISTD_H -g -O2 -Wall -W 
LDFLAGS 	= -ltiff -lm 

PROGS		= toXYZ tiffdiff tiffhist

all: $(PROGS)

clean:
	rm $(PROGS)

.c.o:
	$(CC) $(CINCLUDE) $(CFLAGS) $(LDFLAGS) -o $<  $*.c
