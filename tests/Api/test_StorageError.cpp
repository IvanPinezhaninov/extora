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

#include <gtest/gtest.h>

#include "extora/storage_error.h"

TEST(StorageErrorTest, ReportsSuccessAndFailure)
{
  const extora::storage_error success;
  EXPECT_TRUE(extora::succeeded(success));
  EXPECT_FALSE(extora::failed(success));

  const extora::storage_error failure{extora::storage_error_code::backend_failure, "failure"};
  EXPECT_TRUE(extora::failed(failure));
  EXPECT_FALSE(extora::succeeded(failure));
}

TEST(StorageErrorTest, PreservesMachineReadableCodeAndDiagnostic)
{
  const extora::storage_error error =
      extora::make_error(extora::storage_error_code::invalid_range, "requested range is invalid");
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_range);
  EXPECT_EQ(error.message, "requested range is invalid");
}
