// ============================================================================
//  render3d_test.cpp  —  prueba CPU del modo extendido (esferas 3D orbitando)
//
//  Renderiza UN frame con esferas en órbitas inclinadas alrededor del BH.
//  Parámetros:  ./render3d N t use_disk salida.ppm
//    N        resolución
//    t        tiempo (para posicionar las esferas en su órbita)
//    use_disk 1=con disco de acreción, 0=sin disco
// ============================================================================

#include "geodesic3d.h"
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

int main(int argc,char**argv){
    int N       = argc>1? std::atoi(argv[1]):512;
    double t    = argc>2? std::atof(argv[2]):0.0;
    bool use_disk = argc>3? std::atoi(argv[3])!=0 : false;
    const char* out = argc>4? argv[4]:"render3d.ppm";

    Scene3D sc;
    sc.r_s=2.0; sc.escape_r=100.0; sc.dphi=0.01; sc.max_steps=3000;
    sc.use_disk=use_disk; sc.disk_inner=6.0; sc.disk_outer=18.0;

    // --- 3 esferas grandes de colores vivos en órbitas INCLINADAS ---
    // cada una a distinto radio, distinta fase, plano inclinado.
    auto orbit = [&](double R, double incl, double phase, double speed){
        double a = t*speed + phase;
        // órbita circular de radio R en un plano inclinado 'incl' respecto al ecuador
        double x = R*std::cos(a);
        double z = R*std::sin(a);
        double y = z*std::sin(incl);   // inclinar: y crece con z
        z = z*std::cos(incl);
        return Vec3{x,y,z};
    };
    sc.spheres.push_back({ orbit(14, 0.6, 0.0,      0.9), 2.2, 1.0,0.25,0.20 }); // roja
    sc.spheres.push_back({ orbit(22, -0.4, 2.1,     0.6), 2.8, 0.25,0.5,1.0 });  // azul
    sc.spheres.push_back({ orbit(30, 0.9, 4.0,      0.4), 3.2, 0.3,1.0,0.35 });  // verde
    // amarilla: radio medio, órbita poco inclinada que la lleva por DETRÁS del
    // BH (fase elegida para que cruce la zona trasera y genere anillo de Einstein)
    sc.spheres.push_back({ orbit(18, 0.15, 1.0,     0.5), 3.0, 1.0,0.85,0.2 });  // amarilla

    // --- cámara: mira al BH desde -z, ligeramente elevada ---
    Vec3 cam{0, 6, -45};
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
            // dirección del rayo por píxel
            Vec3 dir = normalize(fwd + right*(sx*fov) + camup*(-sy*fov));
            Result res = trace(sc, cam, dir);

            uint8_t R,G,B;
            if(res.hit==Hit::Horizon){ R=G=B=0; }
            else if(res.hit==Hit::Sphere){
                const Sphere& s=sc.spheres[res.sphere_id];
                // sombreado Lambert: normal = (punto - centro) normalizado
                Vec3 nrm = normalize(res.hit_pos - s.center);
                Vec3 lightdir = normalize(Vec3{-0.4,0.7,-0.6});
                double diff = dot(nrm,lightdir);
                diff = diff<0.15?0.15:diff;   // luz ambiente mínima
                R=(uint8_t)std::fmin(255.0,s.r*255*diff);
                G=(uint8_t)std::fmin(255.0,s.g*255*diff);
                B=(uint8_t)std::fmin(255.0,s.b*255*diff);
            }
            else if(res.hit==Hit::Disk){
                double f=(res.disk_r-sc.disk_inner)/(sc.disk_outer-sc.disk_inner);
                f=f<0?0:(f>1?1:f);
                R=255; G=(uint8_t)(180*(1-f)+60); B=(uint8_t)(40*(1-f));
            }
            else { // cielo
                double ax=res.dir.x, ay=res.dir.y;
                double star=starfield(ax,ay);
                if(star>0){ uint8_t v=(uint8_t)(star*255); R=v;G=v;B=(uint8_t)std::fmin(255.0,v*1.1); }
                else { R=6;G=8;B=16; }
            }
            int idx=(py*N+px)*3;
            img[idx]=R; img[idx+1]=G; img[idx+2]=B;
        }
    }
    auto t1=std::chrono::high_resolution_clock::now();
    double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
    std::printf("render3d N=%d t=%.2f disk=%d  %.1f ms\n",N,t,use_disk,ms);

    FILE*f=std::fopen(out,"wb");
    std::fprintf(f,"P6\n%d %d\n255\n",N,N);
    std::fwrite(img.data(),1,img.size(),f);
    std::fclose(f);
    return 0;
}
