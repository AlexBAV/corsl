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

			template<class A, class V>
			void iset(V &&v) noexcept requires !std::same_as<void, A>
			{
				promise_->return_value(std::forward<V>(v));
			}

			template<class A>
			void iset() noexcept requires std::same_as<void, A>
			{
				promise_->return_void();
			}

			template<class A, class V>
			void iset_async(V &&v) noexcept requires !std::same_as<void, A>
			{
				promise_->return_value_async(std::forward<V>(v));
			}

			template<class A>
			void iset_async() noexcept requires std::same_as<void, A>
			{
				promise_->return_void_async();
			}
		public:
			promise() = default;

			void set() noexcept
			{
				iset<T>();
			}

			void set_async() noexcept
			{
				iset_async<T>();
			}

			template<class V>
			void set(V &&v) noexcept
			{
				iset<T>(std::forward<V>(v));
			}

			template<class V>
			void set_async(V &&v) noexcept
			{
				iset_async<T>(std::forward<V>(v));
			}

			void set_exception(std::exception_ptr &&ex) noexcept
			{
				promise_->internal_set_exception(std::move(ex));
			}

			future<T> get_future() const noexcept
			{
				return promise_->get_return_object();
			}
		};
	}

	using details::promise;
}
