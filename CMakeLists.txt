cmake_minimum_required(VERSION 3.10)

project(pspeanut-gb)

file(GLOB_RECURSE SRC src/*.cpp src/*.h)

add_executable(${PROJECT_NAME} ${SRC})

if(PLATFORM_PSP)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        pspdebug
        pspdisplay
        pspctrl
        pspge
        pspgu
        pspgum
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