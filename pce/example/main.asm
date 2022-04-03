    .code
    .bank $00
    .org $e000

    .include "startup.asm"

    .zp
counter .ds 2

    .code
_main:
	; clear irq config flag
	stz    <irq_m

    ; set vsync vec
    irq_on INT_IRQ1

	; set vsync vec
	irq_on INT_IRQ1
	irq_enable_vec VSYNC
	irq_set_vec #VSYNC, #vsync_proc

    ldx    #3
    cly
    jsr    vgm_play_subtrack

    cla
    jsr    vgm_play_song

    lda    #low(360)
    sta    <counter
    lda    #high(360)
    sta    <counter+1

	cli    
@loop:
	vdc_wait_vsync

    lda    <counter
    ora    <counter+1
    beq    @loop

    lda    <counter
    sec
    sbc    #$01
    sta    <counter
    lda    <counter+1
    sbc    #$00
    sta    <counter+1
    ora    <counter
    bne    @loop

    ldx    #3
    jsr    vgm_stop_subtrack

	bra    @loop

vsync_proc:
	jsr    vgm_update
	rts

    .include "../../pce/vgm.s"

    .bank $01
    .org $4000

vgm_mpr = 3
vgm_data_bank = 2
vgm_data_addr = vgm_mpr<<13

    .include "out/music.inc"
