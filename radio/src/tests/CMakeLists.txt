
add_library(gtests-radio-lib STATIC EXCLUDE_FROM_ALL
  ${googletest_SOURCE_DIR}/googletest/src/gtest-all.cc
)
target_include_directories(gtests-radio-lib PRIVATE
  ${googletest_SOURCE_DIR}/googletest
)

set(TESTS_PATH ${RADIO_SRC_DIR}/tests)
set(TESTS_BUILD_PATH ${CMAKE_CURRENT_BINARY_DIR})
configure_file(${RADIO_SRC_DIR}/tests/location.h.in ${CMAKE_CURRENT_BINARY_DIR}/location.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(EDGE16_NATIVE_SANITIZER_FLAGS "")
set(EDGE16_NATIVE_SANITIZER_LINK_FLAGS "")
if(EDGE16_SANITIZERS)
  string(REPLACE "," ";" EDGE16_SANITIZER_LIST "${EDGE16_SANITIZERS}")
  foreach(SANITIZER ${EDGE16_SANITIZER_LIST})
    string(STRIP "${SANITIZER}" SANITIZER)
    if(SANITIZER STREQUAL "address")
      list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS -fsanitize=address)
      list(APPEND EDGE16_NATIVE_SANITIZER_LINK_FLAGS -fsanitize=address)
    elseif(SANITIZER STREQUAL "undefined")
      list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS -fsanitize=undefined)
      list(APPEND EDGE16_NATIVE_SANITIZER_LINK_FLAGS -fsanitize=undefined)
    elseif(SANITIZER STREQUAL "thread")
      list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS -fsanitize=thread)
      list(APPEND EDGE16_NATIVE_SANITIZER_LINK_FLAGS -fsanitize=thread)
    elseif(SANITIZER STREQUAL "memory")
      if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MemorySanitizer requires Clang.")
      endif()
      list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS
        -fsanitize=memory
        -fsanitize-memory-track-origins=2
        -fno-omit-frame-pointer
        -fPIE
      )
      list(APPEND EDGE16_NATIVE_SANITIZER_LINK_FLAGS
        -fsanitize=memory
        -fsanitize-memory-track-origins=2
        -fPIE
        -pie
      )
    else()
      message(FATAL_ERROR "Unsupported EDGE16_SANITIZERS entry: ${SANITIZER}")
    endif()
  endforeach()
else()
  list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS -fsanitize=address)
  list(APPEND EDGE16_NATIVE_SANITIZER_LINK_FLAGS -fsanitize=address)
endif()

string(REPLACE ";" " " EDGE16_NATIVE_SANITIZER_FLAGS "${EDGE16_NATIVE_SANITIZER_FLAGS}")
string(REPLACE ";" " " EDGE16_NATIVE_SANITIZER_LINK_FLAGS "${EDGE16_NATIVE_SANITIZER_LINK_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_DEBUG} -O0 ${EDGE16_NATIVE_SANITIZER_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_DEBUG} -O0 ${WARNING_FLAGS} ${EDGE16_NATIVE_SANITIZER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EDGE16_NATIVE_SANITIZER_LINK_FLAGS}")

file(GLOB TEST_SRC_FILES CONFIGURE_DEPENDS
  ${RADIO_SRC_DIR}/tests/*.cpp)

if(EDGE16_SAFETY_CHECKS)
  set_source_files_properties(${RADIO_SRC_DIR}/tests/lcd_480x272.cpp
    PROPERTIES COMPILE_OPTIONS -Wno-implicit-fallthrough)
endif()

set(TEST_SRC_FILES ${TEST_SRC_FILES}
  ${SIMU_SRC}
)

add_executable(gtests-radio EXCLUDE_FROM_ALL
  ${TEST_SRC_FILES}
)
target_compile_options(gtests-radio PRIVATE ${SIMU_SRC_OPTIONS})

target_link_libraries(gtests-radio gtests-radio-lib)
message(STATUS "Added optional gtests target")

if(EDGE16_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "EDGE16_BUILD_FUZZERS requires Clang for -fsanitize=fuzzer.")
  endif()
  if(EDGE16_SANITIZERS MATCHES "(^|,)thread(,|$)")
    message(FATAL_ERROR "EDGE16_BUILD_FUZZERS cannot be combined with ThreadSanitizer.")
  endif()

  set(EDGE16_FUZZ_SANITIZER_FLAGS -fsanitize=fuzzer,address,undefined)
  set(EDGE16_FUZZ_COVERAGE_FLAGS -fsanitize=fuzzer-no-link)

  target_compile_options(radiolib_native PUBLIC ${EDGE16_FUZZ_COVERAGE_FLAGS})
  target_compile_options(simu_drivers PUBLIC ${EDGE16_FUZZ_COVERAGE_FLAGS})

  add_executable(edge16-fuzz-yaml EXCLUDE_FROM_ALL
    fuzz/yaml_parser_fuzzer.cpp
    ${RADIO_SRC_DIR}/storage/yaml/yaml_bits.cpp
    ${RADIO_SRC_DIR}/storage/yaml/yaml_parser.cpp
    ${RADIO_SRC_DIR}/storage/yaml/yaml_tree_walker.cpp
  )
  target_compile_options(edge16-fuzz-yaml PRIVATE
    ${EDGE16_FUZZ_SANITIZER_FLAGS}
    -O1
    -g
  )
  target_link_options(edge16-fuzz-yaml PRIVATE
    ${EDGE16_FUZZ_SANITIZER_FLAGS}
  )

  add_executable(edge16-fuzz-mixer EXCLUDE_FROM_ALL
    fuzz/mixer_fuzzer.cpp
    ${SIMU_SRC}
  )
  target_compile_options(edge16-fuzz-mixer PRIVATE
    ${SIMU_SRC_OPTIONS}
    ${EDGE16_FUZZ_SANITIZER_FLAGS}
    -O1
    -g
  )
  target_link_options(edge16-fuzz-mixer PRIVATE
    ${EDGE16_FUZZ_SANITIZER_FLAGS}
  )

  add_executable(edge16-fuzz-protocols EXCLUDE_FROM_ALL
    fuzz/protocols_fuzzer.cpp
    ${SIMU_SRC}
  )
  target_compile_options(edge16-fuzz-protocols PRIVATE
    ${SIMU_SRC_OPTIONS}
    ${EDGE16_FUZZ_SANITIZER_FLAGS}
    -O1
    -g
  )
  target_link_options(edge16-fuzz-protocols PRIVATE
    ${EDGE16_FUZZ_SANITIZER_FLAGS}
  )

  add_custom_target(fuzz-yaml-smoke
    COMMAND ${CMAKE_COMMAND} -E env
      ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
      UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
      $<TARGET_FILE:edge16-fuzz-yaml> -runs=1000 -max_len=4096
    DEPENDS edge16-fuzz-yaml
  )

  add_custom_target(fuzz-mixer-smoke
    COMMAND ${CMAKE_COMMAND} -E env
      ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
      UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
      $<TARGET_FILE:edge16-fuzz-mixer> -runs=1000 -max_len=8192
    DEPENDS edge16-fuzz-mixer
  )

  add_custom_target(fuzz-protocols-smoke
    COMMAND ${CMAKE_COMMAND} -E env
      ASAN_OPTIONS=abort_on_error=1:detect_leaks=0
      UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
      $<TARGET_FILE:edge16-fuzz-protocols>
        -runs=10000
        -max_len=768
        -timeout=5
        -dict=${CMAKE_CURRENT_SOURCE_DIR}/fuzz/protocols.dict
    DEPENDS edge16-fuzz-protocols ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/protocols.dict
  )

  add_custom_target(fuzz-smoke DEPENDS fuzz-yaml-smoke fuzz-mixer-smoke fuzz-protocols-smoke)
  add_custom_target(fuzzers DEPENDS edge16-fuzz-yaml edge16-fuzz-mixer edge16-fuzz-protocols)
endif()
