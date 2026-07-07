#!/bin/bash
# Benchmark modo IMAGEN, versión OpenMP. Barre resoluciones Y número de hilos.
# Salida: results/csv/omp_imagen.csv
set -e
BIN=../cpu_omp/build/omp_image
OUT=../results/csv/omp_imagen.csv
RESOLUCIONES=(512 1024 2048 4096)
HILOS=(1 2 4 8 16 20)
N_REPS=5
TMP=/tmp/bench_out.ppm

if [ ! -x "$BIN" ]; then echo "ERROR: falta $BIN (compila primero)"; exit 1; fi
echo "version,modo,resolucion,hilos,rep,computo_ms,total_ms" > "$OUT"
echo "== Benchmark OPENMP IMAGEN (barrido de hilos) =="

for N in "${RESOLUCIONES[@]}"; do
    for H in "${HILOS[@]}"; do
        echo "  ${N}x${N}, $H hilos..."
        for ((rep=1; rep<=N_REPS; rep++)); do
            t_start=$(date +%s.%N)
            salida=$(OMP_NUM_THREADS=$H "$BIN" "$N" "$TMP")
            t_end=$(date +%s.%N)
            total_ms=$(python3 -c "print(f'{($t_end - $t_start)*1000:.2f}')")
            computo_ms=$(echo "$salida" | grep -oE '[0-9]+\.[0-9]+ ms' | grep -oE '[0-9]+\.[0-9]+')
            echo "omp,imagen,$N,$H,$rep,$computo_ms,$total_ms" >> "$OUT"
        done
    done
done
echo "CSV generado: $OUT"
