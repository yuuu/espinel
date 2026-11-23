# Each Ruby program becomes a VM: its generated C plus its own copy of
# Spinel's runtime, linked into one object (see espinel_add_vm). The
# programs are the entry point ("main") and every tasks/<name>.rb beside it.
set(ESPINEL_MAIN "lib/main.rb" CACHE STRING "The Ruby program's entry point, absolute or relative to this project")
get_filename_component(RUBY_MAIN ${ESPINEL_MAIN} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
get_filename_component(RUBY_LIB ${RUBY_MAIN} DIRECTORY)
set(RUBY_PACKAGES ${CMAKE_CURRENT_LIST_DIR}/../packages)
set(SPINEL_PACKAGES $ENV{SPINEL_DIR}/packages)

set(MAIN_REQUIRES spinel esp_driver_gpio esp_driver_uart
                  esp_driver_i2c esp_driver_spi esp_driver_ledc
                  esp_netif esp_event esp_wifi esp_eth nvs_flash esp_timer
                  mbedtls)

if(CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(PRIV_REQUIRES ${MAIN_REQUIRES})
  return()
endif()

# The programs: "main", then tasks/*.rb by name
set(VM_NAMES main)
set(VM_main_SRC ${RUBY_MAIN})
file(GLOB TASK_PROGRAMS CONFIGURE_DEPENDS ${RUBY_LIB}/tasks/*.rb)
list(SORT TASK_PROGRAMS)
foreach(src ${TASK_PROGRAMS})
  get_filename_component(name ${src} NAME_WE)
  if(NOT name MATCHES "^[a-z_][a-z0-9_]*$" OR name STREQUAL "main")
    message(FATAL_ERROR "${src}: a task's file name must be a lowercase identifier other than main")
  endif()
  list(APPEND VM_NAMES ${name})
  set(VM_${name}_SRC ${src})
endforeach()

# The table of VMs that packages/task starts them from
set(vm_decls "")
set(vm_rows "")
set(VM_OBJS)
foreach(vm ${VM_NAMES})
  string(APPEND vm_decls "int espinel_vm_${vm}_run(espinel_vm *, const char *, unsigned);\n")
  string(APPEND vm_rows "  { \"${vm}\", espinel_vm_${vm}_run },\n")
  list(APPEND VM_OBJS ${CMAKE_CURRENT_BINARY_DIR}/vm_${vm}.o)
endforeach()
list(LENGTH VM_NAMES vm_count)
set(VM_TABLE_C ${CMAKE_CURRENT_BINARY_DIR}/vm_table.c)
file(CONFIGURE OUTPUT ${VM_TABLE_C} CONTENT "#include \"espinel_task.h\"
${vm_decls}const espinel_vm_def espinel_vm_defs[] = {
${vm_rows}};
const int espinel_vm_def_count = ${vm_count};
")
set_source_files_properties(${VM_OBJS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

# packages/*/*.c is linked once and shared by every VM (espinel_task.h)
file(GLOB PACKAGE_C_SRCS ${RUBY_PACKAGES}/*/*.c ${RUBY_PACKAGES}/*/*/*.c)
idf_component_register(SRCS main.c ${VM_TABLE_C} ${VM_OBJS} ${PACKAGE_C_SRCS}
                       PRIV_INCLUDE_DIRS ${RUBY_PACKAGES}/task
                       PRIV_REQUIRES ${MAIN_REQUIRES})

# What each VM takes its own copy of besides the runtime: C that calls the
# runtime (the stack check, Spinel's bundled packages)
set(SPINEL_PACKAGE_C_SRCS
  ${SPINEL_PACKAGES}/json/sp_json.c
  ${SPINEL_PACKAGES}/stringio/sp_stringio.c
  ${SPINEL_PACKAGES}/strscan/sp_strscan.c
  ${SPINEL_PACKAGES}/base64/sp_base64.c)
add_library(espinel_vm_support STATIC stack_check.c ${SPINEL_PACKAGE_C_SRCS})
target_link_libraries(espinel_vm_support PRIVATE idf::spinel idf::freertos)
target_include_directories(espinel_vm_support PRIVATE ${SPINEL_PACKAGES}/json ${SPINEL_PACKAGES}/stringio)
set_source_files_properties(${SPINEL_PACKAGE_C_SRCS} PROPERTIES COMPILE_OPTIONS "-O2;-w")

# Generated C: Spinel's warning policy, its main renamed (vm_entry.c calls
# it), and the stack check's hooks
set(RUBY_C_OPTIONS -O2 -w -Dmain=spinel_main)
if(CONFIG_ESPINEL_STACK_CHECK)
  list(APPEND RUBY_C_OPTIONS -finstrument-functions
       -finstrument-functions-exclude-file-list=$ENV{SPINEL_DIR}/lib)
endif()
# a change to any .rb a program may require compiles every program again
file(GLOB_RECURSE RUBY_SRCS CONFIGURE_DEPENDS ${RUBY_LIB}/*.rb ${RUBY_PACKAGES}/*.rb)

# Keeps each function and object in its own section through `-r`, so the
# final link's --gc-sections still drops what is unused (not every
# section: RISC-V's .riscv.attributes breaks when split)
set(VM_UNIQUE)
foreach(sec .text .literal .rodata .data .bss .sdata .sbss .srodata .tbss .tdata)
  list(APPEND VM_UNIQUE "-Wl,--unique=${sec}.*")
endforeach()

# vm_<name>.o: the program's C, vm_entry.c and the runtime members they use,
# with every symbol made local but espinel_vm_<name>_run, so no two VMs
# share Spinel's globals
function(espinel_add_vm vm src)
  set(c ${CMAKE_CURRENT_BINARY_DIR}/vm_${vm}.c)
  set(r ${CMAKE_CURRENT_BINARY_DIR}/vm_${vm}_r.o)
  set(o ${CMAKE_CURRENT_BINARY_DIR}/vm_${vm}.o)
  add_custom_command(OUTPUT ${c}
    COMMAND spinel -I ${RUBY_PACKAGES} ${src} -c --cc=${CMAKE_C_COMPILER} -o ${c}
    DEPENDS ${RUBY_SRCS}
    COMMENT "Spinel: compiling ${src} to C"
    VERBATIM)
  set_source_files_properties(${c} PROPERTIES COMPILE_OPTIONS "${RUBY_C_OPTIONS}")

  add_library(espinel_vm_${vm} OBJECT ${c} vm_entry.c)
  target_link_libraries(espinel_vm_${vm} PRIVATE idf::spinel idf::freertos idf::esp_hw_support)
  target_include_directories(espinel_vm_${vm} PRIVATE ${RUBY_PACKAGES}/task)
  target_compile_definitions(espinel_vm_${vm} PRIVATE ESPINEL_VM_RUN=espinel_vm_${vm}_run)

  set(libs $<TARGET_FILE:espinel_vm_support> $<TARGET_FILE:espinel_rt>)
  add_custom_command(OUTPUT ${o}
    COMMAND ${CMAKE_C_COMPILER} -nostdlib -r ${VM_UNIQUE} -o ${r}
            $<TARGET_OBJECTS:espinel_vm_${vm}> -Wl,--start-group ${libs} -Wl,--end-group
    COMMAND ${CMAKE_OBJCOPY} --keep-global-symbol=espinel_vm_${vm}_run ${r} ${o}
    DEPENDS espinel_vm_${vm} espinel_vm_support espinel_rt $<TARGET_OBJECTS:espinel_vm_${vm}> ${libs}
    COMMENT "Linking the Ruby VM ${vm}"
    COMMAND_EXPAND_LISTS
    VERBATIM)
endfunction()

foreach(vm ${VM_NAMES})
  espinel_add_vm(${vm} ${VM_${vm}_SRC})
endforeach()
