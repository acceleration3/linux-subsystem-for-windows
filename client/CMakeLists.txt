project(client)

add_subdirectory(resources)

file(GLOB_RECURSE SOURCE_FILES "src/*.cpp")

# Include generated GResource source in the client target
set(RESOURCE_C ${CMAKE_CURRENT_LIST_DIR}/resources/resources.c)
set_source_files_properties(${RESOURCE_C} PROPERTIES GENERATED TRUE)

add_executable(client ${SOURCE_FILES} ${RESOURCE_C})
add_dependencies(client resources)