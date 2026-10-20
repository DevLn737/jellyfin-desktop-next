# doctest 2.4.11 single header, vendored in third_party/doctest (MIT).
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/third_party/doctest)

add_library(jfd_test_support INTERFACE)
target_include_directories(jfd_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(jfd_test_support INTERFACE jfd::core)

add_executable(jfd_core_tests
  core/main.cpp
  core/TimeTest.cpp
  core/EpochTest.cpp
  core/InterfacesTest.cpp
  core/TimelineTest.cpp
  core/ReadinessTest.cpp
  core/ReconcilerTest.cpp
  core/PlaybackSessionTest.cpp)
target_link_libraries(jfd_core_tests PRIVATE jfd::core jfd_test_support doctest)
add_test(NAME jfd_core_tests COMMAND jfd_core_tests)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # GCC reports false positives for std::variant copies of the recording fakes at -O2+.
  target_compile_options(jfd_core_tests PRIVATE -Wno-maybe-uninitialized)
endif()

# Deterministic simulator (see tests/sim/Simulation.h).
add_executable(jfd_simulator sim/main.cpp sim/Simulation.cpp)
target_link_libraries(jfd_simulator PRIVATE jfd::core)
add_test(NAME simulator_chaos COMMAND jfd_simulator --mode chaos --scenarios ${JFD_SIM_SCENARIOS} --seed 1)
add_test(NAME simulator_drift_1x COMMAND jfd_simulator --mode drift --scenarios 3 --duration 1830 --rate 1)
add_test(NAME simulator_drift_1_25x COMMAND jfd_simulator --mode drift --scenarios 3 --duration 1830 --rate 1.25)
add_test(NAME simulator_drift_1_5x COMMAND jfd_simulator --mode drift --scenarios 3 --duration 1830 --rate 1.5)
add_test(NAME simulator_stock_server COMMAND jfd_simulator --mode chaos --scenarios 200 --seed 500000 --stock-server)

# Mutation check: each mutant is core with one injected bug. The simulator
# must report violations, so these tests pass only when it does.
foreach(mutant IN ITEMS "stale_epoch;1" "stale_ready;2" "late_timer;3")
  list(GET mutant 0 name)
  list(GET mutant 1 number)
  jfd_core_mutant(${name} ${number})
  add_executable(jfd_simulator_mutant_${name} sim/main.cpp sim/Simulation.cpp)
  target_link_libraries(jfd_simulator_mutant_${name} PRIVATE jfd_core_mutant_${name})
  add_test(NAME mutation_${name} COMMAND jfd_simulator_mutant_${name} --mode chaos --scenarios 300 --seed 1)
  set_tests_properties(mutation_${name} PROPERTIES PASS_REGULAR_EXPRESSION "violations=[1-9]")
endforeach()
