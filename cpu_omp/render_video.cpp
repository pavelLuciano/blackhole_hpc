// ============================================================================
//  cpu_omp/render_video.cpp  —  modo VIDEO OpenMP (esferas orbitando)
//
//  SIN disco de acreción. Las esferas se leen de un archivo de escena
//  (ver common/scene_config.h y scenes/*.txt), NO están hardcodeadas.
//
//  Uso:  ./render_video N t scene_file salida.ppm
//    N          resolución
//    t          tiempo (posición de las esferas en su órbita)
//    scene_file archivo de configuración de escena
//    salida     archivo PPM de salida
//
//  Compilar:  g++ -O3 -fopenmp -I../common render_video.cpp -o render_video
// ============================================================================

#include "geodesic3d.h"
#include "scene_config.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <cmath>
#include <chrono>
#include <omp.h>
using namespace bh3d;

static double hash2(double x,double y){
    double h=std::sin(x*127.1+y*311.7)*43758.5453; return h-std::floor(h);
}
static double starfield(double ax,double ay){
    double s=40.0, cx=std::floor(ax*s), cy=std::floor(ay*s);
    if(hash2(cx,cy)>0.97){ double br=hash2(cy,cx); return 0.5+0.5*br; }
    return 0.0;
}

// Posición de una esfera en su órbita inclinada, en el tiempo t.
static Vec3 orbit(double R,double incl,double phase,double speed,double t){
    double a=t*speed+phase;
    double x=R*std::cos(a), z=R*std::sin(a);
    double y=z*std::sin(incl); z=z*std::cos(incl);
    return Vec3{x,y,z};
}

int main(int argc,char**argv){
    int N        = argc>1? std::atoi(argv[1]):512;
    double t     = argc>2? std::atof(argv[2]):0.0;
    const char* scene_file = argc>3? argv[3]:"../scenes/default.txt";
    const char* out = argc>4? argv[4]:"render_video.ppm";

    // --- cargar escena ---
    scene::Config cfg;
    if(!scene::load(scene_file, cfg)){
        std::fprintf(stderr,"aviso: no se pudo abrir '%s', uso escena por defecto\n",scene_file);
        scene::defaults(cfg);
    }

    Scene3D sc;
    sc.r_s=2.0; sc.escape_r=100.0; sc.dphi=0.01; sc.max_steps=3000;
    sc.use_disk=false;   // el modo VIDEO no lleva disco

    // colocar las esferas de la config en su posición para este t
    for(const auto& os : cfg.spheres){
        Vec3 p = orbit(os.R, os.incl, os.phase, os.speed, t);
        sc.spheres.push_back({ p, os.radius, os.r, os.g, os.b });
    }

    // --- cámara desde la config ---
    Vec3 cam{ cfg.cam_x, cfg.cam_y, cfg.cam_z };
    Vec3 target{0,0,0};
    Vec3 fwd = normalize(target-cam);
    Vec3 up{0,1,0};
    Vec3 right = normalize(cross(fwd,up));
    Vec3 camup = cross(right,fwd);
    double fov = 0.7;

    std::vector<uint8_t> img(N*N*3);
    auto t0=std::chrono::high_resolution_clock::now();

    #pragma omp parallel for collapse(2) schedule(dynamic,4)
    for(int py=0;py<N;++py){
        for(int px=0;px<N;++px){
            double sx=(2.0*(px+0.5)/N-1.0);
            double sy=(2.0*(py+0.5)/N-1.0);
            Vec3 dir = normalize(fwd + right*(sx*fov) + camup*(-sy*fov));
            Result res = trace(sc, cam, dir);

            uint8_t R,G,B;
            if(res.hit==Hit::Horizon){ R=G=B=0; }
            else if(res.hit==Hit::Sphere){
                const Sphere& s=sc.spheres[res.sphere_id];
                Vec3 nrm = normalize(res.hit_pos - s.center);
                Vec3 ld = normalize(Vec3{-0.4,0.7,-0.6});
                double diff = dot(nrm,ld); diff = diff<0.15?0.15:diff;
                R=(uint8_t)std::fmin(255.0,s.r*255*diff);
                G=(uint8_t)std::fmin(255.0,s.g*255*diff);
                B=(uint8_t)std::fmin(255.0,s.b*255*diff);
            }
            else { // cielo
                double star=starfield(res.dir.x,res.dir.y);
                if(star>0){ uint8_t v=(uint8_t)(star*255); R=v;G=v;B=(uint8_t)std::fmin(255.0,v*1.1); }
                else { R=6;G=8;B=16; }
            }
            int idx=(py*N+px)*3;
            img[idx]=R; img[idx+1]=G; img[idx+2]=B;
        }
    }
    auto t1=std::chrono::high_resolution_clock::now();
    double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
    std::printf("video-omp    N=%d t=%.2f spheres=%zu  %.1f ms\n",
                N,t,cfg.spheres.size(),ms);

    FILE*f=std::fopen(out,"wb");
    std::fprintf(f,"P6\n%d %d\n255\n",N,N);
    std::fwrite(img.data(),1,img.size(),f);
    std::fclose(f);
    return 0;
}
