
#include "vk_engine.h"

#include "vk_images.h"
#include "vk_descriptors.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include "VkBootstrap.h"

#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

constexpr bool bUseValidationLayers = true;

void VulkanEngine::init()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    window = SDL_CreateWindow("Hydraulic Erosion Simulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowExtent.width,
        windowExtent.height, window_flags);
    if (!window) throw std::runtime_error(SDL_GetError());

    initVulkan();

    initSwapchain();

    initCommands();

    initSyncStructures();

    initDescriptors();

    initPipelines();

    initTerrain();

    initImGui();

    // everything went fine
    initialized = true;

    mainCamera.velocity = glm::vec3(0.f);
    mainCamera.orbitTarget = glm::vec3(0.f, 7.f, 0.f);
    mainCamera.orbitDistance = 92.f;
    mainCamera.pitch = 0.30f;
    mainCamera.yaw = 3.14f;
    mainCamera.setOrbitEnabled(true);
    mainCamera.update();
    mainCamera.setOrbitEnabled(false);
}

void VulkanEngine::cleanup()
{
    if (initialized) {

        // make sure the gpu has stopped doing its things
        vkDeviceWaitIdle(device);

        if (terrainMesh.vertexBuffer.buffer != VK_NULL_HANDLE) {
            destroyBuffer(terrainMesh.vertexBuffer);
            destroyBuffer(terrainMesh.indexBuffer);
            terrainMesh = {};
        }
        if (waterParticleMesh.vertexBuffer.buffer != VK_NULL_HANDLE) {
            destroyBuffer(waterParticleMesh.vertexBuffer);
            destroyBuffer(waterParticleMesh.indexBuffer);
            waterParticleMesh = {};
        }
        for (auto& frame : frames) {
            frame.deletionQueue.flush();
        }

        mainDeletionQueue.flush();

        destroySwapchain();

        vkDestroySurfaceKHR(instance, surface, nullptr);

        vmaDestroyAllocator(allocator);

        vkDestroyDevice(device, nullptr);
        vkb::destroy_debug_utils_messenger(instance, debugMessenger);
        vkDestroyInstance(instance, nullptr);

        SDL_DestroyWindow(window);
    }
}

void VulkanEngine::initBackgroundPipeline()
{
	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = &drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant{};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(BackgroundPushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(device, &computeLayout, nullptr, &backgroundPipelineLayout));

	VkShaderModule shader;
	if (!vkutil::load_shader_module("../../shaders/background.comp.spv", device, &shader))
		throw std::runtime_error("Failed to load background.comp.spv");

	VkPipelineShaderStageCreateInfo stageinfo{};
	stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext = nullptr;
	stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = shader;
	stageinfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = backgroundPipelineLayout;
	computePipelineCreateInfo.stage = stageinfo;

	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &backgroundPipeline));
	vkDestroyShaderModule(device, shader, nullptr);
	mainDeletionQueue.push([&]() {
		vkDestroyPipelineLayout(device, backgroundPipelineLayout, nullptr);
		vkDestroyPipeline(device, backgroundPipeline, nullptr);
		});
}


void VulkanEngine::drawMain(VkCommandBuffer cmd)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, backgroundPipeline);

	// bind the descriptor set containing the draw image for the compute pipeline
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, backgroundPipelineLayout, 0, 1, &drawImageDescriptor, 0, nullptr);

	vkCmdPushConstants(cmd, backgroundPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
		sizeof(BackgroundPushConstants), &backgroundConstants);
	// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	vkCmdDispatch(cmd, std::ceil(drawExtent.width / 16.0), std::ceil(drawExtent.height / 16.0), 1);

	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_GENERAL);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = vkinit::rendering_info(drawExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);
	auto start = std::chrono::steady_clock::now();
	drawGeometry(cmd);

	auto end = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	stats.commandRecordTimeMs = elapsed.count() / 1000.f;

	vkCmdEndRendering(cmd);
}

void VulkanEngine::drawImGui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_GENERAL);
	VkRenderingInfo renderInfo = vkinit::rendering_info(windowExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void VulkanEngine::draw()
{
	//wait until the gpu has finished rendering the last frame. Timeout of 1 second
	VK_CHECK(vkWaitForFences(device, 1, &currentFrame().renderFence, true, 1000000000));
	const uint32_t timestampBase = static_cast<uint32_t>(frameNumber % FrameOverlap) * 2;
	if (timestampQueriesSupported && frameNumber >= static_cast<int>(FrameOverlap)) {
		uint64_t timestamps[2] {};
		const VkResult queryResult = vkGetQueryPoolResults(device, timestampQueryPool,
			timestampBase, 2, sizeof(timestamps), timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
		if (queryResult == VK_SUCCESS && timestamps[1] >= timestamps[0]) {
			stats.gpuFrameTimeMs = static_cast<float>(timestamps[1] - timestamps[0])
				* timestampPeriodNanoseconds / 1'000'000.0f;
		}
	}

	currentFrame().deletionQueue.flush();
    currentFrame().descriptors.clear_pools(device);
	//request image from the swapchain
	uint32_t swapchainImageIndex;

	VkResult e = vkAcquireNextImageKHR(device, swapchain, 1000000000, currentFrame().swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resizeRequested = true;
		return ;
	}
	drawExtent.height = std::min(swapchainExtent.height, drawImage.imageExtent.height) * 1.f;
	drawExtent.width = std::min(swapchainExtent.width, drawImage.imageExtent.width) *  1.f;

	VK_CHECK(vkResetFences(device, 1, &currentFrame().renderFence));

	//now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(currentFrame().commandBuffer, 0));

	//naming it cmd for shorter writing
	VkCommandBuffer cmd = currentFrame().commandBuffer;

	//begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	if (timestampQueriesSupported) {
		vkCmdResetQueryPool(cmd, timestampQueryPool, timestampBase, 2);
		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, timestampBase);
	}

	// transition our main draw image into general layout so we can write into it
	// we will overwrite it all so we dont care about what was the older layout
	vkutil::transition_image(cmd, drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    vkutil::transition_image(cmd, depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	drawMain(cmd);

	//transtion the draw image and the swapchain image into their correct transfer layouts
	vkutil::transition_image(cmd, drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::transition_image(cmd, swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkExtent2D extent;
	extent.height = windowExtent.height;
	extent.width = windowExtent.width;
	//extent.depth = 1;

	// execute a copy from the draw image into the swapchain
	vkutil::copy_image_to_image(cmd, drawImage.image, swapchainImages[swapchainImageIndex], drawExtent,swapchainExtent);

	// set swapchain image layout to Attachment Optimal so we can draw it
	vkutil::transition_image(cmd, swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	//draw imgui into the swapchain image
	drawImGui(cmd, swapchainImageViews[swapchainImageIndex]);

	// set swapchain image layout to Present so we can draw it
	vkutil::transition_image(cmd, swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	if (timestampQueriesSupported)
		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampQueryPool, timestampBase + 1);

	//finalize the command buffer (we can no longer add commands, but it can now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));

	//prepare the submission to the queue. 
	//we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
	//we will signal the renderSemaphore, to signal that rendering has finished

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, currentFrame().swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, currentFrame().renderSemaphore);

	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);

	//submit command buffer to the queue and execute it.
	// renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, currentFrame().renderFence));

	//prepare present
	// this will put the image we just rendered to into the visible window.
	// we want to wait on the renderSemaphore for that, 
	// as its necessary that drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentInfo = vkinit::present_info();

	presentInfo.pSwapchains = &swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &currentFrame().renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);
	if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resizeRequested = true;
        return;
	}
	//increase the number of frames drawn
	frameNumber++;
}

void VulkanEngine::drawGeometry(VkCommandBuffer cmd)
{
    AllocatedBuffer gpuSceneDataBuffer = createBuffer(sizeof(GPUSceneData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    currentFrame().deletionQueue.push([=,this](){
        destroyBuffer(gpuSceneDataBuffer);
    });

    auto* sceneUniformData = static_cast<GPUSceneData*>(gpuSceneDataBuffer.info.pMappedData);
    *sceneUniformData = sceneData;

    VkDescriptorSet globalDescriptor = currentFrame().descriptors.allocate(device, sceneDescriptorLayout);

	DescriptorWriter writer;
	writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.update_set(device, globalDescriptor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, simulationPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, simulationPipeline.layout,
        0, 1, &globalDescriptor, 0, nullptr);

    VkViewport viewport {0.0f, 0.0f, static_cast<float>(drawExtent.width),
        static_cast<float>(drawExtent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor {{0, 0}, drawExtent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    stats.drawCallCount = 0;
    stats.triangleCount = 0;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;
    for (const RenderObject& object : drawCommands) {
        const RenderObject& r = object;
        if (r.indexBuffer != lastIndexBuffer) {
            lastIndexBuffer = r.indexBuffer;
            vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
        GPUDrawPushConstants pushConstants {r.transform, r.vertexBufferAddress};
        vkCmdPushConstants(cmd, simulationPipeline.layout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(GPUDrawPushConstants), &pushConstants);

        stats.drawCallCount++;
        stats.triangleCount += r.indexCount / 3;
        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
    }
    drawCommands.clear();
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        auto start = std::chrono::steady_clock::now();

        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {

				if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    resizeRequested = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
					freezeRendering = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
					freezeRendering = false;
				}
            }

			if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
				cameraCaptured = !cameraCaptured;
				mainCamera.setOrbitEnabled(cameraCaptured);
				SDL_SetRelativeMouseMode(cameraCaptured ? SDL_TRUE : SDL_FALSE);
				SDL_ShowCursor(cameraCaptured ? SDL_DISABLE : SDL_ENABLE);
			}
			if (cameraCaptured) {
				mainCamera.processSDLEvent(e);
				// Keep keyboard/window state in ImGui, but captured pointer input belongs to the camera.
				if (e.type != SDL_MOUSEMOTION && e.type != SDL_MOUSEWHEEL)
					ImGui_ImplSDL2_ProcessEvent(&e);
			} else {
				ImGui_ImplSDL2_ProcessEvent(&e);
			}
        }

        if (freezeRendering) continue;

		if (resizeRequested) {
			resizeSwapchain();
		}

        // imgui new frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();

        ImGui::Begin("Stats");

		ImGui::Text("Frame time %.2f ms", stats.frameTimeMs);
		ImGui::Text("CPU simulation %.3f ms", stats.simulationTimeMs);
		ImGui::Text("CPU mesh update %.3f ms", stats.meshUpdateTimeMs);
		ImGui::Text("CPU command recording %.3f ms", stats.commandRecordTimeMs);
		if (timestampQueriesSupported) ImGui::Text("GPU frame %.3f ms", stats.gpuFrameTimeMs);
		else ImGui::TextUnformatted("GPU frame timing unavailable");
		ImGui::Text("Triangles %i", stats.triangleCount);
		ImGui::Text("Draw calls %i", stats.drawCallCount);
		if (realtimeErosion) {
			drawSimulationUi();
		}
		ImGui::Separator();
		ImGui::TextUnformatted(cameraCaptured
			? "Camera: orbit (RMB to release, wheel to zoom)"
			: "UI: pointer active (RMB for orbit camera)");
        ImGui::End();

		ImGui::Render();
        updateScene();


        draw();

        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        stats.frameTimeMs = elapsed.count() / 1000.f;
    }
}

void VulkanEngine::drawSimulationUi()
{
	const auto& drops = realtimeErosion->droplets();
	const double massError = terrain.totalHeight() + realtimeErosion->carriedSediment()
		+ realtimeErosion->totalOutflow() - initialTerrainMass;
	ImGui::Text("active drops %zu/%u", drops.size(), realtimeErosion->maxDroplets());
	ImGui::Text("eroded %.3f  deposited %.3f  mass error %.5f",
		realtimeErosion->totalEroded(), realtimeErosion->totalDeposited(), massError);

	if (ImGui::CollapsingHeader("Simulation speed", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Current physics speed: %.2fx", simulationTimeScale);
		ImGui::SliderFloat("Time multiplier", &simulationTimeScale, 0.25f, 8.0f, "%.2fx");
		if (ImGui::Button("1x")) simulationTimeScale = 1.0f;
		ImGui::SameLine();
		if (ImGui::Button("2x")) simulationTimeScale = 2.0f;
		ImGui::SameLine();
		if (ImGui::Button("4x")) simulationTimeScale = 4.0f;
		ImGui::SameLine();
		if (ImGui::Button("8x")) simulationTimeScale = 8.0f;
		ImGui::SameLine();
		if (ImGui::Button(simulationPaused ? "Resume" : "Pause"))
			simulationPaused = !simulationPaused;
	}

	if (ImGui::CollapsingHeader("Rain", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::SliderFloat("Drops per second", &waterSpawnRate, 0.0f, 200.0f, "%.1f"))
			realtimeErosion->setSpawnRate(waterSpawnRate);
		if (ImGui::SliderInt("Maximum active drops", &waterParticleLimit, 1, static_cast<int>(maxWaterParticles)))
			realtimeErosion->setMaxDroplets(static_cast<uint32_t>(waterParticleLimit));
		if (ImGui::Button("Reset terrain, water and particles")) {
			terrain = initialTerrain;
			realtimeErosion->reset();
			updateTerrainVertices();
			updateWaterParticleVertices();
		}
	}

	bool erosionChanged = false;
	if (ImGui::CollapsingHeader("Erosion physics")) {
		const char* presetPreview = erosionPresetIndex < 0
			? "Custom"
			: erosionPresetName(static_cast<ErosionPreset>(erosionPresetIndex));
		if (ImGui::BeginCombo("Parameter preset", presetPreview)) {
			for (int index = 0; index < 3; ++index) {
				const auto preset = static_cast<ErosionPreset>(index);
				const bool selected = erosionPresetIndex == index;
				if (ImGui::Selectable(erosionPresetName(preset), selected)) {
					erosionPresetIndex = index;
					erosionParameters = erosionParametersForPreset(preset);
					erosionChanged = true;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		bool slidersChanged = false;
		slidersChanged |= ImGui::SliderFloat("Sediment capacity", &erosionParameters.capacityFactor, 0.1f, 12.0f);
		slidersChanged |= ImGui::SliderFloat("Erosion rate", &erosionParameters.erosionRate, 0.0f, 1.0f);
		slidersChanged |= ImGui::SliderFloat("Deposition rate", &erosionParameters.depositionRate, 0.0f, 1.0f);
		slidersChanged |= ImGui::SliderFloat("Evaporation rate", &erosionParameters.evaporationRate, 0.0f, 0.1f, "%.4f");
		slidersChanged |= ImGui::SliderFloat("Erosion radius", &erosionParameters.erosionRadius, 0.5f, 4.0f);
		if (slidersChanged) {
			erosionPresetIndex = -1;
			erosionChanged = true;
		}
	}
	if (erosionChanged) realtimeErosion->setErosionParameters(erosionParameters);

}

void VulkanEngine::updateScene()
{
	mainCamera.update();
	updateRealtimeErosion(std::max(stats.frameTimeMs / 1000.0f, 1.0f / 120.0f));

	glm::mat4 view = mainCamera.getViewMatrix();

	glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)windowExtent.width / (float)windowExtent.height, 10000.f, 0.1f);
	projection[1][1] *= -1;
	sceneData.viewproj = projection * view;

	auto queueMesh = [&](const GPUMeshBuffers& mesh, uint32_t indexCount) {
		if (indexCount == 0) return;
		drawCommands.push_back(RenderObject {
			.indexCount = indexCount,
			.firstIndex = 0,
			.indexBuffer = mesh.indexBuffer.buffer,
			.transform = glm::mat4(1.0f),
			.vertexBufferAddress = mesh.vertexBufferAddress,
		});
	};
	queueMesh(terrainMesh, terrainIndexCount);
	queueMesh(waterParticleMesh, waterParticleIndexCount);
}

AllocatedBuffer VulkanEngine::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    // allocate buffer
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;

    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    AllocatedBuffer newBuffer;

    // allocate the buffer
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
        &newBuffer.info));

    return newBuffer;
}

AllocatedImage VulkanEngine::createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
    if (mipmapped) {
		img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    // always allocate images on dedicated GPU memory
    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // allocate and create the image
    VK_CHECK(vmaCreateImage(allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

    // if the format is a depth format, we will need to have it use the correct
    // aspect flag
    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // build a image-view for the image
    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface;
    
    newSurface.vertexBuffer = createBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);


    VkBufferDeviceAddressInfo deviceAdressInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer};
    newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(device, &deviceAdressInfo);

    newSurface.indexBuffer = createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging = createBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void* data = staging.info.pMappedData;

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy { 0 };
        vertexCopy.dstOffset = 0;
        vertexCopy.srcOffset = 0;
        vertexCopy.size = vertexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

        VkBufferCopy indexCopy { 0 };
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size = indexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
    });

    destroyBuffer(staging);

    return newSurface;
}

FrameData& VulkanEngine::currentFrame()
{
    return frames[frameNumber % FrameOverlap];
}

void VulkanEngine::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    VK_CHECK(vkResetFences(device, 1, &immediateFence));
    VK_CHECK(vkResetCommandBuffer(immediateCommandBuffer, 0));

    VkCommandBuffer cmd = immediateCommandBuffer;
    // begin the command buffer recording. We will use this command buffer exactly
    // once, so we want to let vulkan know that
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

    // submit command buffer to the queue and execute it.
    //  renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, immediateFence));

    VK_CHECK(vkWaitForFences(device, 1, &immediateFence, true, 9999999999));
}

void VulkanEngine::destroyImage(const AllocatedImage& img)
{
    vkDestroyImageView(device, img.imageView, nullptr);
    vmaDestroyImage(allocator, img.image, img.allocation);
}

void VulkanEngine::destroyBuffer(const AllocatedBuffer& buffer)
{
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}

void VulkanEngine::initVulkan()
{
    vkb::InstanceBuilder builder;

    // make the vulkan instance, with basic debug features
    auto inst_ret = builder.set_app_name("Hydraulic Erosion Simulator")
                        .request_validation_layers(bUseValidationLayers)
                        .use_default_debug_messenger()
                        .require_api_version(1, 3, 0)
                        .build();

    vkb::Instance vkb_inst = inst_ret.value();

    // grab the instance
    instance = vkb_inst.instance;
    debugMessenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(window, instance, &surface);

    VkPhysicalDeviceVulkan13Features features13 {};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = true;
	features13.synchronization2 = true;
   
   VkPhysicalDeviceVulkan12Features features12 {};
   features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
   features12.bufferDeviceAddress = true;
    // Select a Vulkan 1.3 device that can present to the SDL surface.
    vkb::PhysicalDeviceSelector selector { vkb_inst };
    vkb::PhysicalDevice selectedDevice = selector.set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .set_surface(surface)
        .select()
        .value();

    vkb::DeviceBuilder deviceBuilder { selectedDevice };

    vkb::Device vkbDevice = deviceBuilder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    device = vkbDevice.device;
    physicalDevice = selectedDevice.physical_device;

    // use vkbootstrap to get a Graphics queue
    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();

    graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &allocator);
}

void VulkanEngine::initSwapchain()
{
    createSwapchain(windowExtent.width, windowExtent.height);

	//depth image size will match the window
	VkExtent3D drawImageExtent = {
		windowExtent.width,
		windowExtent.height,
		1
	};

	//hardcoding the draw format to 32 bit float
	drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimg_info = vkinit::image_create_info(drawImage.imageFormat, drawImageUsages, drawImageExtent);

	//for the draw image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(allocator, &rimg_info, &rimg_allocinfo, &drawImage.image, &drawImage.allocation, nullptr);

	//build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(drawImage.imageFormat, drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(device, &rview_info, nullptr, &drawImage.imageView));

    //create a depth image too
	//hardcoding the draw format to 32 bit float
	depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(depthImage.imageFormat, depthImageUsages, drawImageExtent);

	//allocate and create the image
	vmaCreateImage(allocator, &dimg_info, &rimg_allocinfo, &depthImage.image, &depthImage.allocation, nullptr);

	//build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(depthImage.imageFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(device, &dview_info, nullptr, &depthImage.imageView));


	//add to deletion queues
	mainDeletionQueue.push([=]() {
		vkDestroyImageView(device, drawImage.imageView, nullptr);
		vmaDestroyImage(allocator, drawImage.image, drawImage.allocation);

		vkDestroyImageView(device, depthImage.imageView, nullptr);
		vmaDestroyImage(allocator, depthImage.image, depthImage.allocation);
	});
}


void VulkanEngine::createSwapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder swapchainBuilder{ physicalDevice,device,surface };

	swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		//.use_default_format_selection()
		.set_desired_format(VkSurfaceFormatKHR{ .format = swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	swapchainExtent = vkbSwapchain.extent;
	//store swapchain and its related images
	swapchain = vkbSwapchain.swapchain;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();
}
void VulkanEngine::destroySwapchain()
{
	vkDestroySwapchainKHR(device, swapchain, nullptr);

	// destroy swapchain resources
	for (int i = 0; i < swapchainImageViews.size(); i++) {

		vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	}
}

void VulkanEngine::resizeSwapchain()
{
	vkDeviceWaitIdle(device);

	destroySwapchain();

	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	windowExtent.width = w;
	windowExtent.height = h;

	createSwapchain(windowExtent.width, windowExtent.height);

	resizeRequested = false;
}

void VulkanEngine::initCommands()
{
    // create a command pool for commands submitted to the graphics queue.
    // we also want the pool to allow for resetting of individual command buffers
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FrameOverlap; i++) {

        VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frames[i].commandPool));

        // allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(frames[i].commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &frames[i].commandBuffer));

        mainDeletionQueue.push([=]() { vkDestroyCommandPool(device, frames[i].commandPool, nullptr); });
    }

    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &immediateCommandPool));

    // allocate the default command buffer that we will use for rendering
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(immediateCommandPool, 1);

    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &immediateCommandBuffer));

    mainDeletionQueue.push([=]() { vkDestroyCommandPool(device, immediateCommandPool, nullptr); });
}

void VulkanEngine::initSyncStructures()
{
    // create syncronization structures
    // one fence to control when the gpu has finished rendering the frame,
    // and 2 semaphores to syncronize rendering with swapchain
    // we want the fence to start signalled so we can wait on it on the first
    // frame
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &immediateFence));

    mainDeletionQueue.push([=]() { vkDestroyFence(device, immediateFence, nullptr); });

	VkPhysicalDeviceProperties deviceProperties {};
	vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
	timestampPeriodNanoseconds = deviceProperties.limits.timestampPeriod;
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueProperties(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProperties.data());
	timestampQueriesSupported = graphicsQueueFamily < queueProperties.size()
		&& queueProperties[graphicsQueueFamily].timestampValidBits > 0;
	if (timestampQueriesSupported) {
		VkQueryPoolCreateInfo queryPoolInfo {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
		queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryPoolInfo.queryCount = FrameOverlap * 2;
		VK_CHECK(vkCreateQueryPool(device, &queryPoolInfo, nullptr, &timestampQueryPool));
		mainDeletionQueue.push([=]() { vkDestroyQueryPool(device, timestampQueryPool, nullptr); });
	}

    for (int i = 0; i < FrameOverlap; i++) {

        VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &frames[i].renderFence));

        VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frames[i].renderSemaphore));

        mainDeletionQueue.push([=]() {
            vkDestroyFence(device, frames[i].renderFence, nullptr);
            vkDestroySemaphore(device, frames[i].swapchainSemaphore, nullptr);
            vkDestroySemaphore(device, frames[i].renderSemaphore, nullptr);
        });
    }
}

void VulkanEngine::initTerrain()
{
    const std::filesystem::path heightmapPath = "../../assets/heightmap.pgm";
    try {
        terrain = std::filesystem::exists(heightmapPath)
            ? HeightField::loadPgm(heightmapPath, 20.0f)
            : HeightField::procedural(128, 128, 2026);
    } catch (const std::exception& error) {
        fmt::println("Heightmap load failed ({}); using procedural terrain", error.what());
        terrain = HeightField::procedural(128, 128, 2026);
    }

    // Keep an untouched copy for comparison/reset. No erosion is precomputed:
    // every terrain change from this point on is caused by a visible live droplet.
    initialTerrain = terrain;
    initialTerrainMass = terrain.totalHeight();

    std::vector<Vertex> vertices(static_cast<size_t>(terrain.width()) * terrain.height());
    terrainvis::buildTerrainVertices(terrain, visualSettings, vertices);
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(terrain.width() - 1) * (terrain.height() - 1) * 6);
    for (uint32_t y = 0; y + 1 < terrain.height(); ++y) for (uint32_t x = 0; x + 1 < terrain.width(); ++x) {
        const uint32_t a = y * terrain.width() + x;
        const uint32_t b = a + 1;
        const uint32_t c = a + terrain.width();
        const uint32_t d = c + 1;
        indices.insert(indices.end(), { a, c, b, b, c, d });
    }
    terrainIndexCount = static_cast<uint32_t>(indices.size());
    terrainMesh = uploadMesh(indices, vertices);
    // Terrain vertices live in persistently mapped memory so erosion can deform
    // the rendered surface without recreating buffers every frame.
    destroyBuffer(terrainMesh.vertexBuffer);
    terrainMesh.vertexBuffer = createBuffer(vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);
    terrainVertexData = terrainMesh.vertexBuffer.info.pMappedData;
    memcpy(terrainVertexData, vertices.data(), vertices.size() * sizeof(Vertex));
    VkBufferDeviceAddressInfo terrainAddress{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = terrainMesh.vertexBuffer.buffer};
    terrainMesh.vertexBufferAddress = vkGetBufferDeviceAddress(device, &terrainAddress);

    // Six vertices and eight faces form a small 3D octahedron for every drop.
    std::vector<Vertex> particleVertices(static_cast<size_t>(maxWaterParticles) * 6);
    std::vector<uint32_t> particleIndices;
    particleIndices.reserve(static_cast<size_t>(maxWaterParticles) * 24);
    for (uint32_t i = 0; i < maxWaterParticles; ++i) {
        const uint32_t b = i * 6;
        particleIndices.insert(particleIndices.end(), {
            b, b + 2, b + 4,  b, b + 4, b + 3,
            b, b + 3, b + 5,  b, b + 5, b + 2,
            b + 1, b + 4, b + 2,  b + 1, b + 3, b + 4,
            b + 1, b + 5, b + 3,  b + 1, b + 2, b + 5});
    }
    waterParticleMesh = uploadMesh(particleIndices, particleVertices);
    destroyBuffer(waterParticleMesh.vertexBuffer);
    waterParticleMesh.vertexBuffer = createBuffer(particleVertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);
    waterParticleVertexData = waterParticleMesh.vertexBuffer.info.pMappedData;
    VkBufferDeviceAddressInfo waterAddress{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = waterParticleMesh.vertexBuffer.buffer};
    waterParticleMesh.vertexBufferAddress = vkGetBufferDeviceAddress(device, &waterAddress);

    realtimeErosion = std::make_unique<RealtimeErosionSimulator>(erosionParameters, maxWaterParticles, waterSpawnRate, 7391);
    realtimeErosion->setMaxDroplets(static_cast<uint32_t>(waterParticleLimit));
    fmt::println("Realtime terrain ready: no precomputed erosion; droplets will spawn at {} per second", waterSpawnRate);
}

void VulkanEngine::updateRealtimeErosion(float deltaSeconds)
{
    if (!realtimeErosion) return;
	const auto simulationStart = std::chrono::steady_clock::now();
    if (!simulationPaused) {
        float remainingSimulationTime = std::clamp(deltaSeconds, 0.0f, 0.1f)
            * std::clamp(simulationTimeScale, 0.25f, 8.0f);
        while (remainingSimulationTime > 1e-6f) {
            const float step = std::min(remainingSimulationTime, 0.1f);
            realtimeErosion->update(terrain, step);
            remainingSimulationTime -= step;
        }
    }
	const auto simulationEnd = std::chrono::steady_clock::now();
    updateTerrainVertices();
    updateWaterParticleVertices();
	const auto meshEnd = std::chrono::steady_clock::now();
	stats.simulationTimeMs = std::chrono::duration<float, std::milli>(
		simulationEnd - simulationStart).count();
	stats.meshUpdateTimeMs = std::chrono::duration<float, std::milli>(
		meshEnd - simulationEnd).count();
}

void VulkanEngine::updateTerrainVertices()
{
    if (!terrainVertexData) return;
    std::span<Vertex> vertices(static_cast<Vertex*>(terrainVertexData), terrain.values().size());
    terrainvis::buildTerrainVertices(terrain, visualSettings, vertices);
    vmaFlushAllocation(allocator, terrainMesh.vertexBuffer.allocation, 0, VK_WHOLE_SIZE);
}

void VulkanEngine::updateWaterParticleVertices()
{
    if (!waterParticleVertexData || !realtimeErosion) return;
    const auto& drops = realtimeErosion->droplets();
    std::span<Vertex> vertices(static_cast<Vertex*>(waterParticleVertexData), static_cast<size_t>(maxWaterParticles) * 6);
    const uint32_t count = terrainvis::buildDropletVertices(terrain, drops, visualSettings, vertices);
    waterParticleIndexCount = count * 24;
    vmaFlushAllocation(allocator, waterParticleMesh.vertexBuffer.allocation, 0, VK_WHOLE_SIZE);
}

void VulkanEngine::initImGui()
{
    // 1: create descriptor pool for IMGUI
    //  the size of the pool is very oversize, but it's copied from imgui demo
    //  itself.
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool));

    // 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	// this initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(window);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = instance;
	init_info.PhysicalDevice = physicalDevice;
	init_info.Device = device;
	init_info.Queue = graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainImageFormat;


	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();

	// add the destroy the imgui created structures
	mainDeletionQueue.push([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(device, imguiPool, nullptr);
		});
}

void VulkanEngine::initPipelines()
{
    initBackgroundPipeline();
    simulationPipeline.build(*this);
    mainDeletionQueue.push([&]() { simulationPipeline.destroy(device); });
}

void VulkanEngine::initDescriptors()
{
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
    };

    globalDescriptors.init_pool(device, 10, sizes);
    mainDeletionQueue.push(
        [&]() { vkDestroyDescriptorPool(device, globalDescriptors.pool, nullptr); });

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        drawImageDescriptorLayout = builder.build(device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        sceneDescriptorLayout = builder.build(device, VK_SHADER_STAGE_VERTEX_BIT);
    }

    mainDeletionQueue.push([&]() {
        vkDestroyDescriptorSetLayout(device, drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, sceneDescriptorLayout, nullptr);
    });

    drawImageDescriptor = globalDescriptors.allocate(device, drawImageDescriptorLayout);
    {
        DescriptorWriter writer;	
		writer.write_image(0, drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.update_set(device, drawImageDescriptor);
    }
	for (int i = 0; i < FrameOverlap; i++) {
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
		};

		frames[i].descriptors = DescriptorAllocatorGrowable{};
		frames[i].descriptors.init(device, 1000, frame_sizes);
		mainDeletionQueue.push([&, i]() {
			frames[i].descriptors.destroy_pools(device);
		});
	}
}

void SimulationPipeline::build(VulkanEngine& engine)
{
	VkShaderModule fragmentShader;
	if (!vkutil::load_shader_module("../../shaders/terrain.frag.spv", engine.device, &fragmentShader))
		throw std::runtime_error("Failed to load terrain.frag.spv");
	VkShaderModule vertexShader;
	if (!vkutil::load_shader_module("../../shaders/terrain.vert.spv", engine.device, &vertexShader))
		throw std::runtime_error("Failed to load terrain.vert.spv");

	VkPushConstantRange matrixRange{};
	matrixRange.offset = 0;
	matrixRange.size = sizeof(GPUDrawPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
	mesh_layout_info.setLayoutCount = 1;
	mesh_layout_info.pSetLayouts = &engine.sceneDescriptorLayout;
	mesh_layout_info.pPushConstantRanges = &matrixRange;
	mesh_layout_info.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(engine.device, &mesh_layout_info, nullptr, &layout));

	// build the stage-create-info for both vertex and fragment stages. This lets
	// the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	pipelineBuilder.set_shaders(vertexShader, fragmentShader);

	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);

	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

	pipelineBuilder.set_multisampling_none();

	pipelineBuilder.disable_blending();

	pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	//render format
	pipelineBuilder.set_color_attachment_format(engine.drawImage.imageFormat);
	pipelineBuilder.set_depth_format(engine.depthImage.imageFormat);

	// use the triangle layout we created
	pipelineBuilder._pipelineLayout = layout;

	// finally build the pipeline
	pipeline = pipelineBuilder.build_pipeline(engine.device);
	vkDestroyShaderModule(engine.device, fragmentShader, nullptr);
	vkDestroyShaderModule(engine.device, vertexShader, nullptr);
}

void SimulationPipeline::destroy(VkDevice device)
{
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineLayout(device, layout, nullptr);
}
