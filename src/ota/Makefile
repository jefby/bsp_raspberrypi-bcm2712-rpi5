## Makefile for OTA Client

SRCDIR  := .
OBJDIR  := obj
BINDIR  := bin

CXX := qcc -Vgcc_ntoaarch64le_cxx

CXXFLAGS := -Wall -Wextra -O2 -std=c++11 -fPIC

LDFLAGS := -lcurl -lsocket  -lssl -lcrypto -lz -lc++

# Source files
SOURCES := $(SRCDIR)/ota_client.cpp
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))
TARGET  := $(BINDIR)/ota_client

# Default target
all: $(TARGET)

# Create directories
$(OBJDIR) $(BINDIR):
	mkdir -p $@

# Compile object files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Link executable
$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@
	@echo "Build completed: $@"

# Clean
clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Install (copy to install directory)
install: $(TARGET)
	mkdir -p ../../../install/aarch64/usr/bin
	cp $(TARGET) ../../../install/aarch64/usr/bin/

hinstall: install

.PHONY: all clean install hinstall
