VGM_PSG_REG_COUNT = 10
VGM_PSG_CHAN_COUNT = 6

PCE_VGM_FRAME_END = $00
PCE_VGM_GLOBAL_VOL = $01
PCE_VGM_FINE_FREQ = $02
PCE_VGM_ROUGH_FREQ = $03
PCE_VGM_VOL = $04
PCE_VGM_PAN = $05
PCE_VGM_WAV = $06
PCE_VGM_NOISE_FREQ = $07
PCE_VGM_LFO_FREQ = $08
PCE_VGM_LFO_CTRL = $09
PCE_VGM_SLEEP = $e0
PCE_VGM_DATA_END = $ff

    .zp
vgm_state .ds 1
vgm_si .ds 2
vgm_wav.si .ds 2
vgm_wav.ptr .ds 2

    .bss
vgm_bank        .ds VGM_PSG_CHAN_COUNT
vgm_addr.hi     .ds VGM_PSG_CHAN_COUNT
vgm_addr.lo     .ds VGM_PSG_CHAN_COUNT
vgm_loop.bank   .ds VGM_PSG_CHAN_COUNT
vgm_loop.hi     .ds VGM_PSG_CHAN_COUNT
vgm_loop.lo     .ds VGM_PSG_CHAN_COUNT

vgm_delay       .ds VGM_PSG_CHAN_COUNT
vgm_wav_id      .ds VGM_PSG_CHAN_COUNT

vgm_sub_bank        .ds VGM_PSG_CHAN_COUNT
vgm_sub_addr.hi     .ds VGM_PSG_CHAN_COUNT
vgm_sub_addr.lo     .ds VGM_PSG_CHAN_COUNT

vgm_sub_delay       .ds VGM_PSG_CHAN_COUNT
vgm_sub_wav_id      .ds VGM_PSG_CHAN_COUNT

vgm_psg_ch      .ds 1
vgm_psg_wav_id  .ds VGM_PSG_CHAN_COUNT
vgm_psg_r1      .ds 1                       ; global volume
vgm_psg_r2      .ds VGM_PSG_CHAN_COUNT      ; fine frequency
vgm_psg_r3      .ds VGM_PSG_CHAN_COUNT      ; rough frequency
vgm_psg_r4      .ds VGM_PSG_CHAN_COUNT      ; channel on/off, dda on/off, channel volume
vgm_psg_r5      .ds VGM_PSG_CHAN_COUNT      ; channel balance
vgm_psg_r7      .ds 2                       ; noise enable, noise frequency
vgm_psg_r8      .ds 1                       ; lfo frequency
vgm_psg_r9      .ds 1                       ; lfo trigger, lfo control

vgm_mem_end:

    .code
; map music
  .macro vgm_map
    tma   #page(song)
    pha

    lda   #bank(song)
    tam   #page(song)
  .endm

; unmap music
  .macro vgm_unmap
    pla
    tam   #page(song)
  .endm


; Play song
  .ifdef HUC
_vgm_play_song.1:
    sax
  .endif
vgm_play_song:
    asl    A
    tax

    vgm_map

    lda    song, X
    sta    <vgm_si
    lda    song+1, X
    sta    <vgm_si+1

    cly
@l0:
    lda    [vgm_si], Y
    sta    vgm_bank, Y
    
    iny
    cpy    #(VGM_PSG_CHAN_COUNT*6)
    bne    @l0

    stz    vgm_delay
    tai    vgm_delay, vgm_delay+1, vgm_mem_end-vgm_delay-1
    
    stw #wav, vgm_wav.ptr 

    vgm_unmap

    rts

; Stop song
  .ifdef HUC
_vgm_stop_song:
  .endif
vgm_stop_song:
    clx
@l0:
    stz    vgm_addr.hi, X
    stz    vgm_addr.lo, X
    
    lda    vgm_sub_addr.hi, X
    ora    vgm_sub_addr.lo, X
    bne    @l1
        stx    $0800
        stz    $0804
        stz    vgm_psg_r4, X
@l1:
    inx
    cpx    #VGM_PSG_CHAN_COUNT
    bne    @l0

    lda    vgm_psg_ch
    sta    $0800
    
    rts

; Play subtrack
  .ifdef HUC
_vgm_play_subtrack.2:
    sxy
    ldx    <__al
  .endif
vgm_play_subtrack:
    vgm_map

    lda    subtracks.bank, Y
    sta    vgm_sub_bank, X
    lda    subtracks.hi, Y
    sta    vgm_sub_addr.hi, X
    lda    subtracks.lo, Y
    sta    vgm_sub_addr.lo, X

    stw #wav, vgm_wav.ptr 

    vgm_unmap
    rts

; backup MPRs
  .macro vgm_backup_mprs
    tma    #vgm_mpr
    pha
    tma    #(vgm_mpr+1)
    pha
  .endm

; restore MPRS
  .macro vgm_restore_mprs
    pla
    tam    #(vgm_mpr+1)
    pla
    tam    #(vgm_mpr)
  .endm

; VGM driver update
  .ifdef HUC
_vgm_update:
  .endif
vgm_update:
    vgm_backup_mprs
    vgm_map

    stz    <vgm_state

    clx                                         ; update each channel
@loop:
    stx    vgm_psg_ch
    stx    $0800                                ; select PSG channel
    
    rmb0   <vgm_state

    lda    vgm_sub_addr.lo, X
    ora    vgm_sub_addr.hi, X
    beq    @track
        smb0   <vgm_state

        lda    vgm_sub_delay, X                 ; skip channel update until delay is 0
        beq    @update.sub
@wait_sub:
            dec    vgm_sub_delay, X
            bra    @track
@update.sub:
        cly
        jsr    vgm_update_sub_ch                ; update subtrack

@track:
    lda    vgm_addr.lo, X
    ora    vgm_addr.hi, X
    beq    @next

    lda    vgm_delay, X                         ; skip channel update until delay is 0
    beq    @update
@wait:
        dec    vgm_delay, X
        bra    @next
@update:
    cly
    jsr    vgm_update_ch                        ; update channel

@next:
    inx
    cpx    #VGM_PSG_CHAN_COUNT
    bcc    @loop

    vgm_unmap
    vgm_restore_mprs

    bbr7   <vgm_state, @end                     ; check if all channels reached the song end
@reset:
        clx                                     ; loop (always)
@l0:
        lda    vgm_loop.lo, X                   ; reset all pointers
        sta    vgm_addr.lo, X

        lda    vgm_loop.hi, X
        sta    vgm_addr.hi, X

        lda    vgm_loop.bank, X
        sta    vgm_bank, X

        stz    vgm_delay, X

        inx
        cpx    #VGM_PSG_CHAN_COUNT
        bne    @l0
@end:
    rts

; update subtrack
vgm_update_sub_ch:
    lda    vgm_sub_bank, X                      ; map vgm subtrack
    tam    #vgm_mpr
    inc    A
    tam    #(vgm_mpr+1)

    lda    vgm_sub_addr.lo, X                   ; setup pointer
    sta    <vgm_si
    lda    vgm_sub_addr.hi, X
    sta    <vgm_si+1

    jsr    vgm_parse_sub                         ; decode commands
    
    tya                                         ; update pointer and memory map
    clc
    adc    <vgm_si
    sta    vgm_sub_addr.lo, X
    lda    <vgm_si+1
    adc    #$00
    cmp    #((vgm_mpr+1) << 5)
    bcc    @l0
        sbc    #$20
        inc    vgm_sub_bank, X
@l0:
    sta    vgm_sub_addr.hi, X
    rts

vgm_parse_sub:
    lda    [vgm_si], Y
    beq    @frame_end
    cmp    #PCE_VGM_DATA_END
    bcs    vgm_stop_subtrack
    cmp    #PCE_VGM_WAV
    beq    @wav_upload
    cmp    #PCE_VGM_SLEEP
    bcs    @rest

@reg_set:
    phx
    tax
    iny
    lda    [vgm_si], Y
    sta    $800, X
    plx
    iny
    bra    vgm_parse_sub

@rest:
    iny
    lda    [vgm_si], Y
    sta    vgm_sub_delay, X
    iny
    rts

@wav_upload:
    iny
    lda    [vgm_si], Y
    jsr    vgm_wav_upload
    iny
    bra    vgm_parse_sub

@frame_end:
    iny
    rts

; Stop subtrack
  .ifdef HUC
_vgm_stop_subtrack.1:
  .endif
vgm_stop_subtrack:
    php
    sei

    stx    $0800
    stz    vgm_sub_addr.lo, X
    stz    vgm_sub_addr.hi, X

    plp
    cli

    ; restore wav buffer
    lda    vgm_wav_id, X
    jsr    vgm_wav_upload

    lda    vgm_psg_r1
    sta    $0801
    lda    vgm_psg_r8
    sta    $0808
    lda    vgm_psg_r9
    sta    $0809
    
    lda    vgm_psg_r2, X
    sta    $0802
    lda    vgm_psg_r3, X
    sta    $0803
    lda    vgm_psg_r4, X
    sta    $0804
    lda    vgm_psg_r5, X
    sta    $0805

    cpx    #4
    bcc    @end.1
        lda    vgm_psg_r7-4, X
        sta    $0807
@end.1:
    lda    vgm_psg_ch
    sta    $0800
    
    cly
    stz    <vgm_si
    stz    <vgm_si+1
    rts

; Update channel
vgm_update_ch:   
    lda    vgm_bank, X                          ; map vgm track
    tam    #vgm_mpr
    inc    A
    tam    #(vgm_mpr+1)

    lda    vgm_addr.lo, X                       ; setup pointer
    sta    <vgm_si
    lda    vgm_addr.hi, X
    sta    <vgm_si+1

    jsr    vgm_parse                            ; decode commands
    jsr    vgm_remap                            ; update pointer and memory map
    rts

vgm_parse:
    lda    [vgm_si], Y
    beq    @frame_end
    cmp    #PCE_VGM_DATA_END
    bcs    @end
    cmp    #PCE_VGM_WAV
    beq    @wav_upload
    cmp    #PCE_VGM_SLEEP
    bcs    @rest

@reg_set:
    phx
    tax
    iny
    lda    [vgm_si], Y
    bbs0   <vgm_state, @reg_store
       sta    $800, X
@reg_store:
    sax
    asl    A
    sax
    jmp    [@reg_tbl, X]

@rest:
    iny
    lda    [vgm_si], Y
    sta    vgm_delay, X
    iny
    rts

@wav_upload:
    iny
    lda    [vgm_si], Y
    sta    vgm_wav_id, X
    bbs0   <vgm_state, @wav_upload.skip
        jsr    vgm_wav_upload
@wav_upload.skip:
    iny
    bra    vgm_parse

@frame_end:
    iny
    rts

@end:
    smb7    <vgm_state
    rts

@reg_tbl:
    .dw @psg_none
    .dw @psg_r1
    .dw @psg_r2
    .dw @psg_r3
    .dw @psg_r4
    .dw @psg_r5
    .dw @psg_none
    .dw @psg_r7
    .dw @psg_r8
    .dw @psg_r9

@psg_r1:
    sta    vgm_psg_r1
    plx
    iny
    jmp    vgm_parse
@psg_r2
    plx
    sta    vgm_psg_r2, X
    iny
    jmp    vgm_parse
@psg_r3
    plx
    sta    vgm_psg_r3, X
    iny
    jmp    vgm_parse
@psg_r4
    plx
    sta    vgm_psg_r4, X
    iny
    jmp    vgm_parse
@psg_r5
    plx
    sta    vgm_psg_r5, X
    iny
    jmp    vgm_parse
@psg_r7
    plx
    sta    vgm_psg_r7-4,X
    iny
    jmp    vgm_parse
@psg_r8
    sta    vgm_psg_r8
    plx
    iny
    jmp    vgm_parse
@psg_r9
    sta    vgm_psg_r9
    plx
    iny
    jmp    vgm_parse

@psg_none
    plx
    iny
    jmp    vgm_parse

vgm_remap:
    tya
    clc
    adc    <vgm_si
    sta    vgm_addr.lo, X
    lda    <vgm_si+1
    adc    #$00
    cmp    #((vgm_mpr+1) << 5)
    bcc    @l0
        sbc    #$20
        inc    vgm_bank, X
@l0:
    sta    vgm_addr.hi, X
    rts

vgm_wav_upload:
    sta    vgm_psg_wav_id, X

    vgm_map

    phy

    asl    A
    tay
    lda    [vgm_wav.ptr], Y
    sta    <vgm_wav.si
    iny
    lda    [vgm_wav.ptr], Y
    sta    <vgm_wav.si+1

    stz    $0804
    cly
@l0:
    lda    [vgm_wav.si], Y
    sta    $0806
    iny
    cpy    #$20
    bne    @l0

    lda    #$80                     ; [todo] reload $0804
    sta    $0804
    ply

    vgm_unmap

    rts
