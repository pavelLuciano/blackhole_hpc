#!/bin/bash
# Genera video del modo extendido en CPU (serial u omp).
# Uso: ./make_video.sh <binario> <N> <num_frames> <scene_file> <nombre>
#   binario: ruta al ejecutable (serial_video u omp_video)
set -e
BIN=${1:-../cpu_omp/build/omp_video}
N=${2:-720}; FRAMES=${3:-240}
SCENE=${4:-../scenes/default.txt}
NAME=${5:-blackhole_video}
FRAMEDIR=frames_tmp
mkdir -p $FRAMEDIR; rm -f $FRAMEDIR/*.ppm
T_TOTAL=12.566   # una órbita completa (speed 0.5)

echo "Generando $FRAMES frames (${N}x${N}) escena=$SCENE..."
for ((i=0; i<FRAMES; i++)); do
    t=$(python3 -c "print($T_TOTAL * $i / $FRAMES)")
    printf -v fname "$FRAMEDIR/frame_%04d.ppm" $i
    $BIN $N $t "$SCENE" $fname > /dev/null
    if (( i % 20 == 0 )); then echo "  frame $i/$FRAMES"; fi
done
echo "Ensamblando video..."
ffmpeg -y -framerate 30 -i $FRAMEDIR/frame_%04d.ppm \
    -c:v libx264 -pix_fmt yuv420p -crf 18 ../results/${NAME}.mp4 2>/dev/null
echo "Video listo: results/${NAME}.mp4"
rm -rf $FRAMEDIR
