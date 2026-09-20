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

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include "HttpServer.h"
#include "extora/extora.h"

namespace {

bool parseUnsigned(const char* text, std::uint64_t& result)
{
  if (text == nullptr || *text == '\0' || *text == '-') return false;

  char* end = nullptr;
  errno = 0;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value > (std::numeric_limits<std::uint64_t>::max)()) return false;

  result = static_cast<std::uint64_t>(value);
  return true;
}

bool parsePort(const char* text, std::uint16_t& port)
{
  std::uint64_t value = 0;
  if (!parseUnsigned(text, value) || value == 0 || value > (std::numeric_limits<std::uint16_t>::max)()) return false;

  port = static_cast<std::uint16_t>(value);
  return true;
}

} // namespace

int main(int argc, char* argv[])
{
  if (argc > 5) {
    std::cerr << "Usage: " << argv[0] << " [address] [port] [storage-directory] [multipart-min-part-size]" << std::endl;
    return EXIT_FAILURE;
  }

  const std::string address = argc > 1 ? argv[1] : "127.0.0.1";
  std::uint16_t port = 8080;
  if (argc > 2 && !parsePort(argv[2], port)) {
    std::cerr << "Invalid port: " << argv[2] << std::endl;
    return EXIT_FAILURE;
  }

  extora::object_store_options options;
  options.root_directory = argc > 3 ? argv[3] : "extora-http-server-store";
  if (argc > 4 && !parseUnsigned(argv[4], options.multipart_min_part_size)) {
    std::cerr << "Invalid multipart minimum part size: " << argv[4] << std::endl;
    return EXIT_FAILURE;
  }

  extora::storage_error error;
  std::shared_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  if (extora::failed(error)) {
    std::cerr << "Failed to open store: " << error.message << std::endl;
    return EXIT_FAILURE;
  }

  return extoraHttpExample::runServer(std::move(store), address, port);
}
