PROGS = ptwrap ptunwrap ptalloc
all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS)

.PHONY: clean
