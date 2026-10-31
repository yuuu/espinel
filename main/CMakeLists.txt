# The Ruby program is compiled to C by the host Spinel at build time. Its
# `main` is renamed spinel_main and called from the Ruby task (main.c).
# (`--ext-init`, which emits a library entry instead, does not compile for
# a program this small: spinel_rt.h's SP_TU_NO_POLY_RENDER fallbacks clash
# with the ext kernel's extern declarations, on any host.)
set(RUBY_SRC ${CMAKE_CURRENT_LIST_DIR}/../lib/hello.rb)
set(RUBY_C ${CMAKE_CURRENT_BINARY_DIR}/hello_rb.c)

idf_component_register(SRCS "main.c" "${RUBY_C}"
                       PRIV_REQUIRES spinel)

# --cc names the target compiler so Spinel classifies integer literals for
# its 32-bit Integer.
add_custom_command(
  OUTPUT ${RUBY_C}
  COMMAND spinel ${RUBY_SRC} -c --cc=${CMAKE_C_COMPILER} -o ${RUBY_C}
  DEPENDS ${RUBY_SRC}
  COMMENT "Spinel: compiling hello.rb to C"
  VERBATIM)

# Generated C: Spinel's own warning policy (-Wno-all), not ESP-IDF's -Werror
set_source_files_properties(${RUBY_C} PROPERTIES
  COMPILE_OPTIONS "-O2;-w;-Dmain=spinel_main")
