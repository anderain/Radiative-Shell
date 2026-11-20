# ===================================================================
#  RadiativeShell - MinGW / TDM-GCC (Win32 desktop, i686)
#
#    make           -> rshell.exe    (release, -O2)
#    make debug     -> rshell-d.exe  (debug,   -O0 -g)
#    make run       -> build and launch rshell.exe
#    make clean
#
#  Stores objects in obj/<config>/ so release/debug can coexist.
# ===================================================================

SHELL := cmd.exe
.SUFFIXES:

BUILD  ?= release
SRCDIR := ../frontend
OBJDIR := obj/$(BUILD)

ifeq ($(BUILD),debug)
TARGET := rshell-d.exe
OPT    := -O0 -g
DEFS   := -DDEBUG
else
TARGET := rshell.exe
OPT    := -O2
DEFS   := -DNDEBUG
endif

CC      := gcc
WINDRES := windres

# Make runs through cmd.exe, so paths must use backslashes.
MKDIR = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"

CPPFLAGS := -I$(SRCDIR)
CFLAGS   := $(OPT) $(DEFS) -Wall -fno-common -MMD -MP
LDFLAGS  := -mwindows
LDLIBS   := -lcomdlg32 -lcomctl32 -lgdi32 -luser32 -lkernel32

# Every .c under frontend/ is part of the build, no list to maintain.
SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
RES  := $(OBJDIR)/resource.res

.PHONY: all debug release run clean

all: $(TARGET)

debug:
	$(MAKE) BUILD=debug

release:
	$(MAKE) BUILD=release

$(TARGET): $(OBJS) $(RES)
	$(CC) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# -O coff is required: windres defaults to a .res file that gcc cannot link.
$(RES): $(SRCDIR)/resource.rc $(SRCDIR)/resource.h $(SRCDIR)/app.ico | $(OBJDIR)
	$(WINDRES) -I$(SRCDIR) -O coff -i $< -o $@

$(OBJDIR):
	@$(call MKDIR,$@)

-include $(DEPS)

run: $(TARGET)
	@start "" $(TARGET)

clean:
	@if exist obj rmdir /s /q obj
	@if exist rshell.exe del /q rshell.exe
	@if exist rshell-d.exe del /q rshell-d.exe
