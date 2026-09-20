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

#ifndef EXTORA_OBJECT_STORE_FACTORY_H
#define EXTORA_OBJECT_STORE_FACTORY_H

#include <memory>

#include <extora/export.h>
#include <extora/managed_object_store.h>
#include <extora/object_store_options.h>
#include <extora/storage_error.h>

namespace extora {

/**
 * @brief Opens or creates an object store.
 *
 * On failure, returns nullptr and sets @p error. Caller-owned factories and
 * observers in @p options must outlive the store.
 *
 * @param[in]  options @ref object_store_options for the store.
 * @param[out] error   Receives the failure.
 * @return Open @ref managed_object_store instance, or nullptr on failure.
 */
[[nodiscard]] EXTORA_API std::unique_ptr<managed_object_store> open_object_store(const object_store_options& options,
                                                                                 storage_error& error);

} // namespace extora

#endif // EXTORA_OBJECT_STORE_FACTORY_H
