# ===================================================================
#  rshl-config - MinGW / TDM-GCC build (Win32 desktop, i686)
#
#    make           -> rshlconfig.exe    (release, -O2)
#    make debug     -> rshlconfig-d.exe  (debug,   -O0 -g)
#    make clean
#
#  Stores objects in obj/<config>/ so release/debug can coexist.
#  The shared source and resources live one directory up.
# ===================================================================

SHELL := cmd.exe
.SUFFIXES:

BUILD  ?= release
SRCDIRS := .. ../../../config
OBJDIR := obj/$(BUILD)

ifeq ($(BUILD),debug)
TARGET := rshlconfig-d.exe
OPT    := -O0 -g
DEFS   := -DDEBUG
else
TARGET := rshlconfig.exe
OPT    := -O2
DEFS   := -DNDEBUG
endif

CC      := gcc
WINDRES := windres

# Make runs through cmd.exe, so paths must use backslashes.
MKDIR = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"

CPPFLAGS := $(addprefix -I,$(SRCDIRS))
CFLAGS   := $(OPT) $(DEFS) -Wall -fno-common -MMD -MP
LDFLAGS  := -mwindows
LDLIBS   := -lcomdlg32 -lcomctl32 -lgdi32 -luser32 -lkernel32

# Every .c under the source directories is part of the build, no list to keep.
# vpath lets one pattern rule find files in either directory.
vpath %.c $(SRCDIRS)
SRCS := $(foreach d,$(SRCDIRS),$(wildcard $(d)/*.c))
OBJS := $(addprefix $(OBJDIR)/,$(notdir $(SRCS:.c=.o)))
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

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# -O coff is required: windres defaults to a .res file that gcc cannot link.
$(RES): $(word 1,$(SRCDIRS))/resource.rc $(word 1,$(SRCDIRS))/resource.h $(word 1,$(SRCDIRS))/app_config.ICO | $(OBJDIR)
	$(WINDRES) -I$(word 1,$(SRCDIRS)) -O coff -i $< -o $@

$(OBJDIR):
	@$(call MKDIR,$@)

-include $(DEPS)

run: $(TARGET)
	@start "" $(TARGET)

clean:
	@if exist obj rmdir /s /q obj
	@if exist rshlconfig.exe del /q rshlconfig.exe
	@if exist rshlconfig-d.exe del /q rshlconfig-d.exe
