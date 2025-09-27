project(client)

add_subdirectory(resources)

file(GLOB SOURCE_FILES CONFIGURE_DEPENDS "src/*.cpp" "src/net/*.cpp")

# Include generated GResource source in the client target
set(RESOURCE_C ${CMAKE_CURRENT_LIST_DIR}/resources/resources.c)
set_source_files_properties(${RESOURCE_C} PROPERTIES GENERATED TRUE)

add_executable(client ${SOURCE_FILES} ${RESOURCE_C})
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LIBVIRT REQUIRED libvirt)
pkg_check_modules(WIMLIB REQUIRED wimlib)
find_package(CURL REQUIRED)
target_include_directories(client PRIVATE ${GLIB_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS} ${LIBVIRT_INCLUDE_DIRS} ${WIMLIB_INCLUDE_DIRS})
target_link_libraries(client PRIVATE ${CURL_LIBRARIES} ${GLIB_LIBRARIES} ${LIBVIRT_LIBRARIES} ${WIMLIB_LIBRARIES})
target_include_directories(client PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_definitions(client PRIVATE -DHTTP_CLIENT_ENABLE_LOG)

add_dependencies(client resources)