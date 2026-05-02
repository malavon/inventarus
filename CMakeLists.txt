cmake_minimum_required(VERSION 3.14)

project(electarus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package (SQLite3)
find_package (Curses)

set(DB_PROJECT_DIR ${CMAKE_SOURCE_DIR}/data)

set(CCCURSES_HEADERS
    cccurses/cccurses.hpp
    cccurses/color-internal.hpp
    cccurses/colors.hpp
    cccurses/error.hpp
    cccurses/form.hpp
    cccurses/input.hpp
    cccurses/support.hpp
    cccurses/window.hpp
)

set(ELECTARUS_HEADERS
    db/database.hpp
    db/database-files.hpp
    db/util.hpp
    ui/ui.hpp
)

add_executable(electarus
    electarus.cpp
    db/database.cpp
    db/database-files.cpp
    ui/ui.cpp

    # these are here only for QTCreator
    ${CCCURSES_HEADERS}
    ${ELECTARUS_HEADERS}
)

target_include_directories  (electarus PRIVATE ${SQLite3_INCLUDE_DIRS} ${CURSES_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})
target_link_libraries	    (electarus PRIVATE ${SQLite3_LIBRARIES} ${CURSES_LIBRARIES})

target_compile_definitions  (electarus PUBLIC DB_DIRECTORY="${DB_PROJECT_DIR}/sql")
# todo: option? gcc/clang check
# todo: option? and check for gcc/clang
target_compile_options      (electarus PUBLIC -Werror)
target_compile_options      (electarus PUBLIC -Wall -Wextra -Wpedantic)


include(GNUInstallDirs)
install(TARGETS electarus
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
