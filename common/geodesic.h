#ifndef GEODESIC_H
#define GEODESIC_H

#include <cmath>

// ============================================================================
//  geodesic.h  —  Física compartida (Schwarzschild, plano ecuatorial)
//
//  Unidades geometrizadas: G = c = M = 1  ->  r_s = 2M = 2.
//
//  Ecuación de forma de la órbita (exacta, doc. canónico):
//
//        d²u/dφ²  =  3 M u²  -  u          con u = 1/r,  M = 1
//
//  con la primera integral:
//
//        (du/dφ)²  =  1/b²  -  u²(1 - 2M u)
//
//  donde b = parámetro de impacto. Valor crítico b_crit = 3√3 ≈ 5.196:
//    b < b_crit  -> el rayo cae al horizonte.
//    b > b_crit  -> el rayo escapa.
//
//  El integrador RK4 se porta 1:1 a GLSL para la versión GPU.
// ============================================================================

namespace bh {

constexpr double PI = 3.14159265358979323846;

struct Scene {
    double r_s        = 2.0;     // horizonte
    double cam_r      = 30.0;    // distancia cámara -> agujero negro
    double fov        = 0.45;    // semicampo de visión (rad)
    double disk_inner = 6.0;     // borde interno del disco (3 r_s)
    double disk_outer = 18.0;    // borde externo
    int    max_steps  = 3000;
    double dphi       = 0.005;
};

enum class Hit { Horizon, Disk, Sky };

struct State { double u, du; };   // u = 1/r , du = du/dφ

// d²u/dφ² = 3 M u² - u  (M=1)
static inline State deriv(const State& s) {
    return State{ s.du, 3.0 * s.u * s.u - s.u };
}

// Un paso RK4 en φ. ESTE bloque se traduce literal a GLSL.
static inline State rk4_step(const State& s, double h) {
    State k1 = deriv(s);
    State k2 = deriv(State{ s.u + 0.5*h*k1.u, s.du + 0.5*h*k1.du });
    State k3 = deriv(State{ s.u + 0.5*h*k2.u, s.du + 0.5*h*k2.du });
    State k4 = deriv(State{ s.u +     h*k3.u, s.du +     h*k3.du });
    return State{
        s.u  + (h/6.0)*(k1.u  + 2*k2.u  + 2*k3.u  + k4.u ),
        s.du + (h/6.0)*(k1.du + 2*k2.du + 2*k3.du + k4.du)
    };
}

// --- Traza un rayo dado el ángulo ψ entre la línea de visión y la dirección
//     radial hacia el BH (ψ = 0 apunta justo al centro).
//
//  Geometría en la cámara (r = cam_r):
//    El parámetro de impacto exacto es  b = r·sin(ψ) / sqrt(1 - 2M/r).
//    La órbita arranca en u0 = 1/cam_r, y du0 sale de la primera integral
//    con signo NEGATIVO (el rayo va hacia adentro: r decrece, u crece).
//
//  out_phi recibe el ángulo φ acumulado al escapar: mide cuánto se DESVIÓ el
//  rayo por la gravedad. Es lo que usamos para muestrear el fondo y ver el
//  lensing (un rayo recto desvía poco; uno que roza el BH desvía mucho).
static inline Hit trace_ray(const Scene& sc, double psi, double& hit_r, double& out_phi) {
    double r0 = sc.cam_r;
    double f0 = 1.0 - sc.r_s / r0;                 // f(r) = 1 - 2M/r
    double b  = r0 * std::sin(psi) / std::sqrt(f0);// parámetro de impacto

    State s;
    s.u = 1.0 / r0;
    // (du/dφ)² = 1/b² - u²(1-2Mu).  Rayo entrante -> u CRECE -> signo positivo.
    double rhs = 1.0/(b*b) - s.u*s.u*(1.0 - sc.r_s*s.u);
    s.du = +std::sqrt(rhs > 0.0 ? rhs : 0.0);

    double phi = 0.0;
    bool passed_periapsis = false;
    for (int i = 0; i < sc.max_steps; ++i) {
        State next = rk4_step(s, sc.dphi);
        phi += sc.dphi;

        if (next.u <= 0.0) { hit_r = 1e9; out_phi = phi; return Hit::Sky; }  // r -> ∞
        double r = 1.0 / next.u;

        // Horizonte
        if (r <= sc.r_s) { hit_r = r; out_phi = phi; return Hit::Horizon; }

        // Cruce del disco (plano ecuatorial visto de canto): el rayo cruza el
        // plano del disco cuando φ pasa por π (al otro lado del BH). Si en ese
        // momento r está en [inner, outer], golpea el disco.
        double prev_phi = phi - sc.dphi;
        if (prev_phi < PI && phi >= PI && r >= sc.disk_inner && r <= sc.disk_outer) {
            hit_r = r; out_phi = phi; return Hit::Disk;
        }

        // Detectar periapsis: du pasa de positivo a negativo (el rayo tocó su
        // punto más cercano: u deja de crecer y empieza a decrecer -> r crece).
        // Solo DESPUÉS de eso, si r supera la cámara, el rayo escapó de verdad.
        if (s.du > 0.0 && next.du <= 0.0) passed_periapsis = true;
        if (passed_periapsis && r > sc.cam_r) { hit_r = r; out_phi = phi; return Hit::Sky; }

        s = next;
    }
    hit_r = (s.u > 0) ? 1.0/s.u : 1e9;
    out_phi = phi;
    return Hit::Sky;
}

} // namespace bh

#endif // GEODESIC_H
