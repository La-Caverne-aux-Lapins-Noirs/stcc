CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS ?=
WARN = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wno-misleading-indentation
PREFIX = /usr/local
DATADIR = $(PREFIX)/share/stcc
SRC = src/main.c src/util.c src/object.c src/asm.c src/pp.c src/lex.c src/type.c src/c.c src/gen.c
OBJ = $(SRC:.c=.o)

all: stcc
stcc: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)
src/%.o: src/%.c src/stcc.h src/c.h
	$(CC) $(CPPFLAGS) -DSTCC_DATADIR='"$(DATADIR)"' $(CFLAGS) $(WARN) -std=c99 -c -o $@ $<
test: stcc
	python3 tests/run.py
	python3 tests/cli.py
	python3 tests/function_modes.py
examples: stcc
	python3 tests/build_examples.py
install: stcc
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(DATADIR)/runtime $(DESTDIR)$(DATADIR)/include $(DESTDIR)$(DATADIR)/docs $(DESTDIR)$(PREFIX)/share/man/man1
	install -m755 stcc $(DESTDIR)$(PREFIX)/bin/stcc
	install -m644 runtime/*.s $(DESTDIR)$(DATADIR)/runtime/
	install -m644 include/*.h $(DESTDIR)$(DATADIR)/include/
	install -m644 README.md LICENSE docs/*.md $(DESTDIR)$(DATADIR)/docs/
	install -m644 docs/stcc.1 $(DESTDIR)$(PREFIX)/share/man/man1/
deb: all test
	python3 scripts/build_deb.py
clean:
	rm -f stcc src/*.o
	rm -rf tests/out examples/bin
.PHONY: all clean test install examples deb
