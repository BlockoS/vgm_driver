#include "huc.h"
#include "vgm.h"

void main() {
    /*  disable display */
	disp_off();

	/*  clear display */
	cls();

    /*  enable display */
	disp_on();

    /* play song 0 */
    vgm_play_song(0);

    /* play subtrack 0 on channel 3 */
    vgm_play_subtrack(3, 0);

	for (;;) {
        /* update vgm driver */
        vgm_update();
        vsync();
    }
}

#asm
vgm_mpr = 3
vgm_data_bank = 4
vgm_data_addr = vgm_mpr<<13

    .include "out/music.inc"
#endasm
