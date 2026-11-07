# Knob looks on the Analog Rytm MKII, OS 1.74

The stock ids a mod knob can borrow in `core_knob_looks` (core-ar2 0.12,
docs/ADAPTING.md "Knob looks (core-ar2 0.12, Analog Rytm MKII)"). A stock
1.74 boot builds a UI record for each of the 469 ids; by their flags, turn
steps and four delegates there are 87 different ones, and the ids in a row
below all draw alike. Borrow any id of the row whose knob looks as yours
should: the sample id is the one `examples/look-check-ar2` checks, drawn
pixel for pixel beside a mod knob that borrows it.

- **name (label):** the sample id's stock name and knob label, to find a
  knob on the unit that shows the look. A mod knob keeps its own.
- **range:** the sample id's, which a borrowing knob takes unless its entry
  gives min and max. Some rows' ids have other ranges; the look is the same.
- **graphic:** G1..G46 numbers the 46 different graphics (the delegate at
  +0x24), so rows with the same G draw the same graphic with other flags,
  steps or pop-up. G1 is the plain dial.
- **flags:** the UI record's +0, which parts the knob draws.
- **pop-up:** whether it has the value pop-up delegate (+0x44).

Look 1 is record 0's: what a mod knob without an entry draws. Mods moving
from hidden machines' records: voice-dac's POL is look 457's row (CB TEST's
switch), duck's and fx-sends' 1.0 knobs are 191's row.

Written by `examples/look-check-ar2/looks_table.py` from the same dump as
`looks.h` (README there).

| look | sample id | its name (label) | range | graphic | flags | pop-up | ids with the same UI record |
|---|---|---|---|---|---|---|---|
| 1 | 1 | Error (ERR) | 0..0 | G1 | 00000 | no | 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 55 433 438 442 |
| 2 | 29 | Note (NOT) | 36..84 | G2 | 80000 | yes | 29 |
| 3 | 30 | Velocity (VEL) | 1..127 | G1 | 00000 | yes | 30 |
| 4 | 31 | Length (LEN) | 0..127 | G1 | 00000 | yes | 31 |
| 5 | 32 | Micro Timing (uTM) | -23..23 | G3 | 40000 | no | 32 |
| 6 | 33 | Trig Condition (TRC) | 0..64 | G4 | c0000 | no | 33 |
| 7 | 34 | Synthesis Trig (SYN) | 0..1 | G5 | 00000 | no | 34 35 36 37 |
| 8 | 38 | Probability (PRB) | 0..100 | G6 | 80000 | no | 38 |
| 9 | 39 | Tune (TUN) | 40..88 | G7 | 00000 | yes | 39 40 74 91 125 140 147 157 163 169 171 173 174 177 179 180 183 184 187 188 215 218 221 222 229 230 234 236 237 279 287 292 325 334 342 356 358 359 372 374 376 377 381 390 392 399 |
| 10 | 41 | Sample Slot (SMP) | 0..127 | G8 | 80000 | no | 41 |
| 11 | 42 | Bit Reduction (BR) | 0..127 | G1 | 00000 | yes | 42 59 92 93 151 |
| 12 | 43 | Start (STA) | 0..120 | G9 | 00000 | yes | 43 44 |
| 13 | 45 | Loop (LOP) | 0..1 | G5 | 00000 | no | 45 248 |
| 14 | 46 | Level (LEV) | 0..127 | G10 | 00000 | yes | 46 63 79 88 99 101 109 118 131 134 144 148 156 162 170 175 181 185 204 213 216 219 227 232 278 286 301 303 311 318 326 335 343 352 365 368 378 397 398 |
| 15 | 47 | Frequency (FRQ) | 0..127 | G1 | 20000 | yes | 47 50 51 52 53 56 |
| 16 | 48 | Resonance (RES) | 0..127 | G1 | 20000 | yes | 48 |
| 17 | 49 | Filter Type (TYP) | 0..6 | G11 | 00000 | no | 49 |
| 18 | 54 | Env. Depth (ENV) | 0..127 | G7 | 00000 | yes | 54 |
| 19 | 57 | Hold Time (HLD) | 0..127 | G1 | 20000 | yes | 57 |
| 20 | 58 | Decay Time (DEC) | 0..127 | G1 | 20000 | yes | 58 |
| 21 | 60 | Delay Send (DEL) | 0..127 | G10 | 00000 | yes | 60 61 |
| 22 | 62 | Pan (PAN) | 0..127 | G12 | 00000 | yes | 62 |
| 23 | 64 | Speed (SPD) | 0..127 | G7 | 00000 | yes | 64 |
| 24 | 65 | Multiplier (MUL) | 0..23 | G13 | 80000 | yes | 65 |
| 25 | 66 | Fade In/Out (FAD) | 0..127 | G14 | 00000 | yes | 66 |
| 26 | 67 | Destination (DST) | 0..127 | G15 | 80000 | yes | 67 |
| 27 | 68 | Waveform (WAV) | 0..6 | G16 | 00000 | no | 68 |
| 28 | 69 | Start Phase (SPH) | 0..127 | G1 | 00000 | yes | 69 82 90 94 102 103 104 105 110 111 112 113 116 117 120 123 127 128 129 132 133 137 139 141 149 150 152 154 155 158 161 164 165 166 167 172 176 178 182 186 198 200 201 202 223 225 226 235 280 281 283 284 285 336 337 338 339 344 345 346 347 350 351 354 357 360 361 362 363 366 367 371 373 375 391 393 394 395 400 |
| 29 | 70 | Trig Mode (MOD) | 0..4 | G17 | 80000 | no | 70 |
| 30 | 71 | Depth (DEP) | 0..128.00 | G7 | 00000 | yes | 71 |
| 31 | 72 | Delay Time (TIM) | 0..127 | G1 | 00000 | yes | 72 |
| 32 | 73 | Pingpong (X) | 0..1 | G5 | 00000 | yes | 73 |
| 33 | 75 | Feedback (FDB) | 0..127 | G1 | 00000 | yes | 75 |
| 34 | 76 | HP Filter (HPF) | 0..127 | G1 | 20000 | yes | 76 77 86 87 |
| 35 | 78 | Reverb Send (REV) | 0..127 | G10 | 00000 | yes | 78 107 119 121 130 135 136 145 146 153 160 168 203 320 341 353 355 364 369 370 379 380 396 402 |
| 36 | 80 | Delay Overdr. (DOV) | 0..127 | G1 | 00000 | yes | 80 |
| 37 | 81 | Del Dist/Comp (DEL) | 0..1 | G18 | 80000 | yes | 81 89 |
| 38 | 83 | Decay Time (DEC) | 0..127 | G1 | 00000 | yes | 83 |
| 39 | 84 | Shelving Freq (FRQ) | 0..127 | G1 | 20000 | yes | 84 85 |
| 40 | 95 | Attack (ms) (ATK) | 0..6 | G19 | 80000 | no | 95 |
| 41 | 96 | Release (s) (REL) | 0..7 | G20 | 80000 | no | 96 |
| 42 | 97 | Sidechain EQ (SEQ) | 0..3 | G21 | 80000 | yes | 97 |
| 43 | 98 | Comp. Ratio (RAT) | 0..3 | G22 | 80000 | yes | 98 |
| 44 | 100 | Tune (TUN) | 32..96 | G23 | 00000 | yes | 100 108 122 124 138 142 143 189 196 197 199 205 254 262 270 288 |
| 45 | 106 | Waveform (WAV) | 0..2 | G24 | 00000 | yes | 106 114 340 348 |
| 46 | 115 | Transient (TRA) | 0..55 | G25 | 00000 | yes | 115 349 |
| 47 | 126 | Detune (DET) | 0..32 | G26 | 00000 | yes | 126 |
| 48 | 159 | Snap (SNP) | 0..3 | G27 | 00000 | yes | 159 |
| 49 | 190 | Level (LEV) | 0..127 | G10 | 00000 | no | 190 206 245 253 255 263 271 383 |
| 50 | 191 | FM Amount (FMA) | 0..127 | G1 | 00000 | no | 191 192 193 194 195 208 209 210 211 212 214 217 220 228 231 233 238 239 240 241 242 246 247 249 250 256 257 258 259 260 261 264 265 266 268 269 272 273 274 275 276 277 304 305 306 308 309 312 313 314 315 316 317 384 385 386 387 388 410 411 412 413 418 419 424 425 437 439 443 445 446 447 448 449 451 452 453 454 455 456 458 459 460 |
| 51 | 207 | Mod Type (TYP) | 0..1 | G28 | 80000 | no | 207 |
| 52 | 224 | Osc Reset (RST) | 0..1 | G5 | 00000 | yes | 224 |
| 53 | 243 | Sweep Depth (SWD) | 0..127 | G7 | 00000 | no | 243 389 435 |
| 54 | 244 | Decay (DEC) | 0..127 | G1 | 00000 | no | 244 |
| 55 | 251 | Repeat Count (NUM) | 0..127 | G1 | 00000 | no | 251 |
| 56 | 252 | Polarity (POL) | 0..1 | G29 | 00000 | no | 252 |
| 57 | 267 | Waveform (WAV) | 0..11 | G30 | 00000 | no | 267 307 |
| 58 | 282 | Cymbal Type (TYP) | 0..3 | G31 | 80000 | yes | 282 |
| 59 | 289 | Osc 2 Detune (DET) | 48..80 | G32 | 00000 | yes | 289 |
| 60 | 290 | Osc Config (CFG) | 0..79 | G33 | 00000 | no | 290 |
| 61 | 291 | Osc 2 Decay (DEC) | 0..127 | G1 | 00000 | yes | 291 293 297 319 |
| 62 | 294 | Tune (TUN) | 40..88 | G34 | 00000 | yes | 294 302 310 321 |
| 63 | 295 | Waveform (WAV) | 0..127 | G35 | 00000 | yes | 295 |
| 64 | 296 | Speed (SPD) | 0..65 | G36 | 00000 | yes | 296 |
| 65 | 298 | Offset2 (OF2) | 40..88 | G37 | 00000 | yes | 298 299 300 |
| 66 | 322 | Osc 2 Detune (DET) | 40..88 | G34 | 00000 | yes | 322 |
| 67 | 323 | Waveform 1 (WAV1) | 0..6 | G38 | 00000 | yes | 323 |
| 68 | 324 | Waveform 2 (WAV2) | 0..3 | G39 | 00000 | no | 324 |
| 69 | 327 | Osc1 Freq (OSC1) | 0..127 | G40 | 80000 | yes | 327 329 330 331 332 333 |
| 70 | 328 | Decay Time (DEC) | 0..127 | G1 | 00000 | yes | 328 |
| 71 | 382 | Tune (TUN) | 0..127 | G1 | 00000 | no | 382 |
| 72 | 401 | Snap (SNP) | 0..3 | G41 | 00000 | yes | 401 |
| 73 | 403 | Noise Level (NOL) | 0..128.00 | G1 | 00000 | yes | 403 404 405 407 |
| 74 | 406 | Pulse Level (PLV) | 0..128.00 | G7 | 00000 | yes | 406 |
| 75 | 408 | BD_TUNE1 (TU1) | 0..127.99 | G7 | 00000 | no | 408 409 416 417 422 423 426 427 430 |
| 76 | 414 | BD_FMOD_EN (FM) | 0..1 | G5 | 80000 | no | 414 415 |
| 77 | 420 | SAW1 SAW2 SINE2 (WAV) | 0..7 | G42 | 80000 | no | 420 421 428 429 |
| 78 | 431 | BT_DECAY (DEC) | 0..127 | G1 | 00000 | no | 431 432 436 440 441 |
| 79 | 434 | BT_BIT (BIT) | 0..1 | G5 | 00000 | no | 434 |
| 80 | 444 | HH_SELECT (SEL) | 0..3 | G43 | 00000 | no | 444 |
| 81 | 450 | CY_SELECT (SEL) | 0..2 | G24 | 00000 | no | 450 |
| 82 | 457 | CB_OSC_SELECT (SEL) | 0..1 | G28 | 00000 | no | 457 |
| 83 | 461 | Pulse Gen 1 (PL1) | 0..64 | G44 | 80100 | yes | 461 462 |
| 84 | 463 | Euclidean Mode (EUC) | 0..1 | G5 | 00100 | no | 463 |
| 85 | 464 | Rotation Gen 1 (RO1) | 0..126 | G45 | 00100 | yes | 464 465 466 |
| 86 | 467 | Bool Operator (OP) | 0..3 | G46 | 00100 | yes | 467 |
| 87 | 468 | Track Length (LEN) | 2..64 | G44 | 80100 | yes | 468 |
