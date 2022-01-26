

IDIR =../include
CC=c++
CFLAGS=-I../include -I/usr/local/include -I/usr/local/Cellar/openssl@1.1/1.1.1m/include -Wno-c++11-extensions

ODIR=../obj
LDIR =../lib

LIBS=-lm -L/usr/local/Cellar/pcre++/0.9.5/lib -L/usr/local/Cellar/openssl@1.1/1.1.1m/lib -lpcre++ -lcrypto

_DEPS = BackupEntry.h BackupCache.h Setting.h BackupConfig.h ConfigManager.h PipeExec.h util_generic.h notify.h globals.h globalsdef.h statistics.h colors.h help.h setup.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = BackupEntry.o BackupCache.o Setting.o BackupConfig.o ConfigManager.o PipeExec.o util_generic.o statistics.o notify.o help.o setup.o managebackups.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

managebackups: $(OBJ)
	$(CC) -std=c++17 -o ../bin/$@ $^ $(CFLAGS) $(LIBS) $(LFLAGS)

$(ODIR)/%.o: %.cc $(DEPS)
	$(CC) -c -std=c++17 -o $@ $< $(CFLAGS) $(MAKEFLAGS) 

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~

tight: CFLAGS += -fno-stack-protector -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-math-errno -fno-unroll-loops -fmerge-all-constants -fno-ident -Oz
tight: managebackups
tight: 
	strip ../bin/managebackups
