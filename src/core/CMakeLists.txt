# Pure C++20 simulation core: no SDL, no I/O, deterministic. Unit tests link only this.
add_library(pixeltd_core STATIC
  camera.cpp
  fog.cpp
  fixed_step.cpp
  flow.cpp
  los.cpp
  gen_params.cpp
  layout.cpp
  mapgen.cpp
  overlay.cpp
  preset.cpp
  sim.cpp
  sim_params.cpp
  sim_tuning.cpp
  simplex.cpp
  terrain.cpp)
target_include_directories(pixeltd_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(pixeltd_core PUBLIC cxx_std_20)
# Route builds run on a worker thread (#11 phase 2).
find_package(Threads REQUIRED)
target_link_libraries(pixeltd_core PUBLIC Threads::Threads PRIVATE pixeltd_warnings)
