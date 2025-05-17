### Cross-platform Makefile for Emacs Reader’s dynamic module

# Detect platform
OS_NAME := $(shell uname)
ifeq ($(OS),Windows_NT)
  PLATFORM := windows
else ifeq ($(OS_NAME),Darwin)
  PLATFORM := macos
else
  PLATFORM := linux
endif

# Directories and submodule paths
SUBMODULE_DIR := $(CURDIR)/dep
MUPDF_DIR := $(SUBMODULE_DIR)/mupdf

# Shared library extension and object file extension per platform
ifeq ($(PLATFORM),windows)
  SHARED_EXT := .dll
  OBJ_EXT := .obj
  CC := gcc
  CFLAGS := -DWIN32 -msse4.1 -Wall -Wextra -fPIC
  LDFLAGS := -shared -L$(MUPDF_DIR)/build/shared-release -lmupdf
  RPATHS :=
  MUPDF_LIB := libmupdf.dll
  NEED_MUPDF_BUILD := yes
  MUPDF_SUBFLAGS := -msse4.1
else ifeq ($(PLATFORM),macos)
  SHARED_EXT := .dylib
  OBJ_EXT := .o
  CC := gcc
  HOME_BREW_PREFIX := $(shell brew --prefix)
  CFLAGS := -DMACOS -Wall -Wextra -fPIC
  LDFLAGS := -dynamiclib -L$(HOME_BREW_PREFIX)/lib -lmupdf
  RPATHS := -Wl,-rpath,@loader_path/../lib
  MUPDF_LIB := $(HOME_BREW_PREFIX)/lib/libmupdf.26.dylib
  NEED_MUPDF_BUILD := no
  MUPDF_SUBFLAGS :=
else
  SHARED_EXT := .so
  OBJ_EXT := .o
  CC := gcc
  CFLAGS := -DLINUX -Wall -Wextra -fPIC
  LDFLAGS := -shared -L$(MUPDF_DIR)/build/shared-release -lmupdf
  RPATHS := -Wl,-rpath,$(MUPDF_DIR)/build/shared-release/
  MUPDF_LIB := $(MUPDF_DIR)/build/shared-release/libmupdf.so.26.0
  NEED_MUPDF_BUILD := yes
  MUPDF_SUBFLAGS :=
endif

# Module filenames and library paths
LIB_NAME := render-core$(SHARED_EXT)
LIBMUPDF := $(MUPDF_LIB)

# Include paths
MUPDF_HEADERS := -I$(MUPDF_DIR)/include
EMACS_HEADERS := -I$(CURDIR)/render
CFLAGS += $(MUPDF_HEADERS) $(EMACS_HEADERS)

# Source files and object targets
SRCS := render/elisp-helpers.c render/mupdf-helpers.c render/render-core.c
OBJS := $(SRCS:%.c=%$(OBJ_EXT))

.PHONY: all clean submodule

# Default build target
all: $(LIB_NAME)

# Link the dynamic module
ifeq ($(NEED_MUPDF_BUILD),yes)
$(LIB_NAME): $(OBJS) $(LIBMUPDF)
else
$(LIB_NAME): $(OBJS)
endif
	$(CC) $(LDFLAGS) $(RPATHS) -o $@ $^

# Update and build mupdf submodule if needed
ifeq ($(NEED_MUPDF_BUILD),yes)
$(LIBMUPDF):
	git submodule update --init --recursive
	cd $(MUPDF_DIR) && \
	$(MAKE) shared USE_SYSTEM_LIBS=no \
	  CFLAGS="$(MUPDF_SUBFLAGS)" XCFLAGS="$(MUPDF_SUBFLAGS)"
endif

# Compile C sources into platform-specific object files
%$(OBJ_EXT): %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean artifacts
clean:
	rm -f $(OBJS) $(LIB_NAME)
