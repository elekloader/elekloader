# voiceio-check-ar2

A test for core-ar2's note ends (`core_voice_released`), track mutes
(`core_track_mutes`, `core_set_track_mutes`) and DIN MIDI out
(`core_midi_din_try_send`, `core_midi_din_up`) on the Analog Rytm MKII
(OS 1.74), on screen. `check.c` says what each key does and what the bar
shows.

Choose BD VIO on the BD track, then press PLAYMODE to show the bar. A BD
trig with a note length counts T and then R as its note ends, and sends a
DIN note-off from the audio interrupt (I). QPER mutes and unmutes the BD
track as the mute key does (M bit 0, the pad's mute light and the sound),
without MIDI out. FLTR sends a note-on and a note-off on DIN (D). With
OUT PORT FUNC set to DIN24 or DIN48, U is 0 and every DIN send is refused.
