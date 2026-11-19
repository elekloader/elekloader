| core-a4mk2 0.7: the audio interrupt's voice frees, for std1's ev_voice_end
| (voice.c). Both call stock's free 0x40105410(voice) as stock does; the
| free changes nothing for a voice already free, and core's own mask keeps
| the end to one per start. d0, d1, a0 and a1 are free at both sites.

        .text
        .globl  core_voice_free_cut, core_voice_free_len

| 0x40102174, a new sequencer trig cutting its track's held live note
| (voice %d4, already pushed as the free's argument): `jsr 0x40105410`
core_voice_free_cut:
        move.l  %d4,-(%sp)
        jsr     core_voice_end
        addq.l  #4,%sp
        jmp     0x40105410                | returns to 0x4010217a

| 0x40102e10, the note-length countdown's free (voice %d2; also STOP's
| kill-all and the note-off paths, which set the countdown to 1):
| `move.l %d2,-(%sp); jsr (%a1); addq.l #4,%sp`, %a1 always 0x40105410
core_voice_free_len:
        move.l  %d2,-(%sp)
        jsr     core_voice_end
        jsr     0x40105410
        addq.l  #4,%sp
        rts
