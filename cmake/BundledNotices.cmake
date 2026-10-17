# Source-bundled notices supplement the SDK notices before the existing
# configure-time macOS copy or Windows/Linux install. Keep system-library
# notices with their platform packaging rather than guessing their versions.
function(openscad_bundled_notice source destination)
  configure_file("${CMAKE_SOURCE_DIR}/${source}"
                 "${openaxis_notices}/${destination}" COPYONLY)
endfunction()

openscad_bundled_notice(doc/bundled-source-notices.txt Bundled-source-notices.txt)
openscad_bundled_notice(submodules/manifold/LICENSE Manifold/LICENSE)
openscad_bundled_notice(submodules/manifold/LICENSE GLAD/Apache-2.0.txt)
openscad_bundled_notice(submodules/mimalloc/LICENSE mimalloc/LICENSE)
openscad_bundled_notice(src/libsvg/LICENSE libsvg/LICENSE)
openscad_bundled_notice(submodules/Clipper2/LICENSE Clipper2/LICENSE)
openscad_bundled_notice(src/ext/lexertl/include/lexertl/licence_1_0.txt lexertl/LICENSE)
foreach(license LICENSE.txt LICENSE-bsd.txt LICENSE-gpl3.txt LICENSE-orig.txt)
  openscad_bundled_notice("src/ext/hidapi/${license}" "HIDAPI/${license}")
endforeach()
openscad_bundled_notice(submodules/OpenCSG/copying.txt OpenCSG/copying.txt)
openscad_bundled_notice(submodules/OpenCSG/doc/license/gpl-2.0.txt OpenCSG/gpl-2.0.txt)
openscad_bundled_notice(submodules/OpenCSG/doc/license/gpl-3.0.txt OpenCSG/gpl-3.0.txt)

# The SDK may use a system nlohmann_json target or its pinned FetchContent
# target. Preserve the actual header attribution in either case, independently
# of OpenSCAD's own vendored JSON version. Both use the accompanying MIT text.
get_target_property(openaxis_json_includes nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
set(openaxis_json_header "")
foreach(directory IN LISTS openaxis_json_includes)
  string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.*)>$" "\\1" directory "${directory}")
  if(EXISTS "${directory}/nlohmann/json.hpp")
    set(openaxis_json_header "${directory}/nlohmann/json.hpp")
    break()
  endif()
endforeach()
if(NOT openaxis_json_header)
  message(FATAL_ERROR "Cannot locate the SDK nlohmann_json header for its license notice")
endif()
file(MAKE_DIRECTORY "${openaxis_notices}/JSON")
foreach(component OpenSCAD SDK)
  if(component STREQUAL "OpenSCAD")
    set(header "${CMAKE_SOURCE_DIR}/src/ext/json/json.hpp")
  else()
    set(header "${openaxis_json_header}")
  endif()
  # Upstream places its version/copyright/license comment before the first
  # blank line. Read that comment, not the full megabyte-sized header.
  file(STRINGS "${header}" header_lines LIMIT_COUNT 8)
  list(JOIN header_lines "\n" header_notice)
  if(NOT header_notice MATCHES "Copyright|SPDX-FileCopyrightText")
    message(FATAL_ERROR "JSON header attribution format changed: ${header}")
  endif()
  file(WRITE "${openaxis_notices}/JSON/${component}-header.txt" "${header_notice}\n")
endforeach()
