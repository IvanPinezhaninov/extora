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

#ifndef EXTORA_HTTP_EXAMPLE_SERVER_H
#define EXTORA_HTTP_EXAMPLE_SERVER_H

#include <cstdint>
#include <memory>
#include <string_view>

namespace extora {
class managed_object_store;
} // namespace extora

namespace extoraHttpExample {

int runServer(std::shared_ptr<extora::managed_object_store> store, std::string_view address, std::uint16_t port);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_SERVER_H
