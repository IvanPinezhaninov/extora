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

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <gtest/gtest.h>

#include "extora/asio/async_admission_limiter.h"

namespace extoraTest {
namespace {

using namespace std::chrono_literals;

TEST(AsyncAdmissionLimiterTest, SupportsImmediateNonBlockingAcquisition)
{
  boost::asio::io_context io;
  extora::asio::async_admission_limiter limiter{io.get_executor(), 1, 1, 1s};

  extora::asio::admission_permit first = limiter.try_acquire();
  EXPECT_TRUE(first.acquired());
  EXPECT_FALSE(limiter.try_acquire().acquired());

  first = {};
  EXPECT_TRUE(limiter.try_acquire().acquired());
}

TEST(AsyncAdmissionLimiterTest, FastPathHonorsTheLimitAcrossThreads)
{
  boost::asio::io_context io;
  constexpr int maxActive = 3;
  extora::asio::async_admission_limiter limiter{io.get_executor(), maxActive, 0, 1s};
  std::atomic<int> active{0};
  std::atomic<int> observedMaximum{0};
  std::vector<std::thread> threads;

  for (int threadIndex = 0; threadIndex < 8; ++threadIndex) {
    threads.emplace_back([&] {
      for (int attempt = 0; attempt < 5000; ++attempt) {
        extora::asio::admission_permit permit = limiter.try_acquire();
        if (!permit.acquired()) {
          std::this_thread::yield();
          continue;
        }
        const int current = ++active;
        int maximum = observedMaximum.load();
        while (maximum < current && !observedMaximum.compare_exchange_weak(maximum, current)) {}
        std::this_thread::yield();
        --active;
      }
    });
  }
  for (std::thread& thread : threads)
    thread.join();

  EXPECT_LE(observedMaximum.load(), maxActive);
  EXPECT_GT(observedMaximum.load(), 0);
}

TEST(AsyncAdmissionLimiterTest, DefersCompletionAndSupportsUseFuture)
{
  boost::asio::io_context io;
  extora::asio::async_admission_limiter limiter{io.get_executor(), 1, 1, 1s};

  std::future<extora::asio::admission_result> future =
      limiter.async_acquire(boost::asio::bind_executor(io.get_executor(), boost::asio::use_future));
  EXPECT_EQ(future.wait_for(0ms), std::future_status::timeout);

  io.run();
  extora::asio::admission_result result = future.get();
  EXPECT_TRUE(extora::succeeded(result.error)) << result.error.message;
  EXPECT_TRUE(result.permit.acquired());
}

TEST(AsyncAdmissionLimiterTest, AdmitsQueuedOperationsInFifoOrder)
{
  boost::asio::io_context io;
  extora::asio::async_admission_limiter limiter{io.get_executor(), 1, 2, 1s};
  extora::asio::admission_permit active = limiter.try_acquire();
  ASSERT_TRUE(active.acquired());
  std::vector<int> completionOrder;

  limiter.async_acquire([&](extora::asio::admission_result result) {
    ASSERT_TRUE(extora::succeeded(result.error)) << result.error.message;
    ASSERT_TRUE(result.permit.acquired());
    completionOrder.push_back(1);
  });
  limiter.async_acquire([&](extora::asio::admission_result result) {
    ASSERT_TRUE(extora::succeeded(result.error)) << result.error.message;
    ASSERT_TRUE(result.permit.acquired());
    completionOrder.push_back(2);
  });

  active = {};
  io.run();

  EXPECT_EQ(completionOrder, (std::vector<int>{1, 2}));
}

TEST(AsyncAdmissionLimiterTest, RejectsWhenTheQueueIsFull)
{
  boost::asio::io_context io;
  extora::asio::async_admission_limiter limiter{io.get_executor(), 1, 1, 1s};
  extora::asio::admission_permit active = limiter.try_acquire();
  ASSERT_TRUE(active.acquired());
  extora::storage_error queuedError;
  extora::storage_error rejectedError;

  limiter.async_acquire([&](extora::asio::admission_result result) { queuedError = std::move(result.error); });
  limiter.async_acquire([&](extora::asio::admission_result result) { rejectedError = std::move(result.error); });

  active = {};
  io.run();

  EXPECT_TRUE(extora::succeeded(queuedError)) << queuedError.message;
  EXPECT_EQ(rejectedError.code, extora::storage_error_code::concurrency_limit_exceeded);
}

TEST(AsyncAdmissionLimiterTest, TimesOutAQueuedOperation)
{
  boost::asio::io_context io;
  extora::asio::async_admission_limiter limiter{io.get_executor(), 1, 1, 5ms};
  extora::asio::admission_permit active = limiter.try_acquire();
  ASSERT_TRUE(active.acquired());

  std::future<extora::asio::admission_result> future = limiter.async_acquire(boost::asio::use_future);
  io.run();
  const extora::asio::admission_result result = future.get();

  EXPECT_EQ(result.error.code, extora::storage_error_code::concurrency_limit_exceeded);
  EXPECT_FALSE(result.permit.acquired());
}

TEST(AsyncAdmissionLimiterTest, HonorsAssociatedCancellationSlots)
{
  boost::asio::io_context io;
  extora::asio::async_admission_limiter limiter{io.get_executor(), 1, 1, 1s};
  extora::asio::admission_permit active = limiter.try_acquire();
  ASSERT_TRUE(active.acquired());
  boost::asio::cancellation_signal cancellation;
  extora::storage_error completionError;

  limiter.async_acquire(boost::asio::bind_cancellation_slot(
      cancellation.slot(), [&](extora::asio::admission_result result) { completionError = std::move(result.error); }));
  cancellation.emit(boost::asio::cancellation_type::total);
  io.run();

  EXPECT_EQ(completionError.code, extora::storage_error_code::operation_cancelled);
}

} // namespace
} // namespace extoraTest
