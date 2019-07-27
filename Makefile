CC=gcc
CFLAGS="-Wall"
OUTPUT=iceFUNprog


$(OUTPUT): *.c
	$(CC) $(CFLAGS) -o $(OUTPUT) *.c

install: $(OUTPUT)
	cp $(OUTPUT) /usr/local/bin/

uninstall: $(OUTPUT)
	rm /usr/local/bin/$(OUTPUT)

clean:
	rm -f *.o $(OUTPUT)
