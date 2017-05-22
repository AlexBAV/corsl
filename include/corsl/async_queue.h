//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/dependencies.h"
#include "compatible_base.h"
#include <queue>

#include "srwlock.h"

namespace corsl
{
	namespace details
	{
		template<class T>
		class async_queue
		{
			struct awaitable
			{
				std::experimental::coroutine_handle<> handle;
				async_queue *master;
				T value;

				awaitable(async_queue *master) noexcept :
					master{ master }
				{}

				void set_result(T &&value_) noexcept
				{
					value = std::move(value_);
				}

				void resume() noexcept
				{
					handle();
				}

				bool await_ready() noexcept
				{
					return master->is_ready(value);
				}

				bool await_suspend(std::experimental::coroutine_handle<> handle_)
				{
					handle = handle_;
					return master->set_awaitable(this);
				}

				T await_resume()
				{
					return std::move(value);
				}
			};

			srwlock queue_lock;
			std::queue<T> queue;
			awaitable *current{ nullptr };

			bool is_ready(T &value)
			{
				if (!queue.empty())
				{
					std::unique_lock<srwlock> l(queue_lock);
					if (!queue.empty())
					{
						value = std::move(queue.front());
						queue.pop();
						return true;
					}
				}
				return false;
			}

			bool set_awaitable(awaitable *pointer)
			{
				std::unique_lock<srwlock> l(queue_lock);
				if (!queue.empty())
				{
					pointer->set_result(std::move(queue.front()));
					queue.pop();
					return false;
				}
				current = pointer;
				return true;
			}

			winrt::fire_and_forget drain(std::unique_lock<srwlock> lock)
			{
				co_await corsl::resume_background{};
				auto v = std::move(queue.front());
				queue.pop();
				auto cur = current;
				current = nullptr;
				lock.unlock();
				cur->set_result(std::move(v));
				cur->resume();
			}

		public:
			void push(T &&item)
			{
				std::unique_lock<srwlock> l(queue_lock);
				queue.emplace(std::move(item));
				if (current)
					drain(std::move(l));
			}

			template<class...Args>
			void emplace(Args &&...args)
			{
				push(T(std::forward<Args>(args)...));
			}

			awaitable next() noexcept
			{
				return{ this };
			}
		};
	}

	using details::async_queue;
}
