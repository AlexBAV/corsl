//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "srwlock.h"
#include "promise.h"
#include "future.h"

#include <boost/iterator/iterator_facade.hpp>

namespace corsl
{
	namespace details
	{
		template<class T>
		class async_generator
		{
		public:
			struct promise_type
			{
				promise<T> current;

				void init_frame()
				{
					current = {};
				}

				template<class V>
				void yield_value(V &&value) noexcept
				{
					current.set(std::forward<V>(value));
				}

				auto get_future() const noexcept
				{
					return current.get_future();
				}

				static std::experimental::suspend_always initial_suspend() noexcept
				{
					return {};
				}

				static std::experimental::suspend_always final_suspend() noexcept
				{
					return {};
				}

				async_generator get_return_object() noexcept
				{
					return { std::experimental::coroutine_handle<promise_type>::from_promise(*this) };
				}
			};

			class iterator : public boost::iterator_facade<iterator, future<T>, std::input_iterator_tag, future<T>>
			{
				friend class boost::iterator_core_access;
				friend class async_generator;

				std::experimental::coroutine_handle<promise_type> coro;

				future<T> dereference() const noexcept
				{
					return coro.promise().get_future();
				}

				bool equal(const iterator &o) const noexcept
				{
					return coro == o.coro;
				}

				void increment() noexcept
				{
					coro.promise().init_frame();
					coro.resume();
					if (coro.done())
						coro = nullptr;
				}

				iterator(std::experimental::coroutine_handle<promise_type> coro) noexcept :
					coro{ coro }
				{}

				iterator() noexcept : coro{ nullptr }
				{}
			};

		private:
			std::experimental::coroutine_handle<promise_type> coro{ nullptr };

			async_generator(std::experimental::coroutine_handle<promise_type> coro) noexcept :
				coro{ coro }
			{}

			async_generator(const async_generator &) = delete;
			async_generator &operator =(const async_generator &) = delete;

		public:
			async_generator(async_generator &&o) noexcept :
				coro{ o.coro }
			{}

			async_generator &operator =(async_generator &&o) noexcept
			{
				std::swap(coro, o.coro);
				return *this;
			}

			~async_generator()
			{
				if (coro)
					coro.destroy();
			}

			//
			iterator begin() const noexcept
			{
				if (coro)
				{
					coro.resume();
					if (!coro.done())
						return { coro };
				}
				return {};
			}

			iterator end() const noexcept
			{
				return {};
			}
		};
	}

	using details::async_generator;
}
