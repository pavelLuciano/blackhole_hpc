# Infraestructura — Etapa 2 (INFO335)

Esta carpeta añade, sobre el proyecto ya validado de la etapa 1
(`cpu_serial`, `cpu_omp`, `gpu_vulkan`), dos capas del ecosistema visto en
INFO335:

1. **Reproducibilidad (Docker):** cada implementación se empaqueta en su
   propia imagen, sin depender de que quien evalúe tenga Vulkan/OpenMP
   instalados a mano.
2. **Orquestación (SLURM simulado con Docker Compose):** un scheduler
   simple (`scheduler/run_jobs.sh`) reemplaza la ejecución manual de los
   benchmarks por un manifiesto de jobs, tal como se hace con `sbatch`.

## Requisitos en el host (CachyOS)

- `docker` y el plugin `docker compose` (`docker compose version`).
- Para el servicio `gpu`: **NVIDIA Container Toolkit** (paquete AUR
  `nvidia-container-toolkit`), configurado como runtime de Docker.
  Verificar con `docker info | grep -i nvidia`.

## Primer uso

```bash
# desde la raíz del repo
docker compose -f infra/docker-compose.yml build

# prueba individual de cada versión
docker compose -f infra/docker-compose.yml run --rm serial /app/serial_image 512 /app/results/test_serial.ppm
docker compose -f infra/docker-compose.yml run --rm omp    /app/omp_image    512 /app/results/test_omp.ppm
docker compose -f infra/docker-compose.yml run --rm gpu    /app/bin/gpu_image 512 /app/results/test_gpu.ppm
```

Si el servicio `gpu` falla al crear la instancia de Vulkan, casi siempre
es porque falta el NVIDIA Container Toolkit en el host — **eso también es
un resultado válido para el informe**: documenta el costo de reproducibilidad
de GPU passthrough frente a CPU.

## Cola de jobs simulada (equivalente a SLURM)

```bash
cd infra/scheduler
./run_jobs.sh jobs.txt        # paralelismo 1 (cola secuencial)
./run_jobs.sh jobs.txt 2      # simula 2 "nodos" ejecutando a la vez
```

Salidas:
- `results/jobs/<nombre_job>.log` — salida de cada ejecución (equivalente
  a la salida de un job de SLURM).
- `results/jobs/sacct_resumen.csv` — tabla resumen (nombre, servicio,
  estado, duración), análoga a `sacct`.

## Qué reportar en el informe (Metodología)

- Diagrama de arquitectura: `jobs.txt` → scheduler → Docker Compose →
  contenedor (serial/omp/gpu) → `results/`.
- Comparación de **overhead de contenerización**: mismo benchmark corrido
  nativo (etapa 1) vs. dentro del contenedor, a igual resolución.
- Si se corre `run_jobs.sh` con `max_paralelos > 1`: discutir contención
  de recursos (CPU compartida entre `serial` y `omp` corriendo a la vez,
  o la GPU discreta compartida si se lanzan dos jobs `gpu` en paralelo).
