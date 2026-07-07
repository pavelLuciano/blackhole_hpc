#!/bin/bash
# Benchmark modo VIDEO, serial. Mide generar N_FRAMES frames por resolución.
# Salida: results/csv/serial_video.csv
set -e
BIN=../cpu_serial/build/serial_video
OUT=../results/csv/serial_video.csv
SCENE=../scenes/default.txt
RESOLUCIONES=(512 1024 2048)
N_FRAMES=10          # frames por medición (carga representativa)
N_REPS=3
TMP=/tmp/bench_v.ppm
T_TOTAL=12.566

if [ ! -x "$BIN" ]; then echo "ERROR: falta $BIN"; exit 1; fi
echo "version,modo,resolucion,hilos,rep,frames,computo_ms,total_ms" > "$OUT"
echo "== Benchmark SERIAL VIDEO ($N_FRAMES frames) =="

for N in "${RESOLUCIONES[@]}"; do
    echo "  ${N}x${N}..."
    for ((rep=1; rep<=N_REPS; rep++)); do
        t_start=$(date +%s.%N)
        comp_sum=0
        for ((fr=0; fr<N_FRAMES; fr++)); do
            t=$(python3 -c "print($T_TOTAL*$fr/$N_FRAMES)")
            salida=$("$BIN" "$N" "$t" "$SCENE" "$TMP")
            c=$(echo "$salida" | grep -oE '[0-9]+\.[0-9]+ ms' | grep -oE '[0-9]+\.[0-9]+')
            comp_sum=$(python3 -c "print($comp_sum + $c)")
        done
        t_end=$(date +%s.%N)
        total_ms=$(python3 -c "print(f'{($t_end - $t_start)*1000:.2f}')")
        echo "serial,video,$N,1,$rep,$N_FRAMES,$comp_sum,$total_ms" >> "$OUT"
    done
done
echo "CSV generado: $OUT"
