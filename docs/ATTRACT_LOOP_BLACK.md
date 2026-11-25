# The attract loop goes black on its second pass

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

So the **game** blanked the screen — that is a deliberate `$2100 = $80` — and
nothing turned brightness back on. This is not a missing tile, a zeroed palette
or a failed draw: the picture is suppressed, not broken.

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

**Where to look next.** Two things only the second pass does: it re-enters the
demo from a "already played" state, and it re-uses whatever the first pass left
loaded. The first useful step is to find the frame between 22,800 and 26,280
where the state first differs by more than the RNG/counter noise, and then the
routine that writes that byte. `SNESRECOMP_SPC_TRACE`,
`SNESRECOMP_SPC_PORT_TRACE`, `SNESRECOMP_APURAM_TRACE_FILE` and
`SNESRECOMP_TIER2_CAPTURE` are all available for that pass; `tools/oracle/`
holds the routes and the comparison helpers.

**For the player.** Nothing is lost by playing normally: the black is confined to
the idle attract loop, the game itself starts from the title and plays, and the
battery save is untouched. `diag.bat` on the play copy writes a screenshot every
three seconds if a new black screen needs capturing.
