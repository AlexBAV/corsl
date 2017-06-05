//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "future.h"

namespace corsl
{
	namespace details
	{
		template<class T = void>
		class promise
		{
			using promise_type = promise_type_<T>;
			std::shared_ptr<promise_type> promise_{ std::make_shared<promise_type>() };

		public:
			promise() = default;

			template<class V>
			std::enable_if_t<!std::is_same<void, T>::value, void> set(V &&v) noexcept
			{
				promise_->return_value(std::forward<V>(v));
			}

			// compiler generates error C4716 in the following function if is_same_v used (????)
			std::enable_if_t<std::is_same<void, T>::value> set() noexcept
			{
				promise_->return_void();
			}

			void set_exception(std::exception_ptr &&ex) noexcept
			{
				promise_->set_exception(std::move(ex));
			}

			future<T> get_future() const noexcept
			{
				return promise_->get_return_object();
			}
		};
	}

	using details::promise;
}
