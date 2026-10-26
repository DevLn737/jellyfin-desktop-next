# Jfd.Ui.Requests: Seerr in the app. Browse what is trending and popular,
# search, a title's page with the Request button and its status, the user's
# own requests, and the sign-in as the Jellyfin account. The Requests
# singleton (RequestsUi) is the screens' way into jfd::seerr; the
# application creates the service and sets it before the window loads.
qt_add_library(jfd_ui_requests STATIC
  src/RequestsUi.cpp
  include/jfd/ui/RequestsUi.h)
target_include_directories(jfd_ui_requests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The generated QML type registration includes the header by file name.
target_include_directories(jfd_ui_requests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/jfd/ui)
qt_add_qml_module(jfd_ui_requests
  URI Jfd.Ui.Requests
  VERSION 1.0
  QML_FILES
    BrowsePage.qml
    MyRequestsPage.qml
    SignInPage.qml
    StatusBadge.qml
    TitleCard.qml
    TitlePage.qml
  RESOURCES
    icons/partial.svg
    icons/pending.svg
    icons/processing.svg
    icons/requests.svg
    icons-dark/partial.svg
    icons-dark/pending.svg
    icons-dark/processing.svg
    icons-dark/requests.svg
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Controls Jfd.Ui.Shell Jfd.Ui.Library)
target_link_libraries(jfd_ui_requests
  PUBLIC Qt6::Core Qt6::Quick Qt6::Qml jfd::seerr
  PRIVATE jfd_ui_theme jfd_ui_controls jfd_ui_shell jfd_ui_library)
target_compile_definitions(jfd_ui_requests PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_compile_options(jfd_ui_requests PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
jfd_add_translations(jfd_ui_requests)
