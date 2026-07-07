#!/bin/bash
# Benchmark modo VIDEO, versión OpenMP. Barre resoluciones Y número de hilos.
# Barrido de hilos CONDICIONAL: resoluciones <=2048 usan 1,2,4,8,16,20;
# la resolución 4096 usa solo 8,16,20 (con menos hilos sería demasiado lenta).
# Salida: results/csv/omp_video.csv
set -e
BIN=../cpu_omp/build/omp_video
OUT=../results/csv/omp_video_24h.csv
SCENE=../scenes/default.txt
RESOLUCIONES=(4096)
HILOS_COMPLETO=(20)   # para N <= 2048
HILOS_4096=(2 4)             # para N = 4096 (evita corridas larguísimas)
N_FRAMES=10
N_REPS=1
TMP=/tmp/bench_v.ppm
T_TOTAL=12.566

if [ ! -x "$BIN" ]; then echo "ERROR: falta $BIN"; exit 1; fi
echo "version,modo,resolucion,hilos,rep,frames,computo_ms,total_ms" > "$OUT"
echo "== Benchmark OPENMP VIDEO ($N_FRAMES frames) =="

for N in "${RESOLUCIONES[@]}"; do
    # elegir el set de hilos según la resolución
    if [ "$N" -ge 4096 ]; then
        HILOS=("${HILOS_4096[@]}")
    else
        HILOS=("${HILOS_COMPLETO[@]}")
    fi

    for H in "${HILOS[@]}"; do
        echo "  ${N}x${N}, $H hilos..."
        for ((rep=1; rep<=N_REPS; rep++)); do
            t_start=$(date +%s.%N)
            comp_sum=0
            for ((fr=0; fr<N_FRAMES; fr++)); do
                t=$(python3 -c "print($T_TOTAL*$fr/$N_FRAMES)")
                salida=$(OMP_NUM_THREADS=$H "$BIN" "$N" "$t" "$SCENE" "$TMP")
                c=$(echo "$salida" | grep -oE '[0-9]+\.[0-9]+ ms' | grep -oE '[0-9]+\.[0-9]+')
                comp_sum=$(python3 -c "print($comp_sum + $c)")
            done
            t_end=$(date +%s.%N)
            total_ms=$(python3 -c "print(f'{($t_end - $t_start)*1000:.2f}')")
            echo "omp,video,$N,$H,$rep,$N_FRAMES,$comp_sum,$total_ms" >> "$OUT"
        done
    done
done
echo "CSV generado: $OUT"
