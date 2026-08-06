cmake_minimum_required(VERSION 4.3)
project(8_bit_interpretor C)

set(CMAKE_C_STANDARD 11)

add_executable(8_bit_interpretor cpu.c
        cpu.h
        sound.c
        sound.h
        memory_directory.c
        main.c
)
