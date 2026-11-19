add_executable(pixeltd main.cpp map_view.cpp)
set_target_properties(pixeltd PROPERTIES OUTPUT_NAME "PixelTD")
target_compile_definitions(pixeltd PRIVATE PIXELTD_VERSION="${PROJECT_VERSION}")
target_link_libraries(pixeltd PRIVATE pixeltd_core SDL3::SDL3-static pixeltd_warnings)

# tools/dev.sh build stages the game with: cmake --install <dir> --component game
install(TARGETS pixeltd RUNTIME DESTINATION . COMPONENT game)
if(EXISTS ${PROJECT_SOURCE_DIR}/assets)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/assets DESTINATION . COMPONENT game)
endif()
