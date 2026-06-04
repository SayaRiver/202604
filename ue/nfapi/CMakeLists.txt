# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_compile_options(-Wunused-parameter)
add_subdirectory(oai_integration/aerial)
if (OAI_WLS)
    add_subdirectory(oai_integration/wls_integration)
else () # Not WLS, means its SOCKET-based comms, maybe add a flag specific for UDP
    message(STATUS "No Support for WLS")
    add_subdirectory(oai_integration/socket)
endif ()
add_subdirectory(open-nFAPI)
add_subdirectory(tests)
