cmake_minimum_required(VERSION 3.18)
project(loki C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(LOKI_BUILD_SHARED "Build libloki as a shared library" OFF)

find_package(Lua REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
# Line editing library detection (editline preferred over readline due to BSD license)
set(LINEEDIT_FOUND FALSE)
set(LINEEDIT_LIBRARY "")
set(LINEEDIT_INCLUDE_DIR "")
set(LINEEDIT_TYPE "none")

# Try editline first (BSD license, more permissive than GPL readline)
find_path(EDITLINE_INCLUDE_DIR
    NAMES histedit.h
    PATHS /usr/include /usr/local/include /opt/local/include
)
find_library(EDITLINE_LIBRARY
    NAMES edit
    PATHS /usr/lib /usr/local/lib /opt/local/lib
)

if (EDITLINE_INCLUDE_DIR AND EDITLINE_LIBRARY)
    set(LINEEDIT_FOUND TRUE)
    set(LINEEDIT_LIBRARY ${EDITLINE_LIBRARY})
    set(LINEEDIT_INCLUDE_DIR ${EDITLINE_INCLUDE_DIR})
    set(LINEEDIT_TYPE "editline")
    message(STATUS "Found editline: ${EDITLINE_LIBRARY}")
else()
    # Try readline as fallback
    find_package(Readline QUIET)

    if (NOT Readline_FOUND)
        # Try Homebrew readline on macOS
        execute_process(
            COMMAND brew --prefix readline
            OUTPUT_VARIABLE BREW_READLINE_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if (BREW_READLINE_PREFIX)
            find_path(READLINE_INCLUDE_DIR
                NAMES readline/readline.h
                HINTS
                    ${BREW_READLINE_PREFIX}/include
                    ${BREW_READLINE_PREFIX}/include/readline
            )
            find_library(READLINE_LIBRARY
                NAMES readline
                HINTS ${BREW_READLINE_PREFIX}/lib
            )
            if (READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
                set(Readline_FOUND ON)
                set(Readline_INCLUDE_DIR ${READLINE_INCLUDE_DIR})
                set(Readline_LIBRARY ${READLINE_LIBRARY})
            endif()
        endif()
    endif()

    if (Readline_FOUND)
        set(LINEEDIT_FOUND TRUE)
        set(LINEEDIT_LIBRARY ${Readline_LIBRARY})
        set(LINEEDIT_INCLUDE_DIR ${Readline_INCLUDE_DIR})
        set(LINEEDIT_TYPE "readline")
        message(STATUS "Found readline: ${Readline_LIBRARY}")
    endif()
endif()

if (NOT LINEEDIT_FOUND)
    message(STATUS "Line editing library not found; loki-repl will use basic fallback (no history/completion)")
    message(STATUS "  To enable features: brew install readline (or apt-get install libedit-dev on Linux)")
endif()

add_subdirectory(thirdparty)

set(LOKI_LIBRARY_TYPE STATIC)
if (LOKI_BUILD_SHARED)
    set(LOKI_LIBRARY_TYPE SHARED)
endif()

add_library(libloki ${LOKI_LIBRARY_TYPE}
    src/loki_core.c
    src/loki_lua.c
    src/loki_editor.c
    src/loki_syntax.c
    src/loki_languages.c
    src/loki_selection.c
    src/loki_search.c
    src/loki_modal.c
    src/loki_command.c
    src/loki_terminal.c
    src/loki_undo.c
    src/loki_buffers.c
    src/loki_markdown.c
)

target_include_directories(libloki
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIR}
        ${CURL_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cmark/src
)

target_compile_options(libloki PRIVATE -Wall -Wextra -pedantic)

target_link_libraries(libloki
    PUBLIC
        ${LUA_LIBRARIES}
        CURL::libcurl
        Threads::Threads
        ${CMAKE_DL_LIBS}
        cmark
)

if (NOT MSVC)
    target_link_libraries(libloki PUBLIC m)
endif()

add_executable(loki_editor src/main_editor.c)
target_compile_options(loki_editor PRIVATE -Wall -Wextra -pedantic)
target_link_libraries(loki_editor PRIVATE libloki)
set_target_properties(loki_editor PROPERTIES OUTPUT_NAME "loki-editor")

add_executable(loki_repl src/main_repl.c)
target_include_directories(loki_repl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(loki_repl PRIVATE -Wall -Wextra -pedantic)
target_link_libraries(loki_repl PRIVATE libloki)
set_target_properties(loki_repl PROPERTIES OUTPUT_NAME "loki-repl")

if (LINEEDIT_FOUND)
    target_include_directories(loki_repl PRIVATE ${LINEEDIT_INCLUDE_DIR})
    target_link_libraries(loki_repl PRIVATE ${LINEEDIT_LIBRARY})
    if (LINEEDIT_TYPE STREQUAL "editline")
        target_compile_definitions(loki_repl PRIVATE LOKI_HAVE_EDITLINE=1)
    else()
        target_compile_definitions(loki_repl PRIVATE LOKI_HAVE_READLINE=1)
    endif()
    message(STATUS "loki-repl will use ${LINEEDIT_TYPE} for line editing (history, completion)")
endif()

add_custom_target(show-config
    COMMAND ${CMAKE_COMMAND} -E echo "Lua include: ${LUA_INCLUDE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "Lua libraries: ${LUA_LIBRARIES}"
    COMMAND ${CMAKE_COMMAND} -E echo "libcurl include: ${CURL_INCLUDE_DIRS}"
    COMMAND ${CMAKE_COMMAND} -E echo "libcurl libraries: ${CURL_LIBRARIES}"
    COMMAND ${CMAKE_COMMAND} -E echo "Line editing: ${LINEEDIT_TYPE}"
    COMMAND ${CMAKE_COMMAND} -E echo "Line edit library: ${LINEEDIT_LIBRARY}"
)

enable_testing()
add_test(NAME loki_editor_version COMMAND $<TARGET_FILE:loki_editor> --version)
add_test(NAME loki_repl_version COMMAND $<TARGET_FILE:loki_repl> --version)

# Test framework library
add_library(test_framework STATIC tests/test_framework.c)
target_include_directories(test_framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Unit tests for core functionality
add_executable(test_core tests/test_core.c)
target_include_directories(test_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_core PRIVATE libloki test_framework)
add_test(NAME test_core COMMAND test_core)

# Integration tests for file I/O
add_executable(test_file_io tests/test_file_io.c)
target_include_directories(test_file_io PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_file_io PRIVATE libloki test_framework)
add_test(NAME test_file_io COMMAND test_file_io)

# Integration tests for Lua API
add_executable(test_lua_api tests/test_lua_api.c)
target_include_directories(test_lua_api PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(test_lua_api PRIVATE libloki test_framework)
add_test(NAME test_lua_api COMMAND test_lua_api)

# Unit tests for language registration helpers
add_executable(test_lang_registration tests/test_lang_registration.c)
target_include_directories(test_lang_registration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(test_lang_registration PRIVATE libloki test_framework)
add_test(NAME test_lang_registration COMMAND test_lang_registration)

# Unit tests for HTTP security features
add_executable(test_http_security tests/test_http_security.c)
target_include_directories(test_http_security PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(test_http_security PRIVATE libloki test_framework)
add_test(NAME test_http_security COMMAND test_http_security)

# Unit tests for modal editing (vim-like modes)
add_executable(test_modal tests/test_modal.c)
target_include_directories(test_modal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(test_modal PRIVATE libloki test_framework)
add_test(NAME test_modal COMMAND test_modal)

# Unit tests for syntax highlighting
add_executable(test_syntax tests/test_syntax.c)
target_include_directories(test_syntax PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(test_syntax PRIVATE libloki test_framework)
add_test(NAME test_syntax COMMAND test_syntax)

# Unit tests for search functionality
add_executable(test_search tests/test_search.c)
target_include_directories(test_search PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(test_search PRIVATE libloki test_framework)
add_test(NAME test_search COMMAND test_search)

# Unit tests for buffer management
add_executable(test_buffers tests/test_buffers.c)
target_include_directories(test_buffers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(test_buffers PRIVATE libloki test_framework)
add_test(NAME test_buffers COMMAND test_buffers)
