set(JFD_CORE_SOURCES
  src/PlaybackSession.cpp
  src/Readiness.cpp
  src/Reconciler.cpp
  src/Timeline.cpp)

add_library(jfd_core STATIC ${JFD_CORE_SOURCES})
add_library(jfd::core ALIAS jfd_core)
target_include_directories(jfd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(jfd_core PUBLIC cxx_std_20)

# Builds of core with one deliberately injected bug each, for the simulator's
# mutation check. Not part of the client.
function(jfd_core_mutant name number)
  set(dir ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
  add_library(jfd_core_mutant_${name} STATIC
    ${dir}/src/PlaybackSession.cpp
    ${dir}/src/Readiness.cpp
    ${dir}/src/Reconciler.cpp
    ${dir}/src/Timeline.cpp)
  target_include_directories(jfd_core_mutant_${name} PUBLIC ${dir}/include)
  target_compile_features(jfd_core_mutant_${name} PUBLIC cxx_std_20)
  target_compile_definitions(jfd_core_mutant_${name} PRIVATE JFD_CORE_MUTATION=${number})
  # Injected bugs leave unreachable code behind; that must not fail the build.
  target_compile_options(jfd_core_mutant_${name} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/WX-,-Wno-error>)
endfunction()
