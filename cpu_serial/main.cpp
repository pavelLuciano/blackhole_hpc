// ============================================================================
//  cpu_serial/main.cpp  —  renderer serial (modo base: foto, con fondo)
//
//  Modo BASE del proyecto: un frame, sin objetos en órbita, PERO con un fondo
//  estelar procedural para que la silueta del agujero negro resalte y se vea
//  el lensing gravitacional (las estrellas se distorsionan cerca del anillo).
//
//  Compilar:  g++ -O3 -I../common main.cpp -o serial
//  Correr:    ./serial 1024 ../results/serial.ppm
// ============================================================================

#include "geodesic.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <chrono>
#include <cmath>

// Hash pseudoaleatorio determinista (mismo input -> mismo output).
// Sirve para generar estrellas en posiciones fijas del "cielo".
static double hash2(double x, double y) {
    double h = std::sin(x * 127.1 + y * 311.7) * 43758.5453;
    return h - std::floor(h);
}

// Campo de estrellas procedural muestreado en una dirección angular (ax, ay).
// Devuelve brillo [0,1]: la mayoría del cielo es oscuro, con puntos brillantes.
static double starfield(double ax, double ay) {
    // discretizar el cielo en celdas; en cada celda quizás hay una estrella
    double scale = 40.0;
    double cx = std::floor(ax * scale);
    double cy = std::floor(ay * scale);
    double r = hash2(cx, cy);
    if (r > 0.97) {                       // ~3% de celdas tienen estrella
        // brillo según otra parte del hash
        double br = hash2(cy, cx);
        return 0.5 + 0.5 * br;
    }
    return 0.0;
}

static void shade(bh::Hit hit, double hit_r, double phi,
                  double sx, double sy, const bh::Scene& sc,
                  uint8_t& R, uint8_t& G, uint8_t& B) {
    switch (hit) {
        case bh::Hit::Horizon:
            R = G = B = 0;                 // sombra: negro absoluto
            break;
        case bh::Hit::Disk: {
            double t = (hit_r - sc.disk_inner) / (sc.disk_outer - sc.disk_inner);
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            R = 255;
            G = (uint8_t)(180 * (1.0 - t) + 60);
            B = (uint8_t)(40  * (1.0 - t));
            break;
        }
        case bh::Hit::Sky: {
            // Dirección de cielo: el ángulo del píxel en pantalla, ROTADO por la
            // deflexión gravitacional phi. Así el fondo se ve distorsionado
            // cerca del agujero (donde phi es grande).
            double ang = std::atan2(sy, sx);          // dirección en pantalla
            double rad = std::sqrt(sx*sx + sy*sy);
            // la deflexión aleja aparentemente la dirección de fondo
            double deflect = phi;                      // φ acumulado del rayo
            double ax = std::cos(ang) * (rad + deflect * 0.15);
            double ay = std::sin(ang) * (rad + deflect * 0.15);

            double star = starfield(ax, ay);
            if (star > 0.0) {
                uint8_t v = (uint8_t)(star * 255);
                R = v; G = v; B = (uint8_t)std::fmin(255.0, v * 1.1);
            } else {
                // gradiente de fondo tenue (nebulosa) para no dejar negro plano
                double bg = 10.0 + 12.0 * rad;
                R = (uint8_t)(bg * 0.4);
                G = (uint8_t)(bg * 0.5);
                B = (uint8_t)(bg * 0.9);
            }
            break;
        }
    }
}

int main(int argc, char** argv) {
    int N = (argc > 1) ? std::atoi(argv[1]) : 1024;
    const char* out = (argc > 2) ? argv[2] : "serial.ppm";

    bh::Scene sc;
    std::vector<uint8_t> img(N * N * 3);

    auto t0 = std::chrono::high_resolution_clock::now();

    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            double sx = (2.0 * (x + 0.5) / N - 1.0);
            double sy = (2.0 * (y + 0.5) / N - 1.0);

            double psi = std::sqrt(sx*sx + sy*sy) * sc.fov;
            if (psi < 1e-5) psi = 1e-5;

            double hit_r = 0.0, phi = 0.0;
            bh::Hit hit = bh::trace_ray(sc, psi, hit_r, phi);

            uint8_t R, G, B;
            shade(hit, hit_r, phi, sx, sy, sc, R, G, B);
            int idx = (y * N + x) * 3;
            img[idx+0] = R; img[idx+1] = G; img[idx+2] = B;
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("serial  N=%-5d  %.1f ms\n", N, ms);

    FILE* f = std::fopen(out, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", N, N);
    std::fwrite(img.data(), 1, img.size(), f);
    std::fclose(f);
    return 0;
}
