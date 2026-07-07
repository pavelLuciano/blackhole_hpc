#!/bin/bash
# Genera video del modo extendido en GPU.
# Uso: ./make_video_gpu.sh <N> <num_frames> <scene_file> <nombre>
set -e
N=${1:-720}; FRAMES=${2:-240}
SCENE=${3:-../../scenes/default.txt}
NAME=${4:-blackhole_gpu}
FRAMEDIR=frames_gpu_tmp
mkdir -p $FRAMEDIR; rm -f $FRAMEDIR/*.ppm

FRAMEABS="$(pwd)/$FRAMEDIR/frame"
echo "Generando $FRAMES frames en GPU (escena=$SCENE)..."
# el binario genera todos los frames; corre desde build/ para hallar el shader
(cd ../gpu_vulkan/build && ./gpu_video $N $FRAMES "$SCENE" "$FRAMEABS")

echo "Ensamblando video..."
ffmpeg -y -framerate 30 -i $FRAMEDIR/frame_%04d.ppm \
    -c:v libx264 -pix_fmt yuv420p -crf 18 ../results/${NAME}.mp4 2>/dev/null
echo "Video listo: results/${NAME}.mp4"
rm -rf $FRAMEDIR
