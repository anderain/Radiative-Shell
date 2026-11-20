# ===================================================================
#  sprite - 24 bit BMP to packed 2 bpp array converter
#
#    make          -> sprite.exe
#    make clean
#
#  Built with TDM-GCC, same toolchain as the desktop shell.
# ===================================================================

SHELL := cmd.exe
.SUFFIXES:

CC     := gcc
CFLAGS := -O2 -Wall -Wextra -std=gnu89
TARGET := sprite.exe
SRC    := sprite.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

clean:
	@if exist $(TARGET) del /q $(TARGET)
