cmake_minimum_required(VERSION 3.10)
project(TextEditor C)

set(CMAKE_C_STANDARD 99)

# 包含头文件目录
include_directories(include)

# 源文件列表
set(SOURCES
        src/Main/main.c
        src/terminal.c
        src/render.c
        src/editor.c
        src/input.c
        src/fileio.c
        src/syntax.c
        src/common.c
        src/undo.c
)

# 头文件列表（在某些 IDE 如 Visual Studio 中显示用）
set(HEADERS
        include/common.h
        include/render.h
        include/terminal.h
        include/input.h
        include/editor.h
        include/fileio.h
        include/syntax.h
        include/undo.h
)

add_executable(TextEditor ${SOURCES} ${HEADERS})

# 如果是 Windows，链接kernel32
if(WIN32)
    target_link_libraries(TextEditor kernel32)
endif()