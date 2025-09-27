cmake_minimum_required(VERSION 3.20)

project(lsw LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(ADWAITA REQUIRED libadwaita-1)
pkg_check_modules(WIMLIB REQUIRED wimlib)
pkg_check_modules(ISOFS REQUIRED libisofs-1)

include_directories(${GTK4_INCLUDE_DIRS} ${ADWAITA_INCLUDE_DIRS} ${WIMLIB_INCLUDE_DIRS} ${ISOFS_INCLUDE_DIRS})
link_libraries(${GTK4_LIBRARIES} ${ADWAITA_LIBRARIES} ${WIMLIB_LIBRARIES} ${ISOFS_LIBRARIES})

add_subdirectory(client)