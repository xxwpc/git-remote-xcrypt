SRCS = $(wildcard *.cpp)
OBJS = $(patsubst %.cpp, %.o, $(SRCS))

all: git-remote-xcrypt

%.o : %.cpp common.h
	g++ -std=gnu++23 -O0 -g -c $< -o $@

git-remote-xcrypt: $(OBJS)
	g++ -std=gnu++23 -O0 -g $^ -L/usr/local/lib -lgit2 -lbzip3 -lcrypto -lboost_system -lboost_filesystem -o $@

install: git-remote-xcrypt
	install -C git-remote-xcrypt /usr/bin/



.PHONY: clean

clean:
	rm -fv git-remote-xcrypt *.o
