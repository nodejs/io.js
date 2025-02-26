CC?=cc
CFLAGS := -O $(CFLAGS) -I../..

UNZ_OBJS = miniunz.o unzip.o ioapi.o ../../libz.a
ZIP_OBJS = minizip.o zip.o   ioapi.o ../../libz.a

.c.o:
	$(CC) -c $(CFLAGS) $*.c

all: miniunz minizip

miniunz.o: miniunz.c unzip.h iowin32.h
minizip.o: minizip.c zip.h iowin32.h ints.h
unzip.o: unzip.c unzip.h crypt.h
zip.o: zip.c zip.h crypt.h skipset.h ints.h
ioapi.o: ioapi.c ioapi.h ints.h
iowin32.o: iowin32.c iowin32.h ioapi.h
mztools.o: mztools.c unzip.h

miniunz: $(UNZ_OBJS)
	$(CC) $(CFLAGS) -o $@ $(UNZ_OBJS)

minizip: $(ZIP_OBJS)
	$(CC) $(CFLAGS) -o $@ $(ZIP_OBJS)

test: miniunz minizip
	@rm -f test.*
	@echo hello hello hello > test.txt
	./minizip test test.txt
	./miniunz -l test.zip
	@mv test.txt test.old
	./miniunz test.zip
	@cmp test.txt test.old
	@rm -f test.*

clean:
	/bin/rm -f *.o *~ minizip miniunz test.*
