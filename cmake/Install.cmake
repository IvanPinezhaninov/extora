#============================================================================
#
# Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
#
# This file is part of the extora which can be found at
# https://github.com/IvanPinezhaninov/extora/.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
# THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
#============================================================================

if(extora_build_static_variant)
  install(TARGETS extora_static
    EXPORT extoraTargets
    FILE_SET public_headers
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
      COMPONENT Development
    ARCHIVE
      DESTINATION ${CMAKE_INSTALL_LIBDIR}
      COMPONENT Development
  )
endif()

if(extora_build_shared_variant)
  install(TARGETS extora_shared
    EXPORT extoraTargets
    FILE_SET public_headers
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
      COMPONENT Development
    ARCHIVE
      DESTINATION ${CMAKE_INSTALL_LIBDIR}
      COMPONENT Development
    LIBRARY
      DESTINATION ${CMAKE_INSTALL_LIBDIR}
      COMPONENT Runtime
      NAMELINK_COMPONENT Development
    RUNTIME
      DESTINATION ${CMAKE_INSTALL_BINDIR}
      COMPONENT Runtime
  )
endif()

set(extora_install_asio_adapter OFF)
if(TARGET extora_asio)
  set(extora_install_asio_adapter ON)
  install(TARGETS extora_asio
    EXPORT extoraTargets
    FILE_SET public_headers
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
      COMPONENT Development
    ARCHIVE
      DESTINATION ${CMAKE_INSTALL_LIBDIR}
      COMPONENT Development
  )
endif()

if(extora_build_shared_variant)
  set(extora_license_install_component Runtime)
else()
  set(extora_license_install_component Development)
endif()

install(FILES
  LICENSE
  NOTICE.md
  DESTINATION ${EXTORA_INSTALL_LICENSEDIR}
  COMPONENT ${extora_license_install_component}
)

install(FILES
  LICENSES/xxhash-BSD-2-Clause.txt
  DESTINATION ${EXTORA_INSTALL_LICENSEDIR}/LICENSES
  COMPONENT ${extora_license_install_component}
)

configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/extoraConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/extoraConfig.cmake"
  INSTALL_DESTINATION "${EXTORA_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/extoraConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMinorVersion
)

install(EXPORT extoraTargets
  FILE extoraTargets.cmake
  NAMESPACE extora::
  DESTINATION "${EXTORA_INSTALL_CMAKEDIR}"
  COMPONENT Development
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/extoraConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/extoraConfigVersion.cmake"
  DESTINATION "${EXTORA_INSTALL_CMAKEDIR}"
  COMPONENT Development
)
