# This makefile was generated Tue Jun 30 14:15:18 2020

CC=cc

hed5 : hed5.o die.o quit.o
	$(CC) hed5.o die.o quit.o -o hed5 -lcurses

hed5.o : hed5.c
	$(CC) -c $(CFLAGS) hed5.c

quit.o : quit.c
	$(CC) -c quit.c

die.o : die.c
	$(CC) -c die.c
