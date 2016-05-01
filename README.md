# pseudoterminal-tools

i use this program for simple remote shell.

eg:
in android, you can run modify init.rc to run `ptwrap` as root, then, run ptunwrap in terminal emulator to get root access

just run `make` to build.

## ptwrap
this program will allocate pts and forward pts master I/O to stdin and stdout

### synopsis
    ptwrap program [arg]...

## ptunwrap
this program will set tty to raw mode and redirect stdin/stdout to `upstream` and `downstream`

### synpsis
    ptunwrap <upstream> <downstream>

# Usage
for ptwrap:

    $ mkfifo up
    $ mkfifo down
    $ ptwrap bash < up > down

you can redirect fifo `up` and `down` to network with `nc`, encrypt it, compress it. just be creative.

for ptunwrap:

    $ ptunwrap up down
