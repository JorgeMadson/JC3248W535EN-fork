# Main component: every source in src/, headers in include/.
# Libraries from platformio.ini lib_deps (LVGL, ArduinoJson, JPEGDEC, PNGdec) are the components in
# components/.
# ESP-IDF components (esp-nimble-cpp, esp_new_jpeg) come from idf_component.yml (which also makes
# them requirements of this component).
file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Placar de mesa: lógica, telas e jogos vêm direto do projeto placar-mesa (um código só para os dois
# firmwares); só o Pintor (src/placar/) é deste launcher. A pasta hal/ do placar-mesa fica de fora.
set(PLACAR_DIR "${CMAKE_CURRENT_LIST_DIR}/../../JC3248W535/placar-mesa" CACHE PATH "Pasta do projeto placar-mesa")
file(GLOB_RECURSE placar_sources
     ${PLACAR_DIR}/lib/nucleo/*.cpp ${PLACAR_DIR}/src/ui/*.cpp ${PLACAR_DIR}/src/telas/*.cpp
     ${PLACAR_DIR}/src/jogos/*.cpp)
if(NOT placar_sources)
    message(FATAL_ERROR "placar-mesa não encontrado em ${PLACAR_DIR}")
endif()
list(APPEND app_sources ${placar_sources})
set(placar_includes "placar" "${PLACAR_DIR}/lib/nucleo" "${PLACAR_DIR}/src/ui" "${PLACAR_DIR}/src/telas"
    "${PLACAR_DIR}/src/jogos")

# Freedoom: o motor (doomgeneric) e o código comum (vídeo, toque, mixer de som) vêm do projeto
# freedoom, o mesmo do firmware próprio; só src/doom/ é deste launcher. O motor é C antigo: compila
# em gnu11 (em C23 "true"/"false" são palavras reservadas), sem avisos, e otimizado para velocidade.
set(FREEDOOM_DIR "${CMAKE_CURRENT_LIST_DIR}/../../JC3248W535/freedoom" CACHE PATH "Pasta do projeto freedoom")
file(GLOB doom_motor ${FREEDOOM_DIR}/lib/doomgeneric/src/*.c)
file(GLOB doom_comum ${FREEDOOM_DIR}/src/comum/*.c)
if(NOT doom_motor OR NOT doom_comum)
    message(FATAL_ERROR "freedoom não encontrado em ${FREEDOOM_DIR}")
endif()
list(APPEND app_sources ${doom_motor} ${doom_comum})
set(doom_includes "${FREEDOOM_DIR}/lib/doomgeneric/src;${CMAKE_SOURCE_DIR}/src/doom;${FREEDOOM_DIR}/src/comum")
# REQUIRES: the ESP-IDF components the sources include. Only these (and what they need) are built,
# see the top-level CMakeLists.txt.
idf_component_register(SRCS ${app_sources}
                       LDFRAGMENTS "doom/freedoom.lf"
                       INCLUDE_DIRS "." "../include" ${placar_includes}
                       REQUIRES
                           lvgl ArduinoJson jpegdec pngdec  # lib_deps (components/)
                           app_update                  # OTA slots and rollback (ota_update)
                           bootloader_support          # image header check (ota_update)
                           bt                          # BLE controller + NimBLE host (Air)
                           esp_driver_i2c              # touch (esp_bsp.c)
                           esp_driver_gpio
                           esp_driver_i2s              # speaker (audio_player)
                           esp_driver_ledc             # backlight
                           esp_driver_sdmmc
                           esp_driver_sdspi
                           esp_driver_spi              # display (QSPI)
                           esp_driver_usb_serial_jtag  # serial log
                           esp_event
                           esp_http_client             # weather, lyrics, radio
                           esp_http_server             # web page
                           esp_lcd
                           esp_netif
                           esp_psram
                           esp_timer
                           esp_wifi
                           espcoredump                 # core dump to flash on a panic (esp-coredump)
                           esp-tls
                           fatfs
                           lwip                        # SNTP
                           mbedtls                     # certificate bundle (HTTPS)
                           nvs_flash
                           sdmmc)

# The SD card doesn't answer ESP-IDF's SD Status Register read in SPI mode; let init continue
# without it (see __wrap_sdmmc_init_sd_ssr in sd_card.cpp).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=sdmmc_init_sd_ssr")

# MP3 decoding is the hot loop of the Music app: optimize it for speed, not size.
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/minimp3/minimp3.c PROPERTIES COMPILE_OPTIONS "-O2")

# Freedoom (lista e caminhos lá em cima). Depois do idf_component_register: na leitura das
# dependências o ESP-IDF roda este arquivo como script, e esse comando não vale ali.
set_source_files_properties(${doom_motor} ${doom_comum} PROPERTIES
    COMPILE_OPTIONS "-std=gnu11;-O2;-w;-fpermissive;-DFEATURE_SOUND;-include;${CMAKE_SOURCE_DIR}/src/doom/doom_saida.h"
    INCLUDE_DIRECTORIES "${doom_includes}")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/doom/doom_glue.c PROPERTIES
    COMPILE_OPTIONS "-std=gnu11" INCLUDE_DIRECTORIES "${doom_includes}")
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/doom/doom_ui.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${doom_includes}")
