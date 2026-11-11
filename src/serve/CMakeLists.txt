add_library(ninfer_serve STATIC
  http_transport.cpp
  console_log.cpp
  request_validation.cpp
  openai_chat_request.cpp
  openai_chat_response.cpp
  openai_chat_http.cpp
  openai_common.cpp
  props.cpp
  openai_responses_request.cpp
  openai_responses_state.cpp
  openai_responses_response.cpp
  anthropic_messages_request.cpp
  anthropic_messages_response.cpp
  anthropic_messages_http.cpp
  openai_responses_store.cpp
  translate.cpp
  generation_service.cpp
  request_events.cpp
  operational_log.cpp
  request_log.cpp
  metrics.cpp
  http_server.cpp
  load_report.cpp
  openai_responses_http.cpp
  serve_options.cpp
  config_profile.cpp
)
# Embedded Web UI: the asset tree under webui/ is folded into the binary as a
# generated header (webui_assets.h), so the serve app needs no installable UI
# files. The glob re-runs on asset changes; the custom command rebuilds the
# header whenever an asset or the generator script changes.
file(GLOB_RECURSE NINFER_WEBUI_ASSET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/webui/*)
set(NINFER_WEBUI_ASSETS "")
foreach(_asset ${NINFER_WEBUI_ASSET_FILES})
  string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/webui/" "" _rel ${_asset})
  list(APPEND NINFER_WEBUI_ASSETS "${_rel}")
endforeach()
list(SORT NINFER_WEBUI_ASSETS)
set(NINFER_WEBUI_ASSET_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/make_webui_assets.cmake
                           ${CMAKE_CURRENT_SOURCE_DIR}/webui_assets.h.in)
foreach(_asset IN LISTS NINFER_WEBUI_ASSETS)
  list(APPEND NINFER_WEBUI_ASSET_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/webui/${_asset})
endforeach()
set(NINFER_WEBUI_HEADER ${CMAKE_CURRENT_BINARY_DIR}/webui_assets.h)
add_custom_command(
  OUTPUT ${NINFER_WEBUI_HEADER}
  COMMAND ${CMAKE_COMMAND}
          -DOUT=${NINFER_WEBUI_HEADER}
          -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/webui
          -P ${CMAKE_CURRENT_SOURCE_DIR}/make_webui_assets.cmake
  DEPENDS ${NINFER_WEBUI_ASSET_DEPS}
  COMMENT "Embedding Web UI assets")

ninfer_internal_includes(ninfer_serve)
target_sources(ninfer_serve PRIVATE ${NINFER_WEBUI_HEADER})
target_include_directories(ninfer_serve PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ninfer_serve
  PUBLIC ninfer_engine Threads::Threads ninfer::json ninfer::httplib
  PRIVATE ninfer_media_acquire ninfer_s3_object_store ninfer_product_logging ninfer_product_version ninfer_grammar
          ${NINFER_CUDART_TARGET})
