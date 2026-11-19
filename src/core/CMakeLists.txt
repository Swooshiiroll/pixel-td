# Pure C++20 simulation core: no SDL, no I/O, deterministic. Unit tests link only this.
add_library(pixeltd_core STATIC
  camera.cpp
  fixed_step.cpp
  mapgen.cpp
  terrain.cpp)
target_include_directories(pixeltd_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(pixeltd_core PUBLIC cxx_std_20)
target_link_libraries(pixeltd_core PRIVATE pixeltd_warnings)
