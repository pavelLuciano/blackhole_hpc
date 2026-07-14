#!/bin/bash
# ============================================================================
#  bench_docker.sh — benchmark ampliado de las versiones contenerizadas
#  (serial + omp), modo imagen Y modo video, en múltiples resoluciones y
#  conteos de hilos.
#
#  Uso (funciona sin importar desde qué directorio lo invoques):
#      bash infra/scheduler/bench_docker.sh
#
#  Estimación de tiempo total: ~40-45 minutos (imagen ~8 min, video ~33 min).
#  Requiere python3 disponible en el host (para calcular el instante t de
#  cada frame, igual que los scripts originales de benchmark).
#
#  Salida:
#    results/csv_etapa_actual/docker_bench_imagen.csv
#    results/csv_etapa_actual/docker_bench_video.csv
# ============================================================================
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

OUT_DIR="~/Dev/blackhole-hpc/results"
mkdir -p "$OUT_DIR"

extraer_ms() {
    grep -oE '[0-9]+\.[0-9]+ ms' | head -1 | grep -oE '[0-9]+\.[0-9]+'
}

# ============================================================================
# MODO IMAGEN
# ============================================================================
OUT_IMG="$OUT_DIR/docker_bench_imagen.csv"
echo "version,modo,resolucion,hilos,rep,computo_ms" > "$OUT_IMG"
echo "Imagen -> $(cd "$OUT_DIR" && pwd)/docker_bench_imagen.csv"

RES_IMG=(512 1024 2048)
HILOS=(1 2 4 8 16 20)
REPS_IMG=3

echo "== [IMAGEN] Serial =="
for N in "${RES_IMG[@]}"; do
    for r in $(seq 1 $REPS_IMG); do
        raw=$(docker run --rm blackhole-serial /app/serial_image "$N" /tmp/tmp.ppm 2>&1)
        ms=$(echo "$raw" | extraer_ms)
        if [ -z "$ms" ]; then
            echo "  [AVISO] serial imagen N=$N rep=$r sin match. Salida:"
            echo "$raw" | sed 's/^/    /'
            continue
        fi
        echo "serial,imagen,$N,1,$r,$ms" >> "$OUT_IMG"
        echo "  serial N=$N rep=$r -> ${ms}ms"
    done
done

echo "== [IMAGEN] OpenMP =="
for N in "${RES_IMG[@]}"; do
    for h in "${HILOS[@]}"; do
        for r in $(seq 1 $REPS_IMG); do
            raw=$(docker run --rm --cpus="$h" -e OMP_NUM_THREADS="$h" blackhole-omp \
                 /app/omp_image "$N" /tmp/tmp.ppm 2>&1)
            ms=$(echo "$raw" | extraer_ms)
            if [ -z "$ms" ]; then
                echo "  [AVISO] omp imagen N=$N hilos=$h rep=$r sin match. Salida:"
                echo "$raw" | sed 's/^/    /'
                continue
            fi
            echo "omp,imagen,$N,$h,$r,$ms" >> "$OUT_IMG"
            echo "  omp N=$N hilos=$h rep=$r -> ${ms}ms"
        done
    done
done

# ============================================================================
# MODO VIDEO (un frame por invocación, igual que el protocolo original)
# ============================================================================
OUT_VID="$OUT_DIR/docker_bench_video.csv"
echo "version,modo,resolucion,hilos,rep,frames,computo_ms" > "$OUT_VID"
echo "Video  -> $(cd "$OUT_DIR" && pwd)/docker_bench_video.csv"

SCENE="/app/scenes/default.txt"
N_FRAMES=10
T_TOTAL=12.566

renderizar_video() {
    # $1=imagen_docker $2=binario $3=N $4=hilos(1 si es serial) $5=rep $6=version(serial|omp)
    local img="$1" bin="$2" N="$3" h="$4" rep="$5" version="$6"
    local comp_sum=0 ms raw t
    for fr in $(seq 0 $((N_FRAMES - 1))); do
        t=$(python3 -c "print($T_TOTAL*$fr/$N_FRAMES)")
        if [ "$version" = "omp" ]; then
            raw=$(docker run --rm --cpus="$h" -e OMP_NUM_THREADS="$h" "$img" \
                  "$bin" "$N" "$t" "$SCENE" /tmp/tmp.ppm 2>&1)
        else
            raw=$(docker run --rm "$img" "$bin" "$N" "$t" "$SCENE" /tmp/tmp.ppm 2>&1)
        fi
        ms=$(echo "$raw" | extraer_ms)
        if [ -z "$ms" ]; then
            echo "    [AVISO] frame $fr sin match. Salida:"
            echo "$raw" | sed 's/^/      /'
            return 1
        fi
        comp_sum=$(python3 -c "print($comp_sum + $ms)")
    done
    echo "$version,video,$N,$h,$rep,$N_FRAMES,$comp_sum" >> "$OUT_VID"
    echo "  $version N=$N hilos=$h rep=$rep -> ${comp_sum}ms (suma de $N_FRAMES frames)"
}

echo "== [VIDEO] Serial (512/1024, 2 reps; 2048, 1 rep) =="
for N in 512 1024; do
    for r in 1 2; do
        renderizar_video blackhole-serial /app/serial_video "$N" 1 "$r" serial
    done
done
renderizar_video blackhole-serial /app/serial_video 2048 1 1 serial

echo "== [VIDEO] OpenMP (512/1024, 2 reps; 2048, 1 rep) =="
for N in 512 1024; do
    for h in "${HILOS[@]}"; do
        for r in 1 2; do
            renderizar_video blackhole-omp /app/omp_video "$N" "$h" "$r" omp
        done
    done
done
for h in "${HILOS[@]}"; do
    renderizar_video blackhole-omp /app/omp_video 2048 "$h" 1 omp
done

echo "== Listo =="
echo "-- imagen --"; wc -l "$OUT_IMG"
echo "-- video  --"; wc -l "$OUT_VID"
