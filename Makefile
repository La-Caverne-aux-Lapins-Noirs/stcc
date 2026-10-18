CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS ?=
WARN = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wno-misleading-indentation
PREFIX = /usr/local
DATADIR = $(PREFIX)/share/stcc
SRC = src/main.c src/util.c src/object.c src/asm.c src/pp.c src/lex.c src/type.c src/c.c src/gen.c
OBJ = $(SRC:.c=.o)

all: stcc stmkdisk
stcc: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)
stmkdisk: src/mkdisk.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/mkdisk.o
src/%.o: src/%.c src/stcc.h src/c.h
	$(CC) $(CPPFLAGS) -DSTCC_DATADIR='"$(DATADIR)"' $(CFLAGS) $(WARN) -std=c99 -c -o $@ $<
test: all
	python3 tests/run.py
	python3 tests/cli.py
	python3 tests/function_modes.py
	python3 tests/registers.py
	python3 tests/arguments.py
	python3 tests/rom.py
	python3 tests/mkdisk.py
	python3 tests/build_system.py
examples: stcc
	python3 tests/build_examples.py
rom: stcc
	mkdir -p examples/bin
	./stcc --rom examples/15_rom/main.c -o examples/bin/15ROM.IMG --map examples/bin/15ROM.map
install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(DATADIR)/runtime $(DESTDIR)$(DATADIR)/include $(DESTDIR)$(DATADIR)/docs $(DESTDIR)$(PREFIX)/share/man/man1
	install -m755 stcc stmkdisk $(DESTDIR)$(PREFIX)/bin/
	install -m644 runtime/*.s $(DESTDIR)$(DATADIR)/runtime/
	install -m644 runtime/*.c $(DESTDIR)$(DATADIR)/runtime/
	install -m644 include/*.h $(DESTDIR)$(DATADIR)/include/
	install -m644 README.md LICENSE docs/*.md $(DESTDIR)$(DATADIR)/docs/
	install -m644 docs/stcc.1 $(DESTDIR)$(PREFIX)/share/man/man1/
	install -m644 docs/stmkdisk.1 $(DESTDIR)$(PREFIX)/share/man/man1/
	@if test -d tos; then \
		install -d "$(DESTDIR)$(DATADIR)/tos" && \
		cp -RL tos/. "$(DESTDIR)$(DATADIR)/tos/" && \
		find "$(DESTDIR)$(DATADIR)/tos" -type d -exec chmod 755 {} + && \
		find "$(DESTDIR)$(DATADIR)/tos" -type f -exec chmod 644 {} +; \
	fi
deb: all test
	python3 scripts/build_deb.py
clean:
	rm -f src/*.o stcc-asan stmkdisk-asan *.log *.tmp *.tmp.* *.pyc *.pyo
	rm -rf tests/out validation __pycache__
	find src tests scripts examples -type d -name __pycache__ -prune -exec rm -rf {} +
	find src tests scripts examples -type f \( -name '*.pyc' -o -name '*.pyo' -o -name '*.log' -o -name '*.tmp' -o -name '*.tmp.*' \) -delete
fclean: clean
	rm -f stcc stmkdisk
	rm -rf examples/bin dist
.PHONY: all clean fclean test install examples rom deb
