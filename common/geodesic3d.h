#ifndef GEODESIC3D_H
#define GEODESIC3D_H

#include <cmath>
#include <vector>

// ============================================================================
//  geodesic3d.h  —  Trazado 3D reusando la física plana ya validada
//
//  Cada rayo se mueve en UN plano (el que contiene la cámara, la dirección del
//  rayo y el centro del BH). Integramos en ese plano con la ecuación de Binet
//  d²u/dφ² = 3Mu² - u (que YA reproduce b_crit=3√3), y reconstruimos la
//  posición 3D real en cada paso para poder testear intersección con esferas.
//
//  Así: física correcta (heredada del modo plano) + posiciones 3D (para objetos).
// ============================================================================

namespace bh3d {

struct Vec3 { double x, y, z; };
static inline Vec3 operator+(Vec3 a, Vec3 b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline Vec3 operator-(Vec3 a, Vec3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static inline Vec3 operator*(Vec3 a, double s){ return {a.x*s,a.y*s,a.z*s}; }
static inline double dot(Vec3 a, Vec3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline Vec3 cross(Vec3 a, Vec3 b){
    return { a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x };
}
static inline double length(Vec3 a){ return std::sqrt(dot(a,a)); }
static inline Vec3 normalize(Vec3 a){ double l=length(a); return l>0? a*(1.0/l):a; }

struct Sphere { Vec3 center; double radius; double r,g,b; };

struct Scene3D {
    double r_s = 2.0;
    double escape_r = 100.0;
    double dphi = 0.01;
    int    max_steps = 3000;
    std::vector<Sphere> spheres;
    // Disco de acreción (anillo en el plano ecuatorial y=0). Conmutable.
    bool   use_disk = false;
    double disk_inner = 6.0;
    double disk_outer = 18.0;
};

enum class Hit { Horizon, Sphere, Sky, Disk };
struct Result { Hit hit; Vec3 dir; int sphere_id; double disk_r; Vec3 hit_pos; };

// Binet en el plano: estado (u, du) con u=1/r.
struct St { double u, du; };
static inline St deriv(St s){ return { s.du, 3.0*s.u*s.u - s.u }; }
static inline St rk4(St s, double h){
    St k1=deriv(s);
    St k2=deriv({s.u+0.5*h*k1.u, s.du+0.5*h*k1.du});
    St k3=deriv({s.u+0.5*h*k2.u, s.du+0.5*h*k2.du});
    St k4=deriv({s.u+h*k3.u,     s.du+h*k3.du});
    return { s.u+(h/6)*(k1.u+2*k2.u+2*k3.u+k4.u),
             s.du+(h/6)*(k1.du+2*k2.du+2*k3.du+k4.du) };
}

// Traza un rayo desde 'origin' con dirección 'dir' (en 3D real).
static inline Result trace(const Scene3D& sc, Vec3 origin, Vec3 dir) {
    dir = normalize(dir);
    Vec3 O = origin;
    double r0 = length(O);

    // --- Construir la base del plano del rayo ---
    // e_r: dirección radial (de la cámara hacia afuera del BH, = O normalizado)
    // El plano contiene O y dir. Normal del plano:
    Vec3 n = cross(O, dir);
    double nlen = length(n);
    if (nlen < 1e-12) {
        // rayo radial puro: cae directo si apunta al BH
        // (degenerado, lo tratamos como que apunta al centro)
        n = Vec3{0,0,1};
    } else {
        n = n * (1.0/nlen);
    }
    // base ortonormal del plano: ex (radial hacia el BH desde cámara), ey
    Vec3 ex = normalize(O);                 // apunta de BH a cámara
    Vec3 ey = normalize(cross(n, ex));      // perpendicular en el plano

    // --- Ángulo psi entre el rayo y la dirección -radial (hacia el BH) ---
    // dir tiene componentes en ex,ey. El ángulo respecto a -ex (hacia BH):
    double d_ex = dot(dir, ex);
    double d_ey = dot(dir, ey);
    // psi = ángulo entre dir y la dirección hacia el centro (-ex)
    double psi = std::atan2(std::fabs(d_ey), -d_ex);

    // parámetro de impacto
    double f0 = 1.0 - sc.r_s/r0;
    double b = r0 * std::sin(psi) / std::sqrt(f0 > 0 ? f0 : 1e-9);

    // condición inicial en el plano: empezamos en φ=0, r=r0
    St s; s.u = 1.0/r0;
    double rhs = 1.0/(b*b) - s.u*s.u*(1.0 - sc.r_s*s.u);
    s.du = std::sqrt(rhs>0?rhs:0.0);

    // signo de φ: hacia dónde gira el rayo (según d_ey)
    double phi_sign = (d_ey >= 0) ? 1.0 : -1.0;

    double phi = 0.0;
    bool passed_peri = false;
    Vec3 prevPos = O;

    for (int i=0;i<sc.max_steps;++i){
        St next = rk4(s, sc.dphi);
        phi += sc.dphi;

        if (next.u <= 0.0) {
            // escapó al infinito; dirección ~ posición actual normalizada
            Vec3 p = ex*(std::cos(phi)) + ey*(std::sin(phi)*phi_sign);
            return { Hit::Sky, normalize(p), -1, 0.0, {0,0,0} };
        }
        double r = 1.0/next.u;
        if (r <= sc.r_s) return { Hit::Horizon, dir, -1, 0.0, {0,0,0} };

        // --- posición 3D real en este paso ---
        Vec3 pos = ex*(r*std::cos(phi)) + ey*(r*std::sin(phi)*phi_sign);

        // --- disco de acreción: cruce del plano ecuatorial y=0 ---
        // Si la coordenada 'y' (componente vertical) cambió de signo entre
        // prevPos y pos, el rayo cruzó el plano del disco. Si el radio en el
        // plano está en [inner,outer], golpeó el disco.
        if (sc.use_disk && (prevPos.y * pos.y < 0.0)) {
            // interpolar el punto de cruce (y=0)
            double t = prevPos.y / (prevPos.y - pos.y);
            Vec3 cross_pt = prevPos + (pos - prevPos) * t;
            double rr = length(cross_pt);
            if (rr >= sc.disk_inner && rr <= sc.disk_outer)
                return { Hit::Disk, normalize(pos), -1, rr, cross_pt };
        }

        // test de esferas (extremo pos dentro de la esfera)
        for (size_t k=0;k<sc.spheres.size();++k){
            if (length(pos - sc.spheres[k].center) <= sc.spheres[k].radius)
                return { Hit::Sphere, normalize(pos), (int)k, 0.0, pos };
        }

        if (s.du>0 && next.du<=0) passed_peri = true;
        if (passed_peri && r > sc.escape_r) {
            Vec3 d = pos - prevPos;
            return { Hit::Sky, normalize(d), -1, 0.0, {0,0,0} };
        }

        prevPos = pos;
        s = next;
    }
    Vec3 d = prevPos - O;
    return { Hit::Sky, normalize(d), -1, 0.0, {0,0,0} };
}

} // namespace bh3d
#endif
