add_library(jfd_core INTERFACE)
add_library(jfd::core ALIAS jfd_core)
target_include_directories(jfd_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(jfd_core INTERFACE cxx_std_20)
