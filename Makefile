CC=gcc
WARN=-Wall
CFLAGS=-O0 -march=native -ggdb3 $(WARN)

OBJS=futex.o mythread.o
LIB=mythread.a
TEST_OBJS=test.o testmythread.o testpthread.o

all: $(TEST_OBJS) $(LIB)
	$(CC) -o test test.o $(LIB)
	$(CC) -o testmythread testmythread.o $(LIB)
	$(CC) -o testpthread testpthread.o -pthread

$(LIB): $(OBJS)
	ar rcs $(LIB) $(OBJS)

clean:
	rm -f $(OBJS) $(TEST_OBJS) test testmythread testpthread $(LIB)
