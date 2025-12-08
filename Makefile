### Makefile for Emacs Reader’s dynamic module
##
## Copyright (C) 2025 Divya Ranjan Pattanaik
## Copyright (C) 2025 prom
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Detect platforms
OS_NAME := $(shell uname)
HAVE_GUIX := $(shell command -v guix >/dev/null 2>&1 && echo yes || echo no)
HAVE_NIX  := $(shell command -v nix >/dev/null 2>&1 && echo yes || echo no)
HAVE_DNF  := $(shell command -v dnf >/dev/null 2>&1 && echo yes || echo no)
ifneq ($(filter MINGW%, $(OS_NAME)),)
  HAVE_MINGW64 := yes
else
  HAVE_MINGW64 := no
endif
USE_PKGCONFIG := yes

ifeq ($(HAVE_GUIX),yes)
  $(info Guix detected: skipping pkg-config checks.)
  USE_PKGCONFIG := no
else ifeq ($(HAVE_NIX),yes)
  $(info Nix detected: skipping pkg-config checks.)
  USE_PKGCONFIG := no
else ifeq ($(HAVE_DNF),yes)
  $(info dnf detected: skipping pkg-config checks.)
  USE_PKGCONFIG := no
else ifeq ($(OS_NAME),Darwin)
  $(info macOS detected: skipping pkg-config and using Homebrew for MuPDF paths.)
  USE_PKGCONFIG := no
else ifeq ($(HAVE_MINGW64), yes)
  $(info MinGW64 detected: assuming dll is located in $(MINGW_PREFIX)/bin.)
endif

ifeq ($(OS_NAME),Darwin)
  SO := dylib
else ifeq ($(HAVE_MINGW64),yes)
  SO := dll
else
  SO := so
endif

# Compiler and base flags
CC := gcc
CFLAGS += -Wall -Wextra -fPIC
LDFLAGS :=

ifeq ($(OS_NAME),Darwin)
  CFLAGS += -DMACOS
  LDFLAGS += -dynamiclib
else ifeq ($(HAVE_MINGW64),yes)
  CFLAGS += -DWIN32 -pthread
  LDFLAGS += -shared -lpthread -L$$MINGW_PREFIX/bin
else
  CFLAGS += -DLINUX -pthread
  LDFLAGS += -shared -lpthread
endif

# Required MuPDF version
REQUIRED_VERSION := 1.26.0

# --- MuPDF detection and flags ---
ifeq ($(USE_PKGCONFIG),yes)
  PKG_EXISTS := $(shell pkg-config --exists mupdf && echo yes || echo no)
  ifeq ($(PKG_EXISTS),no)
    $(error "MuPDF not found via pkg-config. Please install it or set PKG_CONFIG_PATH.")
  endif

  MUPDF_VERSION := $(shell pkg-config --modversion mupdf)
  VER_OK := $(shell [ "$$(printf '%s\n' $(REQUIRED_VERSION) $(MUPDF_VERSION) | sort -V | head -n1)" = "$(REQUIRED_VERSION)" ] && echo yes || echo no)
  ifeq ($(VER_OK),no)
    $(error "MuPDF version $(MUPDF_VERSION) too old. Require ≥ $(REQUIRED_VERSION).")
  endif

  CFLAGS += $(shell pkg-config --cflags mupdf)
  LDFLAGS += $(shell pkg-config --libs mupdf)
else ifeq ($(OS_NAME),Darwin)
  # macOS manual detection via brew
  BREW_PREFIX := $(shell brew --prefix mupdf 2>/dev/null)
  ifeq ($(BREW_PREFIX),)
    $(error "MuPDF not found via Homebrew. Run: brew install mupdf")
  endif
  MUPDF_VERSION := $(shell grep -o '[0-9][^"]*' $(BREW_PREFIX)/include/mupdf/fitz/version.h | head -n1)
  VER_OK := $(shell [ "$$(printf '%s\n' $(REQUIRED_VERSION) $(MUPDF_VERSION) | sort -V | head -n1)" = "$(REQUIRED_VERSION)" ] && echo yes || echo no)
  ifeq ($(VER_OK),no)
    $(error "MuPDF version $(MUPDF_VERSION) too old. Require ≥ $(REQUIRED_VERSION).")
  endif
  CFLAGS += -I$(BREW_PREFIX)/include
  LDFLAGS += -L$(BREW_PREFIX)/lib -lmupdf
else
  # Generic fallback for Guix/Nix
  CFLAGS += -DMUPDF_NO_PKGCONFIG
  LDFLAGS += -lmupdf
endif

# --- Build Rules ---
LIB_NAME := render-core.$(SO)
SRCS := render/elisp-helpers.c render/mupdf-helpers.c render/render-threads.c render/render-core.c render/render-theme.c
OBJS := $(SRCS:%.c=%.o)

.PHONY: all clean

all: $(LIB_NAME)

$(LIB_NAME): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB_NAME)

autoloads:
	emacs --batch --eval '(loaddefs-generate default-directory (file-name-concat default-directory "reader-autoloads.el"))'
