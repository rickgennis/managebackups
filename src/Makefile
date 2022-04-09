
MAKEFLAGS := -j3

IDIR =../include
CC=c++
CFLAGS=-I../include -I/usr/local/include -I/usr/local/Cellar/openssl@1.1/1.1.1m/include -Wno-c++11-extensions -fno-stack-protector -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-math-errno -fno-unroll-loops -fmerge-all-constants -fno-ident
ODIR=../obj
LDIR =../lib

LIBS=-lm -L/usr/local/Cellar/pcre++/0.9.5/lib -L/usr/local/Cellar/openssl@1.1/1.1.1m/lib -lpcre++ -lcrypto

_DEPS = BackupEntry.h BackupCache.h Setting.h BackupConfig.h ConfigManager.h PipeExec.h util_generic.h notify.h globals.h globalsdef.h statistics.h colors.h help.h setup.h debug.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = BackupEntry.o BackupCache.o Setting.o BackupConfig.o ConfigManager.o PipeExec.o util_generic.o statistics.o notify.o help.o setup.o debug.o managebackups.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
	CFLAGS += -Os
endif

ifeq ($(UNAME_S),Darwin)
	CFLAGS += -Oz
endif

managebackups: $(OBJ)
	$(CC) -std=c++17 -o ../bin/$@ $^ $(CFLAGS) $(LIBS) $(LFLAGS)
	strip ../bin/managebackups

help.base.cc:

help.cc: managebackups.1.md help.base.cc
	@./addman

$(ODIR)/%.o: %.cc $(DEPS)
	$(CC) -c -std=c++17 -o $@ $< $(CFLAGS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~ help.cc

both:
	make clean
	tar -C /Users/rennis -czf /tmp/managebackups.tgz managebackups
	scp /tmp/managebackups.tgz fw:/tmp
	ssh fw "sudo /home/ennis/managebackups/src/package.deb/scripts/prep" &
	make
