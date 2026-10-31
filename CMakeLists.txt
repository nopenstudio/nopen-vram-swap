cmake_minimum_required(VERSION 3.20)
project(nopen_vram_swap VERSION 0.8.5 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Safe default: building the repository never launches a GPU runtime,
# creates a block device, changes system swap or modifies boot settings.
option(NOPEN_OPENCL "Compile OpenCL backend (never run it from CI)" OFF)
option(NOPEN_BUILD_LEGACY_VULKAN "Build the archived Vulkan 0.10 memory probe" OFF)

enable_testing()
add_subdirectory(v085)

if(NOPEN_BUILD_LEGACY_VULKAN)
    find_package(Vulkan REQUIRED)
    add_executable(nopen-vulkan-probe src/nopen_vulkan_probe.c)
    target_link_libraries(nopen-vulkan-probe PRIVATE Vulkan::Vulkan)
    target_compile_options(nopen-vulkan-probe PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME legacy_vulkan_probe_help COMMAND nopen-vulkan-probe --help)
endif()

# Repository organization and safety are static; no host mutation.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME repository_structure_and_safety
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/repo_contract.py")
set_tests_properties(repository_structure_and_safety PROPERTIES
    PASS_REGULAR_EXPRESSION "REPOSITORY_CONTRACT_PASS" TIMEOUT 10)
