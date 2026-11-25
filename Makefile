CC     = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra
SRC    = $(wildcard src/*.c)
TEST_SRC = tests/test_routes.c $(filter-out src/main.c src/server.c,$(SRC))
LIBS   = $(if $(filter Windows_NT,$(OS)),-lws2_32,)

metro: $(SRC) src/metro.h
	$(CC) $(CFLAGS) -o metro $(SRC) -lm $(LIBS)

test: $(TEST_SRC) src/metro.h
	$(CC) $(CFLAGS) -Isrc -o test_routes $(TEST_SRC) -lm
	./test_routes

demo: metro
	./metro --demo

gui: metro
	./metro --gui

clean:
	rm -f metro metro.exe test_routes test_routes.exe
