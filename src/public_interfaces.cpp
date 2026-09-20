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

#include "extora/managed_object_store.h"
#include "extora/object_stream.h"
#include "extora/operation_observer.h"

namespace extora {

object_store::object_store() noexcept = default;

object_store::~object_store() noexcept = default;

managed_object_store::managed_object_store() noexcept = default;

managed_object_store::~managed_object_store() noexcept = default;

object_reader::object_reader() noexcept = default;

object_reader::~object_reader() noexcept = default;

object_writer::object_writer() noexcept = default;

object_writer::~object_writer() noexcept = default;

operation_observer::operation_observer() noexcept = default;

operation_observer::~operation_observer() noexcept = default;

} // namespace extora
