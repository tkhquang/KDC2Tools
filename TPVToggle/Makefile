# Makefile for building KCD2_TPVToggle.asi using g++ (MinGW) - Refactored Structure

# Compiler and linker
CXX := g++ # Use immediate assignment for consistency
WINDRES := windres

# --- Base Directories ---
# Use := for immediate assignment, especially for paths
SRC_DIR := src
ASM_DIR := $(SRC_DIR)/asm
EXTERNAL_DIR := external
BUILD_DIR := build

# MinHook Source Directories
MINHOOK_DIR := $(EXTERNAL_DIR)/minhook
MINHOOK_INCLUDE_DIR := $(MINHOOK_DIR)/include
MINHOOK_SRC_DIR := $(MINHOOK_DIR)/src
MINHOOK_HDE_SRC_DIR := $(MINHOOK_SRC_DIR)/hde

# --- Output Directories ---
# Base object directory within the build directory
OBJ_DIR := $(BUILD_DIR)/obj
# Specific output location for MinHook object files (keep them separate)
MINHOOK_OBJ_OUT_DIR := $(OBJ_DIR)/minhook

# --- Final Target ---
TARGET := $(BUILD_DIR)/KCD2_TPVToggle.asi

# --- Sanity Checks ---
ifeq ($(wildcard $(MINHOOK_INCLUDE_DIR)/MinHook.h),)
$(error MinHook submodule not initialized or header missing. Please run 'git submodule update --init --recursive')
endif

# --- Compiler and Linker Flags ---
INCLUDE_PATHS := -I$(MINHOOK_INCLUDE_DIR) -I$(SRC_DIR)
CXXFLAGS := -std=c++20 -m64 -O2 -Wall -Wextra $(INCLUDE_PATHS)
# MinGW flags (use C++ flags for MinHook's C code for consistency)
CFLAGS := $(CXXFLAGS)

# Linker flags: Static libs, create DLL
LDFLAGS := -static -static-libgcc -static-libstdc++ -shared
# Required Windows libs for API calls
LDLIBS := -lpsapi -luser32 -lkernel32

# --- Source File Lists ---
# Find source files in their respective directories
CPP_SRCS := $(wildcard $(SRC_DIR)/*.cpp)
ASM_SRCS := $(wildcard $(ASM_DIR)/*.S)
MINHOOK_C_SRCS := $(wildcard $(MINHOOK_SRC_DIR)/*.c)
MINHOOK_HDE_SRCS := $(wildcard $(MINHOOK_HDE_SRC_DIR)/*.c)

# --- Object File Lists ---
# Create lists of the corresponding object file paths based on source files
# Our C++ source -> main object directory ($(OBJ_DIR))
CPP_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(CPP_SRCS))
# Our Assembly source -> main object directory ($(OBJ_DIR))
ASM_OBJS := $(patsubst $(ASM_DIR)/%.S,$(OBJ_DIR)/%.o,$(ASM_SRCS))
# MinHook Core C source -> dedicated minhook object directory ($(MINHOOK_OBJ_OUT_DIR))
MINHOOK_C_OBJS := $(patsubst $(MINHOOK_SRC_DIR)/%.c,$(MINHOOK_OBJ_OUT_DIR)/%.o,$(MINHOOK_C_SRCS))
# MinHook HDE C source -> dedicated minhook object directory ($(MINHOOK_OBJ_OUT_DIR))
MINHOOK_HDE_OBJS := $(patsubst $(MINHOOK_HDE_SRC_DIR)/%.c,$(MINHOOK_OBJ_OUT_DIR)/%.o,$(MINHOOK_HDE_SRCS))

# Combine ALL object file paths required for linking
ALL_OBJS := $(sort $(CPP_OBJS) $(ASM_OBJS) $(MINHOOK_C_OBJS) $(MINHOOK_HDE_OBJS))

# --- Make Rules ---

# Default target depends on 'prepare' (to create dirs) and then the final target file
.PHONY: all
all: prepare $(TARGET)

# Prepare build directories: Create them if they don't exist using mkdir -p
.PHONY: prepare
prepare:
	@echo "Creating build directories..."
	@mkdir -p $(OBJ_DIR) $(MINHOOK_OBJ_OUT_DIR)

# Link the final target DLL/ASI using all compiled object files
$(TARGET): $(ALL_OBJS)
	@echo "Linking $@..."
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS) # $^ expands to all prerequisites (.o files)
	@echo "Build complete: $@"

# Rule to compile our C++ source files (src/*.cpp -> build/obj/*.o)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "Compiling C++ $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to assemble our Assembly source files (src/asm/*.S -> build/obj/*.o)
$(OBJ_DIR)/%.o: $(ASM_DIR)/%.S
	@echo "Assembling $<..."
	$(CXX) -c $< -o $@ # Using CXX driver works for GAS syntax

# Rule to compile MinHook Core C source files (-> build/obj/minhook/*.o)
$(MINHOOK_OBJ_OUT_DIR)/%.o: $(MINHOOK_SRC_DIR)/%.c
	@echo "Compiling MinHook $<..."
	$(CXX) $(CFLAGS) -c $< -o $@

# Rule to compile MinHook HDE C source files (-> build/obj/minhook/*.o)
$(MINHOOK_OBJ_OUT_DIR)/%.o: $(MINHOOK_HDE_SRC_DIR)/%.c
	@echo "Compiling MinHook HDE $<..."
	$(CXX) $(CFLAGS) -c $< -o $@

# --- Utility Targets ---

# Clean: Remove only generated object files and the final target DLL/ASI
.PHONY: clean
clean:
	@echo "Cleaning object files and target..."
	rm -f $(ALL_OBJS) $(TARGET)

# Distclean: Remove the entire build directory (more thorough clean)
.PHONY: distclean
distclean: clean # Make distclean depend on clean first
	@echo "Cleaning entire build directory..."
	rm -rf $(BUILD_DIR)

# Install: Copy necessary files for distribution to the build directory
.PHONY: install
install: all # Ensure build is complete before copying
	@echo "Copying additional files to $(BUILD_DIR)..."
	@cp -fv $(SRC_DIR)/../KCD2_TPVToggle.ini $(BUILD_DIR)/
	@cp -fv $(SRC_DIR)/../README.txt $(BUILD_DIR)/
	@cp -fv $(SRC_DIR)/../THIRD-PARTY-LICENSES.txt $(BUILD_DIR)/
	@echo "Install step complete."

# Help: Display available make commands
.PHONY: help
help:
	@echo "Available commands:"
	@echo "  make          - Build the mod ($(TARGET))"
	@echo "  make clean    - Remove generated object files and the mod file"
	@echo "  make distclean - Remove the entire build directory"
	@echo "  make install  - Build and copy config/docs to build directory"
	@echo "  make help     - Display this help information"
