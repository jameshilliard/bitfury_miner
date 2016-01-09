CC = g++
CFLAGS = -c -g
# -Wall -fexceptions
LDFLAGS = -g

all: release

clean:
	rm -f MinerTest *.o

debug: executable

profile: CFLAGS += -pg
profile: LDFLAGS += -pg
profile: executable

release: CFLAGS += -O3
release: executable

executable: main.o MultiChipMainer.o StringObject.o 
	$(CC) $(LDFLAGS) main.o MultiChipMainer.o StringObject.o -lpthread -lssl -lcrypt -lrt -o MinerTest

.cpp.o:
	$(CC) $(CFLAGS) -std=c++0x $< -o $@

