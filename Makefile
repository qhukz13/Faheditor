CC = gcc
CFLAGS = -Wall -Wextra -std=c99
TARGET = qzeditor
PREFIX = /usr/local/bin
all: $(TARGET)
$(TARGET): qz.c
	$(CC) $(CFLAGS) -o $(TARGET) qz.c
debug: qz.c
	$(CC) $(CFLAGS) -g -O0 -fsanitize=address,undefined -o $(TARGET)-debug qz.c
install: $(TARGET)
	sudo install -m 755 $(TARGET) $(PREFIX)/$(TARGET)
clean:
	rm -f $(TARGET) $(TARGET)-debug
.PHONY: all debug install clean
