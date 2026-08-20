# Build with: source /etc/profile.d/devkit-env.sh && make
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment (devkitPro).")
endif

include $(DEVKITARM)/3ds_rules

TARGET   := pockettransfer
BUILD    := build
SOURCES  := source ../shared
DATA     :=
INCLUDES := source ../shared
ROMFS    := romfs

APP_TITLE := Pockettransfer
APP_DESCRIPTION := Pokémon bank client
APP_AUTHOR := Pockettransfer

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS  := $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lcitro2d -lcitro3d -lctru -lm

LIBDIRS  := $(CTRULIB) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH  := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(notdir $(wildcard source/*.c)) $(notdir $(wildcard ../shared/*.c))
export OFILES_SRC := $(CFILES:.c=.o)
export OFILES     := $(OFILES_SRC)
export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

else
$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf: $(OFILES)
-include $(DEPSDIR)/*.d
endif
