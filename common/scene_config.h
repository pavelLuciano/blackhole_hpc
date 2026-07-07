#ifndef SCENE_CONFIG_H
#define SCENE_CONFIG_H

// ============================================================================
//  scene_config.h  —  Lee la configuración de escena del modo video desde
//  un archivo de texto plano. Compartido por serial, omp y (el host) gpu.
//
//  Formato del archivo (líneas '#' son comentarios):
//    cam x y z
//    sphere R incl phase speed radius r g b
//  donde:
//    cam:    posición de la cámara
//    sphere: R=radio orbital, incl=inclinación(rad), phase=fase inicial,
//            speed=velocidad angular, radius=tamaño, (r,g,b)=color 0..1
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

namespace scene {

// Una esfera orbitante definida por el usuario.
struct OrbitSphere {
    double R, incl, phase, speed;   // parámetros de la órbita
    double radius;                  // tamaño de la esfera
    double r, g, b;                 // color
};

struct Config {
    double cam_x = 0, cam_y = 0, cam_z = -45;
    std::vector<OrbitSphere> spheres;
};

// Carga la config desde 'path'. Devuelve false si no se pudo abrir.
// Si el archivo no existe, el llamador puede usar una escena por defecto.
static inline bool load(const std::string& path, Config& cfg) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;

    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        // saltar espacios iniciales
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;  // comentario/vacía

        if (std::strncmp(p, "cam", 3) == 0) {
            std::sscanf(p+3, "%lf %lf %lf",
                        &cfg.cam_x, &cfg.cam_y, &cfg.cam_z);
        }
        else if (std::strncmp(p, "sphere", 6) == 0) {
            OrbitSphere s;
            int n = std::sscanf(p+6, "%lf %lf %lf %lf %lf %lf %lf %lf",
                        &s.R, &s.incl, &s.phase, &s.speed,
                        &s.radius, &s.r, &s.g, &s.b);
            if (n == 8) cfg.spheres.push_back(s);
        }
    }
    std::fclose(f);
    return true;
}

// Escena por defecto (las 4 esferas que veníamos usando).
static inline void defaults(Config& cfg) {
    cfg.cam_x = 0; cfg.cam_y = 0; cfg.cam_z = -45;
    cfg.spheres = {
        {14, 0.6,  0.0, 0.9, 2.2, 1.0, 0.25, 0.20},
        {22, -0.4, 2.1, 0.6, 2.8, 0.25, 0.5,  1.0 },
        {30, 0.9,  4.0, 0.4, 3.2, 0.3,  1.0,  0.35},
        {18, 0.0,  1.0, 0.5, 3.0, 1.0,  0.85, 0.2 }
    };
}

} // namespace scene
#endif
