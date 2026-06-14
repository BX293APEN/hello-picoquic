#----------------------------------------------------------------
# Generated CMake target import file for configuration "MinSizeRel".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "picoquic::picoquic-log" for configuration "MinSizeRel"
set_property(TARGET picoquic::picoquic-log APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(picoquic::picoquic-log PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "C:/Program Files (x86)/hello_quic/lib/picoquic-log.lib"
  )

list(APPEND _cmake_import_check_targets picoquic::picoquic-log )
list(APPEND _cmake_import_check_files_for_picoquic::picoquic-log "C:/Program Files (x86)/hello_quic/lib/picoquic-log.lib" )

# Import target "picoquic::picoquic-core" for configuration "MinSizeRel"
set_property(TARGET picoquic::picoquic-core APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(picoquic::picoquic-core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "C:/Program Files (x86)/hello_quic/lib/picoquic-core.lib"
  )

list(APPEND _cmake_import_check_targets picoquic::picoquic-core )
list(APPEND _cmake_import_check_files_for_picoquic::picoquic-core "C:/Program Files (x86)/hello_quic/lib/picoquic-core.lib" )

# Import target "picoquic::picotls-core" for configuration "MinSizeRel"
set_property(TARGET picoquic::picotls-core APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(picoquic::picotls-core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "C:/Program Files (x86)/hello_quic/lib/picotls-core.lib"
  )

list(APPEND _cmake_import_check_targets picoquic::picotls-core )
list(APPEND _cmake_import_check_files_for_picoquic::picotls-core "C:/Program Files (x86)/hello_quic/lib/picotls-core.lib" )

# Import target "picoquic::picotls-minicrypto" for configuration "MinSizeRel"
set_property(TARGET picoquic::picotls-minicrypto APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(picoquic::picotls-minicrypto PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "C:/Program Files (x86)/hello_quic/lib/picotls-minicrypto.lib"
  )

list(APPEND _cmake_import_check_targets picoquic::picotls-minicrypto )
list(APPEND _cmake_import_check_files_for_picoquic::picotls-minicrypto "C:/Program Files (x86)/hello_quic/lib/picotls-minicrypto.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
