find_package(SDL2)

set(player_SRCS 
                Audio_Scope.cpp
                Audio_Scope.h
                Music_Player.cpp
                Music_Player.h
                # 04_key_presses.cpp)
                player.cpp)
if(SDL2_FOUND)

    # message(FATAL_ERROR "SDL2_STATIC_LIBRARIES = ${SDL2_STATIC_LIBRARIES}")
    message(" ** SDL2 library located, player demo is available to be built in the /player directory")

    include_directories(${SDL2_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
        "${CMAKE_HOME_DIRECTORY}" "${CMAKE_HOME_DIRECTORY}/gme"
        "${CMAKE_BINARY_DIR}/gme")

    add_executable(gme_player ${player_SRCS})
    target_link_libraries(gme_player ${SDL2_STATIC_LIBRARIES} gme)

    # Is not to be installed though
else()
    message("SDL2 library not found, disabling player demo build")
endif()
