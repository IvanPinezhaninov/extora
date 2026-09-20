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

#ifndef EXTORA_HTTP_EXAMPLE_HTTPCONDITIONS_H
#define EXTORA_HTTP_EXAMPLE_HTTPCONDITIONS_H

#include <cstdint> // IWYU pragma: keep
#include <string>
#include <string_view>

#include <boost/beast/http/fields.hpp>

#include <extora/object_types.h>
#include <extora/storage_error.h>

namespace extoraHttpExample {

bool parseObjectMetadata(const boost::beast::http::fields& fields, extora::object_metadata& metadata,
                         std::string& errorMessage);

bool parseObjectConditions(const boost::beast::http::fields& fields, std::string_view headerPrefix,
                           extora::object_conditions& conditions, std::string& errorMessage);

bool hasObjectConditions(const boost::beast::http::fields& fields);

extora::storage_error checkObjectConditions(const boost::beast::http::fields& fields,
                                            const extora::object_info& object);

} // namespace extoraHttpExample

#endif // EXTORA_HTTP_EXAMPLE_HTTPCONDITIONS_H
