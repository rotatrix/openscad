# Run after translation-make.sh and before signing. VERIFY_ONLY checks the
# extracted package without modifying it.
if(NOT DEFINED APP_BUNDLE)
  message(FATAL_ERROR "Set APP_BUNDLE to the OpenSCAD.app directory")
endif()
get_filename_component(source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(locale_dir "${APP_BUNDLE}/Contents/Resources/locale")

if(NOT VERIFY_ONLY)
  # Match the native resource install rule; catalogs are generated after the
  # configure-time resource copy. The upstream release script also copies them
  # immediately before macdeployqt.
  file(COPY "${source_dir}/locale" DESTINATION "${APP_BUNDLE}/Contents/Resources"
       FILES_MATCHING PATTERN "*/LC_MESSAGES/openscad.mo")
endif()

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
