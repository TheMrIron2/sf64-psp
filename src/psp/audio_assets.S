    .section .rodata
    .balign 64

    .global audio_seq_ROM_START
audio_seq_ROM_START:
    .incbin "baserom/audio_seq.us.rev1.bin"
    .global audio_seq_ROM_END
audio_seq_ROM_END:

    .balign 64
    .global audio_bank_ROM_START
audio_bank_ROM_START:
    .incbin "baserom/audio_bank.us.rev1.bin"
    .global audio_bank_ROM_END
audio_bank_ROM_END:

    .balign 64
    .global audio_table_ROM_START
audio_table_ROM_START:
    .incbin "baserom/audio_table.us.rev1.bin"
    .global audio_table_ROM_END
audio_table_ROM_END:

    .global audio_seq_VRAM
    .set audio_seq_VRAM, audio_seq_ROM_START
    .global audio_seq_VRAM_END
    .set audio_seq_VRAM_END, audio_seq_ROM_END
    .global audio_bank_VRAM
    .set audio_bank_VRAM, audio_bank_ROM_START
    .global audio_bank_VRAM_END
    .set audio_bank_VRAM_END, audio_bank_ROM_END
    .global audio_table_VRAM
    .set audio_table_VRAM, audio_table_ROM_START
    .global audio_table_VRAM_END
    .set audio_table_VRAM_END, audio_table_ROM_END

    .global audio_seq_TEXT_START
    .set audio_seq_TEXT_START, audio_seq_ROM_START
    .global audio_seq_TEXT_END
    .set audio_seq_TEXT_END, audio_seq_ROM_START
    .global audio_seq_DATA_START
    .set audio_seq_DATA_START, audio_seq_ROM_START
    .global audio_seq_DATA_END
    .set audio_seq_DATA_END, audio_seq_ROM_END
    .global audio_seq_DATA_SIZE
    .set audio_seq_DATA_SIZE, audio_seq_ROM_END - audio_seq_ROM_START
    .global audio_seq_RODATA_START
    .set audio_seq_RODATA_START, audio_seq_ROM_START
    .global audio_seq_RODATA_END
    .set audio_seq_RODATA_END, audio_seq_ROM_END
    .global audio_seq_BSS_START
    .set audio_seq_BSS_START, audio_seq_ROM_END
    .global audio_seq_BSS_END
    .set audio_seq_BSS_END, audio_seq_ROM_END

    .global audio_bank_TEXT_START
    .set audio_bank_TEXT_START, audio_bank_ROM_START
    .global audio_bank_TEXT_END
    .set audio_bank_TEXT_END, audio_bank_ROM_START
    .global audio_bank_DATA_START
    .set audio_bank_DATA_START, audio_bank_ROM_START
    .global audio_bank_DATA_END
    .set audio_bank_DATA_END, audio_bank_ROM_END
    .global audio_bank_DATA_SIZE
    .set audio_bank_DATA_SIZE, audio_bank_ROM_END - audio_bank_ROM_START
    .global audio_bank_RODATA_START
    .set audio_bank_RODATA_START, audio_bank_ROM_START
    .global audio_bank_RODATA_END
    .set audio_bank_RODATA_END, audio_bank_ROM_END
    .global audio_bank_BSS_START
    .set audio_bank_BSS_START, audio_bank_ROM_END
    .global audio_bank_BSS_END
    .set audio_bank_BSS_END, audio_bank_ROM_END

    .global audio_table_TEXT_START
    .set audio_table_TEXT_START, audio_table_ROM_START
    .global audio_table_TEXT_END
    .set audio_table_TEXT_END, audio_table_ROM_START
    .global audio_table_DATA_START
    .set audio_table_DATA_START, audio_table_ROM_START
    .global audio_table_DATA_END
    .set audio_table_DATA_END, audio_table_ROM_END
    .global audio_table_DATA_SIZE
    .set audio_table_DATA_SIZE, audio_table_ROM_END - audio_table_ROM_START
    .global audio_table_RODATA_START
    .set audio_table_RODATA_START, audio_table_ROM_START
    .global audio_table_RODATA_END
    .set audio_table_RODATA_END, audio_table_ROM_END
    .global audio_table_BSS_START
    .set audio_table_BSS_START, audio_table_ROM_END
    .global audio_table_BSS_END
    .set audio_table_BSS_END, audio_table_ROM_END
