add_executable(history_benchmark history_benchmark.cpp ../sources.qrc)
target_link_libraries(history_benchmark PRIVATE pastes_storage pastes_ui Qt6::Sql)
target_include_directories(history_benchmark PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(history_benchmark PRIVATE PASTES_BENCHMARK_BUILD_TYPE="$<CONFIG>")
add_dependencies(history_benchmark pastes_build_version)

# Benchmarks are opt-in developer tools, not timing-sensitive CTest assertions.
