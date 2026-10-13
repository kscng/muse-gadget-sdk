#!/bin/zsh
# Writes the StackChan settings, and your secrets, into the CoreS3 build's sdkconfig. Run it from esp32/:
#   ../stackchan/configure.sh [build-dir]        (default build-muse-m5stack-cores3)
# Then build and flash as usual:  tools/muse/board.sh build cores3  &&  tools/muse/board.sh flash cores3
#
# Secrets are read from files outside this repository, one value per file, and are never printed:
#   $STACKCHAN_SECRETS (default ~/.stackchan/secrets)
#     muse-sdk-token        required: from gadgets.muse.ai > Account > SDK tokens (mgst_...)
#     elevenlabs-api-key    optional: spoken replies and Scribe speech-to-text; use a restricted key with a usage limit
#     elevenlabs-voice-id   optional: the voice to speak with (default: a stock ElevenLabs voice)
#     remote-token          optional: any long random string (openssl rand -hex 16); turns on the Wi-Fi trigger
set -e
SECRETS="${STACKCHAN_SECRETS:-$HOME/.stackchan/secrets}"
B="${1:-build-muse-m5stack-cores3}"
S="$B/sdkconfig"

secret() { [ -s "$SECRETS/$1" ] && tr -d '[:space:]' < "$SECRETS/$1"; }
TOKEN="$(secret muse-sdk-token || true)"
[ -n "$TOKEN" ] || { echo "missing $SECRETS/muse-sdk-token (your Muse SDK token)"; exit 1; }
EL_KEY="$(secret elevenlabs-api-key || true)"
VOICE="$(secret elevenlabs-voice-id || true)"
REMOTE="$(secret remote-token || true)"

mkdir -p "$B"
touch "$S"
chmod 600 "$S"
# Drop earlier values of everything written below, then append the current ones.
sed -i '' -E '/^(# )?CONFIG_(GADGET_SDK_TOKEN|MUSE_(STACKCHAN|CORES3_CAMERA|REMOTE|ELEVENLABS_API_KEY|ELEVENLABS_VOICE_ID|SCRIBE|MIC_BOOST)|HOMEHUB_(STACKCHAN_IR|IR_)|MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE)/d' "$S"
{
  echo "CONFIG_GADGET_SDK_TOKEN=\"$TOKEN\""
  # The head: servos, petting, gestures, and the hand-near trigger. The zero positions are per unit (see README).
  echo 'CONFIG_MUSE_STACKCHAN_HEAD=y'
  echo 'CONFIG_MUSE_STACKCHAN_PROX_DELTA=50'
  echo 'CONFIG_MUSE_STACKCHAN_YAW_ZERO=461'
  echo 'CONFIG_MUSE_STACKCHAN_PITCH_ZERO=579'
  # Photos on request, and the IR remote commands.
  echo 'CONFIG_MUSE_CORES3_CAMERA=y'
  echo 'CONFIG_HOMEHUB_STACKCHAN_IR=y'
  # Speech: the quiet-speech boost always; ElevenLabs voice and transcription when a key is given.
  echo 'CONFIG_MUSE_MIC_BOOST=y'
  if [ -n "$EL_KEY" ]; then
    echo "CONFIG_MUSE_ELEVENLABS_API_KEY=\"$EL_KEY\""
    [ -n "$VOICE" ] && echo "CONFIG_MUSE_ELEVENLABS_VOICE_ID=\"$VOICE\""
    echo 'CONFIG_MUSE_SCRIBE_STT=y'
    echo 'CONFIG_MUSE_SCRIBE_LANGUAGE="yue"'
  fi
  # ElevenLabs' chain ends at the old GlobalSign Root CA, which ESP-IDF's bundle lacks.
  echo 'CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y'
  echo 'CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH="certs/globalsign-root-ca.pem"'
  # The Wi-Fi trigger (stackchan.sh).
  if [ -n "$REMOTE" ]; then
    echo 'CONFIG_MUSE_REMOTE=y'
    echo "CONFIG_MUSE_REMOTE_TOKEN=\"$REMOTE\""
    echo 'CONFIG_MUSE_REMOTE_PORT=8080'
  fi
} >> "$S"
echo "wrote $S ($(grep -c '^CONFIG_' "$S") settings; secrets not shown)"
