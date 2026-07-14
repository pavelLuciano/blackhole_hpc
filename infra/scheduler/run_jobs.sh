#!/bin/bash
# ============================================================================
#  run_jobs.sh — scheduler simulado (estilo SLURM) sobre Docker Compose
#
#  Emula, sin cluster real, el comportamiento de sbatch/squeue/sacct que
#  vimos en la Clase 1 (SLURM sin cluster real → mini-cluster con Docker
#  Compose): lee un manifiesto de jobs, los encola, respeta un número
#  máximo de jobs "en ejecución" (simulando --cpus-per-task / nodos
#  disponibles) y deja un log + un resumen final por job, igual que sacct.
#
#  Uso:
#      cd infra/scheduler
#      ./run_jobs.sh jobs.txt [max_paralelos]
#
#  Por defecto max_paralelos=1 (cola secuencial, análoga a un cluster de
#  1 nodo). Súbelo (p. ej. a 2 o 3) para simular varios nodos disponibles
#  y observar contención de recursos entre jobs — un resultado interesante
#  de reportar (¿corren más lento en paralelo por compartir CPU/GPU?).
# ============================================================================
set -euo pipefail

MANIFEST="${1:-jobs.txt}"
MAX_PARALLEL="${2:-1}"
COMPOSE_FILE="../docker-compose.yml"
LOG_DIR="../../results/jobs"
SUMMARY="$LOG_DIR/sacct_resumen.csv"

mkdir -p "$LOG_DIR"
echo "job_name,servicio,estado,inicio,fin,duracion_s" > "$SUMMARY"

echo "== Cola de jobs (manifiesto: $MANIFEST, paralelismo máximo: $MAX_PARALLEL) =="

run_job() {
    local job_name="$1" servicio="$2"; shift 2
    local cmd=("$@")
    local logfile="$LOG_DIR/${job_name}.log"
    local t_start t_end dur estado

    echo "[SUBMITTED] $job_name  (servicio=$servicio)"
    t_start=$(date +%s)
    if docker compose -f "$COMPOSE_FILE" run --rm "$servicio" "${cmd[@]}" > "$logfile" 2>&1; then
        estado="COMPLETED"
    else
        estado="FAILED"
    fi
    t_end=$(date +%s)
    dur=$((t_end - t_start))
    echo "[$estado]  $job_name  (${dur}s)  -> $logfile"
    echo "$job_name,$servicio,$estado,$t_start,$t_end,$dur" >> "$SUMMARY"
}

running=0
while IFS=';' read -r job_name servicio comando || [ -n "$job_name" ]; do
    # saltar líneas vacías o comentarios
    [[ -z "$job_name" || "$job_name" == \#* ]] && continue

    # respetar el límite de paralelismo (simula nodos/slots disponibles)
    while [ "$(jobs -rp | wc -l)" -ge "$MAX_PARALLEL" ]; do
        wait -n
    done

    read -ra cmd_array <<< "$comando"
    run_job "$job_name" "$servicio" "${cmd_array[@]}" &
    running=$((running + 1))
done < "$MANIFEST"

wait
echo "== Todos los jobs finalizaron. Resumen: $SUMMARY =="
column -s, -t "$SUMMARY"
