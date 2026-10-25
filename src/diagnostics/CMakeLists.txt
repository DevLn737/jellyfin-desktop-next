# Leaf module: frame and process metrics. Plain C++20, no Qt (ADR 0002).
add_library(jfd_diagnostics STATIC
  src/FrameStats.cpp
  src/ProcessStats.cpp
  src/Redact.cpp
  src/Report.cpp
  src/Zip.cpp)
add_library(jfd::diagnostics ALIAS jfd_diagnostics)
target_include_directories(jfd_diagnostics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(jfd_diagnostics PUBLIC cxx_std_20)
if(WIN32)
  target_link_libraries(jfd_diagnostics PRIVATE psapi)
endif()
