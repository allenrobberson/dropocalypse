#!/usr/bin/env bash
# Records vertical (9:16) social clips from the real Linux app, with a music track playing into a
# virtual PulseAudio speaker so the visuals react to that exact music, then muxes the same audio.
#
#   scripts/make-social-clips.sh MANIFEST AUDIO_DIR OUT_DIR
#
# MANIFEST lines (# comments allowed):
#   slug|style index|items|extra settings|audio file|start second|clip seconds
# items: icon | text:WORDS | icon+text:WORDS
# Needs a Linux build in build-linux/ (with libprojectM beside it), xvfb, pulseaudio, ffmpeg.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build-linux/dropocalypse"
MANIFEST="$1"; AUDIO="$2"; OUT="$3"
ICON="$ROOT/linux/dropocalypse.png"
W=${CLIP_W:-576}; H=${CLIP_H:-1024}; FPS=30
WORK="$(mktemp -d)"
mkdir -p "$OUT"

pulseaudio --check 2>/dev/null || pulseaudio -D --exit-idle-time=-1
pactl load-module module-null-sink sink_name=clip >/dev/null
pactl set-default-sink clip

while IFS='|' read -r -u 3 slug index items extra audio start secs; do
  [[ -z "$slug" || "$slug" == \#* ]] && continue
  echo "== $slug (style $index, $audio from ${start}s, ${secs}s)"
  cfg=/tmp/dropocalypse-selftest/dropocalypse
  rm -rf /tmp/dropocalypse-selftest && mkdir -p "$cfg"
  {
    printf 'sceneIndex=%s\nautoCycle=0\nprivacyNoticeSeen=1\ncheckForUpdates=0\nwhatsNewLastSeen=99.0.0\nportrait=0\n' "$index"
    n=0
    if [[ "$items" == icon* ]]; then
      printf 'item%d.kind=image\nitem%d.file=%s\nitem%d.name=icon\n' $n $n "$ICON" $n; n=$((n+1))
    fi
    if [[ "$items" == *text:* ]]; then
      printf 'item%d.kind=text\nitem%d.text=%s\nitem%d.font=1\nitem%d.color=0\n' $n $n "${items#*text:}" $n $n; n=$((n+1))
    fi
    printf 'items=%d\n' $n
    for kv in $extra; do printf '%s\n' "$kv"; done
  } > "$cfg/settings.ini"

  # Music from a little before the clip, so the app's 4 s warm-up hears it too.
  lead=6
  from=$(python3 -c "print(max(0.0, $start - $lead))")
  ffmpeg -nostdin -v error -y -ss "$from" -i "$AUDIO/$audio" -t $((secs + lead + 6)) -ac 2 -ar 48000 "$WORK/play.wav"

  frames="$WORK/$slug"; rm -rf "$frames" && mkdir -p "$frames"
  audio_epoch=$(date +%s.%N)
  paplay --device=clip "$WORK/play.wav" </dev/null &
  PLAYER=$!
  sleep $(python3 -c "print(max(0.0, $lead - 4.0 - 1.5))")   # app start-up takes about 1.5 s
  xvfb-run -a -s "-screen 0 1400x1400x24" "$APP" --preview="$frames" --preview-seconds="$secs" \
    --preview-size=${W}x${H} </dev/null | tee "$frames/log.txt"
  kill $PLAYER 2>/dev/null || true; wait $PLAYER 2>/dev/null || true

  # Frames at a steady 30 fps (nearest captured frame) and the matching slice of the music.
  python3 - "$frames" "$secs" "$FPS" <<'PY'
import os, sys
d, secs, fps = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
times = [tuple(map(float, l.split())) for l in open(os.path.join(d, "times.txt")) if l.strip()]
os.makedirs(os.path.join(d, "sel"), exist_ok=True)
for k in range(int(secs * fps)):
    want = k / fps
    idx = int(min(times, key=lambda it: abs(it[1] - want))[0])
    os.link(os.path.join(d, "%04d.ppm" % idx), os.path.join(d, "sel", "%05d.ppm" % k))
print(f"{len(times)} frames captured, {len(times) / max(times[-1][1], 0.01):.1f} fps")
PY
  cap_epoch=$(cat "$frames/start-epoch.txt")
  aoff=$(python3 -c "print(max(0.0, $cap_epoch - $audio_epoch))")
  ffmpeg -nostdin -v error -y -framerate $FPS -i "$frames/sel/%05d.ppm" -ss "$aoff" -i "$WORK/play.wav" \
    -map 0:v -map 1:a -t "$secs" \
    -vf "scale=720:1280:flags=lanczos,format=yuv420p" -c:v libx264 -preset slow -crf 16 -profile:v high \
    -c:a aac -b:a 192k -af "afade=t=in:d=0.3,afade=t=out:st=$(python3 -c "print($secs-0.8)"):d=0.8" \
    -movflags +faststart "$OUT/$slug.mp4"
  # A still for the image posts.
  ffmpeg -nostdin -v error -y -i "$frames/sel/$(printf %05d $((secs * FPS / 2))).ppm" -vf scale=1080:1920:flags=lanczos \
    "$OUT/$slug-still.png"
  ffmpeg -nostdin -v error -y -i "$frames/sel/$(printf %05d $((secs * FPS / 4))).ppm" -vf scale=1080:1920:flags=lanczos \
    "$OUT/$slug-still2.png"
  rm -rf "$frames"
done 3< "$MANIFEST"
ls -la "$OUT"
