CC ?= cc
PKG_CONFIG ?= pkg-config

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPDIR ?= $(DATADIR)/applications
METADATA_DIR ?= $(DATADIR)/metainfo
ICON_DIR ?= $(DATADIR)/icons/hicolor/scalable/apps
SCHEMA_DIR ?= $(DATADIR)/glib-2.0/schemas

BUILD_DIR := build
TEST_DIR := $(BUILD_DIR)/tests

APP_ID := com.binark.Binark
VERSION := 1.1.0

WARNINGS := -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-parameter
COMMON_CFLAGS := -std=c17 -D_GNU_SOURCE -O2 -g $(WARNINGS)
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 libadwaita-1)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 libadwaita-1)
GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 gio-2.0)
GLIB_LIBS := $(shell $(PKG_CONFIG) --libs glib-2.0 gio-2.0)

APP_SOURCES := \
    src/main.c \
    src/window.c \
    src/converter.c \
    src/text_converter.c \
    src/number_converter.c \
    src/file_converter.c \
    src/settings.c \
    src/history.c

APP_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(APP_SOURCES))
RESOURCE_C := $(BUILD_DIR)/binark-resources.c
RESOURCE_H := $(BUILD_DIR)/binark-resources.h
RESOURCE_OBJECT := $(BUILD_DIR)/binark-resources.o
SCHEMA_XML := data/$(APP_ID).gschema.xml
SCHEMA_COMPILED := $(BUILD_DIR)/gschemas/gschemas.compiled
ICON := data/icons/hicolor/scalable/apps/$(APP_ID).svg

TEST_TEXT_OBJECTS := $(TEST_DIR)/test_text.o $(BUILD_DIR)/text_converter.o $(BUILD_DIR)/converter.o
TEST_NUMBER_OBJECTS := $(TEST_DIR)/test_numbers.o $(BUILD_DIR)/number_converter.o
TEST_FILE_OBJECTS := $(TEST_DIR)/test_files.o $(BUILD_DIR)/file_converter.o $(BUILD_DIR)/text_converter.o $(BUILD_DIR)/converter.o

TEST_BINARIES := $(TEST_DIR)/test_text $(TEST_DIR)/test_numbers $(TEST_DIR)/test_files

.PHONY: all run test clean install uninstall

all: $(BUILD_DIR)/binark $(SCHEMA_COMPILED)

$(BUILD_DIR)/binark: $(APP_OBJECTS) $(RESOURCE_OBJECT)
	$(CC) $(APP_OBJECTS) $(RESOURCE_OBJECT) $(GTK_LIBS) -o $@

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(GTK_CFLAGS) -Ibuild -Isrc -MMD -MP -c $< -o $@

$(RESOURCE_C): data/resources.gresource.xml data/style.css $(ICON)
	@mkdir -p $(BUILD_DIR)
	glib-compile-resources --sourcedir=data \
	    --c-name=binark --generate-source --target=$(RESOURCE_C) $<

$(RESOURCE_H): data/resources.gresource.xml data/style.css $(ICON)
	@mkdir -p $(BUILD_DIR)
	glib-compile-resources --sourcedir=data \
	    --c-name=binark --generate-header --target=$(RESOURCE_H) \
	    data/resources.gresource.xml

$(BUILD_DIR)/main.o: $(RESOURCE_H)

$(RESOURCE_OBJECT): $(RESOURCE_C)
	$(CC) $(COMMON_CFLAGS) $(GLIB_CFLAGS) -Ibuild -c $(RESOURCE_C) -o $@

$(TEST_DIR)/%.o: tests/%.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(COMMON_CFLAGS) $(GLIB_CFLAGS) -Isrc -MMD -MP -c $< -o $@

$(TEST_DIR)/test_text: $(TEST_TEXT_OBJECTS)
	$(CC) $^ $(GLIB_LIBS) -o $@

$(TEST_DIR)/test_numbers: $(TEST_NUMBER_OBJECTS)
	$(CC) $^ $(GLIB_LIBS) -o $@

$(TEST_DIR)/test_files: $(TEST_FILE_OBJECTS)
	$(CC) $^ $(GLIB_LIBS) -o $@

$(SCHEMA_COMPILED): $(SCHEMA_XML)
	@mkdir -p $(BUILD_DIR)/gschemas
	cp $(SCHEMA_XML) $(BUILD_DIR)/gschemas/
	glib-compile-schemas $(BUILD_DIR)/gschemas

run: all
	GSETTINGS_SCHEMA_DIR="$(abspath $(BUILD_DIR))/gschemas" $(BUILD_DIR)/binark

test: $(TEST_BINARIES)
	@set -e; \
	for test in $(TEST_BINARIES); do \
	    echo "==> $$test"; \
	    $$test; \
	done

clean:
	rm -rf $(BUILD_DIR)

install: all
	install -d "$(DESTDIR)$(BINDIR)"
	install -m755 $(BUILD_DIR)/binark "$(DESTDIR)$(BINDIR)/binark"
	install -Dm644 data/$(APP_ID).desktop \
	    "$(DESTDIR)$(APPDIR)/$(APP_ID).desktop"
	install -Dm644 data/$(APP_ID).metainfo.xml \
	    "$(DESTDIR)$(METADATA_DIR)/$(APP_ID).metainfo.xml"
	install -Dm644 $(ICON) "$(DESTDIR)$(ICON_DIR)/$(APP_ID).svg"
	install -Dm644 $(SCHEMA_XML) \
	    "$(DESTDIR)$(SCHEMA_DIR)/$(APP_ID).gschema.xml"
	glib-compile-schemas "$(DESTDIR)$(SCHEMA_DIR)"
	if [ -z "$(DESTDIR)" ] && command -v update-desktop-database >/dev/null 2>&1; then \
	    update-desktop-database "$(APPDIR)" >/dev/null 2>&1 || true; \
	fi
	if [ -z "$(DESTDIR)" ] && command -v gtk4-update-icon-cache >/dev/null 2>&1; then \
	    gtk4-update-icon-cache -q -t -f "$(DATADIR)/icons/hicolor" >/dev/null 2>&1 || true; \
	fi

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/binark"
	rm -f "$(DESTDIR)$(APPDIR)/$(APP_ID).desktop"
	rm -f "$(DESTDIR)$(METADATA_DIR)/$(APP_ID).metainfo.xml"
	rm -f "$(DESTDIR)$(ICON_DIR)/$(APP_ID).svg"
	rm -f "$(DESTDIR)$(SCHEMA_DIR)/$(APP_ID).gschema.xml"
	if [ -f "$(DESTDIR)$(SCHEMA_DIR)/gschemas.compiled" ]; then \
	    glib-compile-schemas "$(DESTDIR)$(SCHEMA_DIR)" || true; \
	fi
	if [ -z "$(DESTDIR)" ] && command -v update-desktop-database >/dev/null 2>&1; then \
	    update-desktop-database "$(APPDIR)" >/dev/null 2>&1 || true; \
	fi

-include $(APP_OBJECTS:.o=.d) $(TEST_TEXT_OBJECTS:.o=.d) \
         $(TEST_NUMBER_OBJECTS:.o=.d) $(TEST_FILE_OBJECTS:.o=.d)
