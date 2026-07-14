#!/bin/bash
# ============================================================================
#  bench_docker.sh — benchmark ampliado de las versiones contenerizadas
#  (serial + omp) a través de varias resoluciones y conteos de hilos.
#
#  Uso:
#      cd infra/scheduler
#      bash bench_docker.sh
#
#  (Este script es bash puro — se ejecuta igual aunque tu shell interactivo
#  sea fish, solo invócalo con `bash bench_docker.sh`, no lo pegues línea
#  por línea en la terminal.)
#
#  Salida: infra/scheduler/../../results/csv_etapa_actual/docker_bench.csv
#  con el mismo esquema de columnas que usaste en la etapa anterior, para
#  poder comparar directo.
# ============================================================================
set -euo pipefail

RESOLUCIONES=(512 1024 2048)
HILOS=(1 2 4 8)
REPS=3

OUT_DIR="../../results/csv_etapa_actual"
OUT_CSV="$OUT_DIR/docker_bench.csv"
mkdir -p "$OUT_DIR"
echo "version,modo,resolucion,hilos,rep,computo_ms" > "$OUT_CSV"

extraer_ms() {
    # el binario imprime algo como "serial  N=512    1744.4 ms"
    grep -oE '[0-9]+\.[0-9]+ ms' | head -1 | grep -oE '[0-9]+\.[0-9]+'
}

echo "== Serial =="
for N in "${RESOLUCIONES[@]}"; do
    for r in $(seq 1 $REPS); do
        ms=$(docker run --rm blackhole-serial /app/serial_image "$N" /tmp/tmp.ppm 2>&1 | extraer_ms)
        echo "serial,imagen,$N,1,$r,$ms" >> "$OUT_CSV"
        echo "  serial N=$N rep=$r -> ${ms}ms"
    done
done

echo "== OpenMP =="
for N in "${RESOLUCIONES[@]}"; do
    for h in "${HILOS[@]}"; do
        for r in $(seq 1 $REPS); do
            ms=$(docker run --rm --cpus="$h" -e OMP_NUM_THREADS="$h" blackhole-omp \
                 /app/omp_image "$N" /tmp/tmp.ppm 2>&1 | extraer_ms)
            echo "omp,imagen,$N,$h,$r,$ms" >> "$OUT_CSV"
            echo "  omp N=$N hilos=$h rep=$r -> ${ms}ms"
        done
    done
done

echo "== Listo. Resultados en $OUT_CSV =="
