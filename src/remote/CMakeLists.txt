# Remote control: the server's Play, Playstate and GeneralCommand session
# messages ("Play on" in Jellyfin Web and the phone apps) and the
# second-start --command commands, carried out through actions the
# application connects. Qt Core only; no screens.
add_library(jfd_remote STATIC
  src/RemoteCommands.cpp
  src/RemoteControl.cpp
  src/RemoteSettings.cpp
  include/jfd/remote/RemoteCommands.h
  include/jfd/remote/RemoteControl.h
  include/jfd/remote/RemoteSettings.h)
add_library(jfd::remote ALIAS jfd_remote)
target_include_directories(jfd_remote PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_remote PUBLIC Qt6::Core jfd::core jfd::settings jfd::jellyfin_api jfd::integrations)
target_compile_definitions(jfd_remote PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
# The settings section's strings, in i18n/remote_ru.ts.
jfd_add_translations(jfd_remote)
