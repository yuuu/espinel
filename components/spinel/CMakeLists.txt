# The Spinel runtime (libspinel_rt's members) compiled from the Spinel
# checkout at $SPINEL_DIR for the ESP-IDF target. Nothing in that checkout
# is changed: what ESP-IDF lacks is supplied from port/ (headers searched
# before the toolchain's, and the stand-in functions in port/sp_esp.c).
set(SPINEL_DIR $ENV{SPINEL_DIR})
if(NOT SPINEL_DIR)
  message(FATAL_ERROR "SPINEL_DIR is not set (the Docker image sets it)")
endif()
set(SPINEL_LIB ${SPINEL_DIR}/lib)

# RT_MEMBERS and RE_SRC of Spinel's Makefile
set(SPINEL_RT_MEMBERS
  sp_bigint sp_crypto sp_pack sp_time sp_core sp_net sp_system sp_gc sp_slab
  sp_alloc sp_dtoa sp_marshal sp_format sp_string sp_inspect sp_poly_cold
  sp_array sp_str sp_str_cold sp_str_crypt sp_hash sp_proc sp_exc sp_re
  sp_random sp_fiber sp_sched sp_io sp_iobuffer sp_cold sp_enum_str
  sp_process sp_process_status)
set(SPINEL_RE_SRCS re_compile re_exec re_utf8 unicase re_spinel)

# The runtime, built into an archive of its own that the app does not link:
# every Ruby VM takes the members it needs from it into an object of its own
# (main/CMakeLists.txt), so each VM has its own copy of Spinel's globals.
# sp_esp_gc.c reads and sets those globals, so it goes with them.
set(rt_srcs port/sp_esp_gc.c)
foreach(m ${SPINEL_RT_MEMBERS})
  list(APPEND rt_srcs ${SPINEL_LIB}/${m}.c)
endforeach()
foreach(m ${SPINEL_RE_SRCS})
  list(APPEND rt_srcs ${SPINEL_LIB}/regexp/${m}.c)
endforeach()

# The component itself is what stands in for the C library (one for the
# whole process, shared by every VM). WHOLE_ARCHIVE: its poll replaces
# ESP-IDF's.
idf_component_register(SRCS port/sp_esp.c port/sp_esp_random.c
                       INCLUDE_DIRS port ${SPINEL_LIB}
                       PRIV_INCLUDE_DIRS ${SPINEL_LIB}/regexp ${SPINEL_LIB}/regexp/shim
                       REQUIRES pthread lwip esp_hw_support heap esp_timer vfs
                       WHOLE_ARCHIVE)

# What Spinel's Makefile gives the runtime (COPT, SEC_FLAGS) and the
# generated program, for both halves of the program to agree.
set(SPINEL_DEFS -DSP_ONE_STRING -ffp-contract=off -include sp_esp_config.h)
target_compile_options(${COMPONENT_LIB} PUBLIC ${SPINEL_DEFS})
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-all -w)

# picolibc's fflush(NULL) dereferences the NULL instead of flushing every
# stream; sp_esp.c's __wrap_fflush takes it (every VM's runtime calls it).
if(CONFIG_LIBC_PICOLIBC)
  target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=fflush")
endif()

if(CMAKE_BUILD_EARLY_EXPANSION)
  return()
endif()

add_library(espinel_rt STATIC ${rt_srcs})
target_link_libraries(espinel_rt PRIVATE ${COMPONENT_LIB})
# port/ ahead of every other component's headers, as for the component
target_include_directories(espinel_rt BEFORE PRIVATE port ${SPINEL_LIB}
                           ${SPINEL_LIB}/regexp ${SPINEL_LIB}/regexp/shim)
target_compile_options(espinel_rt PRIVATE -O2 -Wno-all -w)
