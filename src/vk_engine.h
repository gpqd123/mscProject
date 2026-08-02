#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include <vk_mem_alloc.h>
#include <vk_types.h>

#include <camera.h>
#include <vk_descriptors.h>
#include <vk_pipelines.h>

#include "terrain_erosion.h"
#include "terrain_visualization.h"

struct DeletionQueue {
    std::deque<std::function<void()>> functions;

    void push(std::function<void()>&& function) { functions.push_back(std::move(function)); }

    void flush()
    {
        for (auto it = functions.rbegin(); it != functions.rend(); ++it) (*it)();
        functions.clear();
    }
};

struct BackgroundPushConstants {
    glm::vec4 color {0.0f, 0.0f, 0.0f, 1.0f};
};

struct RenderObject {
    uint32_t indexCount {};
    uint32_t firstIndex {};
    VkBuffer indexBuffer {VK_NULL_HANDLE};
    glm::mat4 transform {1.0f};
    VkDeviceAddress vertexBufferAddress {};
};

struct FrameData {
    VkSemaphore swapchainSemaphore {VK_NULL_HANDLE};
    VkSemaphore renderSemaphore {VK_NULL_HANDLE};
    VkFence renderFence {VK_NULL_HANDLE};
    DescriptorAllocatorGrowable descriptors;
    DeletionQueue deletionQueue;
    VkCommandPool commandPool {VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer {VK_NULL_HANDLE};
};

struct EngineStats {
    float frameTimeMs {};
    float simulationTimeMs {};
    float meshUpdateTimeMs {};
    float gpuFrameTimeMs {};
    int triangleCount {};
    int drawCallCount {};
    float commandRecordTimeMs {};
};

class VulkanEngine;

struct SimulationPipeline {
    VkPipeline pipeline {VK_NULL_HANDLE};
    VkPipelineLayout layout {VK_NULL_HANDLE};

    void build(VulkanEngine& engine);
    void destroy(VkDevice device);
};

class VulkanEngine {
public:
    static constexpr unsigned int FrameOverlap = 2;

    bool initialized {false};
    int frameNumber {};
    VkExtent2D windowExtent {1700, 900};
    struct SDL_Window* window {nullptr};

    VkInstance instance {VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger {VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice {VK_NULL_HANDLE};
    VkDevice device {VK_NULL_HANDLE};
    VkQueue graphicsQueue {VK_NULL_HANDLE};
    uint32_t graphicsQueueFamily {};

    FrameData frames[FrameOverlap];
    VkSurfaceKHR surface {VK_NULL_HANDLE};
    VkSwapchainKHR swapchain {VK_NULL_HANDLE};
    VkFormat swapchainImageFormat {};
    VkExtent2D swapchainExtent {};
    VkExtent2D drawExtent {};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    DescriptorAllocator globalDescriptors;
    VkDescriptorSet drawImageDescriptor {VK_NULL_HANDLE};
    VkDescriptorSetLayout drawImageDescriptorLayout {VK_NULL_HANDLE};
    VkDescriptorSetLayout sceneDescriptorLayout {VK_NULL_HANDLE};

    VkPipeline backgroundPipeline {VK_NULL_HANDLE};
    VkPipelineLayout backgroundPipelineLayout {VK_NULL_HANDLE};
    BackgroundPushConstants backgroundConstants;
    SimulationPipeline simulationPipeline;

    DeletionQueue mainDeletionQueue;
    VmaAllocator allocator {VK_NULL_HANDLE};
    AllocatedImage drawImage {};
    AllocatedImage depthImage {};

    VkFence immediateFence {VK_NULL_HANDLE};
    VkCommandBuffer immediateCommandBuffer {VK_NULL_HANDLE};
    VkCommandPool immediateCommandPool {VK_NULL_HANDLE};
    VkQueryPool timestampQueryPool {VK_NULL_HANDLE};
    float timestampPeriodNanoseconds {1.0f};
    bool timestampQueriesSupported {false};

    GPUMeshBuffers terrainMesh {};
    GPUMeshBuffers waterParticleMesh {};
    uint32_t terrainIndexCount {};
    uint32_t waterParticleIndexCount {};
    uint32_t maxWaterParticles {4096};
    void* terrainVertexData {};
    void* waterParticleVertexData {};

    HeightField terrain;
    HeightField initialTerrain;
    double initialTerrainMass {};
    std::unique_ptr<RealtimeErosionSimulator> realtimeErosion;
    ErosionParameters erosionParameters;
    int erosionPresetIndex {1};
    terrainvis::Settings visualSettings;
    bool simulationPaused {false};
    float simulationTimeScale {1.0f};
    float waterSpawnRate {20.0f};
    int waterParticleLimit {128};
    std::vector<RenderObject> drawCommands;

    GPUSceneData sceneData;
    Camera mainCamera;
    bool cameraCaptured {false};
    EngineStats stats;

    bool resizeRequested {false};
    bool freezeRendering {false};

    void init();
    void run();
    void cleanup();
    void draw();
    void drawMain(VkCommandBuffer commandBuffer);
    void drawImGui(VkCommandBuffer commandBuffer, VkImageView targetImageView);
    void drawGeometry(VkCommandBuffer commandBuffer);
    void updateScene();

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
    FrameData& currentFrame();
    AllocatedBuffer createBuffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    AllocatedImage createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    void immediateSubmit(std::function<void(VkCommandBuffer commandBuffer)>&& function);
    void destroyImage(const AllocatedImage& image);
    void destroyBuffer(const AllocatedBuffer& buffer);

private:
    void initVulkan();
    void initSwapchain();
    void createSwapchain(uint32_t width, uint32_t height);
    void resizeSwapchain();
    void destroySwapchain();
    void initCommands();
    void initPipelines();
    void initBackgroundPipeline();
    void initDescriptors();
    void initSyncStructures();
    void initImGui();
    void initTerrain();
    void updateRealtimeErosion(float deltaSeconds);
    void updateTerrainVertices();
    void updateWaterParticleVertices();
    void drawSimulationUi();
};
