cmake_minimum_required(VERSION 3.20)

project(lsw)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(ADWAITA REQUIRED libadwaita-1)

include_directories(${GTK4_INCLUDE_DIRS} ${ADWAITA_INCLUDE_DIRS})
link_libraries(${GTK4_LIBRARIES} ${ADWAITA_LIBRARIES})

add_subdirectory(client)