# The Ruby program, whose entry point is always lib/main.rb, is compiled to
# C by the host Spinel at build time. Its `main` is renamed spinel_main and
# called from the Ruby task (main.c).
# (`--ext-init`, which emits a library entry instead, does not compile for
# a program this small: spinel_rt.h's SP_TU_NO_POLY_RENDER fallbacks clash
# with the ext kernel's extern declarations, on any host.)
set(RUBY_LIB ${CMAKE_CURRENT_LIST_DIR}/../lib)
set(RUBY_MAIN ${RUBY_LIB}/main.rb)
set(RUBY_C ${CMAKE_CURRENT_BINARY_DIR}/main_rb.c)
# Packages under packages/<name>/<name>.rb resolve `require "<name>"`
# (spinel -I packages); the C they carry is compiled here.
set(RUBY_PACKAGES ${CMAKE_CURRENT_LIST_DIR}/../packages)
file(GLOB PACKAGE_C_SRCS ${RUBY_PACKAGES}/*/*.c)
# The C that Spinel's own bundled packages carry, for `require "json"` and
# the like. Spinel's driver links a package's C only when the program
# requires it (the generated C's SPINEL_LINK_OBJ markers); here all of it is
# compiled into this component's archive, whose objects the linker takes
# only when the program refers to them. zlib (libz), openssl, pty, ffi and
# tmpdir are left out: ESP-IDF has nothing for them to stand on.
set(SPINEL_PACKAGES $ENV{SPINEL_DIR}/packages)
set(SPINEL_PACKAGE_C_SRCS
  ${SPINEL_PACKAGES}/json/sp_json.c
  ${SPINEL_PACKAGES}/stringio/sp_stringio.c
  ${SPINEL_PACKAGES}/strscan/sp_strscan.c
  ${SPINEL_PACKAGES}/base64/sp_base64.c)
idf_component_register(SRCS "main.c" "stack_check.c" "${RUBY_C}" ${PACKAGE_C_SRCS} ${SPINEL_PACKAGE_C_SRCS}
                       PRIV_INCLUDE_DIRS ${SPINEL_PACKAGES}/json ${SPINEL_PACKAGES}/stringio
                       PRIV_REQUIRES spinel esp_driver_gpio esp_driver_uart esp_driver_spi)

# ESP-IDF first reads this file in script mode, only for the requirements
# above; the rest is for the real configure.
if(CMAKE_BUILD_EARLY_EXPANSION)
  return()
endif()

# Every .rb under lib/: main.rb splices in what it require_relative's, so a
# change to any of them recompiles the program.
file(GLOB_RECURSE RUBY_SRCS CONFIGURE_DEPENDS ${RUBY_LIB}/*.rb ${RUBY_PACKAGES}/*.rb)

# --cc names the target compiler so Spinel classifies integer literals for
# its 32-bit Integer.
add_custom_command(
  OUTPUT ${RUBY_C}
  COMMAND spinel -I ${RUBY_PACKAGES} ${RUBY_MAIN} -c --cc=${CMAKE_C_COMPILER} -o ${RUBY_C}
  # A program with no ivar inspector gets an inspector table of { NULL }.
  # GCC folds the call through it into a call to address 0, which the
  # Xtensa linker refuses ("windowed longcall crosses 1GB boundary"), so the
  # NULL becomes a stub that is never called (port/sp_esp.c). Spinel's
  # src/codegen.c emits the NULL; nothing calls through an empty table.
  COMMAND sed -i "s/^\\(static const char \\*(\\*const sp_insp_fn\\[\\])(void \\*, const uint8_t \\*) = {\\) NULL };$/\\1 sp_esp_insp_none };/" ${RUBY_C}
  DEPENDS ${RUBY_SRCS}
  COMMENT "Spinel: compiling lib/main.rb to C"
  VERBATIM)

# Generated C: Spinel's own warning policy (-Wno-all), not ESP-IDF's -Werror.
# With CONFIG_ESPINEL_STACK_CHECK its functions check the stack on entry
# (stack_check.c); the runtime's inline helpers from spinel_rt.h are left
# out, for speed.
set(RUBY_C_OPTIONS -O2 -w -Dmain=spinel_main)
if(CONFIG_ESPINEL_STACK_CHECK)
  list(APPEND RUBY_C_OPTIONS -finstrument-functions
       -finstrument-functions-exclude-file-list=$ENV{SPINEL_DIR}/lib)
endif()
set_source_files_properties(${RUBY_C} PROPERTIES COMPILE_OPTIONS "${RUBY_C_OPTIONS}")

# Spinel's packages: Spinel's warning policy, as for the generated C
set_source_files_properties(${SPINEL_PACKAGE_C_SRCS} PROPERTIES
  COMPILE_OPTIONS "-O2;-w")
