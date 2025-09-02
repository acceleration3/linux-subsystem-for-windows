project(resources)

find_program(GLIB_COMPILE_RESOURCES NAMES glib-compile-resources)
find_program(BLUEPRINT_COMPILER NAMES blueprint-compiler)

if (NOT GLIB_COMPILE_RESOURCES)
    message(FATAL_ERROR "glib-compile-resources not found. Please install glib2 development tools.")
endif()

if (NOT BLUEPRINT_COMPILER)
    message(FATAL_ERROR "blueprint-compiler not found. Please install blueprint-compiler.")
endif()

# Compile Blueprint files to UI files
set(BLUEPRINT_FILE ${CMAKE_CURRENT_LIST_DIR}/ui/installer.blp)
set(COMPILED_UI_FILE ${CMAKE_CURRENT_LIST_DIR}/ui/installer.ui)

# Create the ui directory in build directory
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/ui
    COMMENT "Creating ui directory"
)

# Compile Blueprint to UI
add_custom_command(
    OUTPUT ${COMPILED_UI_FILE}
    COMMAND ${BLUEPRINT_COMPILER} compile --output ${COMPILED_UI_FILE} ${BLUEPRINT_FILE}
    DEPENDS ${BLUEPRINT_FILE} ${CMAKE_CURRENT_BINARY_DIR}/ui
    COMMENT "Compiling Blueprint file to UI"
)

# Generate GResource from compiled UI file
set(XML_FILE ${CMAKE_CURRENT_LIST_DIR}/resources.xml)
set(GENERATED_C ${CMAKE_CURRENT_LIST_DIR}/resources.c)

add_custom_command(
    OUTPUT ${GENERATED_C}
    COMMAND ${GLIB_COMPILE_RESOURCES}
            --generate-source
            --sourcedir=${CMAKE_CURRENT_BINARY_DIR}
            --target=${GENERATED_C}
            ${XML_FILE}
    DEPENDS ${XML_FILE} ${COMPILED_UI_FILE}
    COMMENT "Generating GResource source"
)

add_custom_target(resources ALL DEPENDS ${GENERATED_C})