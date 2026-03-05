CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lsqlite3 -lpthread

SRCS = server.c db_utils.c
OBJS = $(SRCS:.c=.o)
TARGET = randvu_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
