# Unit tests: core library only, so they run headless (no window, no GPU) locally and on CI.
add_executable(pixeltd_tests
  test_main.cpp
  test_camera.cpp
  test_fixed_step.cpp
  test_fog.cpp
  test_los.cpp
  test_mapgen.cpp
  test_overlay.cpp
  test_rng.cpp)
target_link_libraries(pixeltd_tests PRIVATE pixeltd_core doctest::doctest pixeltd_warnings)
add_test(NAME core COMMAND pixeltd_tests)
