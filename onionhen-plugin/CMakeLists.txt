cmake_minimum_required(VERSION 3.20)
# Plugin "Omega" per il Toolbox di OnionHEN: una pagina che si apre anche
# durante il gioco (scorciatoia del Toolbox, es. L2+R3) per comandare la musica
# di Omega e vedere gli amici online. Parla col demone omega_redirect su
# 127.0.0.1:9095. È facoltativo: Omega funziona anche senza OnionHEN.
project(OmegaOnionPlugin VERSION 1.0.0 LANGUAGES C)

# SDK dei plugin di OnionHEN (GPL-3.0), alla stessa revisione dei plugin ufficiali.
set(ONIONHEN_PLUGIN_SDK_SOURCE "" CACHE PATH "Copia locale di onionHEN-plugin-sdk (vuoto = scarica)")
if(NOT ONIONHEN_PLUGIN_SDK_SOURCE AND DEFINED ENV{ONIONHEN_PLUGIN_SDK_SOURCE})
  set(ONIONHEN_PLUGIN_SDK_SOURCE "$ENV{ONIONHEN_PLUGIN_SDK_SOURCE}")
endif()
if(ONIONHEN_PLUGIN_SDK_SOURCE)
  add_subdirectory("${ONIONHEN_PLUGIN_SDK_SOURCE}" "${CMAKE_BINARY_DIR}/_deps/onionhen-plugin-sdk-build" EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(onionhen_plugin_sdk
    GIT_REPOSITORY https://github.com/OnionBuddies/onionHEN-plugin-sdk.git
    GIT_TAG 0cf4346536705fb271ba0eb2226d97ebcb23559c
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(onionhen_plugin_sdk)
endif()

onion_add_plugin(
  NAME OMGA00001
  TITLE_ID OMGA00001
  VERSION 1.00
  SOURCES source/main.c source/i18n_data.c
  LIBRARIES SceLibcInternal SceSystemService kernel_sys)
