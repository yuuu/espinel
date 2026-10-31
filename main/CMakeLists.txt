# The Ruby program, whose entry point is always lib/main.rb, is compiled to
# C by the host Spinel at build time. Its `main` is renamed spinel_main and
# called from the Ruby task (main.c).
# (`--ext-init`, which emits a library entry instead, does not compile for
# a program this small: spinel_rt.h's SP_TU_NO_POLY_RENDER fallbacks clash
# with the ext kernel's extern declarations, on any host.)
set(RUBY_LIB ${CMAKE_CURRENT_LIST_DIR}/../lib)
set(RUBY_MAIN ${RUBY_LIB}/main.rb)
set(RUBY_C ${CMAKE_CURRENT_BINARY_DIR}/main_rb.c)
idf_component_register(SRCS "main.c" "${RUBY_C}"
                       PRIV_REQUIRES spinel)

# ESP-IDF first reads this file in script mode, only for the requirements
# above; the rest is for the real configure.
if(CMAKE_BUILD_EARLY_EXPANSION)
  return()
endif()

# Every .rb under lib/: main.rb splices in what it require_relative's, so a
# change to any of them recompiles the program.
file(GLOB_RECURSE RUBY_SRCS CONFIGURE_DEPENDS ${RUBY_LIB}/*.rb)

# --cc names the target compiler so Spinel classifies integer literals for
# its 32-bit Integer.
add_custom_command(
  OUTPUT ${RUBY_C}
  COMMAND spinel ${RUBY_MAIN} -c --cc=${CMAKE_C_COMPILER} -o ${RUBY_C}
  DEPENDS ${RUBY_SRCS}
  COMMENT "Spinel: compiling lib/main.rb to C"
  VERBATIM)

# Generated C: Spinel's own warning policy (-Wno-all), not ESP-IDF's -Werror
set_source_files_properties(${RUBY_C} PROPERTIES
  COMPILE_OPTIONS "-O2;-w;-Dmain=spinel_main")
