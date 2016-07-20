PROGS = ptwrap ptunwrap
all: $(PROGS)

%: %.c fail.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS)

.PHONY: clean
