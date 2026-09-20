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

#ifndef EXTORA_HTTP_EXAMPLE_OBJECTHANDLERS_H
#define EXTORA_HTTP_EXAMPLE_OBJECTHANDLERS_H

#include <optional>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/fields.hpp>

#include <HttpRoute.h>
#include <HttpStorage.h>
#include <HttpStreams.h>

namespace extoraHttpExample {

using Tcp = boost::asio::ip::tcp;

void handleListObjects(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                       boost::asio::yield_context yield);

void handleListObjectVersions(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                              boost::asio::yield_context yield);

void handlePutObject(Tcp::socket& socket, boost::beast::flat_buffer& buffer, RequestParser& parser, AsyncStore& store,
                     const Route& route, boost::asio::yield_context yield);

void handleCopyObject(Tcp::socket& socket, const boost::beast::http::fields& fields, AsyncStore& store,
                      unsigned version, const Route& route, boost::asio::yield_context yield);

void handleHeadObject(Tcp::socket& socket, const boost::beast::http::fields& fields, AsyncStore& store,
                      unsigned version, const Route& route, const std::optional<extora::object_version_id>& versionId,
                      boost::asio::yield_context yield);

void handleGetObject(Tcp::socket& socket, const boost::beast::http::fields& fields, AsyncStore& store, unsigned version,
                     const Route& route, const std::optional<extora::object_version_id>& versionId,
                     boost::asio::yield_context yield);

void handleDeleteObject(Tcp::socket& socket, AsyncStore& store, unsigned version, const Route& route,
                        const std::optional<extora::object_version_id>& versionId, boost::asio::yield_context yield);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_OBJECTHANDLERS_H
