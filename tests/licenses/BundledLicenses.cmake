# Part of the licenses_check test (included by IconLicenses.cmake).
#   -DSOURCE_DIR=<repository root>
# The packages carry the licence of every bundled third-party component:
# - packaging/licenses has THIRD-PARTY-NOTICES.txt and each licence text it
#   names, none empty, and no file there is left out of the notices;
# - the notices name the versions pinned in deps/versions.json (a version
#   bump has to update them);
# - both package builds copy the folder in, and check what they bundle
#   against it (package.yml for the MSI's DLLs, build-appimage.sh for the
#   AppImage's libraries).
cmake_policy(VERSION 3.24)

set(problems "")
set(dir "${SOURCE_DIR}/packaging/licenses")
set(notices_file "${dir}/THIRD-PARTY-NOTICES.txt")
if(NOT EXISTS "${notices_file}")
  message(FATAL_ERROR "Bundled licences: packaging/licenses/THIRD-PARTY-NOTICES.txt is missing")
endif()
file(READ "${notices_file}" notices)

# Each component's licence text.
set(required
  GPL-2.0.txt GPL-3.0.txt LGPL-2.1.txt LGPL-3.0.txt
  Qt-third-party.txt SDL3.txt HIDAPI.txt mpv.txt FFmpeg.txt libplacebo.txt libass.txt
  ICU.txt Vulkan-Loader.txt Mesa.txt LLVM.txt libmpv-windows.txt SOURCES.txt)
foreach(name IN LISTS required)
  if(NOT EXISTS "${dir}/${name}")
    string(APPEND problems "  packaging/licenses/${name} is missing\n")
    continue()
  endif()
  file(SIZE "${dir}/${name}" size)
  if(size LESS 200)
    string(APPEND problems "  packaging/licenses/${name} is (nearly) empty\n")
  endif()
endforeach()

# Every file in the folder is listed in the notices' file list.
file(GLOB present RELATIVE "${dir}" "${dir}/*")
foreach(name IN LISTS present)
  string(FIND "${notices}" "\n${name} " at)
  if(at EQUAL -1)
    string(APPEND problems "  packaging/licenses/${name} is not in the file list of THIRD-PARTY-NOTICES.txt\n")
  endif()
endforeach()

# The components, by name, and the pinned versions.
foreach(component IN ITEMS "Qt " "SDL3" "HIDAPI" "mpv" "FFmpeg" "libplacebo" "libass" "ICU" "Vulkan loader"
                         "Mesa" "LLVM" "Visual C++" "d3dcompiler_47.dll" "dxcompiler.dll" "libmpv-2.dll" "vulkan-1.dll"
                         "opengl32sw.dll")
  string(FIND "${notices}" "${component}" at)
  if(at EQUAL -1)
    string(APPEND problems "  THIRD-PARTY-NOTICES.txt does not mention ${component}\n")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/deps/versions.json" pins)
foreach(path IN ITEMS "qt;version" "sdl3;version" "windows;mpv;version" "windows;mpv;commit"
                      "windows;vulkanRuntime;version" "linux;sources;mpv;commit" "linux;sources;ffmpeg;tag"
                      "linux;sources;libplacebo;tag" "linux;sources;libass;tag" "linux;sources;nvCodecHeaders;tag")
  string(JSON value GET "${pins}" ${path})
  string(FIND "${notices}" "${value}" at)
  if(at EQUAL -1)
    string(REPLACE ";" "." key "${path}")
    string(APPEND problems "  THIRD-PARTY-NOTICES.txt does not name ${key} = ${value} from deps/versions.json\n")
  endif()
endforeach()
# The Windows Qt archives' Mesa build.
string(JSON count LENGTH "${pins}" windows qt archives)
math(EXPR last "${count} - 1")
foreach(i RANGE ${last})
  string(JSON archive GET "${pins}" windows qt archives ${i} name)
  if(archive MATCHES "^opengl32sw-64-mesa_([0-9]+)_([0-9]+)_([0-9]+)")
    set(mesa "Mesa ${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    string(FIND "${notices}" "${mesa}" at)
    if(at EQUAL -1)
      string(APPEND problems "  THIRD-PARTY-NOTICES.txt does not name ${mesa} (deps/versions.json: ${archive})\n")
    endif()
  endif()
endforeach()

# libmpv-2.dll (Windows): every library of deps/libmpv-windows-sources.json
# has its licences in libmpv-windows.txt (the libmpv sources workflow checks
# the texts against the sources), and the notices name the source release.
file(READ "${SOURCE_DIR}/deps/libmpv-windows-sources.json" libmpv_sources)
string(JSON libmpv_build GET "${libmpv_sources}" build)
string(JSON pinned_build GET "${pins}" windows mpv version)
if(NOT libmpv_build STREQUAL pinned_build)
  string(APPEND problems "  deps/libmpv-windows-sources.json is for libmpv ${libmpv_build}, deps/versions.json pins ${pinned_build}\n")
endif()
string(FIND "${notices}" "libmpv-windows-${pinned_build}" at)
if(at EQUAL -1)
  string(APPEND problems "  THIRD-PARTY-NOTICES.txt does not name the source release libmpv-windows-${pinned_build}\n")
endif()
string(JSON mirror GET "${pins}" windows mpv mirror)
string(FIND "${mirror}" "/libmpv-windows-${pinned_build}/" at)
if(at EQUAL -1)
  string(APPEND problems "  deps/versions.json windows.mpv.mirror is not in the release libmpv-windows-${pinned_build}\n")
endif()
if(EXISTS "${dir}/libmpv-windows.txt")
  file(READ "${dir}/libmpv-windows.txt" libmpv_licences)
  string(FIND "${libmpv_licences}" "(${pinned_build})" at)
  if(at EQUAL -1)
    string(APPEND problems "  libmpv-windows.txt is not for libmpv ${pinned_build}\n")
  endif()
  string(JSON count LENGTH "${libmpv_sources}" components)
  if(count LESS 1)
    string(APPEND problems "  deps/libmpv-windows-sources.json lists no libraries\n")
  else()
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
      string(JSON name GET "${libmpv_sources}" components ${i} name)
      string(FIND "${libmpv_licences}" "\n# ${name} (" at)
      if(at EQUAL -1)
        string(APPEND problems "  libmpv-windows.txt has no licences of ${name}\n")
      endif()
    endforeach()
  endif()
endif()

# Both packages take the folder in and check what they bundle.
file(READ "${SOURCE_DIR}/.github/workflows/package.yml" package_yml)
foreach(needle IN ITEMS "packaging/licenses/*.txt" "No licence notice for")
  string(FIND "${package_yml}" "${needle}" at)
  if(at EQUAL -1)
    string(APPEND problems "  package.yml (MSI) no longer has: ${needle}\n")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/packaging/linux/build-appimage.sh" appimage)
foreach(needle IN ITEMS "/packaging/licenses/*.txt" "no licence for these libraries" "/usr/share/doc/$owner/copyright")
  string(FIND "${appimage}" "${needle}" at)
  if(at EQUAL -1)
    string(APPEND problems "  build-appimage.sh no longer has: ${needle}\n")
  endif()
endforeach()

if(problems)
  message(FATAL_ERROR "Bundled licences (packaging/licenses):\n${problems}")
endif()
list(LENGTH present files)
message(STATUS "${files} licence files in packaging/licenses, each in THIRD-PARTY-NOTICES.txt")
