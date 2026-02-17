################################################################
# Atari ST build (MiNTlib SDL 1.2 via atarist-toolkit-docker)
################################################################

ifeq ($(V),1)
	VB=
else
	VB=@
endif

CC = m68k-atari-mint-gcc
SDL_CONFIG ?= m68k-atari-mint-sdl-config
SDL_PKG_CONFIG ?= m68k-atari-mint-pkg-config

ATARI_USE_SUPERVISOR ?= 0
ATARI_TARGET_FPS ?= 9
ATARI_SHOW_FPS ?= 0
ATARI_FALLBACK_AUDIO_RATE ?= 6258
ATARI_FALLBACK_AUDIO_DEVICE_CHANNELS ?= 1
ATARI_FALLBACK_MIX_CHANNELS ?= 2
ATARI_FALLBACK_AUDIO_U8 ?= 1
ATARI_SND_CHANNELS ?= $(ATARI_FALLBACK_MIX_CHANNELS)

ifeq ($(shell command -v $(SDL_CONFIG) >/dev/null 2>&1 && echo yes),yes)
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS := $(shell $(SDL_CONFIG) --libs)
else
SDL_CFLAGS := $(shell $(SDL_PKG_CONFIG) --cflags sdl 2>/dev/null)
SDL_LIBS := $(shell $(SDL_PKG_CONFIG) --libs sdl 2>/dev/null)
ifeq ($(strip $(SDL_CFLAGS)),)
SDL_CFLAGS := -I/usr/m68k-atari-mint/include/SDL -D_GNU_SOURCE=1
endif
ifeq ($(strip $(SDL_LIBS)),)
SDL_LIBS := -lSDL -lgem -lldg -lgem
endif
endif

CFLAGS += -O2 -fomit-frame-pointer -std=gnu99 -m68000 -Wall -D_DEFAULT_SOURCE
CFLAGS += -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
CFLAGS += -DATARI_USE_SUPERVISOR=$(ATARI_USE_SUPERVISOR)
CFLAGS += -DATARI_TARGET_FPS=$(ATARI_TARGET_FPS)
CFLAGS += -DATARI_SHOW_FPS=$(ATARI_SHOW_FPS)
CFLAGS += -DATARI_FALLBACK_AUDIO_RATE=$(ATARI_FALLBACK_AUDIO_RATE)
CFLAGS += -DATARI_FALLBACK_AUDIO_DEVICE_CHANNELS=$(ATARI_FALLBACK_AUDIO_DEVICE_CHANNELS)
CFLAGS += -DATARI_FALLBACK_MIX_CHANNELS=$(ATARI_FALLBACK_MIX_CHANNELS)
CFLAGS += -DATARI_FALLBACK_AUDIO_U8=$(ATARI_FALLBACK_AUDIO_U8)
CFLAGS += -DATARI_SND_CHANNELS=$(ATARI_SND_CHANNELS)
CFLAGS += -DCMAP256
CFLAGS += -DFEATURE_SOUND
CFLAGS += $(SDL_CFLAGS)
LDFLAGS +=
LIBS += -lm -lc $(SDL_LIBS)

SDL_MIXER_HEADER := $(wildcard /usr/m68k-atari-mint/include/SDL/SDL_mixer.h)
SDL_MIXER_LIB := $(firstword \
	$(wildcard /usr/m68k-atari-mint/lib/libSDL_mixer.a) \
	$(wildcard /usr/m68k-atari-mint/lib/libSDL_mixer.la))
ifeq ($(strip $(SDL_MIXER_HEADER) $(SDL_MIXER_LIB)),)
HAVE_SDL_MIXER := 0
else
HAVE_SDL_MIXER := 1
endif

SRCDIR = doomgeneric
OBJDIR = obj
BUILDDIR = build
OUTPUT_DOOM = $(BUILDDIR)/DOOM.TOS
OUTPUT_030 = $(BUILDDIR)/DOOM_030.TOS

SRC_DOOM = dummy.o am_map.o doomdef.o doomstat.o dstrings.o d_event.o d_items.o d_iwad.o d_loop.o d_main.o d_mode.o d_net.o f_finale.o f_wipe.o g_game.o hu_lib.o hu_stuff.o info.o i_cdmus.o i_endoom.o i_joystick.o i_scale.o i_sound.o i_system.o i_timer.o memio.o m_argv.o m_bbox.o m_cheat.o m_config.o m_controls.o m_fixed.o m_menu.o m_misc.o m_random.o p_ceilng.o p_doors.o p_enemy.o p_floor.o p_inter.o p_lights.o p_map.o p_maputl.o p_mobj.o p_plats.o p_pspr.o p_saveg.o p_setup.o p_sight.o p_spec.o p_switch.o p_telept.o p_tick.o p_user.o r_bsp.o r_data.o r_draw.o r_main.o r_plane.o r_segs.o r_sky.o r_things.o sha1.o sounds.o statdump.o st_lib.o st_stuff.o s_sound.o tables.o v_video.o wi_stuff.o w_checksum.o w_file.o w_main.o w_wad.o z_zone.o w_file_stdc.o i_input.o i_video.o doomgeneric.o doomgeneric_atarist.o mus2mid.o
ifeq ($(HAVE_SDL_MIXER),1)
CFLAGS += -DDG_HAVE_SDLMIXER
LIBS += -lSDL_mixer
SRC_DOOM += i_sdlsound.o i_sdlmusic.o gusconf.o
else
SRC_DOOM += i_sdlfallbacksound.o
$(warning SDL_mixer not found in atarist-toolkit; using SDL audio fallback for SFX only (music disabled))
endif
OBJDIR_DOOM = $(OBJDIR)/doom
OBJDIR_030 = $(OBJDIR)/030
OBJS_DOOM = $(addprefix $(OBJDIR_DOOM)/,$(SRC_DOOM))
OBJS_030 = $(addprefix $(OBJDIR_030)/,$(SRC_DOOM))

CFLAGS_DOOM = $(CFLAGS)
CFLAGS_030 = $(filter-out -m68000,$(CFLAGS_DOOM)) -m68030
LDFLAGS_030 = $(LDFLAGS) -m68030 -m68882

.PHONY: all doom doom-030 clean print

all: doom doom-030

doom: $(OUTPUT_DOOM)

doom-030: $(OUTPUT_030)

clean:
	rm -rf obj
	rm -f $(OUTPUT_DOOM) $(OUTPUT_030)

$(OUTPUT_DOOM): $(OBJS_DOOM) | $(BUILDDIR)
	@echo [Linking $@]
	$(VB)$(CC) $(CFLAGS_DOOM) $(LDFLAGS) $(OBJS_DOOM) -o $@ $(LIBS)

$(OUTPUT_030): $(OBJS_030) | $(BUILDDIR)
	@echo [Linking $@]
	$(VB)$(CC) $(CFLAGS_030) $(LDFLAGS_030) $(OBJS_030) -o $@ $(LIBS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OBJS_DOOM): | $(OBJDIR_DOOM)
$(OBJS_030): | $(OBJDIR_030)

$(OBJDIR_DOOM):
	mkdir -p $(OBJDIR_DOOM)

$(OBJDIR_030):
	mkdir -p $(OBJDIR_030)

$(OBJDIR_DOOM)/%.o: $(SRCDIR)/%.c
	@echo [Compiling $<]
	$(VB)$(CC) $(CFLAGS_DOOM) -c $< -o $@

$(OBJDIR_030)/%.o: $(SRCDIR)/%.c
	@echo [Compiling $<]
	$(VB)$(CC) $(CFLAGS_030) -c $< -o $@

$(OBJDIR_DOOM)/doomgeneric_atarist.o: $(SRCDIR)/doomgeneric_atarist.c | $(OBJDIR_DOOM)
	@echo [Compiling $<]
	$(VB)$(CC) $(filter-out -fomit-frame-pointer,$(CFLAGS_DOOM)) -fno-omit-frame-pointer -c $< -o $@

$(OBJDIR_030)/doomgeneric_atarist.o: $(SRCDIR)/doomgeneric_atarist.c | $(OBJDIR_030)
	@echo [Compiling $<]
	$(VB)$(CC) $(filter-out -fomit-frame-pointer,$(CFLAGS_030)) -fno-omit-frame-pointer -c $< -o $@

print:
	@echo OBJS_DOOM: $(OBJS_DOOM)
	@echo OBJS_030: $(OBJS_030)
