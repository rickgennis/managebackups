

IDIR =../include
CC=c++
CFLAGS=-I../include -I/usr/local/include -I/usr/local/Cellar/openssl@1.1/1.1.1j/include -Wno-c++11-extensions

ODIR=../obj
LDIR =../lib

LIBS=-lm -L/usr/local/Cellar/pcre++/0.9.5/lib -L/usr/local/Cellar/openssl@1.1/1.1.1j/lib -lpcre++ -lcrypto

_DEPS = BackupEntry.h BackupCache.h Setting.h BackupConfig.h ConfigManager.h util.h globals.h statistics.h colors.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = BackupEntry.o BackupCache.o Setting.o BackupConfig.o ConfigManager.o util.o statistics.o managebackups.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))


$(ODIR)/%.o: %.cc $(DEPS)
	$(CC) -c -std=c++17 -o $@ $< $(CFLAGS)

managebackups: $(OBJ)
	$(CC) -std=c++17 -o ../bin/$@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~

