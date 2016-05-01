PROGS = ptwrap ptunwrap
all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm $(PROGS)
