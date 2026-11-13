# Called by CPack's External generator with the staged app directory in
# CPACK_TEMPORARY_DIRECTORY. ares-package (from ares-cli-rs) turns it into an .ipk.
execute_process(
    COMMAND ares-package "${CPACK_TEMPORARY_DIRECTORY}" -o "${CPACK_PACKAGE_DIRECTORY}"
    RESULT_VARIABLE _ares_rc)
if(NOT _ares_rc EQUAL 0)
    message(FATAL_ERROR "ares-package failed with ${_ares_rc}")
endif()