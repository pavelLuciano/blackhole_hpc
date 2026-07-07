#include "geodesic.h"
#include <cstdio>
#include <cmath>
// Test: barre b alrededor de b_crit=3√3≈5.196 y verifica que la transición
// horizonte<->escape ocurre ahí. Es la validación física definitiva.
int main(){
    bh::Scene sc; sc.disk_inner=1e9; sc.disk_outer=1e9; // desactiva disco
    double bcrit = 3.0*std::sqrt(3.0);
    printf("b_crit teórico = %.4f\n", bcrit);
    printf("  b     resultado\n");
    for(double b=4.5; b<=6.0; b+=0.1){
        // invertir: dado b, qué psi -> b = r sin(psi)/sqrt(f)
        double r0=sc.cam_r, f0=1-sc.r_s/r0;
        double sinpsi = b*std::sqrt(f0)/r0;
        if(sinpsi>1) continue;
        double psi=std::asin(sinpsi);
        double hr; bh::Hit h=bh::trace_ray(sc,psi,hr);
        const char* nm = h==bh::Hit::Horizon?"HORIZONTE":(h==bh::Hit::Sky?"escapa":"disco");
        printf(" %.3f  %s\n", b, nm);
    }
    return 0;
}