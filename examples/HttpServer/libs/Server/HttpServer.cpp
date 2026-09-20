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

#include "HttpServer.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/system/error_code.hpp>

#include "HttpSession.h"
#include "HttpStorage.h"
#include "extora/managed_object_store.h"

namespace extoraHttpExample {

namespace {

namespace net = boost::asio;

} // namespace

using Tcp = net::ip::tcp;

int runServer(std::shared_ptr<extora::managed_object_store> store, std::string_view address, std::uint16_t port)
{
  boost::system::error_code error;
  const net::ip::address listenAddress = net::ip::make_address(std::string{address}, error);
  if (error) {
    std::cerr << "Invalid listen address: " << error.message() << std::endl;
    return EXIT_FAILURE;
  }

  const unsigned hardwareThreadCount = (std::max)(2U, std::thread::hardware_concurrency());
  const unsigned workerThreadBudget = (std::min)(16U, hardwareThreadCount);
  const unsigned storageThreadCount = workerThreadBudget / 2;
  const unsigned ioThreadCount = workerThreadBudget - storageThreadCount;
  const std::size_t maxActiveReads = storageThreadCount * 64;
  const std::size_t maxActiveWrites = (std::max)(1U, storageThreadCount / 2);
  const std::size_t maxQueuedReads = maxActiveReads * 4;
  const std::size_t maxQueuedWrites = maxActiveWrites * 16;
  constexpr auto queueTimeout = std::chrono::seconds{1};
  net::io_context ioc{static_cast<int>(ioThreadCount)};
  net::strand<net::io_context::executor_type> controlStrand{ioc.get_executor()};
  net::thread_pool storagePool{storageThreadCount};
  AsyncStore asyncStore{std::move(store), ioc.get_executor(), storagePool.get_executor(),
                        maxActiveReads,   maxQueuedReads,     maxActiveWrites,
                        maxQueuedWrites,  queueTimeout};
  Tcp::acceptor acceptor{controlStrand};
  acceptor.open(listenAddress.is_v6() ? Tcp::v6() : Tcp::v4(), error);
  if (!error) acceptor.set_option(net::socket_base::reuse_address{true}, error);
  if (!error) acceptor.bind(Tcp::endpoint{listenAddress, port}, error);
  if (!error) acceptor.listen(net::socket_base::max_listen_connections, error);
  if (error) {
    std::cerr << "Failed to listen: " << error.message() << std::endl;
    return EXIT_FAILURE;
  }

  net::signal_set signals{controlStrand, SIGINT, SIGTERM};
  signals.async_wait([&](const boost::system::error_code&, int) {
    boost::system::error_code closeError;
    acceptor.close(closeError);
    ioc.stop();
  });

  net::spawn(
      controlStrand,
      [&](net::yield_context yield) {
        while (acceptor.is_open()) {
          Tcp::socket socket{ioc};
          boost::system::error_code acceptError;
          acceptor.async_accept(socket, yield[acceptError]);
          if (acceptError) {
            if (acceptor.is_open()) std::cerr << "Accept failed: " << acceptError.message() << std::endl;
            continue;
          }

          net::spawn(
              ioc,
              [connection = std::move(socket), &asyncStore](net::yield_context connYield) mutable {
                serveConnection(connection, asyncStore, connYield);
              },
              net::detached);
        }
      },
      net::detached);

  std::cout << "Listening on http://" << address << ':' << port << " using " << ioThreadCount << " I/O and "
            << storageThreadCount << " storage threads; admission limits: " << maxActiveReads << '+' << maxQueuedReads
            << " reads, " << maxActiveWrites << '+' << maxQueuedWrites << " writes" << std::endl;

  net::thread_pool ioPool{ioThreadCount};
  for (unsigned thread = 0; thread < ioThreadCount; ++thread)
    net::post(ioPool, [&ioc] { ioc.run(); });
  ioPool.join();
  storagePool.join();
  return EXIT_SUCCESS;
}

} // namespace extoraHttpExample
