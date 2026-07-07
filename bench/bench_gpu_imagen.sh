#!/bin/bash
# Benchmark modo IMAGEN, versión GPU. Barre resoluciones.
# IMPORTANTE: correr desde bench/; el binario GPU se ejecuta desde build/.
# Salida: results/csv/gpu_imagen.csv
set -e
BINDIR=../gpu_vulkan/build
OUT=../results/csv/gpu_imagen.csv
RESOLUCIONES=(512 1024 2048 4096)
N_REPS=5
TMP=/tmp/bench_gpu.ppm

if [ ! -x "$BINDIR/gpu_image" ]; then echo "ERROR: falta gpu_image (compila + shaders)"; exit 1; fi
echo "version,modo,resolucion,hilos,rep,computo_ms,total_ms" > "$OUT"
echo "== Benchmark GPU IMAGEN =="

for N in "${RESOLUCIONES[@]}"; do
    echo "  ${N}x${N}..."
    for ((rep=1; rep<=N_REPS; rep++)); do
        t_start=$(date +%s.%N)
        salida=$(cd "$BINDIR" && ./gpu_image "$N" "$TMP")
        t_end=$(date +%s.%N)
        total_ms=$(python3 -c "print(f'{($t_end - $t_start)*1000:.2f}')")
        # la GPU imprime el cómputo como 'XX.XX ms (dispatch+wait)'
        computo_ms=$(echo "$salida" | grep -oE '[0-9]+\.[0-9]+ ms' | grep -oE '[0-9]+\.[0-9]+' | head -1)
        echo "gpu,imagen,$N,0,$rep,$computo_ms,$total_ms" >> "$OUT"
    done
done
echo "CSV generado: $OUT"
