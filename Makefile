#---------------------------------------------------------------------------------
# PS3SX2 - PSL1GHT build. `make` builds the .self, `make pkg` the installable .pkg.
# Host unit tests live in tests/ and don't need the PS3 toolchain: `make test`.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifneq ($(MAKECMDGOALS),test)
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif
endif

#---------------------------------------------------------------------------------
# TITLE, APPID, CONTENTID, ICON0 and SFOXML must be set before ppu_rules.
#---------------------------------------------------------------------------------
TITLE		:=	PS3SX2
APPID		:=	PS3SX2001
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
ICON0		:=	$(CURDIR)/pkgfiles/ICON0.PNG
SFOXML		:=	$(CURDIR)/sfo.xml

ifneq ($(MAKECMDGOALS),test)
include $(PSL1GHT)/ppu_rules
endif

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	ps3sx2
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include

#---------------------------------------------------------------------------------
# libfont3d comes with Tiny3D; freetype comes from ps3libraries.
#---------------------------------------------------------------------------------
LIBS		:=	-lfont3d -lfreetype -lpng -ltiny3d -lrsx -lz -lsimdmath -lgcm_sys -lio -lsysutil \
				-lpngdec -ljpgdec -lrt -llv2 -lsysmodule -lm

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
CFLAGS		=	-O2 -std=gnu99 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)

LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

export BUILDDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

export LD	:=	$(CC)

export OFILES	:=	$(addsuffix .o,$(BINFILES)) $(CFILES:.c=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					$(LIBPSL1GHT_INC) \
					-I$(CURDIR)/$(BUILD) -I$(PORTLIBS)/include -I$(PORTLIBS)/include/freetype2

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					$(LIBPSL1GHT_LIB) -L$(PORTLIBS)/lib

.PHONY: $(BUILD) clean run pkg test

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).fake.self $(OUTPUT)*.pkg
	@$(MAKE) --no-print-directory -C tests clean

#---------------------------------------------------------------------------------
run:
	ps3load $(OUTPUT).self

#---------------------------------------------------------------------------------
pkg:	$(BUILD) $(OUTPUT).pkg

#---------------------------------------------------------------------------------
test:
	@$(MAKE) --no-print-directory -C tests

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf:	$(OFILES)

#---------------------------------------------------------------------------------
# This rule links in binary data with the .bin extension
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
