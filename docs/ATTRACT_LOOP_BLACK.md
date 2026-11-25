# The attract loop goes black on its second pass

**Status: fixed** (`snesrecomp` `7f06b2a0`, branch `ff6-pin`). The cause was the
`$4210` NMI latch, not a scene load: the event interpreter parked inside
`play_song`, waiting for a vblank bit that a frame host never set. The
measurements that found it are kept below, then the cause, the fix and the
verification.

**Symptom.** Left untouched, the game plays the whole opening demo — title, the
Narshe cliff, the soldiers and Terra, the walk through the snow, the credits —
fades back to the title, starts the demo again, and then the screen stays black
forever. The emulator does not crash and does not slow down: it keeps running at
59.99 fps with a blank screen.

**Reproduced without any input.** Run the exe with a script that only waits:

    FinalFantasyViSNESRecomp.exe --no-launcher --rom ff3u11.sfc \
      --script C:/Temp/idle.txt          # wait 30600 / quit

Nothing is pressed, so the attract loop runs on its own. Three independent runs
put the black at the same frame:

| run | frames | first black frame |
|---|---|---|
| player's session (diag.bat) | 29,596 | 26,281 |
| headless, frame dumps every 2 s | 24,000 (stopped before) | — |
| headless, dumps every 120 frames | 26,520 | 26,280 |

**What the frame before it shows.** The second pass does start: at frame 26,101
the picture is the intro's opening shot (the cliff at night, pink-lit clouds,
the lights of Narshe below). One second later the picture is gone.

**What the PPU is doing at the black** (state dump at frame 27,000,
`p2.regs.json`):

    inidisp      = $80     <- forced blank, brightness 0
    tm           = $1F     <- all five layers enabled
    hdmaen_last  = 0       <- no HDMA in flight
    fb           = all zero pixels

That `$80` turned out to be the ordinary end-of-frame blank (the field IRQ
writes it at scanline 215 every frame), and `tm = $1F` meant rendering was
fine. What was actually wrong is the brightness variable: `$4c` was 0 and
nothing ever raised it, because the script that would have faded in never ran.
The picture was not broken or suppressed — it was simply at brightness zero.

**The sound chip is not the blocker.** The APU port trace (`.apu.tsv`, a 65,536
event window) ends at frame 26,277 in dumps taken 120 frames apart, i.e. the CPU
stops touching `$2140-$2143` entirely:

    26273  cpu_wr $2140=$F0 $2141=$02 $2142=$FF $2143=$00   (music command)
    26273  spc_wr $2140=$F0                                  (driver echoes it)
    26277  cpu_wr $2140=$11 $2141=$02 $2142=$80 $2143=$1B
    26277  spc_rd $2140=$11                                  (driver reads it)
    ...     no further port traffic

A CPU blocked on the driver would keep reading `$2140` and leave a trail. There
is no trail, so the wait is elsewhere.

**Measured against the reference.** Same route, same frame numbers, on
bsnes-accuracy (`snesref`) with per-frame WRAM dumps on both sides:

| frame | differing WRAM bytes (port vs bsnes) |
|---|---|
| 22,800 | 21 |
| 23,040 | 20 |
| 23,160 | 177 |
| 25,080 | 20 |
| 26,040 | 48 |
| 26,160 | 46 |
| 26,280 | 745 |
| 26,400 | 26,408 |

At 27,000 the reference is showing the prologue narration ("Long ago, the War of
the Magi reduced the world to a scorched wasteland…") over the drawn scene; the
port is blank. The two machines agree to within a few dozen bytes (RNG and
counters) through the whole first pass and then come apart at the blank.

**Ruled out.**

- Not a crash: `0 unresolved-abandon`, `0` SEH events, clean `SDL_QUIT` exit.
- Not the sound driver (above).
- Not the QoL code: the pre-widescreen encounter build and the current build
  produce byte-identical frames through this whole stretch.
- Not the intro's own fade: the port reproduces the intro's first scene change
  correctly — black at 7,860, picture back at 8,220, against the cartridge's
  black at 7,960/8,080 and picture at 8,200.

**Where it was stuck.** The event-script pointer sat on `$CC:985D` for ever — the
first command of the prologue-narration script (post-fix dumps show it advancing
to `$CC:98CA`):

    _cc985b:
            play_song SILENCE                 <- stuck here
            load_map 23, {18, 25}, DOWN, {ASYNC, NO_FADE_IN}
            dlg DLG_6, {ASYNC, TEXT_ONLY}
                    ;  Long ago, the War of the Magi ...
            ...

`EventCmd_f0` (src/field/event.asm) ends:

    @b799:  sta $1300        ; start this song
            jsl ExecSound_ext
    @b7a0:  lda hRDNMI       ; $4210 -- "a vblank happened"
            bpl @b7a0        ; loop while clear
            lda #2
            jmp IncEventPtrContinue

The game spins on `$4210` bit 7, never reaches `load_map 23`, so brightness stays
0 and the screen stays black. Interrupts were delivered every frame throughout
(the handler's `$45`/`$46` counters advanced once per field, exactly as the ROM
does), which is why the machine looked alive.

**Root cause -- three defects that had to line up.**

1. `beam_frame_driver.c` and `generic_frame_driver.c` deliver the NMI themselves
   at the start of each field instead of walking the beam to V=225. Only
   `snes_runClocks` set `rdnmiPending` (the `$4210` "NMI occurred" latch) -- the
   frame hosts never did, so there was nothing for the poll to see.
2. `snes_runClocks` cleared `rdnmiPending` again at the field wrap (`v >= 262`),
   wiping a latch a frame host had just set for the field it was about to run.
3. `snes_readReg($4210)` cleared `inNmi` on every read. `inNmi` protects the latch
   from the handler's own acknowledgement read, so the *first* read inside the
   handler ended the "inside the handler" window and a later read in the same
   handler cleared the latch before the main loop could look.

The framework already carried a comment about this deadlock ("Final Fantasy VI
deadlocked exactly that way at the end of its intro") and a commit
`snes: keep the NMI-pending latch for a main-loop $4210 poll`, but the latch was
only ever armed on the beam path, which this build does not use, and the field
wrap and the read-side clear both fought it.

**Fix.**

- Both frame hosts latch `g_snes->rdnmiPending = true` when they deliver the NMI.
- The field wrap no longer clears it: on hardware the bit is set at vblank and
  stays set until the CPU reads it, which is what the frame host now models.
- The `$4210` read no longer clears `inNmi`; the host owns that flag and clears
  it after the handler returns.

**Verified -- same no-input route, frames 26,200-28,000, port against bsnes.**

| frame | before fix | after fix | bsnes |
|---|---|---|---|
| 26,200 | picture | picture | picture |
| 26,320 | black | black | narration text |
| 26,560 | **black** | picture (12,174) | picture (12,145) |
| 26,800-27,040 | **black** | picture | picture |
| 27,160 | black | black | picture (8,182) |
| 27,280-27,880 | **black** | picture (12,188) | picture (12,152) |
| 28,000 | black | black | black |

Blank/picture pattern agrees with the cartridge on 13 of 16 dump pairs. The state
that was frozen now moves (event pointer `$CC:985D -> $CC:98CA`, map index
`$0012 -> $00F4`), and a 40,000-frame no-input run keeps cycling scenes to the end
instead of stopping dead at 26,280.

**Residual -- measured at 12-frame granularity, and smaller than it looked.** At
120-frame sampling three of sixteen frames read "port black, cartridge drawing",
which is what suggested the port was slower into each fade. It is not. Dumping
both machines every 12 frames across the transition (26,280-26,700) shows the two
in lockstep:

| frame | port event PC | port `$4c` | cartridge PC | cartridge `$4c` | WRAM bytes differing |
|---|---|---|---|---|---|
| 26,304 | `$986D` | 00 | `$9865` | 00 | 8,127 |
| 26,328 | `$986D` | 00 | `$986D` | 00 | 154 |
| 26,376 | `$9878` | 00 | `$986D` | 00 | 277 |
| 26,400 | `$9878` | 00 | `$9878` | 00 | 120 |
| 26,484 | `$9885` | 18 | `$987B` | 00 | 139 |
| 26,508 | `$9885` | 48 | `$9885` | 24 | 125 |
| 26,592 | `$9885` | f0 | `$9885` | cc | 97 |
| 26,616 | `$9885` | f0 | `$9885` | f0 | 168 |
| 26,700 | `$9885` | f0 | `$9885` | f0 | 54 |

Same script position (within one sample step), same map id, same brightness ramp
at the same slope (2 levels per frame), and the whole of WRAM differs by 35-170
bytes out of 131,072 -- the RNG/counter noise this port carries at its best
(scene 5,084 measured 2-5 bytes and the pre-fix transition here measured 26,408).
Against that, a 120-frame sample of a fade that moves 2 levels per frame turns a
one-frame capture offset into what looks like a 200-frame lag. The three "port
black, cartridge drawing" frames were the two dumps not being the same instant.

So: the freeze is fixed, the state matches the cartridge, and what is left is a
capture-phase difference between the port's dump and the core's video output --
worth remembering the next time picture-vs-picture is used across platforms, not
worth chasing.

**For the player.** The attract loop now cycles indefinitely; normal play is
unchanged and the battery save is untouched. `diag.bat` on the play copy writes a screenshot every
three seconds if a new black screen needs capturing.
