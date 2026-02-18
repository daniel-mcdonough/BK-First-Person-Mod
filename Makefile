BUILD_DIR := build
NAME := bk_first_person_mode
VERSION := $(shell grep '^version' mod.toml | head -1 | sed 's/.*"\(.*\)"/\1/')

# Allow the user to specify the compiler and linker on macOS
# as Apple Clang does not support MIPS architecture
ifeq ($(OS),Windows_NT)
    CC      := clang
    LD      := ld.lld
    PROG_SUFFIX := .exe
else ifneq ($(shell uname),Darwin)
    CC      := clang
    LD      := ld.lld
    PROG_SUFFIX :=
else
    CC      ?= clang
    LD      ?= ld.lld
    PROG_SUFFIX :=
endif

MODTOOL := ./RecompModTool

ifeq ($(wildcard $(MODTOOL)$(PROG_SUFFIX)),)
$(error "Please place the RecompModTool executable in the root of this repo.")
endif

TARGET  := $(BUILD_DIR)/mod.elf
NRM     := $(BUILD_DIR)/$(NAME).nrm
NRM_VER := $(BUILD_DIR)/$(NAME)-$(VERSION).nrm
ZIP_VER := $(NAME)-$(VERSION).zip

# Native library (mouse input)
CC_NATIVE     := gcc
CC_NATIVE_WIN := x86_64-w64-mingw32-gcc
NATIVE_SRC    := native/bk_mouse_input.c
NATIVE_SO     := $(BUILD_DIR)/bk_mouse_input.so
NATIVE_DLL    := $(BUILD_DIR)/bk_mouse_input.dll

LDSCRIPT := mod.ld
ARCHFLAGS := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
             -fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset -funsigned-char -fno-builtin-sinf -fno-builtin-cosf
WARNFLAGS := -Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable \
             -Wno-missing-braces -Wno-unsupported-floating-point-opt -Wno-cast-function-type-mismatch -Werror=section -Wno-visibility
CFLAGS   := $(ARCHFLAGS) $(WARNFLAGS) -D_LANGUAGE_C -nostdinc -ffunction-sections
CPPFLAGS := -nostdinc -DMIPS -DF3DEX_GBI -I include -I include/dummy_headers \
			-I bk-decomp/include -I bk-decomp/include/2.0L -I bk-decomp/include/2.0L/PR
LDFLAGS  := -nostdlib -T $(LDSCRIPT) -Map $(BUILD_DIR)/mod.map --unresolved-symbols=ignore-all --emit-relocs -e 0 --no-nmagic -gc-sections

rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
getdirs = $(sort $(dir $(1)))

C_SRCS := $(call rwildcard,src,*.c)
C_OBJS := $(addprefix $(BUILD_DIR)/, $(C_SRCS:.c=.o))
C_DEPS := $(addprefix $(BUILD_DIR)/, $(C_SRCS:.c=.d))

ALL_OBJS := $(C_OBJS)
ALL_DEPS := $(C_DEPS)
BUILD_DIRS := $(call getdirs,$(ALL_OBJS))

all: $(NRM_VER) $(NATIVE_SO) $(NATIVE_DLL)

$(NRM): $(TARGET)
	$(MODTOOL) mod.toml $(BUILD_DIR)

$(NRM_VER): $(NRM)
	cp $(NRM) $(NRM_VER)

$(NATIVE_SO): $(NATIVE_SRC) | $(BUILD_DIR)
	$(CC_NATIVE) -shared -fPIC -Wall -Wextra -o $@ $< -lX11 -lXfixes -lpthread

$(NATIVE_DLL): $(NATIVE_SRC) | $(BUILD_DIR)
	$(CC_NATIVE_WIN) -shared -Wall -Wextra -o $@ $<

release: $(NRM_VER) $(NATIVE_SO) $(NATIVE_DLL)
	zip $(ZIP_VER) $(NRM_VER) $(NATIVE_SO) $(NATIVE_DLL)

$(TARGET): $(ALL_OBJS) $(LDSCRIPT) | $(BUILD_DIR)
	$(LD) $(ALL_OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR) $(BUILD_DIRS):
ifeq ($(OS),Windows_NT)
	if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
else
	mkdir -p $@
endif

$(C_OBJS): $(BUILD_DIR)/%.o : %.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
ifeq ($(OS),Windows_NT)
	if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR)
else
	rm -rf $(BUILD_DIR)
endif

-include $(ALL_DEPS)

.PHONY: clean all release

# Print target for debugging
print-% : ; $(info $* is a $(flavor $*) variable set to [$($*)]) @true
