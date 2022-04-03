void __fastcall vgm_play_song(char id<acc>);
void __fastcall vgm_stop_song();

void __fastcall vgm_play_subtrack(char ch<__al>, char id<acc>);
void __fastcall vgm_stop_subtrack(char ch<acc>);

void __fastcall vgm_update();

#asm
    .include "vgm.s"
#endasm
