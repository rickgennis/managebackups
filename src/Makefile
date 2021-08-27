

#managebackups: main.cc BackupCache.cc BackupEntry.cc
#	c++ -Wno-c++11-extensions main.cc BackupCache.cc BackupEntry.cc -o managebackups

IDIR =../include
CC=c++
CFLAGS=-I$(IDIR) -Wno-c++11-extensions

ODIR=../obj
LDIR =../lib

LIBS=-lm

_DEPS = BackupEntry.h BackupCache.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = BackupEntry.o BackupCache.o managebackups.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))


$(ODIR)/%.o: %.cc $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

managebackups: $(OBJ)
	$(CC) -o ../bin/$@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~

