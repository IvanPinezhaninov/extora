/******************************************************************************
**
** Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
**
** This file is part of the extora which can be found at
** https://github.com/IvanPinezhaninov/extora/.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
** DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <process.h>
#endif // defined(_WIN32)

#include <gtest/gtest.h>

#include "PublicTestSupport.h"

namespace extoraTest {

namespace {

#if defined(_WIN32)
constexpr char ownershipParentProcessIdVariable[] = "EXTORA_OWNERSHIP_PARENT_PROCESS_ID";
constexpr char ownershipRootVariable[] = "EXTORA_OWNERSHIP_ROOT";

bool readEnvironmentVariable(const char* name, std::string& value)
{
  char* buffer = nullptr;
  std::size_t size = 0;
  if (::_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) return false;

  value = buffer;
  std::free(buffer);
  return true;
}

bool isReexecutedDeathTestChild()
{
  std::string parentProcessId;
  return readEnvironmentVariable(ownershipParentProcessIdVariable, parentProcessId) &&
         parentProcessId != std::to_string(::_getpid());
}
#else
bool isReexecutedDeathTestChild()
{
  return false;
}
#endif // defined(_WIN32)

std::string makeCrossProcessRoot()
{
#if defined(_WIN32)
  if (isReexecutedDeathTestChild()) {
    std::string inheritedRoot;
    if (!readEnvironmentVariable(ownershipRootVariable, inheritedRoot)) return {};
    return inheritedRoot;
  }

  const std::string root = makeTempRoot();
  if (::_putenv_s(ownershipRootVariable, root.c_str()) != 0 ||
      ::_putenv_s(ownershipParentProcessIdVariable, std::to_string(::_getpid()).c_str()) != 0)
    return {};
  return root;
#else
  return makeTempRoot();
#endif // defined(_WIN32)
}

[[noreturn]] void runCompetingOwner(const extora::object_store_options& options)
{
  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  if (store) std::_Exit(20);
  if (error.code != extora::storage_error_code::storage_in_use) std::_Exit(21);
  std::_Exit(0);
}

} // namespace

TEST(StorageOwnershipTest, RejectsASecondOwnerOfTheSameRoot)
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  options.segment_capacity = 1024 * 1024;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> first = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(first);

  std::unique_ptr<extora::managed_object_store> second = extora::open_object_store(options, error);
  EXPECT_EQ(second, nullptr);
  EXPECT_EQ(error.code, extora::storage_error_code::storage_in_use);

  ASSERT_TRUE(succeeded(first->create_bucket(extora::bucket_name{"photos"})));
  first.reset();

  std::unique_ptr<extora::managed_object_store> reopened = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(reopened);
  extora::bucket_info info;
  EXPECT_TRUE(succeeded(reopened->head_bucket(extora::bucket_name{"photos"}, info)));
}

TEST(StorageOwnershipTest, RejectsRootPathThatIsARegularFile)
{
  const std::filesystem::path parent = makeTempRoot();
  ASSERT_TRUE(std::filesystem::create_directories(parent));
  const std::filesystem::path rootFile = parent / "storage-file";
#if defined(_MSC_VER)
  std::FILE* file = nullptr;
  ::_wfopen_s(&file, rootFile.c_str(), L"wb");
#elif defined(_WIN32)
  std::FILE* file = ::_wfopen(rootFile.c_str(), L"wb");
#else
  std::FILE* file = std::fopen(rootFile.c_str(), "wb");
#endif // defined(_MSC_VER)
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(std::fclose(file), 0);

  extora::object_store_options options;
  options.root_directory = rootFile;
  extora::storage_error error;
  EXPECT_EQ(extora::open_object_store(options, error), nullptr);
  EXPECT_EQ(error.code, extora::storage_error_code::backend_failure);
}

TEST(StorageOwnershipTest, RejectsAnOwnerInAnotherProcess)
{
  extora::object_store_options options;
  options.root_directory = makeCrossProcessRoot();
  options.segment_capacity = 1024 * 1024;
  ASSERT_FALSE(options.root_directory.empty());

  if (isReexecutedDeathTestChild()) runCompetingOwner(options);

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> first = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(first);

  ASSERT_EXIT(runCompetingOwner(options), testing::ExitedWithCode(0), "");
}

} // namespace extoraTest
