cmake_minimum_required(VERSION 4.3)
project(8_bit_interpretor C)

set(CMAKE_C_STANDARD 11)

add_executable(8_bit_interpretor cpu.c
        cpu.h
        sound.c
        sound.h
        memory_directory.c
        memory_directory.h
        main.c
)

add_subdirectory(external/SDL EXCLUDE_FROM_ALL)
target_link_libraries(8_bit_interpretor PRIVATE SDL3::SDL3)