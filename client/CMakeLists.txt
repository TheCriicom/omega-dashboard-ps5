cmake_minimum_required(VERSION 3.20)
project(omega_ui C)

set(SDK "$ENV{PS5_PAYLOAD_SDK}")
set(CMAKE_C_STANDARD 17)

execute_process(COMMAND ${SDK}/bin/prospero-sdl2-config --cflags OUTPUT_VARIABLE SDL_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${SDK}/bin/prospero-sdl2-config --libs   OUTPUT_VARIABLE SDL_LIBS   OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${SDK}/bin/prospero-freetype-config --libs OUTPUT_VARIABLE FT_LIBS  OUTPUT_STRIP_TRAILING_WHITESPACE)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${D_CWD}/../bin)

add_executable(omega_ui)
set_target_properties(omega_ui PROPERTIES OUTPUT_NAME "omega_ui.elf")
file(GLOB SRC ${D_CWD}/source/*.c)
# Aggiornamenti automatici solo nella build ufficiale: update.c non è nel sorgente pubblico.
if(EXISTS ${D_CWD}/source/update.c)
  add_compile_definitions(OMEGA_UPDATES)
endif()
target_sources(omega_ui PRIVATE ${SRC})

set(CMAKE_C_FLAGS "--target=x86_64-sie-ps5 -DPS5 -fPIC -fPIE -march=znver2 -O2 -Wall ${SDL_CFLAGS}")

# minizip, quickjs e opus stanno tra i pacchetti homebrew dell'SDK.
set(HB "${SDK}/target/user/homebrew")
target_include_directories(omega_ui PUBLIC "${HB}/include")

# Dipendenze circolari (SDL2 ↔ Sce*, SDL2_ttf ↔ freetype): tutto in un gruppo.
set(OURLIBS "-lSceHttp -lSceSsl -lSceNet -lSceNetCtl -lSceSysmodule -lSceSysCore -lSceAppInstUtil -lSceIpmi -lSceLibcInternal -lkernel_sys")
set(TTFDEPS "-lharfbuzz -lpng16 -lz -lbz2")
set(IMGDEPS "-lSDL2_image -ljpeg -lwebp -lwebpdemux")
set(HBDEPS  "-lminizip -lquickjs -lopus")   # zip dello Store, homebrew.js, voce del party
set_target_properties(omega_ui PROPERTIES LINK_FLAGS
  "-L${HB}/lib -L${HB}/lib/quickjs -Wl,--start-group -lSDL2_ttf ${IMGDEPS} ${FT_LIBS} ${TTFDEPS} ${HBDEPS} ${SDL_LIBS} ${OURLIBS} -Wl,--end-group")
target_link_directories(omega_ui PUBLIC "${HB}/lib")
