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

set(CPACK_GENERATOR "DEB;RPM;TGZ;ZIP")
set(CPACK_PACKAGE_NAME "extora")
set(CPACK_PACKAGE_VENDOR "Ivan Pinezhaninov")
set(CPACK_PACKAGE_CONTACT "Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Embedded object storage for C++17")
set(CPACK_PACKAGE_DESCRIPTION
  "Extora is an embedded object storage library for C++17 applications."
)
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/IvanPinezhaninov/extora")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_RELOCATABLE OFF)
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ
  OWNER_WRITE
  OWNER_EXECUTE
  GROUP_READ
  GROUP_EXECUTE
  WORLD_READ
  WORLD_EXECUTE
)

set(CPACK_COMPONENTS_ALL Development)
if(extora_build_shared_variant)
  list(INSERT CPACK_COMPONENTS_ALL 0 Runtime)
endif()

set(extora_runtime_package_description "Extora shared library.")
set(extora_development_package_description
  "Extora C++ headers and CMake integration files."
)

set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "${extora_runtime_package_description}")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION "${extora_development_package_description}")

set(extora_debian_runtime_package_name "libextora${extora_abi_version}")
set(extora_rpm_runtime_package_name "extora")

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SOURCE "extora")

set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "${extora_debian_runtime_package_name}")
set(CPACK_DEBIAN_RUNTIME_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SHLIBDEPS ON)

set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME "libextora-dev")
set(CPACK_DEBIAN_DEVELOPMENT_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_SECTION "libdevel")

set(extora_debian_development_depends)
if(extora_build_shared_variant)
  set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
  list(APPEND extora_debian_development_depends
    "${extora_debian_runtime_package_name} (= ${PROJECT_VERSION}-1)"
  )
endif()

if(extora_sqlite_external)
  list(APPEND extora_debian_development_depends "libsqlite3-dev")
endif()
if(extora_xxhash_external)
  list(APPEND extora_debian_development_depends "libxxhash-dev")
endif()

if(extora_debian_development_depends)
  string(REPLACE
    ";"
    ", "
    CPACK_DEBIAN_DEVELOPMENT_PACKAGE_DEPENDS
    "${extora_debian_development_depends}"
  )
endif()

set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")

set(CPACK_RPM_RUNTIME_PACKAGE_NAME "${extora_rpm_runtime_package_name}")
set(CPACK_RPM_RUNTIME_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_RUNTIME_PACKAGE_SUMMARY "Extora shared library")
set(CPACK_RPM_RUNTIME_PACKAGE_GROUP "System Environment/Libraries")
set(CPACK_RPM_RUNTIME_PACKAGE_DESCRIPTION "${extora_runtime_package_description}")

set(CPACK_RPM_DEVELOPMENT_PACKAGE_NAME "extora-devel")
set(CPACK_RPM_DEVELOPMENT_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_DEVELOPMENT_PACKAGE_SUMMARY "Extora development files")
set(CPACK_RPM_DEVELOPMENT_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_DEVELOPMENT_PACKAGE_DESCRIPTION "${extora_development_package_description}")

set(extora_rpm_development_requires)
if(extora_build_shared_variant)
  list(APPEND extora_rpm_development_requires
    "${extora_rpm_runtime_package_name} = ${PROJECT_VERSION}-1"
  )
endif()

if(extora_sqlite_external)
  list(APPEND extora_rpm_development_requires "sqlite-devel")
endif()
if(extora_xxhash_external)
  list(APPEND extora_rpm_development_requires "xxhash-devel")
endif()

if(extora_rpm_development_requires)
  string(REPLACE
    ";"
    ", "
    CPACK_RPM_DEVELOPMENT_PACKAGE_REQUIRES
    "${extora_rpm_development_requires}"
  )
endif()

set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

include(CPack)
