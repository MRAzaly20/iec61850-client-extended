cmake_minimum_required(VERSION 3.16)
project(iec61850_client C)

set(CMAKE_C_STANDARD 11)

# ---------- GTK4 via pkg-config ----------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

# ---------- libiec61850 (pre-built) ----------
set(LIB61850_ROOT "${CMAKE_CURRENT_LIST_DIR}/../libiec61850")

set(LIB61850_INCLUDE_DIRS
  ${LIB61850_ROOT}/src/iec61850/inc
  ${LIB61850_ROOT}/src/iec61850/inc_private
  ${LIB61850_ROOT}/src/mms/inc
  ${LIB61850_ROOT}/src/mms/inc_private
  ${LIB61850_ROOT}/src/mms/iso_mms/asn1c
  ${LIB61850_ROOT}/src/common/inc
  ${LIB61850_ROOT}/src/logging
  ${LIB61850_ROOT}/src/goose
  ${LIB61850_ROOT}/src/sampled_values
  ${LIB61850_ROOT}/src/r_session
  ${LIB61850_ROOT}/hal/inc
  ${LIB61850_ROOT}/builds/config
)

set(LIB61850_LIBRARIES
  ${LIB61850_ROOT}/builds/src/libiec61850.a
  ${LIB61850_ROOT}/builds/hal/libhal.a
)

# ---------- Sources ----------
set(SOURCES
  src/main.c
  src/iec61850_conn.c
  src/iec61850_gui.c
  src/sld_draw.c
  src/cJSON.c
  src/substation_config.c
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${LIB61850_INCLUDE_DIRS}
  ${GTK4_INCLUDE_DIRS}
)

target_compile_options(${PROJECT_NAME} PRIVATE ${GTK4_CFLAGS_OTHER})

target_link_libraries(${PROJECT_NAME} PRIVATE
  ${LIB61850_LIBRARIES}
  ${GTK4_LIBRARIES}
  ws2_32 # Winsock for libiec61850
)

target_link_directories(${PROJECT_NAME} PRIVATE ${GTK4_LIBRARY_DIRS})

# ---------- Copy CSS into build dir ----------
file(COPY ${CMAKE_CURRENT_LIST_DIR}/res/style.css DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/res)
