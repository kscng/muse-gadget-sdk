# StackChan on the Muse Gadget SDK

Additions to the [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk) for an
[M5Stack StackChan](https://docs.m5stack.com/en/StackChan): a CoreS3 on a robot body with two servos, a
three-zone touch strip, a proximity sensor, a camera and an IR receiver and LED. The SDK already runs a CoreS3
(`tools/muse/board.sh build cores3`); this adds the body, a voice that speaks Muse's replies, better speech
recognition for Cantonese and English, and a Wi-Fi trigger. Built on Meta's SDK as of 16 October 2026 (upstream commit `f60347a`): first made from commit `812c46f`, then merged with the newer SDK (new boards, SDK-token pairing, web flasher).

Tested on one StackChan with Muse. Flashing custom firmware can brick a board; the SDK's own warning applies.

## What it does

| Feature | How it works |
|---|---|
| **Spoken replies** | Muse's reply text goes to ElevenLabs (`eleven_v4_turbo`) and plays through the speaker. It replaces the SDK's silent captions. Retries when ElevenLabs is busy. Needs the root-certificate fix below. The reply's tone adds an audio tag so it sounds curious, excited or apologetic. |
| **Speech to text** | After you stop talking, the recording goes to ElevenLabs Scribe v2 with a Cantonese hint (`yue`; English and mixed sentences still work) and Muse gets `[StackChan] <text>`. Falls back to sending a voice note if Scribe is unreachable. About $0.22 per hour of audio. |
| **Quiet-speech boost** | Software gain of up to +15 dB on quiet speech (the mic hardware is already at its maximum), never past clipping. |
| **Emotions** | Muse opens a reply to the StackChan with one tag from a fixed list (`[excited] [playful] [peaceful] [amazed] [anxious] [frustrated] [scared] [tired] [curious] [angry] [sad] [laughs] [crying] [sighs] [clears throat] [mischievously]`). The tag is hidden on the screen, passed to ElevenLabs as an audio tag so the voice sounds that way, and picks a matching head gesture (bounce, shake, droop, tilt). Without a tag, the reply's tone picks one. Needs a standing instruction in Muse (below). |
| **Hands-free listening** | A rising chirp, then recording, then a falling chirp. Stops about 1.4 s after you stop talking, judged against the room's own noise level. Started from the Wi-Fi trigger or the proximity sensor. |
| **Hand near the screen** | The proximity sensor (LTR-553) wakes the device and starts listening. |
| **Head movement** | Servos follow the avatar: idle glances, a sway while thinking, nods while speaking, a shake on errors, a wiggle with the hearts. Still while you hold PWR and while the screen sleeps. |
| **Reply tone** | The first sentence of a reply picks a gesture: a question tilts, yes nods, no shakes, cheer bounces (English and Cantonese). |
| **Petting** | The head touch strip wakes the screen, shows the hearts, and a long hold purrs. |
| **Camera** | One photo on request: the Muse command `camera.capture`, and `GET /photo` over Wi-Fi (full picture, for checking what it sees). A two-note beep warns that a photo is coming and a camera click marks the shot. For Muse, the photo is posted into the chat as an attached file (`stackchan_photo.jpg`), because Muse can open files but can't use a long block of base64 text from a tool result; the StackChan keeps the turn open to speak Muse's description. Nothing streams, and nothing is stored on the device. |
| **Wi-Fi trigger** | A small HTTP server (token required): `POST /talk`, `/talk/down`, `/talk/up`, `GET /photo`, `GET /status`. |
| **IR remote (experimental)** | Muse commands `ir.learn`, `ir.send`, `ir.list`. Untested end to end: the receiver saw no signal on the author's unit. |
| **Long jobs** | A voice turn waits up to 15 minutes while Muse is still working, then speaks the answer. |
| **Voice-note tag** | StackChan voice notes are named `stackchan_voice_note.wav`, so Muse can tell them from the phone app's. |

LED ring support was tried and removed: the 12 body LEDs stayed dark on the author's unit.

## Set up

1. Get an [SDK token](https://gadgets.muse.ai/settings/sdk-tokens) and, optionally, an ElevenLabs key (use a
   restricted key with a usage limit: it is built into the firmware and can be read from the device).
2. Put secrets in `~/.stackchan/secrets/` (one value per file; see `configure.sh` for the names).
3. From `esp32/`: `../stackchan/configure.sh`, then `tools/muse/board.sh build cores3` and `tools/muse/board.sh flash cores3`.
   Pair it in the Muse app as in the SDK's README.
4. Back up the original firmware first if you want a way back:
   `python -m esptool --chip esp32s3 -p PORT read-flash 0 ALL original.bin`.
5. Calibrate the head: the servo "zero" positions are per unit. M5Stack's firmware stores them in its NVS as
   `zero_pos_1` (left-right) and `zero_pos_2` (up-down); read them from your backup and set
   `CONFIG_MUSE_STACKCHAN_YAW_ZERO` / `_PITCH_ZERO` (the defaults are one unit's).

6. Tell your Muse once, in chat, what the StackChan expects. For example: *"Messages that start with [StackChan] come from my StackChan desk robot. Reply short and easy to say aloud, with no links, IDs or markdown, in the language I used. Begin each reply with exactly one emotion tag from [excited] [playful] [peaceful] [amazed] [anxious] [frustrated] [scared] [tired] [curious] [angry] [sad] [laughs] [crying] [sighs] [clears throat] [mischievously], or none for a neutral answer. Never use more than one tag or put one anywhere but the start. Don't use tags for the phone app or typed chat."* Muse is told that voice notes are Cantonese, English or a mix, which helps when a transcript looks odd.

On the Mac: `stackchan/make-apps.sh` makes the clickable apps. Put the device's address in
`~/.stackchan/stackchan-ip.txt` (it logs `remote: listening on http://...` at boot) and the `remote-token` value
in `~/.stackchan/stackchan-remote-token.txt`. Then `stackchan.sh talk | toggle | look | status`.

## Settings added

| Setting | Default | Meaning |
|---|---|---|
| `MUSE_STACKCHAN_HEAD` | n | Servos, petting, gestures, hand-near trigger |
| `MUSE_STACKCHAN_YAW_ZERO` / `_PITCH_ZERO` | 461 / 579 | Per-unit servo home positions |
| `MUSE_STACKCHAN_PROX_DELTA` | 50 | Hand-near trigger, in sensor counts above the room (0 = off) |
| `MUSE_CORES3_CAMERA` | n | Photos on request |
| `MUSE_REMOTE`, `_TOKEN`, `_PORT` | n, empty, 8080 | Wi-Fi trigger |
| `MUSE_ELEVENLABS_API_KEY`, `_VOICE_ID`, `_MODEL` | empty, stock voice, `eleven_v4_turbo` | Spoken replies |
| `MUSE_ELEVENLABS_EMOTION` | n | Emotion tags on spoken replies: Muse's own leading tag, else one picked from the reply's tone |
| `MUSE_SCRIBE_STT`, `_LANGUAGE` | n, `yue` | ElevenLabs speech to text |
| `MUSE_MIC_BOOST`, `_MAX_DB`, `_TARGET_DB` | n, 15, -26 | Quiet-speech boost |
| `HOMEHUB_STACKCHAN_IR`, `_IR_TX_GPIO`, `_IR_RX_GPIO`, `_IR_CARRIER_HZ` | n, 5, 10, 36700 | IR remote (Panasonic air conditioners use 36.7 kHz; most others 38 kHz) |

Also in `devices/sdkconfig.muse-m5stack-cores3`: the GC0308 camera driver and a smaller camera DMA buffer
(internal RAM is tight). And `certs/globalsign-root-ca.pem`, the old GlobalSign root, which ElevenLabs'
certificate chain ends at and ESP-IDF's bundle lacks (it expires January 2028).

## Where the code is

- `esp32/components/muse/`: `muse_head.c` (servos, touch, proximity, gestures),
  `muse_head_tone.c` (reply tone), `muse_camera.c`, `muse_stt.c` (Scribe), `muse_remote.c` (Wi-Fi trigger);
  changes in `muse_voice.c`, `muse_input.c`, `muse_chat_session.cpp`, `Kconfig`.
- `esp32/main/`: `ir_remote.c`, `ir_pulses.c`; commands registered in `noise_control.cpp` and `app.c`.
- `esp32/tests/`: `muse_head_tone_selfcheck.c`, `ir_pulses_selfcheck.c` (each has its build line at the top).
- `stackchan/`: this folder.

Servo, expander, touch and IR details come from M5Stack's [StackChan-BSP](https://github.com/m5stack/StackChan-BSP)
(MIT), used as a reference; the code here is new.

## Things to know

- **Security:** the Wi-Fi trigger has one shared token and no encryption; use it on a network you trust. Keys and
  the token are in the firmware.
- **Privacy:** speech goes to Muse and, with Scribe on, to ElevenLabs; photos go to Muse or your computer only
  when asked.
- **Internal RAM is tight.** The camera's DMA buffer needs a 16 KB contiguous block of internal RAM, and Meta's newer SDK left only 10 KB. The head, touch and Wi-Fi-server task stacks now live in PSRAM, which brings the largest free block to about 32 KB. Watch `int=free/largest` in the boot log, and re-test a photo after adding features.
- **Battery:** on battery the Wi-Fi sleeps after the screen has been asleep a while, and the Wi-Fi trigger can't
  reach the device until it wakes.
- **Not included:** the avatar. Your Muse draws it with `tools/muse/avatar.py`; the SDK ignores the result
  (`components/muse/avatar/`), and so does this fork.
- **Photos in your chat:** every photo Muse takes stays in your Muse chat history as a file.
- **Known issues:** camera photos can show a torn horizontal band (the DMA buffer had to be shrunk); photos are dark
  in a dark room; the IR receiver did not work on the author's unit.
