#!/bin/zsh
# Makes two clickable macOS apps in ~/Applications: "StackChan Talk" (start it listening) and "StackChan Look"
# (take a photo with its camera and open it). They run ~/.stackchan/stackchan.sh, so put that script and your
# address and token files there first (see the README).
set -e
mkdir -p ~/Applications ~/.stackchan
cp "${0:A:h}/stackchan.sh" ~/.stackchan/stackchan.sh
chmod +x ~/.stackchan/stackchan.sh
cd ~/Applications
rm -rf "StackChan Talk.app" "StackChan Look.app"
osacompile -o "StackChan Talk.app" -e "do shell script \"$HOME/.stackchan/stackchan.sh talk\""
osacompile -o "StackChan Look.app" -e "do shell script \"$HOME/.stackchan/stackchan.sh look\""
echo "made ~/Applications/StackChan Talk.app and StackChan Look.app (drag them to the Dock)"
