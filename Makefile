SDK_PATH ?= $(shell xcrun --show-sdk-path)

SRC_DIR      = src
BUILD_DIR    = build
KEXT_NAME    = CS35L56Audio
KEXT_BUNDLE  = $(BUILD_DIR)/$(KEXT_NAME).kext

CXX          = clang++
CXXFLAGS     = -arch x86_64 \
               -mkernel \
               -nostdinc \
               -fapple-kext \
               -std=c++17 \
               -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE \
               -I$(SDK_PATH)/System/Library/Frameworks/Kernel.framework/Headers \
               -Wall -Wno-inconsistent-missing-override

LDFLAGS      = -arch x86_64 \
               -mkernel \
               -nostdlib \
               -Xlinker -kext \
               -lkmod -lcc_kext

OBJS         = $(BUILD_DIR)/CS35L56Audio.o $(BUILD_DIR)/IntelLpssSpi.o

.PHONY: all clean info

all: $(KEXT_BUNDLE)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@echo "==> Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(KEXT_BUNDLE): $(OBJS) $(SRC_DIR)/Info.plist
	@echo "==> Packaging $(KEXT_BUNDLE)..."
	@mkdir -p $(KEXT_BUNDLE)/Contents/MacOS
	$(CXX) $(LDFLAGS) $(OBJS) -o $(KEXT_BUNDLE)/Contents/MacOS/$(KEXT_NAME)
	@cp $(SRC_DIR)/Info.plist $(KEXT_BUNDLE)/Contents/Info.plist
	@codesign --force --deep -s - $(KEXT_BUNDLE)
	@echo "==> Build complete: $(KEXT_BUNDLE)"
	@file $(KEXT_BUNDLE)/Contents/MacOS/$(KEXT_NAME)

clean:
	@echo "==> Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)

load: all
	@echo "==> Setting permissions and loading..."
	@sudo chown -R root:wheel $(KEXT_BUNDLE)
	@sudo chmod -R 755 $(KEXT_BUNDLE)
	@-sudo kmutil load -p $(KEXT_BUNDLE) 2>/dev/null || sudo kextload $(KEXT_BUNDLE)
	@echo "==> Driver loaded. Status:"
	@kmutil showloaded | grep -i $(KEXT_NAME) || kextstat | grep -i $(KEXT_NAME) || echo "Not found in kextstat"
	@echo "==> Recent kernel logs for $(KEXT_NAME):"
	@sudo dmesg | grep -i CS35L56 | tail -n 25 || true

unload:
	@echo "==> Unloading $(KEXT_NAME)..."
	@-sudo kmutil unload -p $(KEXT_BUNDLE) 2>/dev/null || sudo kextunload -b com.zephyrus.$(KEXT_NAME) 2>/dev/null || true
	@sudo chown -R $(shell id -u):$(shell id -g) $(BUILD_DIR)
	@echo "==> Driver unloaded."

reload: unload load
