// ============================================================================
//  gpu_vulkan/main.cpp  —  Fase 3c: pipeline de cómputo completo
//
//  Amplía 3a (instance/device/queue) con todo lo necesario para EJECUTAR el
//  shader y recuperar la imagen:
//    - storage buffer en memoria host-visible (para leerlo desde CPU)
//    - carga del SPIR-V (geodesic.spv)
//    - descriptor set (conecta el buffer con el binding=0 del shader)
//    - compute pipeline + push constants (parámetros de la escena)
//    - command buffer: bind + dispatch
//    - fence: esperar a que la GPU termine
//    - copiar resultado a CPU y guardar PPM
//
//  Uso:
//    ./blackhole_gpu 1024 ../../results/gpu.ppm
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

// --- Parámetros de la escena. DEBE coincidir EXACTO con el bloque
//     push_constant del shader (mismo orden, mismos tipos). ---
struct Params {
    int32_t N;
    float   r_s;
    float   cam_r;
    float   fov;
    float   disk_inner;
    float   disk_outer;
    int32_t max_steps;
    float   dphi;
};

static std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) throw std::runtime_error("no se pudo abrir " + path);
    size_t size = (size_t)file.tellg();
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

int main(int argc, char** argv) {
    int  N   = (argc > 1) ? std::atoi(argv[1]) : 1024;
    std::string out = (argc > 2) ? argv[2] : "gpu.ppm";

    try {
        // ===== SETUP (igual que 3a) =====
        vk::raii::Context context;

        vk::ApplicationInfo appInfo{
            .pApplicationName = "blackhole-hpc",
            .apiVersion = VK_API_VERSION_1_3
        };
        std::vector<const char*> layers = { "VK_LAYER_KHRONOS_validation" };
        vk::InstanceCreateInfo instInfo{
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = (uint32_t)layers.size(),
            .ppEnabledLayerNames = layers.data()
        };
        vk::raii::Instance instance(context, instInfo);

        vk::raii::PhysicalDevices physicalDevices(instance);
        std::optional<vk::raii::PhysicalDevice> chosen;
        for (auto& pd : physicalDevices)
            if (pd.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
                chosen = std::move(pd);
        if (!chosen) { std::cerr << "no hay GPU discreta\n"; return 1; }
        std::cout << ">>> usando: " << chosen->getProperties().deviceName << "\n";

        auto qfams = chosen->getQueueFamilyProperties();
        uint32_t cf = UINT32_MAX;
        for (uint32_t i = 0; i < qfams.size(); ++i)
            if (qfams[i].queueFlags & vk::QueueFlagBits::eCompute) { cf = i; break; }

        float prio = 1.0f;
        vk::DeviceQueueCreateInfo qInfo{
            .queueFamilyIndex = cf, .queueCount = 1, .pQueuePriorities = &prio
        };
        vk::DeviceCreateInfo dInfo{
            .queueCreateInfoCount = 1, .pQueueCreateInfos = &qInfo
        };
        vk::raii::Device device(*chosen, dInfo);
        vk::raii::Queue  queue = device.getQueue(cf, 0);

        // ===== BUFFER de salida: N*N pixeles, cada uno vec4 (16 bytes) =====
        vk::DeviceSize bufSize = (vk::DeviceSize)N * N * 4 * sizeof(float);

        vk::BufferCreateInfo bufInfo{
            .size = bufSize,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer,
            .sharingMode = vk::SharingMode::eExclusive
        };
        vk::raii::Buffer buffer(device, bufInfo);

        auto memReq = buffer.getMemoryRequirements();
        auto memProps = chosen->getMemoryProperties();
        uint32_t memType = UINT32_MAX;
        auto wanted = vk::MemoryPropertyFlagBits::eHostVisible |
                      vk::MemoryPropertyFlagBits::eHostCoherent;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & wanted) == wanted) {
                memType = i; break;
            }
        }
        if (memType == UINT32_MAX) { std::cerr << "sin memoria host-visible\n"; return 1; }

        vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memReq.size, .memoryTypeIndex = memType
        };
        vk::raii::DeviceMemory memory(device, allocInfo);
        buffer.bindMemory(*memory, 0);

        // ===== SHADER + DESCRIPTOR LAYOUT =====
        auto code = readFile("../shaders/geodesic.spv");
        vk::ShaderModuleCreateInfo smInfo{
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };
        vk::raii::ShaderModule shader(device, smInfo);

        vk::DescriptorSetLayoutBinding binding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
        };
        vk::DescriptorSetLayoutCreateInfo dslInfo{
            .bindingCount = 1, .pBindings = &binding
        };
        vk::raii::DescriptorSetLayout dsLayout(device, dslInfo);

        vk::PushConstantRange pcRange{
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .offset = 0,
            .size = sizeof(Params)
        };
        vk::PipelineLayoutCreateInfo plInfo{
            .setLayoutCount = 1,
            .pSetLayouts = &*dsLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pcRange
        };
        vk::raii::PipelineLayout pipelineLayout(device, plInfo);

        // ===== COMPUTE PIPELINE =====
        vk::PipelineShaderStageCreateInfo stageInfo{
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = *shader,
            .pName = "main"
        };
        vk::ComputePipelineCreateInfo cpInfo{
            .stage = stageInfo,
            .layout = *pipelineLayout
        };
        vk::raii::Pipeline pipeline(device, nullptr, cpInfo);

        // ===== DESCRIPTOR SET =====
        vk::DescriptorPoolSize poolSize{
            .type = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1
        };
        vk::DescriptorPoolCreateInfo dpInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize
        };
        vk::raii::DescriptorPool descPool(device, dpInfo);

        vk::DescriptorSetAllocateInfo dsAlloc{
            .descriptorPool = *descPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &*dsLayout
        };
        vk::raii::DescriptorSets descSets(device, dsAlloc);
        vk::raii::DescriptorSet& descSet = descSets[0];

        vk::DescriptorBufferInfo dbInfo{
            .buffer = *buffer, .offset = 0, .range = bufSize
        };
        vk::WriteDescriptorSet write{
            .dstSet = *descSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo = &dbInfo
        };
        device.updateDescriptorSets({write}, {});

        // ===== COMMAND BUFFER =====
        vk::CommandPoolCreateInfo cpoolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = cf
        };
        vk::raii::CommandPool cmdPool(device, cpoolInfo);

        vk::CommandBufferAllocateInfo cbAlloc{
            .commandPool = *cmdPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };
        vk::raii::CommandBuffers cmdBufs(device, cbAlloc);
        vk::raii::CommandBuffer& cmd = cmdBufs[0];

        Params params{
            .N = N, .r_s = 2.0f, .cam_r = 30.0f, .fov = 0.45f,
            .disk_inner = 6.0f, .disk_outer = 18.0f,
            .max_steps = 3000, .dphi = 0.005f
        };

        uint32_t groups = (uint32_t)((N + 15) / 16);

        cmd.begin(vk::CommandBufferBeginInfo{});
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                               *pipelineLayout, 0, {*descSet}, {});
        cmd.pushConstants<Params>(*pipelineLayout,
                                  vk::ShaderStageFlagBits::eCompute, 0, {params});
        cmd.dispatch(groups, groups, 1);
        cmd.end();

        // ===== ENVIAR + ESPERAR (fence) + medir tiempo =====
        vk::raii::Fence fence(device, vk::FenceCreateInfo{});

        auto t0 = std::chrono::high_resolution_clock::now();

        vk::SubmitInfo submit{
            .commandBufferCount = 1, .pCommandBuffers = &*cmd
        };
        queue.submit({submit}, *fence);

        auto res = device.waitForFences({*fence}, VK_TRUE, UINT64_MAX);
        (void)res;

        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf(">>> GPU  N=%-5d  %.2f ms (dispatch+wait)\n", N, ms);

        // ===== LEER buffer y guardar PPM =====
        void* mapped = memory.mapMemory(0, bufSize);
        float* fpix = reinterpret_cast<float*>(mapped);

        std::vector<uint8_t> img(N * N * 3);
        for (int i = 0; i < N * N; ++i) {
            img[i*3+0] = (uint8_t)(std::fmin(fpix[i*4+0], 1.0f) * 255.0f);
            img[i*3+1] = (uint8_t)(std::fmin(fpix[i*4+1], 1.0f) * 255.0f);
            img[i*3+2] = (uint8_t)(std::fmin(fpix[i*4+2], 1.0f) * 255.0f);
        }
        memory.unmapMemory();

        std::ofstream f(out, std::ios::binary);
        f << "P6\n" << N << " " << N << "\n255\n";
        f.write(reinterpret_cast<char*>(img.data()), img.size());
        f.close();
        std::cout << ">>> imagen guardada en " << out << "\n";
    }
    catch (const vk::SystemError& e) {
        std::cerr << "Vulkan error: " << e.what() << "\n"; return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n"; return 1;
    }
    return 0;
}