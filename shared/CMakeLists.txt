
add_library (erosion_render_support STATIC
  vk_types.h
  vk_initializers.cpp
  vk_initializers.h
  vk_images.h
  vk_images.cpp 
  vk_descriptors.h
  vk_descriptors.cpp
  vk_pipelines.h
  vk_pipelines.cpp
  camera.cpp
  camera.h
)

set_property(TARGET erosion_render_support PROPERTY CXX_STANDARD 20)
target_compile_definitions(erosion_render_support PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_include_directories(erosion_render_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(erosion_render_support PUBLIC vma glm Vulkan::Vulkan fmt::fmt SDL2::SDL2)

target_precompile_headers(erosion_render_support PUBLIC <optional> <vector> <memory> <string> <unordered_map> <glm/mat4x4.hpp> <glm/vec4.hpp> <vulkan/vulkan.h>)
