CC ?= gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS = -lsqlite3 -ldl

TARGET = mops
SRC_DIR = src
OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

.PHONY: all clean install directories dev deb

all: directories $(TARGET)

dev: CFLAGS += -DDEV_MODE
dev: all

directories:
	@mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET) deb-build *.deb

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(MANDIR)
	install -m 644 man/mops.1 $(DESTDIR)$(MANDIR)

deb: all
	@echo "Building Debian package..."
	mkdir -p deb-build/DEBIAN
	@echo "Package: mops" > deb-build/DEBIAN/control
	@echo "Version: 1.0.0" >> deb-build/DEBIAN/control
	@echo "Section: utils" >> deb-build/DEBIAN/control
	@echo "Priority: optional" >> deb-build/DEBIAN/control
	@echo "Architecture: $$(dpkg --print-architecture 2>/dev/null || echo amd64)" >> deb-build/DEBIAN/control
	@echo "Depends: libsqlite3-0" >> deb-build/DEBIAN/control
	@echo "Maintainer: MLOps Engineer <admin@example.com>" >> deb-build/DEBIAN/control
	@echo "Description: Multipurpose Operations CLI for DevOps and MLOps" >> deb-build/DEBIAN/control
	@echo " mops is a centralized utility for Linux system monitoring, hardware metrics," >> deb-build/DEBIAN/control
	@echo " containerized environment tracking, and background task management." >> deb-build/DEBIAN/control
	$(MAKE) install DESTDIR=deb-build PREFIX=/usr
	dpkg-deb --build deb-build mops_1.0.0_$$(dpkg --print-architecture 2>/dev/null || echo amd64).deb
	rm -rf deb-build
