cmake_minimum_required(VERSION 3.20)

set(MCU atmega328p)
set(F_CPU 16000000UL)

set(CMAKE_C_COMPILER avr-gcc)
set(CMAKE_OBJCOPY avr-objcopy)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(PORT /dev/ttyACM0)

project(engine-sim C)

add_subdirectory(lib/aurum)

add_executable(${PROJECT_NAME}
    src/main.c
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        aurum
)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -mmcu=${MCU}
    -DF_CPU=${F_CPU}
    -Os
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)

target_link_options(${PROJECT_NAME} PRIVATE
    -mmcu=${MCU}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        -O ihex
        -R .eeprom
        $<TARGET_FILE:${PROJECT_NAME}>
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMENT "Generating ${PROJECT_NAME}.hex"
)

add_custom_target(flash
    COMMAND avrdude
        -p ${MCU}
        -c arduino
        -P ${PORT}
        -b 115200
        -U flash:w:${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex:i
    DEPENDS ${PROJECT_NAME}
    COMMENT "Flashing ${MCU}"
)

# add_custom_target(monitor
#     COMMAND picocom
#         -b 9600
#         ${PORT}
#     COMMENT "Opening serial monitor on ${PORT}"
# )