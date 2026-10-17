# Run after translation-make.sh and before signing. VERIFY_ONLY checks the
# extracted package without modifying it.
if(NOT DEFINED APP_BUNDLE)
  message(FATAL_ERROR "Set APP_BUNDLE to the OpenSCAD.app directory")
endif()
get_filename_component(source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(locale_dir "${APP_BUNDLE}/Contents/Resources/locale")
set(font_dir "${APP_BUNDLE}/Contents/Resources/fonts")

if(NOT VERIFY_ONLY)
  # Match the native resource install rule; catalogs are generated after the
  # configure-time resource copy. The upstream release script also copies them
  # immediately before macdeployqt.
  file(COPY "${source_dir}/locale" DESTINATION "${APP_BUNDLE}/Contents/Resources"
       FILES_MATCHING PATTERN "*/LC_MESSAGES/openscad.mo")
  # Match release-common.sh: FontCache uses this bundled configuration instead
  # of Fontconfig's build-machine defaults.
  file(COPY "${source_dir}/fonts-osx/" DESTINATION "${font_dir}")
endif()

file(GLOB_RECURSE font_configs LIST_DIRECTORIES false
     RELATIVE "${source_dir}/fonts-osx" "${source_dir}/fonts-osx/*")
foreach(config IN LISTS font_configs)
  if(NOT EXISTS "${font_dir}/${config}")
    message(FATAL_ERROR "Missing Fontconfig resource: ${font_dir}/${config}")
  endif()
  file(SHA256 "${source_dir}/fonts-osx/${config}" expected_hash)
  file(SHA256 "${font_dir}/${config}" actual_hash)
  if(NOT actual_hash STREQUAL expected_hash)
    message(FATAL_ERROR "Incorrect Fontconfig resource: ${font_dir}/${config}")
  endif()
endforeach()

file(STRINGS "${source_dir}/locale/LINGUAS" language_lines)
foreach(line IN LISTS language_lines)
  string(REGEX REPLACE "#.*$" "" line "${line}")
  string(REGEX MATCHALL "[^ \t]+" languages "${line}")
  foreach(language IN LISTS languages)
    set(catalog "${locale_dir}/${language}/LC_MESSAGES/openscad.mo")
    if(NOT EXISTS "${catalog}")
      message(FATAL_ERROR "Missing translation catalog: ${catalog}")
    endif()
    file(SIZE "${catalog}" catalog_size)
    if(catalog_size LESS 28)
      message(FATAL_ERROR "Truncated translation catalog: ${catalog}")
    endif()
    file(READ "${catalog}" magic LIMIT 4 HEX)
    if(NOT magic STREQUAL "de120495" AND NOT magic STREQUAL "950412de")
      message(FATAL_ERROR "Invalid gettext catalog: ${catalog}")
    endif()
  endforeach()
endforeach()
