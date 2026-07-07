// ============================================================================
//  gpu_vulkan/main3d.cpp  —  Modo extendido en GPU: video de esferas orbitando
//
//  Reusa la infraestructura de la Fase 3c y añade:
//    - un SEGUNDO storage buffer (binding=1) con las esferas
//    - cálculo de las órbitas (posiciones dependientes de t) en el host
//    - bucle de FRAMES: por cada t, actualiza esferas, despacha, guarda PPM
//
//  Uso:
//    ./blackhole_gpu3d N num_frames scene_file prefijo_salida
//  Ejemplo:
//    ./blackhole_gpu3d 720 240 0 ../../results/frames/f
//    -> genera f_0000.ppm ... f_0239.ppm
//
//  Los frames se ensamblan luego con ffmpeg (ver bench/make_video_gpu.sh).
// ============================================================================

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <optional>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <cmath>
#include <cstdio>
#include "scene_config.h"

// ---- Debe COINCIDIR con el push_constant del shader (128 bytes) ----
struct Params {
    int32_t N;
    float   r_s;
    float   escape_r;
    float   dphi;
    int32_t max_steps;
    int32_t use_disk;
    float   disk_inner;
    float   disk_outer;
    int32_t num_spheres;
    float   pad0, pad1, pad2;
    float   cam[4];
    float   fwd[4];
    float   right[4];
    float   camup[4];
    float   fov;
    float   pad3, pad4, pad5;
};

// ---- Debe coincidir con SphereData del shader (2 vec4 = 32 bytes) ----
struct SphereData {
    float c[4];    // xyz=centro, w=radio
    float col[4];  // rgb=color
};

static std::vector<char> readFile(const std::string& path){
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if(!f) throw std::runtime_error("no se pudo abrir "+path);
    size_t sz=(size_t)f.tellg(); std::vector<char> b(sz);
    f.seekg(0); f.read(b.data(), sz); return b;
}

// ---- Vec3 mínimo para las órbitas y la cámara ----
struct V3{ float x,y,z; };
static V3 sub(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};}
static float len(V3 a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
static V3 norm(V3 a){float l=len(a); return l>0?V3{a.x/l,a.y/l,a.z/l}:a;}

// Órbita inclinada (idéntica a la versión CPU).
static V3 orbit(float R,float incl,float phase,float speed,float t){
    float a=t*speed+phase;
    float x=R*std::cos(a), z=R*std::sin(a);
    float y=z*std::sin(incl); z=z*std::cos(incl);
    return {x,y,z};
}

int main(int argc,char**argv){
    int N          = argc>1? std::atoi(argv[1]):720;
    int num_frames = argc>2? std::atoi(argv[2]):240;
    std::string scene_file = argc>3? argv[3]:"../../scenes/default.txt";
    std::string prefix = argc>4? argv[4]:"frame";

    // cargar escena desde archivo (esferas + cámara)
    scene::Config cfg;
    if(!scene::load(scene_file, cfg)){
        std::fprintf(stderr,"aviso: no se pudo abrir '%s', uso escena por defecto\n",scene_file.c_str());
        scene::defaults(cfg);
    }
    int num_spheres = (int)cfg.spheres.size();
    if(num_spheres>4){ num_spheres=4; std::fprintf(stderr,"aviso: max 4 esferas, usando las primeras 4\n"); }

    try{
        // ===== SETUP Vulkan (idéntico a 3c) =====
        vk::raii::Context context;
        vk::ApplicationInfo appInfo{ .pApplicationName="bh3d", .apiVersion=VK_API_VERSION_1_3 };
        std::vector<const char*> layers={"VK_LAYER_KHRONOS_validation"};
        vk::InstanceCreateInfo instInfo{
            .pApplicationInfo=&appInfo,
            .enabledLayerCount=(uint32_t)layers.size(),
            .ppEnabledLayerNames=layers.data()
        };
        vk::raii::Instance instance(context, instInfo);

        vk::raii::PhysicalDevices pds(instance);
        std::optional<vk::raii::PhysicalDevice> chosen;
        for(auto& pd:pds)
            if(pd.getProperties().deviceType==vk::PhysicalDeviceType::eDiscreteGpu)
                chosen=std::move(pd);
        if(!chosen){ std::cerr<<"no hay GPU discreta\n"; return 1; }
        std::cout<<">>> usando: "<<chosen->getProperties().deviceName<<"\n";

        auto qfams=chosen->getQueueFamilyProperties();
        uint32_t cf=UINT32_MAX;
        for(uint32_t i=0;i<qfams.size();++i)
            if(qfams[i].queueFlags & vk::QueueFlagBits::eCompute){cf=i;break;}
        float prio=1.0f;
        vk::DeviceQueueCreateInfo qi{.queueFamilyIndex=cf,.queueCount=1,.pQueuePriorities=&prio};
        vk::DeviceCreateInfo di{.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
        vk::raii::Device device(*chosen, di);
        vk::raii::Queue queue=device.getQueue(cf,0);

        // ===== Buffer de salida (binding 0) =====
        vk::DeviceSize outSize=(vk::DeviceSize)N*N*4*sizeof(float);
        auto makeHostBuffer=[&](vk::DeviceSize size, vk::raii::Buffer& buf, vk::raii::DeviceMemory& mem){
            vk::BufferCreateInfo bi{.size=size,.usage=vk::BufferUsageFlagBits::eStorageBuffer,
                                    .sharingMode=vk::SharingMode::eExclusive};
            buf=vk::raii::Buffer(device,bi);
            auto req=buf.getMemoryRequirements();
            auto mp=chosen->getMemoryProperties();
            auto want=vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent;
            uint32_t mt=UINT32_MAX;
            for(uint32_t i=0;i<mp.memoryTypeCount;++i)
                if((req.memoryTypeBits&(1u<<i)) && (mp.memoryTypes[i].propertyFlags&want)==want){mt=i;break;}
            vk::MemoryAllocateInfo ai{.allocationSize=req.size,.memoryTypeIndex=mt};
            mem=vk::raii::DeviceMemory(device,ai);
            buf.bindMemory(*mem,0);
        };

        vk::raii::Buffer outBuf(nullptr); vk::raii::DeviceMemory outMem(nullptr);
        makeHostBuffer(outSize, outBuf, outMem);

        // ===== Buffer de esferas (binding 1) =====
        const int MAX_SPHERES=4;
        vk::DeviceSize sphSize=MAX_SPHERES*sizeof(SphereData);
        vk::raii::Buffer sphBuf(nullptr); vk::raii::DeviceMemory sphMem(nullptr);
        makeHostBuffer(sphSize, sphBuf, sphMem);

        // ===== Shader =====
        auto code=readFile("../shaders/geodesic3d.spv");
        vk::ShaderModuleCreateInfo smi{.codeSize=code.size(),
            .pCode=reinterpret_cast<const uint32_t*>(code.data())};
        vk::raii::ShaderModule shader(device, smi);

        // ===== Descriptor layout: binding 0 (salida) + binding 1 (esferas) =====
        std::array<vk::DescriptorSetLayoutBinding,2> binds{
            vk::DescriptorSetLayoutBinding{.binding=0,
                .descriptorType=vk::DescriptorType::eStorageBuffer,
                .descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{.binding=1,
                .descriptorType=vk::DescriptorType::eStorageBuffer,
                .descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute}
        };
        vk::DescriptorSetLayoutCreateInfo dsli{.bindingCount=2,.pBindings=binds.data()};
        vk::raii::DescriptorSetLayout dsLayout(device, dsli);

        vk::PushConstantRange pcr{.stageFlags=vk::ShaderStageFlagBits::eCompute,
            .offset=0,.size=sizeof(Params)};
        vk::PipelineLayoutCreateInfo pli{.setLayoutCount=1,.pSetLayouts=&*dsLayout,
            .pushConstantRangeCount=1,.pPushConstantRanges=&pcr};
        vk::raii::PipelineLayout pipelineLayout(device, pli);

        vk::PipelineShaderStageCreateInfo ssi{.stage=vk::ShaderStageFlagBits::eCompute,
            .module=*shader,.pName="main"};
        vk::ComputePipelineCreateInfo cpi{.stage=ssi,.layout=*pipelineLayout};
        vk::raii::Pipeline pipeline(device,nullptr,cpi);

        // ===== Descriptor pool + set =====
        vk::DescriptorPoolSize ps{.type=vk::DescriptorType::eStorageBuffer,.descriptorCount=2};
        vk::DescriptorPoolCreateInfo dpi{
            .flags=vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
        vk::raii::DescriptorPool descPool(device, dpi);
        vk::DescriptorSetAllocateInfo dsa{.descriptorPool=*descPool,
            .descriptorSetCount=1,.pSetLayouts=&*dsLayout};
        vk::raii::DescriptorSets descSets(device, dsa);
        vk::raii::DescriptorSet& descSet=descSets[0];

        vk::DescriptorBufferInfo dbi0{.buffer=*outBuf,.offset=0,.range=outSize};
        vk::DescriptorBufferInfo dbi1{.buffer=*sphBuf,.offset=0,.range=sphSize};
        std::array<vk::WriteDescriptorSet,2> writes{
            vk::WriteDescriptorSet{.dstSet=*descSet,.dstBinding=0,.descriptorCount=1,
                .descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&dbi0},
            vk::WriteDescriptorSet{.dstSet=*descSet,.dstBinding=1,.descriptorCount=1,
                .descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&dbi1}
        };
        device.updateDescriptorSets(writes, {});

        // ===== Command pool =====
        vk::CommandPoolCreateInfo cpci{
            .flags=vk::CommandPoolCreateFlagBits::eResetCommandBuffer,.queueFamilyIndex=cf};
        vk::raii::CommandPool cmdPool(device, cpci);
        vk::CommandBufferAllocateInfo cbi{.commandPool=*cmdPool,
            .level=vk::CommandBufferLevel::ePrimary,.commandBufferCount=1};
        vk::raii::CommandBuffers cmdBufs(device, cbi);
        vk::raii::CommandBuffer& cmd=cmdBufs[0];

        // ===== Cámara (fija, igual que en la versión CPU) =====
        V3 cam{(float)cfg.cam_x,(float)cfg.cam_y,(float)cfg.cam_z}, target{0,0,0}, up{0,1,0};
        V3 fwd=norm(sub(target,cam));
        V3 right=norm(cross(fwd,up));
        V3 camup=cross(right,fwd);
        float fov=0.7f;

        Params params{};
        params.N=N; params.r_s=2.0f; params.escape_r=100.0f; params.dphi=0.01f;
        params.max_steps=3000; params.use_disk=0;
        params.disk_inner=6.0f; params.disk_outer=18.0f; params.num_spheres=num_spheres;
        params.cam[0]=cam.x; params.cam[1]=cam.y; params.cam[2]=cam.z;
        params.fwd[0]=fwd.x; params.fwd[1]=fwd.y; params.fwd[2]=fwd.z;
        params.right[0]=right.x; params.right[1]=right.y; params.right[2]=right.z;
        params.camup[0]=camup.x; params.camup[1]=camup.y; params.camup[2]=camup.z;
        params.fov=fov;

        uint32_t groups=(uint32_t)((N+15)/16);
        float T_TOTAL=12.566f;  // una órbita de la amarilla (speed 0.5)

        std::vector<uint8_t> img(N*N*3);
        auto tStart=std::chrono::high_resolution_clock::now();

        // ===== BUCLE DE FRAMES =====
        for(int frame=0; frame<num_frames; ++frame){
            float t = T_TOTAL * frame / num_frames;

            // --- actualizar posiciones de las esferas para este t ---
            SphereData sd[MAX_SPHERES];
            for(int k=0;k<num_spheres;++k){
                const auto& os=cfg.spheres[k];
                V3 p=orbit((float)os.R,(float)os.incl,(float)os.phase,(float)os.speed,t);
                sd[k].c[0]=p.x; sd[k].c[1]=p.y; sd[k].c[2]=p.z; sd[k].c[3]=(float)os.radius;
                sd[k].col[0]=(float)os.r; sd[k].col[1]=(float)os.g; sd[k].col[2]=(float)os.b; sd[k].col[3]=1.0f;
            }

            void* smap=sphMem.mapMemory(0,sphSize);
            std::memcpy(smap, sd, num_spheres*sizeof(SphereData));
            sphMem.unmapMemory();

            // --- grabar y despachar ---
            cmd.reset();
            cmd.begin(vk::CommandBufferBeginInfo{});
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                   *pipelineLayout,0,{*descSet},{});
            cmd.pushConstants<Params>(*pipelineLayout,
                                      vk::ShaderStageFlagBits::eCompute,0,{params});
            cmd.dispatch(groups,groups,1);
            cmd.end();

            vk::raii::Fence fence(device, vk::FenceCreateInfo{});
            vk::SubmitInfo si{.commandBufferCount=1,.pCommandBuffers=&*cmd};
            queue.submit({si},*fence);
            auto r=device.waitForFences({*fence},VK_TRUE,UINT64_MAX); (void)r;

            // --- leer y guardar PPM ---
            void* omap=outMem.mapMemory(0,outSize);
            float* fp=reinterpret_cast<float*>(omap);
            for(int i=0;i<N*N;++i){
                img[i*3+0]=(uint8_t)(std::fmin(fp[i*4+0],1.0f)*255.0f);
                img[i*3+1]=(uint8_t)(std::fmin(fp[i*4+1],1.0f)*255.0f);
                img[i*3+2]=(uint8_t)(std::fmin(fp[i*4+2],1.0f)*255.0f);
            }
            outMem.unmapMemory();

            char fname[512];
            std::snprintf(fname,sizeof(fname),"%s_%04d.ppm",prefix.c_str(),frame);
            std::ofstream f(fname,std::ios::binary);
            f<<"P6\n"<<N<<" "<<N<<"\n255\n";
            f.write(reinterpret_cast<char*>(img.data()),img.size());
            f.close();

            if(frame%20==0) std::printf("  frame %d/%d (t=%.2f)\n",frame,num_frames,t);
        }

        auto tEnd=std::chrono::high_resolution_clock::now();
        double ms=std::chrono::duration<double,std::milli>(tEnd-tStart).count();
        std::printf(">>> GPU 3D: %d frames a %dx%d en %.1f ms (%.2f ms/frame)\n",
                    num_frames,N,N,ms,ms/num_frames);
        std::printf(">>> frames guardados como %s_XXXX.ppm\n",prefix.c_str());
    }
    catch(const vk::SystemError& e){ std::cerr<<"Vulkan error: "<<e.what()<<"\n"; return 1; }
    catch(const std::exception& e){ std::cerr<<"Error: "<<e.what()<<"\n"; return 1; }
    return 0;
}
