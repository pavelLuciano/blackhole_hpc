#!/bin/bash
# Benchmark modo VIDEO, GPU. El binario gpu_video genera N_FRAMES internamente.
# Salida: results/csv/gpu_video.csv
set -e
BINDIR=../gpu_vulkan/build
OUT=../results/csv/gpu_video.csv
SCENE=../../scenes/default.txt
RESOLUCIONES=(512 1024 2048 4096)
N_FRAMES=10
N_REPS=3
PREFIX=/tmp/bench_gpuv

if [ ! -x "$BINDIR/gpu_video" ]; then echo "ERROR: falta gpu_video"; exit 1; fi
echo "version,modo,resolucion,hilos,rep,frames,computo_ms,total_ms" > "$OUT"
echo "== Benchmark GPU VIDEO ($N_FRAMES frames) =="

for N in "${RESOLUCIONES[@]}"; do
    echo "  ${N}x${N}..."
    for ((rep=1; rep<=N_REPS; rep++)); do
        t_start=$(date +%s.%N)
        salida=$(cd "$BINDIR" && ./gpu_video "$N" "$N_FRAMES" "$SCENE" "$PREFIX")
        t_end=$(date +%s.%N)
        total_ms=$(python3 -c "print(f'{($t_end - $t_start)*1000:.2f}')")
        # gpu_video imprime 'XXXX ms (YY ms/frame)'; tomo el total de cómputo
        computo_ms=$(echo "$salida" | grep -oE 'en [0-9]+\.[0-9]+ ms' | grep -oE '[0-9]+\.[0-9]+')
        echo "gpu,video,$N,0,$rep,$N_FRAMES,$computo_ms,$total_ms" >> "$OUT"
    done
done
echo "CSV generado: $OUT"
