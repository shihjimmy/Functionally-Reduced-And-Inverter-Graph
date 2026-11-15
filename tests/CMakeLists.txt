# One CTest test per line of cases.txt.  Run them with
#     ctest --test-dir build --output-on-failure
# The test names are "golden.<case-name>".

set(CASES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cases.txt)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CASES_FILE})
file(STRINGS ${CASES_FILE} CASE_LINES)

foreach(line IN LISTS CASE_LINES)
  if(line MATCHES "^[ \t]*(#|$)")
    continue()
  endif()
  string(REGEX MATCH "^[ \t]*([^ \t|]+)" _ "${line}")
  set(case_name ${CMAKE_MATCH_1})
  add_test(NAME golden.${case_name}
           COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/golden.sh run $<TARGET_FILE:fraig> ${case_name}
           WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(golden.${case_name} PROPERTIES TIMEOUT 300)
endforeach()

# Random circuits (tests/gen_aig.py): fraig flows must stay equivalent
# according to CIRVerify.  No reference binary needed.
add_test(NAME fuzz.equiv
         COMMAND env NOREF=1 MINE=$<TARGET_FILE:fraig> bash ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_diff.sh 200
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(fuzz.equiv PROPERTIES TIMEOUT 600)

# Unit tests (GoogleTest).  Downloaded at configure time; turn off with
# -DFRAIG_UNIT_TESTS=OFF when there is no network.  Pinned to the commit of
# a release tag, so every build uses exactly the same GoogleTest.
option(FRAIG_UNIT_TESTS "Build the GoogleTest unit tests" ON)
if(FRAIG_UNIT_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        b514bdc898e2951020cbdca1304b75f5950d1f59   # v1.15.2 (needs C++14)
    GIT_SHALLOW    FALSE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(unit_tests
    unit/test_gate.cpp
    unit/test_cirmgr.cpp)
  target_link_libraries(unit_tests PRIVATE cir cmd sat util GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(unit_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                       PROPERTIES TIMEOUT 300)
endif()
