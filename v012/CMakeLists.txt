cmake_minimum_required(VERSION 3.20)
project(nopen_vram_012 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion -Wno-sign-conversion -fno-omit-frame-pointer)
option(NOPEN_OPENCL "Enable GPU OpenCL backend, strict no-fallback" ON)
add_library(nopen_vram_store STATIC src/nopen_store.c)
target_include_directories(nopen_vram_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(NOPEN_OPENCL)
    find_package(OpenCL REQUIRED)
    target_link_libraries(nopen_vram_store PUBLIC OpenCL::OpenCL)
    target_compile_definitions(nopen_vram_store PUBLIC NOPEN_HAS_OPENCL=1)
endif()
add_executable(nopen-vram-lab src/nopen_lab.c)
target_link_libraries(nopen-vram-lab PRIVATE nopen_vram_store pthread)
add_executable(nopen-vram-test-store tests/test_store.c)
target_link_libraries(nopen-vram-test-store PRIVATE nopen_vram_store)
enable_testing()
add_test(NAME mock_store_integrity_bounds_injection COMMAND nopen-vram-test-store)
add_test(NAME forbidden_nbd_options COMMAND nopen-vram-lab --self-audit)

add_test(NAME isolated_gpu_pagein_failure_terminates_only_lab
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fault_isolation.sh $<TARGET_FILE:nopen-vram-lab>)
set_tests_properties(isolated_gpu_pagein_failure_terminates_only_lab PROPERTIES TIMEOUT 15)

# v0.12.0-alpha.2 cooperative, bounded-residency page manager (NO system swap)
add_library(nopen_vram_memory STATIC src/nopen_memory.c)
target_include_directories(nopen_vram_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nopen_vram_memory PUBLIC nopen_vram_store)
add_executable(nopen-vram-managed src/nopen_managed.c)
target_link_libraries(nopen-vram-managed PRIVATE nopen_vram_memory)
add_executable(nopen-vram-test-memory tests/test_memory.c)
target_link_libraries(nopen-vram-test-memory PRIVATE nopen_vram_memory)
add_test(NAME managed_lru_pageout_pagein_checksum_atomicity COMMAND nopen-vram-test-memory)
add_test(NAME managed_cooperative_mock_cli COMMAND nopen-vram-managed --backend mock --pool-mib 4 --logical-pages 128 --ram-pages 8 --rounds 2)

# alpha.3: bounded process-only UFFD pager using actual GPU store.
add_executable(nopen-vram-demand src/nopen_fault_managed.c)
target_link_libraries(nopen-vram-demand PRIVATE nopen_vram_store pthread)
add_test(NAME demand_uffd_mock_and_fault_isolation
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_demand.sh $<TARGET_FILE:nopen-vram-demand>)
set_tests_properties(demand_uffd_mock_and_fault_isolation PROPERTIES TIMEOUT 40)

# alpha.4: deterministic random 4KiB long-run stress + fault latency sampling.
# Every test invokes real userfaultfd only inside its own process.
add_test(NAME demand_random_stress_uffd_mock
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_stress.sh $<TARGET_FILE:nopen-vram-demand>)
set_tests_properties(demand_random_stress_uffd_mock PROPERTIES TIMEOUT 90)