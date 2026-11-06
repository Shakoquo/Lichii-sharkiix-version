#!/bin/zsh
set -u

usage() {
    cat <<EOF
Usage: $0 <eden-cli> <exefs main> <sdl config ini> [seconds] [VAR=value...]

Boots Mario Kart World in eden-cli, drives the menus with a key script
(L+R, then A: Grand Prix, Mario, Mushroom Cup, 50cc, OK), holds A in the
race and prints the race frame rate from 15 to 85 s into the race (the race
starts when the cup intro movie ends, or about 10 s after the menus when
the game skips the movie). 300 seconds is enough with a warm shader cache.
The VAR=value arguments are passed to eden-cli as environment variables.

The config needs the default keyboard bindings (A on the A key, L on F,
R on H). Output goes to ./mkw_race_bench/<timestamp>.
EOF
    exit 1
}

[[ $# -lt 3 ]] && usage
binary=$1
game=$2
config=$3
shift 3
seconds=300
if [[ $# -gt 0 && $1 == <-> ]]; then
    seconds=$1
    shift
fi

out=./mkw_race_bench/$(date +%Y%m%d-%H%M%S)
mkdir -p $out/frames

keys="f780:9,f780:11,f790:-9,f790:-11"
for ((frame = 850; frame <= 1800; frame += 40)); do
    keys="$keys,f$frame:4,f$((frame + 4)):-4"
done
keys="$keys,f1900:4"
print -r -- $keys > $out/keys.txt

environment=(EDEN_NX2_FPS_LOG=1 EDEN_NX2_AUTO_STUB=1 EDEN_NX2_KEY_SCRIPT=@$out/keys.txt
             EDEN_NX2_FRAME_CAPTURE=$out/frames EDEN_NX2_FRAME_CAPTURE_EVERY=600)
if [[ -z ${LIBVULKAN_PATH:-} ]]; then
    moltenvk=$(dirname $binary)/eglantine.app/Contents/Frameworks/libMoltenVK.dylib
    [[ -f $moltenvk ]] && environment+=(LIBVULKAN_PATH=$moltenvk)
fi

env $environment "$@" $binary -c $config -g $game > $out/log.txt 2>&1 &
pid=$!
for ((elapsed = 0; elapsed < seconds; ++elapsed)); do
    kill -0 $pid 2>/dev/null || break
    sleep 1
done
kill $pid 2>/dev/null
for ((waited = 0; waited < 15; ++waited)); do
    kill -0 $pid 2>/dev/null || break
    sleep 1
done
kill -9 $pid 2>/dev/null

LC_ALL=C sed 's/\x1b\[[0-9;]*m//g' $out/log.txt | tr -d '\000' > $out/log_clean.txt
grep -a "FPS:" $out/log_clean.txt |
    sed -E 's/.*\[ *([0-9]+)\.[0-9]+\].*FPS: ([0-9.]+).*/\1 \2/' > $out/fps.txt
movie_end=$(awk 'match($0, /^ *\[ *[0-9]+\./) { time = substr($0, RSTART, RLENGTH); gsub(/[^0-9]/, "", time) }
                 /2560x1440.*end of stream/ { print time; exit }' $out/log_clean.txt)
if grep -aq "PANIC" $out/log_clean.txt; then
    print "The game panicked, see $out/log_clean.txt"
fi
hold=$(awk '/Key script: press key 4 at/ && match($0, /\(frame [0-9]+\)/) &&
                substr($0, RSTART + 7, RLENGTH - 8) + 0 >= 1900 && match($0, /^ *\[ *[0-9]+\./) {
                time = substr($0, RSTART, RLENGTH); gsub(/[^0-9]/, "", time); print time; exit }' $out/log_clean.txt)
if [[ -n $movie_end ]]; then
    race_start=$movie_end
elif [[ -n $hold ]]; then
    race_start=$((hold + 10))
else
    print "Did not reach the race in ${seconds} s, see $out/frames"
    exit 1
fi
start=$((race_start + 15))
end=$((race_start + 85))
awk -v start=$start -v end=$end '
    $1 >= start && $1 <= end { sum += $2; count++; if (min == "" || $2 < min) min = $2 }
    { last = $1 }
    END { if (last < end - 2) { printf "The log ends at %d s, before the race window ends at %d s\n", last, end; exit 1 }
          if (count) printf "Race FPS: %.1f average, %.1f minimum (%d samples)\n", sum / count, min, count
          else { print "No frame rate samples in the race window"; exit 1 } }
' $out/fps.txt || { print "Logs and frames: $out"; exit 1 }
print "Logs and frames: $out"
