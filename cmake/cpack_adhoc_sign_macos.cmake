# Sign the final staged bundle after CPack has installed all of its files.
set(_app "${CPACK_TEMPORARY_DIRECTORY}/CoreDeck.app")
# DragNDrop stages component installs under ALL_IN_ONE when components are
# grouped (as with CoreDeck's CPACK_COMPONENTS_ALL configuration).
if(NOT IS_DIRECTORY "${_app}")
    set(_app "${CPACK_TEMPORARY_DIRECTORY}/ALL_IN_ONE/CoreDeck.app")
endif()
if(NOT IS_DIRECTORY "${_app}")
    message(FATAL_ERROR "Cannot find CoreDeck.app in the CPack staging root or ALL_IN_ONE: ${CPACK_TEMPORARY_DIRECTORY}")
endif()
message(STATUS "Ad-hoc signing staged bundle: ${_app}")

# Sign embedded executables and libraries first, including crashpad_handler.
file(GLOB_RECURSE _files LIST_DIRECTORIES false "${_app}/Contents/*")
foreach(_file IN LISTS _files)
    if(IS_SYMLINK "${_file}")
        continue()
    endif()
    execute_process(
        COMMAND /usr/bin/file -b "${_file}"
        OUTPUT_VARIABLE _type
        COMMAND_ERROR_IS_FATAL ANY
    )
    if(_type MATCHES "Mach-O")
        execute_process(
            COMMAND /usr/bin/codesign --force --sign - --timestamp=none "${_file}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
endforeach()

execute_process(
    COMMAND /usr/bin/codesign --force --sign - --timestamp=none "${_app}"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2 "${_app}"
    COMMAND_ERROR_IS_FATAL ANY
)
