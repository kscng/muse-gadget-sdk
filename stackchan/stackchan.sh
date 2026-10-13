#!/bin/zsh
# Talk to the StackChan over Wi-Fi.
#   ./stackchan.sh talk [secs]    listen until you stop talking; or for secs seconds (1-15) if given
#   ./stackchan.sh down | up      press / release the talk button by hand
#   ./stackchan.sh toggle         start listening, or stop if it already is (one key does both)
#   ./stackchan.sh look           take a photo with its camera and open it (for checking what it sees)
#   ./stackchan.sh status         its mode and whether the screen is asleep
# Set the address once:  echo 192.168.x.y > stackchan-ip.txt   (the device logs it at boot)
cd "${0:A:h}"
IP="${STACKCHAN_IP:-$(cat stackchan-ip.txt 2>/dev/null)}"
[ -n "$IP" ] || { echo "no address: put the StackChan's IP in stackchan-ip.txt"; exit 1; }
TOKEN="$(tr -d '[:space:]' < stackchan-remote-token.txt)"
URL="http://$IP:8080"
case "$1" in
  talk)   curl -sS --max-time 10 -X POST -H "X-Token: $TOKEN" "$URL/talk${2:+?secs=$2}" ;;
  down)   curl -sS --max-time 10 -X POST -H "X-Token: $TOKEN" "$URL/talk/down" ;;
  up)     curl -sS --max-time 10 -X POST -H "X-Token: $TOKEN" "$URL/talk/up" ;;
  toggle) if curl -sS --max-time 10 -H "X-Token: $TOKEN" "$URL/status" | grep -q '"mode":"listening"'; then
            curl -sS --max-time 10 -X POST -H "X-Token: $TOKEN" "$URL/talk/up"
          else
            curl -sS --max-time 10 -X POST -H "X-Token: $TOKEN" "$URL/talk/down"
          fi ;;
  look)   OUT="$HOME/.stackchan/look.jpg"
          CODE=$(curl -sS --max-time 25 -H "X-Token: $TOKEN" -o "$OUT" -w "%{http_code}" "$URL/photo")
          if [ "$CODE" = "200" ]; then open "$OUT"; echo "photo: $OUT"; else cat "$OUT"; rm -f "$OUT"; echo "(HTTP $CODE)"; fi ;;
  status) curl -sS --max-time 10 -H "X-Token: $TOKEN" "$URL/status" ;;
  *) sed -n 2,8p "$0"; exit 1 ;;
esac
echo
