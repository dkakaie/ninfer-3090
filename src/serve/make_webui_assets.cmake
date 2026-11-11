# src/serve/make_webui_assets.cmake
#
# Emits the self-contained C++ header (webui_assets.h) that embeds the Web UI asset
# tree as hex-encoded string constants. The bytes are decoded once, on first access,
# so the embedded copy is byte-identical to the files. The static header text lives
# in webui_assets.h.in; this script only substitutes the asset table.
#
# Invocation (from src/serve/CMakeLists.txt):
#   cmake -DOUT=<generated hdr> -DSRC_DIR=<abs asset dir> -P <this file>
#
# The asset tree is globbed here; the custom command's DEPENDS list already
# rebuilds the header when any asset changes.

if(NOT DEFINED OUT OR NOT DEFINED SRC_DIR)
  message(FATAL_ERROR "make_webui_assets: require -DOUT and -DSRC_DIR")
endif()

file(GLOB_RECURSE _asset_files ${SRC_DIR}/*)
list(SORT _asset_files)

set(_entries "")
set(_count 0)
foreach(_asset IN LISTS _asset_files)
  string(REPLACE "${SRC_DIR}/" "" _rel ${_asset})
  set(_hex "")
  file(READ "${_asset}" _hex HEX)
  string(APPEND _entries "    {\"/${_rel}\",\"${_hex}\"},\n")
  math(EXPR _count "${_count} + 1")
endforeach()

file(READ "${CMAKE_CURRENT_LIST_DIR}/webui_assets.h.in" _template)
string(REPLACE "@ENTRIES@" "${_entries}" _header "${_template}")
file(WRITE "${OUT}" "${_header}")
message(STATUS "webui_assets.h: embedded ${_count} assets from ${SRC_DIR}")
