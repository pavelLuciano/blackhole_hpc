#!/bin/bash
# ============================================================================
#  bench_serial_imagen.sh  —  benchmark del modo IMAGEN, versión serial
#
#  Barre resoluciones 512/1024/2048/4096, repite cada una N_REPS veces y
#  promedia. Mide cómputo puro (de la salida del programa) y tiempo total
#  wall-clock (cronometrado externamente).
#
#  Salida: results/csv/serial_imagen.csv
#  Uso:  ./bench_serial_imagen.sh
# ============================================================================
set -e
BIN=../cpu_serial/build/serial_image
OUT=../results/csv/serial_imagen.csv
RESOLUCIONES=(512 1024 2048 4096)
N_REPS=5
TMP=/tmp/bench_out.ppm

# comprobar binario
if [ ! -x "$BIN" ]; then echo "ERROR: falta $BIN (compila primero)"; exit 1; fi

echo "version,modo,resolucion,hilos,rep,computo_ms,total_ms" > "$OUT"
echo "== Benchmark SERIAL IMAGEN =="

for N in "${RESOLUCIONES[@]}"; do
    echo "  resolución ${N}x${N}..."
    for ((rep=1; rep<=N_REPS; rep++)); do
        # tiempo total wall-clock (ns) alrededor de TODO el proceso
        t_start=$(date +%s.%N)
        salida=$("$BIN" "$N" "$TMP")
        t_end=$(date +%s.%N)
        total_ms=$(python3 -c "print(f'{($t_end - $t_start)*1000:.2f}')")
        # cómputo puro: el número antes de 'ms' en la salida del programa
        computo_ms=$(echo "$salida" | grep -oE '[0-9]+\.[0-9]+ ms' | grep -oE '[0-9]+\.[0-9]+')
        echo "serial,imagen,$N,1,$rep,$computo_ms,$total_ms" >> "$OUT"
    done
done
echo "CSV generado: $OUT"
