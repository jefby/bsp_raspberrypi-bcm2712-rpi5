## Makefile for OTA Client

SRCDIR  := .
OBJDIR  := obj
BINDIR  := bin

# Compiler and flags
CC      := qcc -Vgcc_ntoaarch64
CXX     := qcc -Vgcc_ntoaarch64
CFLAGS  := -Wall -Wextra -O2 -fPIC
CXXFLAGS:= $(CFLAGS) -std=c++11
LDFLAGS := -lcurl

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

.PHONY: all clean install
