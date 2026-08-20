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
APP_PRODUCT_CODE := CTR-P-PTFR
APP_UNIQUE_ID := 0xA054E

PYTHON  ?= python3
MAKEROM ?= $(CURDIR)/cia/tools/makerom
BANNERTOOL ?= $(CURDIR)/cia/tools/bannertool
ICON    := $(CTRULIB)/default_icon.png

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS  := $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm

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
export LD         := $(CC)

export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
ifneq ($(ROMFS),)
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all cia

all: $(BUILD)

cia: $(BUILD) $(OUTPUT).cia

$(OUTPUT).cia: $(OUTPUT).elf $(OUTPUT).smdh cia/app.rsf cia/fetch_tools.py cia/gen_assets.py
	@$(PYTHON) cia/fetch_tools.py
	@$(PYTHON) cia/gen_assets.py $(BUILD)
	@$(BANNERTOOL) makebanner -i $(BUILD)/banner.png -a $(BUILD)/banner.wav -o $(BUILD)/banner.bnr
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i "$(ICON)" -f visible -o $(BUILD)/icon.icn
	@$(STRIP) $(OUTPUT).elf -o $(BUILD)/stripped.elf
	@$(MAKEROM) -f cia -o $@ -elf $(BUILD)/stripped.elf -rsf cia/app.rsf \
		-icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t \
		-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" \
		-DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)"
	@echo built ... $(notdir $@)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).cia

else
$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf: $(OFILES)
-include $(DEPSDIR)/*.d
endif
