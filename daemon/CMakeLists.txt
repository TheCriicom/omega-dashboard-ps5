cmake_minimum_required(VERSION 3.20)
set(basename "omega_redirect")
project(${basename} C)
set(SDK "$ENV{PS5_PAYLOAD_SDK}")
set(CMAKE_C_STANDARD 17)
set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${D_CWD}/../bin)
add_executable(${basename})
set_target_properties(${basename} PROPERTIES OUTPUT_NAME "${basename}.elf")
set(CMAKE_C_FLAGS "--target=x86_64-sie-ps5 -DPS5 -fPIC -fPIE -march=znver2 -O2 -Wall")
file(GLOB SRC ${D_CWD}/source/*.c)
target_sources(${basename} PRIVATE ${SRC})

# Lettore musicale: FFmpeg (con OpenSSL per https). Di preferenza la build
# ridotta al solo audio (tools/build-ffmpeg-audio.sh, ~3 MB); altrimenti quella
# completa dell'SDK, che però registra anche tutti i codec video (~30 MB).
set(HB "${SDK}/target/user/homebrew")
set(FFA "${D_CWD}/ffmpeg-audio")
if(EXISTS "${FFA}/lib/libavformat.a")
  target_include_directories(${basename} BEFORE PUBLIC "${FFA}/include")
  # per percorso: prospero-clang mette davanti a tutto la cartella lib dell'SDK,
  # che con -lavcodec sceglierebbe la build completa
  target_link_libraries(${basename} PRIVATE "${FFA}/lib/libavformat.a" "${FFA}/lib/libavcodec.a"
    "${FFA}/lib/libswresample.a" "${FFA}/lib/libavutil.a")
  set(AVLIBS "-lssl -lcrypto -lz")
else()
  message(WARNING "ffmpeg-audio assente: uso il FFmpeg completo dell'SDK (demone molto più grande)")
  set(AVLIBS "-lavformat -lavcodec -lswresample -lavutil -lx264 -lvpx -lmp3lame -lopus -lvorbisenc -lvorbis -logg -ltheoraenc -ltheoradec -lwebp -lwebpmux -lxml2 -liconv -lzstd -lssl -lcrypto -lz -lbz2 -llzma")
endif()
target_include_directories(${basename} PUBLIC "${HB}/include")
set(OURLIBS "-lopus -lSceAudioOut -lSceSystemService -lSceHttp -lSceSsl -lSceNet -lSceNetCtl -lSceSysmodule -lSceLibcInternal -lkernel_sys")
set_target_properties(${basename} PROPERTIES LINK_FLAGS
  "-Wl,--start-group ${AVLIBS} ${OURLIBS} -Wl,--end-group")
target_link_directories(${basename} PUBLIC "${PROJECT_ROOT}/lib" "${HB}/lib")
