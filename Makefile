PROGS = ptwrap ptforward
all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm $(PROGS)
