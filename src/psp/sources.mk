# Curated source manifest for the PSP port.
#
# Keep this explicit. New sources should be added deliberately so the PSP build
# does not silently pick up generated/decomp files that are not portable yet.

PSP_BOOTSTRAP_C_FILES := src/psp/main.c

PSP_N64PSP_C_FILES :=

ifeq ($(USE_N64PSP_QUEUES),1)
PSP_N64PSP_C_FILES += \
    src/psp/n64psp_integration.c
endif
ifeq ($(N64PSP_QUEUE_SELFTEST),1)
PSP_N64PSP_C_FILES += \
    src/psp/n64psp_platform_selftest.c \
    src/psp/n64psp_queue_selftest.c
endif

# Active PSP renderer architecture:
#   Star Fox 64 / Fast3D display-list frontend in gfx_psp_dl.c
#       -> backend-neutral draw state and batches
#       -> PSPGL backend in gfx_pspgl.c
#       -> public PSP bridge in renderer_pspgl.c
# This explicit list is authoritative. The retired direct-GU renderer
# src/psp/renderer.c and its private helpers are legacy/reference code only.
PSP_RENDERER_C_FILES := \
    src/psp/gfx/gfx_psp.c \
    src/psp/gfx/gfx_psp_dl.c \
    src/psp/gfx/gfx_pspgl.c \
    src/psp/renderer_pspgl.c

PSP_AUDIO_C_FILES := \
    src/audio/audio_context.c \
    src/audio/audio_effects.c \
    src/audio/audio_general.c \
    src/audio/audio_heap.c \
    src/audio/audio_load.c \
    src/audio/audio_playback.c \
    src/audio/audio_seqplayer.c \
    src/audio/audio_synthesis.c \
    src/audio/audio_tables.c \
    src/audio/audio_thread.c \
    src/audio/note_data.c \
    src/audio/wave_samples.c \
    src/psp/audio_mixer.c \
    src/psp/audio_output.c

PSP_GAME_S_FILES := \
    src/psp/audio_assets.S

PSP_GAME_C_FILES := \
    src/buffers.c \
    src/dmatable.c \
    src/libc_math64.c \
    src/libc_sprintf.c \
    src/assets/ast_7_ti_1/ast_7_ti_1.c \
    src/assets/ast_7_ti_2/ast_7_ti_2.c \
    src/assets/ast_8_ti/ast_8_ti.c \
    src/assets/ast_9_ti/ast_9_ti.c \
    src/assets/ast_allies/ast_allies.c \
    src/assets/ast_andross/ast_andross.c \
    src/assets/ast_aquas/ast_aquas.c \
    src/assets/ast_area_6/ast_area_6.c \
    src/assets/ast_arwing/ast_arwing.c \
    src/assets/ast_A_ti/ast_A_ti.c \
    src/assets/ast_bg_planet/ast_bg_planet.c \
    src/assets/ast_bg_space/ast_bg_space.c \
    src/assets/ast_blue_marine/ast_blue_marine.c \
    src/assets/ast_bolse/ast_bolse.c \
    src/assets/ast_common/ast_common.c \
    src/assets/ast_corneria/ast_corneria.c \
    src/assets/ast_ending/ast_ending.c \
    src/assets/ast_ending_award_back/ast_ending_award_back.c \
    src/assets/ast_ending_award_front/ast_ending_award_front.c \
    src/assets/ast_ending_expert/ast_ending_expert.c \
    src/assets/ast_enmy_planet/ast_enmy_planet.c \
    src/assets/ast_enmy_space/ast_enmy_space.c \
    src/assets/ast_font_3d/ast_font_3d.c \
    src/assets/ast_fortuna/ast_fortuna.c \
    src/assets/ast_great_fox/ast_great_fox.c \
    src/assets/ast_katina/ast_katina.c \
    src/assets/ast_landmaster/ast_landmaster.c \
    src/assets/ast_logo/ast_logo.c \
    src/assets/ast_macbeth/ast_macbeth.c \
    src/assets/ast_map/ast_map.c \
    src/assets/ast_meteo/ast_meteo.c \
    src/assets/ast_option/ast_option.c \
    src/assets/ast_radio/ast_radio.c \
    src/assets/ast_sector_x/ast_sector_x.c \
    src/assets/ast_sector_y/ast_sector_y.c \
    src/assets/ast_sector_z/ast_sector_z.c \
    src/assets/ast_solar/ast_solar.c \
    src/assets/ast_star_wolf/ast_star_wolf.c \
    src/assets/ast_text/ast_text.c \
    src/assets/ast_titania/ast_titania.c \
    src/assets/ast_title/ast_title.c \
    src/assets/ast_training/ast_training.c \
    src/assets/ast_ve1_boss/ast_ve1_boss.c \
    src/assets/ast_venom_1/ast_venom_1.c \
    src/assets/ast_venom_2/ast_venom_2.c \
    src/assets/ast_versus/ast_versus.c \
    src/assets/ast_vs_menu/ast_vs_menu.c \
    src/assets/ast_warp_zone/ast_warp_zone.c \
    src/assets/ast_zoness/ast_zoness.c \
    src/engine/fox_360.c \
    src/engine/fox_beam.c \
    src/engine/fox_bg.c \
    src/engine/fox_blur.c \
    src/engine/fox_boss.c \
    src/engine/fox_col1.c \
    src/engine/fox_col2.c \
    src/engine/fox_context.c \
    src/engine/fox_demo.c \
    src/engine/fox_display.c \
    src/engine/fox_edata.c \
    src/engine/fox_edisplay.c \
    src/engine/fox_effect.c \
    src/engine/fox_enmy.c \
    src/engine/fox_enmy2.c \
    src/engine/fox_fade.c \
    src/engine/fox_game.c \
    src/engine/fox_hud.c \
    src/engine/fox_load.c \
    src/engine/fox_message.c \
    src/engine/fox_msg_palette.c \
    src/engine/fox_pause.c \
    src/engine/fox_play.c \
    src/engine/fox_radio.c \
    src/engine/fox_rcp.c \
    src/engine/fox_rcp_init.c \
    src/engine/fox_reset.c \
    src/engine/fox_save.c \
    src/engine/fox_shapes.c \
    src/engine/fox_std_lib.c \
    src/engine/fox_tank.c \
    src/engine/fox_versus.c \
    src/engine/fox_wheels.c \
    src/libultra/gu/cosf.c \
    src/libultra/gu/lookat.c \
    src/libultra/gu/mtxutil.c \
    src/libultra/gu/ortho.c \
    src/libultra/gu/perspective.c \
    src/libultra/gu/sinf.c \
    src/libultra/libc/ldiv.c \
    src/libultra/libc/ll.c \
    src/libultra/libc/string.c \
    src/libultra/libc/xldtob.c \
    src/libultra/libc/xlitob.c \
    src/libultra/libc/xprintf.c \
    src/libultra/vimodes/vimodempallan1.c \
    src/libultra/vimodes/vimodentsclan1.c \
    src/libultra/vimodes/vimodepallan1.c \
    src/overlays/ovl_ending/fox_end1.c \
    src/overlays/ovl_ending/fox_end2.c \
    src/overlays/ovl_i1/fox_co.c \
    src/overlays/ovl_i1/fox_i1.c \
    src/overlays/ovl_i1/fox_tr.c \
    src/overlays/ovl_i1/fox_tr360.c \
    src/overlays/ovl_i1/fox_ve1.c \
    src/overlays/ovl_i2/fox_i2.c \
    src/overlays/ovl_i2/fox_me.c \
    src/overlays/ovl_i2/fox_sx.c \
    src/overlays/ovl_i3/fox_a6.c \
    src/overlays/ovl_i3/fox_aq.c \
    src/overlays/ovl_i3/fox_i3.c \
    src/overlays/ovl_i3/fox_so.c \
    src/overlays/ovl_i3/fox_zo.c \
    src/overlays/ovl_i4/fox_bo.c \
    src/overlays/ovl_i4/fox_fo.c \
    src/overlays/ovl_i4/fox_i4.c \
    src/overlays/ovl_i4/fox_ka.c \
    src/overlays/ovl_i4/fox_sz.c \
    src/overlays/ovl_i5/fox_ground.c \
    src/overlays/ovl_i5/fox_i5.c \
    src/overlays/ovl_i5/fox_ma.c \
    src/overlays/ovl_i5/fox_ti.c \
    src/overlays/ovl_i5/fox_ti_cs.c \
    src/overlays/ovl_i6/fox_andross.c \
    src/overlays/ovl_i6/fox_i6.c \
    src/overlays/ovl_i6/fox_sy.c \
    src/overlays/ovl_i6/fox_turret.c \
    src/overlays/ovl_i6/fox_ve2.c \
    src/overlays/ovl_menu/fox_i_menu.c \
    src/overlays/ovl_menu/fox_map.c \
    src/overlays/ovl_menu/fox_option.c \
    src/overlays/ovl_menu/fox_title.c \
    src/overlays/ovl_unused/fox_unused.c \
    src/psp/input.c \
    src/psp/main.c \
    $(PSP_N64PSP_C_FILES) \
    src/psp/platform.c \
    src/psp/profiler.c \
    src/psp/ultra_reimpl.c \
    $(PSP_AUDIO_C_FILES) \
    $(PSP_RENDERER_C_FILES) \
    src/sys/sys_fault.c \
    src/sys/sys_joybus.c \
    src/sys/sys_lib.c \
    src/sys/sys_lights.c \
    src/sys/sys_main.c \
    src/sys/sys_math.c \
    src/sys/sys_matrix.c \
    src/sys/sys_memory.c \
    src/sys/sys_save.c \
    src/sys/sys_timer.c
