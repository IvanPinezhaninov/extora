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

#ifndef EXTORA_HTTP_EXAMPLE_BUCKETHANDLERS_H
#define EXTORA_HTTP_EXAMPLE_BUCKETHANDLERS_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>

#include <HttpRoute.h>
#include <HttpStorage.h>

namespace extoraHttpExample {

using Tcp = boost::asio::ip::tcp;

void handleListBuckets(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                       boost::asio::yield_context yield);

void handleGetBucketVersioning(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                               boost::asio::yield_context yield);

void handleGetBucketUsage(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                          boost::asio::yield_context yield);

void handlePutBucketVersioning(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                               extora::bucket_versioning_configuration configuration, boost::asio::yield_context yield);

void handleGetReclamationEstimate(Tcp::socket& socket, AsyncStore& store, unsigned version,
                                  boost::asio::yield_context yield);

void handleReclaimStorage(Tcp::socket& socket, AsyncStore& store, unsigned version, boost::asio::yield_context yield);

void handleCompactStorage(Tcp::socket& socket, AsyncStore& store, unsigned version, boost::asio::yield_context yield);

void handleCreateBucket(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                        boost::asio::yield_context yield);

void handleHeadBucket(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                      boost::asio::yield_context yield);

void handleDeleteBucket(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                        boost::asio::yield_context yield);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_BUCKETHANDLERS_H
