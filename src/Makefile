

IDIR =../include
CC=c++
CFLAGS=-I../include -I/usr/local/include -I/usr/local/Cellar/openssl@1.1/1.1.1j/include -Wno-c++11-extensions

ODIR=../obj
LDIR =../lib

LIBS=-lm -L/usr/local/Cellar/pcre++/0.9.5/lib -L/usr/local/Cellar/openssl@1.1/1.1.1j/lib -lpcre++ -lcrypto

_DEPS = BackupEntry.h BackupCache.h BackupConfig.h util.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = BackupEntry.o BackupCache.o BackupConfig.o util.o managebackups.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))


$(ODIR)/%.o: %.cc $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

managebackups: $(OBJ)
	$(CC) -o ../bin/$@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~

