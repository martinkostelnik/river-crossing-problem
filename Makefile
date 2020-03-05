CC = gcc
CFLAGS = -std=gnu99 -Wall -Wextra -Werror -pedantic
LDFLAGS = -lrt -pthread

EXECUTABLE = proj2


OBJS = proj2.o

.PHONY: all clean leaks run

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJS)
	$(CC) $(CFLAGS) -o proj2 proj2.c $(LDFLAGS)

leaks: $(EXECUTABLE)
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./$(EXECUTABLE) 20 20 20 20 20 20 $(CMDLINE)
	
clean:
	rm -f $(EXECUTABLE) *.o *.out

run:
	./proj2 6 0 0 200 200 5
