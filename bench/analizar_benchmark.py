#!/usr/bin/env python3
# ============================================================================
#  analizar_benchmark.py  —  consume los CSVs de benchmark y produce:
#    - CSVs promediados (agrupando las repeticiones)
#    - gráficos (tiempo vs resolución, speedup vs hilos para IMAGEN y VIDEO,
#      comparación de versiones)
#    - tabla resumen para el informe
#
#  Los gráficos de speedup usan un eje X CATEGÓRICO: los valores de hilos
#  (1,2,4,8,16,20) se colocan uniformemente espaciados y marcados de forma
#  explícita. Cada resolución grafica solo los hilos que existan en el CSV
#  (así, p.ej., el video a 4096 muestra solo 8,16,20).
#
#  Uso:  python3 analizar_benchmark.py
#  Requiere: pandas, matplotlib
# ============================================================================
import os, glob
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

CSV_DIR = "../results/csv"
OUT_DIR = "../results/analisis"
os.makedirs(OUT_DIR, exist_ok=True)

# --- cargar todos los CSVs disponibles ---
frames = []
for f in glob.glob(f"{CSV_DIR}/*.csv"):
    try:
        frames.append(pd.read_csv(f))
    except Exception as e:
        print(f"aviso: no pude leer {f}: {e}")
if not frames:
    print("No hay CSVs en", CSV_DIR, "- corre primero los scripts bench_*.sh")
    raise SystemExit(1)

data = pd.concat(frames, ignore_index=True)
print(f"Cargados {len(data)} registros de {len(frames)} archivos")

# --- promediar sobre las repeticiones ---
group_cols = ["version","modo","resolucion","hilos"]
agg = data.groupby(group_cols).agg(
    computo_ms=("computo_ms","mean"),
    computo_std=("computo_ms","std"),
    total_ms=("total_ms","mean"),
    total_std=("total_ms","std"),
).reset_index()
agg.to_csv(f"{OUT_DIR}/promedios.csv", index=False)
print(f"Promedios -> {OUT_DIR}/promedios.csv")

# valores de hilos que probamos, en el orden deseado para el eje categórico
HILOS_ORDEN = [1, 2, 4, 8, 16, 20]

def grafico_speedup(modo):
    """Gráfico de speedup vs hilos para un modo (imagen/video).

    Usa eje X en escala LOGARÍTMICA base 2, de modo que las duplicaciones de
    hilos (1->2->4->8->16) queden equiespaciadas y el paso 16->20 (que NO es
    una duplicación) aparezca más corto, reflejando visualmente el crecimiento
    exponencial del número de núcleos. Cada resolución muestra solo los hilos
    disponibles en el CSV."""
    omp = agg[(agg["version"]=="omp") & (agg["modo"]==modo)].copy()
    if omp.empty:
        print(f"  (sin datos OMP para modo {modo}, omito gráfico)")
        return
    plt.figure(figsize=(8,6))

    for N in sorted(omp["resolucion"].unique()):
        s = omp[omp["resolucion"]==N].sort_values("hilos")
        hilos_disp = sorted(s["hilos"].unique())
        base_h = hilos_disp[0]
        base_t = s[s["hilos"]==base_h]["computo_ms"].values[0]
        # eje X = valores REALES de hilos (para que la escala log los ubique bien)
        xs = list(s["hilos"])
        ys = [ base_t / s[s["hilos"]==h]["computo_ms"].values[0] * (1 if base_h==1 else base_h)
               for h in s["hilos"] ]
        etiqueta = f"N={N}" + ("" if base_h==1 else f" (base {base_h}h)")
        plt.plot(xs, ys, marker="o", label=etiqueta)

    # línea ideal (speedup = nº de hilos), en el rango real de hilos
    hmax = int(omp["hilos"].max())
    x_ideal = [h for h in HILOS_ORDEN if h <= hmax]
    plt.plot(x_ideal, x_ideal, "k--", alpha=0.4, label="ideal (lineal)")

    # eje X LINEAL (posiciones reales de los hilos: 1 y 2 cerca, 8 y 16 con
    # gran separación, 20 pegado al 16), pero marcando SOLO los valores probados
    # en lugar de las marcas automáticas de matplotlib.
    plt.xticks(HILOS_ORDEN, [str(h) for h in HILOS_ORDEN])
    plt.xlabel("Número de hilos")
    plt.ylabel("Speedup (t₁ / t_H)")
    plt.title(f"OpenMP {modo}: speedup vs hilos")
    plt.grid(True, alpha=0.3); plt.legend()
    plt.savefig(f"{OUT_DIR}/omp_{modo}_speedup.png", dpi=120, bbox_inches="tight")
    plt.close()
    print(f"Gráfico: omp_{modo}_speedup.png")

# --- speedup para IMAGEN y VIDEO ---
grafico_speedup("imagen")
grafico_speedup("video")

# ========================================================================
#  GRÁFICO: tiempo de cómputo vs resolución (por modo), comparando versiones
# ========================================================================
def grafico_computo_resolucion(modo):
    m = agg[agg["modo"]==modo].copy()
    if m.empty: return
    plt.figure(figsize=(8,6))
    for ver in m["version"].unique():
        sub = m[m["version"]==ver]
        if ver=="omp":
            # usar el mejor caso (más hilos) por resolución
            sub = sub.sort_values("hilos").groupby("resolucion").last().reset_index()
            label = "omp (max hilos)"
        else:
            label = ver
        sub = sub.sort_values("resolucion")
        plt.plot(sub["resolucion"], sub["computo_ms"], marker="o", label=label)
    plt.xlabel("Resolución (N, imagen NxN)")
    plt.ylabel("Tiempo de cómputo (ms)")
    plt.title(f"Modo {modo}: cómputo vs resolución")
    plt.yscale("log"); plt.xscale("log", base=2)
    plt.grid(True, alpha=0.3); plt.legend()
    plt.savefig(f"{OUT_DIR}/{modo}_computo_vs_resolucion.png", dpi=120, bbox_inches="tight")
    plt.close()
    print(f"Gráfico: {modo}_computo_vs_resolucion.png")

grafico_computo_resolucion("imagen")
grafico_computo_resolucion("video")

# ========================================================================
#  GRÁFICO: comparación de versiones (cómputo vs total) a máxima resolución
# ========================================================================
for modo in agg["modo"].unique():
    sub = agg[agg["modo"]==modo]
    Nmax = sub["resolucion"].max()
    s = sub[sub["resolucion"]==Nmax].copy()
    rows=[]
    for ver in s["version"].unique():
        vs = s[s["version"]==ver]
        if ver=="omp": vs = vs.sort_values("hilos").tail(1)
        rows.append(vs)
    s = pd.concat(rows)
    if s.empty: continue
    plt.figure(figsize=(8,6))
    x = range(len(s)); w=0.35
    plt.bar([i-w/2 for i in x], s["computo_ms"], w, label="cómputo puro")
    plt.bar([i+w/2 for i in x], s["total_ms"], w, label="total (wall-clock)")
    plt.xticks(list(x), s["version"])
    plt.ylabel("Tiempo (ms)")
    plt.title(f"{modo} a {int(Nmax)}x{int(Nmax)}: cómputo vs total")
    plt.grid(True, alpha=0.3, axis="y"); plt.legend()
    plt.savefig(f"{OUT_DIR}/comparacion_{modo}_{int(Nmax)}.png", dpi=120, bbox_inches="tight")
    plt.close()
    print(f"Gráfico: comparacion_{modo}_{int(Nmax)}.png")

# ========================================================================
#  TABLA RESUMEN (markdown)
# ========================================================================
with open(f"{OUT_DIR}/tabla_resumen.md","w") as f:
    f.write("# Resumen de benchmark\n\n")
    for modo in sorted(agg["modo"].unique()):
        f.write(f"## Modo {modo}\n\n")
        sub = agg[agg["modo"]==modo].sort_values(["resolucion","version","hilos"])
        f.write("| versión | resolución | hilos | cómputo (ms) | total (ms) |\n")
        f.write("|---|---|---|---|---|\n")
        for _,r in sub.iterrows():
            h = int(r["hilos"]) if r["hilos"]>0 else "-"
            f.write(f"| {r['version']} | {int(r['resolucion'])} | {h} | "
                    f"{r['computo_ms']:.1f} | {r['total_ms']:.1f} |\n")
        f.write("\n")
print(f"Tabla -> {OUT_DIR}/tabla_resumen.md")
print("\n¡Análisis completo! Revisa", OUT_DIR)
