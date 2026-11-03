# Script mode (cmake -P): writes a header with the text of a file as a raw
# string literal, so the client runs exactly the script CI tests.
#   -DINPUT=<file> -DOUTPUT=<header> -DNAME=<constant name>
file(READ "${INPUT}" text)
cmake_path(GET INPUT FILENAME source)
if(text MATCHES "\\)JFDSCRIPT\"")
  message(FATAL_ERROR "${INPUT} contains the raw string delimiter")
endif()
file(WRITE "${OUTPUT}.tmp"
  "#pragma once\n// Generated from ${source} by EmbedScript.cmake.\n"
  "inline constexpr char ${NAME}[] = R\"JFDSCRIPT(${text})JFDSCRIPT\";\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
