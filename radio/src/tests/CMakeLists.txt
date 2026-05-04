
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
if(EDGE16_SANITIZERS)
  string(REPLACE "," ";" EDGE16_SANITIZER_LIST "${EDGE16_SANITIZERS}")
  foreach(SANITIZER ${EDGE16_SANITIZER_LIST})
    string(STRIP "${SANITIZER}" SANITIZER)
    if(SANITIZER STREQUAL "address")
      list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS -fsanitize=address)
    elseif(SANITIZER STREQUAL "undefined")
      list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS -fsanitize=undefined)
    elseif(SANITIZER STREQUAL "thread")
      list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS -fsanitize=thread)
    else()
      message(FATAL_ERROR "Unsupported EDGE16_SANITIZERS entry: ${SANITIZER}")
    endif()
  endforeach()
else()
  list(APPEND EDGE16_NATIVE_SANITIZER_FLAGS -fsanitize=address)
endif()

string(REPLACE ";" " " EDGE16_NATIVE_SANITIZER_FLAGS "${EDGE16_NATIVE_SANITIZER_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_DEBUG} -O0 ${EDGE16_NATIVE_SANITIZER_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_DEBUG} -O0 ${WARNING_FLAGS} ${EDGE16_NATIVE_SANITIZER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EDGE16_NATIVE_SANITIZER_FLAGS}")

file(GLOB TEST_SRC_FILES ${RADIO_SRC_DIR}/tests/*.cpp
  CONFIGURE_DEPENDS "${RADIO_SRC_DIR}/tests/*.cpp")

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
