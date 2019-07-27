CC ?= gcc
CFLAGS? ?= "-Wall"
OUTPUT ?= iceFUNprog
PREFIX ?= /usr/local


$(OUTPUT): *.c
	$(CC) $(CFLAGS) -o $(OUTPUT) *.c

install: $(OUTPUT)
	cp $(OUTPUT) $(DESTDIR)$(PREFIX)/bin/

uninstall: $(OUTPUT)
	rm $(DESTDIR)$(PREFIX)/bin/$(OUTPUT)

clean:
	rm -f *.o $(OUTPUT)
