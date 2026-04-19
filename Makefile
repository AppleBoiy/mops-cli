CC ?= gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g -D_XOPEN_SOURCE=700 -MMD -MP
LDFLAGS ?= 
LDLIBS = -lsqlite3 -ldl -lncurses

VERSION ?= 1.1.1

TARGET = mops
SRC_DIR = src
OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
DEPS = $(OBJS:.o=.d)

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

# Dry-run support: when DRY_RUN=1, print commands instead of executing them
DRY_RUN ?= 0
ifeq ($(DRY_RUN),1)
DO = @echo
else
DO =
endif

.PHONY: all clean install uninstall install-dry-run uninstall-dry-run directories dev deb

all: directories $(TARGET)

dev: CFLAGS += -DDEV_MODE
dev: all

directories:
	@mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) deb-build *.deb

install: all
	$(DO)install -d $(DESTDIR)$(BINDIR)
	$(DO)install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)
	$(DO)install -d $(DESTDIR)$(MANDIR)
	$(DO)install -m 644 man/mops.1 $(DESTDIR)$(MANDIR)

# Helper target: dry-run install
install-dry-run: DRY_RUN=1
install-dry-run: install

uninstall:
	$(DO)rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	$(DO)rm -f $(DESTDIR)$(MANDIR)/mops.1

# Helper target: dry-run uninstall
uninstall-dry-run: DRY_RUN=1
uninstall-dry-run: uninstall


deb: all
	@echo "Building Debian package..."
	mkdir -p deb-build/DEBIAN
	@echo "Package: mops" > deb-build/DEBIAN/control
	@echo "Version: $(VERSION)" >> deb-build/DEBIAN/control
	@echo "Section: utils" >> deb-build/DEBIAN/control
	@echo "Priority: optional" >> deb-build/DEBIAN/control
	@echo "Architecture: $$(dpkg --print-architecture 2>/dev/null || echo amd64)" >> deb-build/DEBIAN/control
	@echo "Depends: libsqlite3-0, libncurses6 | libncurses5" >> deb-build/DEBIAN/control
	@echo "Maintainer: Chaipat J. <contact.chaipat@gmail.com>" >> deb-build/DEBIAN/control
	@echo "Description: Multipurpose Operations CLI for DevOps and MLOps" >> deb-build/DEBIAN/control
	@echo " mops is a centralized utility for Linux system monitoring, hardware metrics," >> deb-build/DEBIAN/control
	@echo " containerized environment tracking, and background task management." >> deb-build/DEBIAN/control
	$(MAKE) install DESTDIR=deb-build PREFIX=/usr
	dpkg-deb --build deb-build mops_$(VERSION)_$$(dpkg --print-architecture 2>/dev/null || echo amd64).deb
	rm -rf deb-build
