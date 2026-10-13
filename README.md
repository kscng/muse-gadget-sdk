# StackChan × Muse

I turned an [M5Stack StackChan](https://docs.m5stack.com/en/StackChan) (a CoreS3 on a small robot body with
two servos) into a desk companion for [Muse](https://developer.meta.com/ai/lp/muse-code/), by building on Meta's
open-source [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk). It listens, answers out loud in
Cantonese or English, and moves its head while it does.

This is a fork. The original SDK README is kept as [README.upstream.md](README.upstream.md). What I added is
described below, and in detail in [stackchan/README.md](stackchan/README.md). I built it together with
[Claude Code](https://claude.com/claude-code), testing each step on the real device.

## What I built

- **It talks.** Muse's replies are spoken through ElevenLabs (`eleven_v4_turbo`), in English and Cantonese. The SDK
  only showed text.
- **It understands Cantonese, English and a mix.** Muse's own speech recognition kept guessing Vietnamese or
  Japanese for my Cantonese, so I added ElevenLabs Scribe with a Cantonese language hint. English words inside a
  Cantonese sentence come through correctly. A software boost helps quiet speech.
- **Hands-free.** Hold your hand near the screen, or click an app on my Mac, and it wakes, beeps and listens.
  It stops about a second and a half after I stop talking, judged against the room's own noise, so a fan doesn't
  fool it.
- **It moves.** The head glances around, sways while thinking, nods while speaking, and its gesture follows the
  reply: a question tilts, "yes" nods, "no" shakes, good news bounces. Petting the top of its head makes it purr.
- **It can see.** One photo on request, either from my Mac or when Muse asks for it. Nothing streams and nothing is
  stored.
- **Wi-Fi control.** A small token-protected server lets a Mac trigger listening or take a photo, with clickable
  apps and a shell command.
- **Slow jobs.** If Muse is still working on a long task, the StackChan waits (up to 15 minutes) and speaks the
  answer when it arrives.

```
you speak ─▶ microphone (+boost) ─▶ ElevenLabs Scribe ─▶ "[StackChan] 你好…" ─▶ Muse
                                                                                   │
speaker ◀─ ElevenLabs voice ◀─ reply text ◀────────────────────────────────────────┘
head gestures ◀─ reply tone          camera ◀─ Muse asks for a photo
```

## What didn't work

- The 12 LEDs on the body stayed dark, although the chip accepted every command and M5Stack's own start-up sequence
  is identical, so I removed that code.
- The infrared receiver never saw my remote, so learning an air-conditioner remote is unfinished (the code is
  there, marked experimental).
- Photos can show a torn band across the picture, and are dark in a dark room.

## Things I learned the hard way

- ElevenLabs' certificate chain ends at the old GlobalSign root, which ESP-IDF's built-in list lacks, so HTTPS
  failed until I added that root (it expires in January 2028).
- The microphone gain was already at the chip's maximum, so "more sensitive" had to be done in software.
- A fixed "is this speech?" level fails in a room with a fan; it has to be relative to the room's noise.
- M5Stack's camera needs its sensor driver switched on and a smaller DMA buffer to fit in memory here.

## Try it

You need a StackChan, a [Muse SDK token](https://gadgets.muse.ai/settings/sdk-tokens), and optionally an ElevenLabs
key. Setup, every setting and the file list are in [stackchan/README.md](stackchan/README.md). Flashing custom
firmware can brick a board, so back up the original first. The servo home positions are per unit: read yours from
your own backup (the guide says how).

## Credits and licence

Meta's Muse Gadget SDK (Apache-2.0), M5Stack's [StackChan-BSP](https://github.com/m5stack/StackChan-BSP) (MIT, used
as a reference for the hardware), Espressif's ESP-IDF, and ElevenLabs for voice and transcription. My additions
follow the SDK's Apache-2.0 licence. The avatar is not included: your Muse draws its own, and mine is a third-party
character.
