# Sign the final staged bundle after CPack has installed all of its files.
set(_app "${CPACK_TEMPORARY_DIRECTORY}/CoreDeck.app")
if(NOT IS_DIRECTORY "${_app}")
    message(FATAL_ERROR "Cannot ad-hoc sign missing bundle: ${_app}")
endif()

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
