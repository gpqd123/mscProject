
add_executable (hydraulic_erosion
  main.cpp
  vk_engine.cpp
  vk_engine.h

  terrain_erosion.h
  terrain_erosion.cpp
  terrain_visualization.h
  terrain_visualization.cpp
)


set_property(TARGET hydraulic_erosion PROPERTY CXX_STANDARD 20)
set_property(TARGET hydraulic_erosion PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:hydraulic_erosion>)

target_include_directories(hydraulic_erosion PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(hydraulic_erosion erosion_render_support vkbootstrap imgui)

add_executable(terrain_erosion_tests
  terrain_erosion_tests.cpp
  terrain_erosion.h
  terrain_erosion.cpp
)
set_property(TARGET terrain_erosion_tests PROPERTY CXX_STANDARD 20)
enable_testing()
add_test(NAME terrain_erosion_tests COMMAND terrain_erosion_tests)

add_executable(terrain_erosion_benchmark
  terrain_erosion_benchmark.cpp
  terrain_erosion.h
  terrain_erosion.cpp
)
set_property(TARGET terrain_erosion_benchmark PROPERTY CXX_STANDARD 20)

add_custom_command(TARGET hydraulic_erosion POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:hydraulic_erosion> $<TARGET_FILE_DIR:hydraulic_erosion>
  COMMAND_EXPAND_LISTS
  )
