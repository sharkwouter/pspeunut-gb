cmake_minimum_required(VERSION 3.10)

project(pspeanut-gb)

file(GLOB_RECURSE SRC src/*.cpp)

add_executable(${PROJECT_NAME} ${SRC} Peanut-GB/peanut_gb.h)

if(PLATFORM_PSP)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        pspdebug
        pspdisplay
        pspctrl
        pspge
        pspgu
        pspvram
    )
    create_pbp_file(
        TARGET ${PROJECT_NAME}
        ICON_PATH NULL
        BACKGROUND_PATH NULL
        PREVIEW_PATH NULL
        TITLE ${PROJECT_NAME}
    )
endif()