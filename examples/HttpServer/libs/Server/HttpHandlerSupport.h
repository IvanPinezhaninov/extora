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

#ifndef EXTORA_HTTP_EXAMPLE_HTTPHANDLERSUPPORT_H
#define EXTORA_HTTP_EXAMPLE_HTTPHANDLERSUPPORT_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

#include <extora/object_store.h>

#include <HttpConstants.h>
#include <HttpProtocol.h>
#include <HttpRoute.h>
#include <HttpStreams.h>

namespace extoraHttpExample {

using Tcp = boost::asio::ip::tcp;

bool hasQuerySelector(std::string_view query, std::string_view selector);
std::string_view queryAfterSelector(std::string_view query, std::string_view selector);
std::string toString(boost::beast::string_view value);

template<typename Body>
void setCommonHeaders(boost::beast::http::response<Body>& response)
{
  response.set(boost::beast::http::field::server, serverName);
  response.keep_alive(response.version() >= 11);
}

template<typename Body>
void setDateHeader(boost::beast::http::response<Body>& response, boost::beast::http::field field,
                   std::chrono::system_clock::time_point value)
{
  const std::optional<std::string> formatted = formatHttpDate(value);
  if (formatted.has_value()) response.set(field, *formatted);
}

template<typename Body>
void setObjectHeaders(boost::beast::http::response<Body>& response, const extora::object_info& info)
{
  response.set(boost::beast::http::field::etag, '"' + info.etag + '"');
  if (info.content_type.has_value()) response.set(boost::beast::http::field::content_type, *info.content_type);
  if (info.cache_control.has_value()) response.set(boost::beast::http::field::cache_control, *info.cache_control);
  if (info.content_disposition.has_value())
    response.set(boost::beast::http::field::content_disposition, *info.content_disposition);
  if (info.content_encoding.has_value())
    response.set(boost::beast::http::field::content_encoding, *info.content_encoding);
  if (info.content_language.has_value())
    response.set(boost::beast::http::field::content_language, *info.content_language);
  if (info.expires_at.has_value()) setDateHeader(response, boost::beast::http::field::expires, *info.expires_at);
  setDateHeader(response, boost::beast::http::field::last_modified, info.modified_at);
  response.set(versionIdHeader, info.version_id.value);
  response.set(deleteMarkerHeader, info.is_delete_marker ? "true" : "false");
  for (const extora::metadata_entry& metadata : info.custom_metadata)
    response.set(std::string{customMetadataHeaderPrefix} + metadata.name, metadata.value);
  if (info.checksum.has_value()) {
    response.set(checksumAlgorithmHeader, info.checksum->checksum_algorithm.value);
    response.set(checksumHeader, info.checksum->value);
    response.set(checksumTypeHeader,
                 info.checksum->type == extora::object_checksum_type::composite ? "composite" : "full_object");
  }
}

bool writeJsonResponse(Tcp::socket& socket, unsigned version, boost::beast::http::status status, std::string body,
                       boost::asio::yield_context yield);

bool writeHtmlResponse(Tcp::socket& socket, unsigned version, boost::beast::http::status status, std::string_view body,
                       boost::asio::yield_context yield);

bool writeFaviconResponse(Tcp::socket& socket, unsigned version, bool headOnly, boost::asio::yield_context yield);

bool writeEmptyResponse(Tcp::socket& socket, unsigned version, boost::beast::http::status status,
                        boost::asio::yield_context yield);

bool writeErrorResponse(Tcp::socket& socket, unsigned version, boost::beast::http::status status, std::string_view code,
                        std::string_view message, bool headOnly, boost::asio::yield_context yield);

bool writeMethodNotAllowed(Tcp::socket& socket, unsigned version, bool headOnly, boost::asio::yield_context yield);

bool writeBucketInfoResponse(Tcp::socket& socket, unsigned version, const extora::bucket_info& info,
                             boost::asio::yield_context yield);

bool writeStorageError(Tcp::socket& socket, unsigned version, const extora::storage_error& error, bool headOnly,
                       boost::asio::yield_context yield);

bool writeNotModified(Tcp::socket& socket, unsigned version, const extora::object_info& info,
                      boost::asio::yield_context yield);

bool writeContinue(Tcp::socket& socket, unsigned version, boost::asio::yield_context yield);

bool expectsContinue(const boost::beast::http::fields& fields);

bool parseCopySource(const boost::beast::http::fields& fields, Route& sourceRoute,
                     std::optional<extora::object_version_id>& sourceVersionId, std::string& errorMessage);

bool parseCopyOptions(const boost::beast::http::fields& fields, extora::copy_object_options& options,
                      std::string& errorMessage);

extora::storage_error readCompletionBody(Tcp::socket& socket, boost::beast::flat_buffer& buffer, RequestParser& parser,
                                         boost::asio::yield_context yield, std::string& body);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_HTTPHANDLERSUPPORT_H
