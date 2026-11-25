#!/usr/bin/env bash
# Records the README preview GIFs (docs/media/styles/*.gif) from the real Linux app.
#
# Runs dropocalypse under a virtual display while a synthetic dance loop plays into a virtual
# PulseAudio speaker, so the styles react exactly as they do to music. Needs: a Linux build in
# build-linux/ (with libprojectM beside it), xvfb, pulseaudio, ffmpeg, gifsicle and numpy.
#
#   scripts/make-style-previews.sh                 # every style listed below
#   scripts/make-style-previews.sh monstercat-bars # just the ones named
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build-linux/dropocalypse"
OUT="$ROOT/docs/media/styles"
WORK="$(mktemp -d)"
ICON="$ROOT/linux/dropocalypse.png"

# slug | style index | items (icon or text:WORDS) | extra settings
STYLES=(
  "monstercat-bars|26|icon|"
  "lo-fi-chill|27|icon|"
  "glitch-vhs|28|icon|"
  "liquid-chrome|29|icon|"
  "kinetic-text|30|text:DROPOCALYPSE|logoSize=0.6"
  "logo-layers|11|text:D  R  O  P|layerExplode=55 layerWave=35 logoSize=0.75"
  "milk-acid-mandala|31|icon|"
  "milk-flame-face|32|icon|"
  "milk-mind-blob|33|icon|"
  "milk-geiss-lightning|34|icon|"
  "milk-dark-galaxy|35|icon|"
  "milk-hypraxis|36|icon|"
  "milk-planar-travel|37|icon|"
  "milk-box-of-tricks|38|icon|"
  "milk-lorenz-chaser|39|icon|"
)

# Music: a virtual speaker that the app listens to ("what this computer is playing").
python3 "$ROOT/scripts/make-preview-track.py" "$WORK/track.wav" 90
pulseaudio --check 2>/dev/null || pulseaudio -D --exit-idle-time=-1
pactl load-module module-null-sink sink_name=preview >/dev/null
pactl set-default-sink preview
( while true; do paplay --device=preview "$WORK/track.wav"; done ) &
PLAYER=$!
trap 'kill $PLAYER 2>/dev/null || true' EXIT

for entry in "${STYLES[@]}"; do
  IFS='|' read -r slug index items extra <<<"$entry"
  if [ $# -gt 0 ] && [[ " $* " != *" $slug "* ]]; then continue; fi
  echo "== $slug (style $index)"
  cfg=/tmp/dropocalypse-selftest/dropocalypse
  rm -rf /tmp/dropocalypse-selftest && mkdir -p "$cfg"
  {
    printf 'sceneIndex=%s\nautoCycle=0\nprivacyNoticeSeen=1\ncheckForUpdates=0\nwhatsNewLastSeen=99.0.0\n' "$index"
    if [ "$items" = icon ]; then
      printf 'items=1\nitem0.kind=image\nitem0.file=%s\nitem0.name=icon\n' "$ICON"
    else
      printf 'items=1\nitem0.kind=text\nitem0.text=%s\nitem0.font=1\nitem0.color=0\n' "${items#text:}"
    fi
    for kv in $extra; do printf '%s\n' "$kv"; done
  } > "$cfg/settings.ini"

  frames="$WORK/$slug"
  rm -rf "$frames" && mkdir -p "$frames"
  xvfb-run -a -s "-screen 0 1280x720x24" "$APP" --preview="$frames" --preview-seconds=4.2 | tee "$frames/log.txt"

  # Pick the frame nearest each 1/12 s step, so the GIF runs at real speed.
  python3 - "$frames" <<'PY'
import os, sys
d = sys.argv[1]
times = [tuple(map(float, l.split())) for l in open(os.path.join(d, "times.txt")) if l.strip()]
os.makedirs(os.path.join(d, "sel"), exist_ok=True)
for k in range(48):
    want = k / 12.0
    idx = int(min(times, key=lambda it: abs(it[1] - want))[0])
    os.link(os.path.join(d, "%04d.ppm" % idx), os.path.join(d, "sel", "%04d.ppm" % k))
print(f"{len(times)} frames captured, {len(times) / max(times[-1][1], 0.01):.1f} fps")
PY
  ffmpeg -loglevel error -y -framerate 12 -i "$frames/sel/%04d.ppm" \
    -vf "scale=360:202:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle" \
    "$frames/out.gif"
  gifsicle -O3 --lossy=40 "$frames/out.gif" -o "$OUT/$slug.gif"
  ls -la "$OUT/$slug.gif"
done
