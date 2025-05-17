### Makefile for Emacs Reader’s dynamic module

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
  CFLAGS += -DWIN32
  LDFLAGS := -shared -L$(MUPDF_DIR)/build/shared-release -lmupdf
  RPATHS :=
  MUPDF_LIB := libmupdf.dll
else ifeq ($(PLATFORM),macos)
  SHARED_EXT := .dylib
  OBJ_EXT := .o
  CC := gcc
  CFLAGS += -DMACOS
  LDFLAGS := -dynamiclib -L$(MUPDF_DIR)/build/shared-release -lmupdf
  RPATHS := -Wl,-rpath,@loader_path/../dep/mupdf/build/shared-release
  MUPDF_LIB := libmupdf.26.dylib
else
  SHARED_EXT := .so
  OBJ_EXT := .o
  CC := gcc
  CFLAGS += -DLINUX
  LDFLAGS := -shared -L$(MUPDF_DIR)/build/shared-release -lmupdf
  RPATHS := -Wl,-rpath,$(MUPDF_DIR)/build/shared-release/
  MUPDF_LIB := libmupdf.so.26.0
endif

# Module filenames and library paths
LIB_NAME := render-core$(SHARED_EXT)
LIBMUPDF := $(MUPDF_DIR)/build/shared-release/$(MUPDF_LIB)

# Compiler and headers
MUPDF_HEADERS := $(MUPDF_DIR)/include/
CFLAGS += -Wall -Wextra -fPIC -I$(MUPDF_HEADERS) -I$(CURDIR)/render/emacs-module.h

# Source files and object targets
SRCS := render/elisp-helpers.c render/mupdf-helpers.c render/render-core.c
OBJS := $(SRCS:%.c=%$(OBJ_EXT))

.PHONY: all clean submodule

# Default build target
all: $(LIB_NAME)

# Link the dynamic module, ensuring libmupdf is built first
$(LIB_NAME): $(OBJS) $(LIBMUPDF)
	$(CC) $(LDFLAGS) $(RPATHS) -o $@ $^

# Update and build mupdf submodule
$(LIBMUPDF):
	git submodule update --init --recursive
	$(MAKE) -C $(MUPDF_DIR) shared USE_SYSTEM_LIBS=no XCFLAGS="-DLCMS2MT_PREFIX=lcms2mt_"

# Compile C sources into platform-specific object files
# Pattern-rule automatically adapts to OBJ_EXT
%$(OBJ_EXT): %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean artifacts
clean:
	rm -f $(OBJS) $(LIB_NAME)
