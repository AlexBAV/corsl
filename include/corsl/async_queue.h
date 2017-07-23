//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include <queue>
#include <variant>

#include "impl/dependencies.h"
#include "compatible_base.h"
#include "srwlock.h"

namespace corsl
{
	namespace details
	{
		template<class T, class Queue = std::queue<T>>
		class async_queue
		{
			using queue_t = Queue;

			struct awaitable
			{
				std::experimental::coroutine_handle<> handle;
				async_queue *master;
				std::variant<std::exception_ptr, T> value;

				awaitable(async_queue *master) noexcept :
					master{ master }
				{}

				void set_result(T &&value_) noexcept
				{
					value = std::move(value_);
				}

				void set_exception(std::exception_ptr &&ptr) noexcept
				{
					value = std::move(ptr);
				}

				bool await_ready()
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
					if (value.index() == 0)
						std::rethrow_exception(std::get<std::exception_ptr>(std::move(value)));
					else
						return std::get<T>(std::move(value));
				}
			};

			srwlock queue_lock;
			queue_t queue;
			awaitable *current{ nullptr };
			bool is_cancelled{ false };

			bool is_ready(std::variant<std::exception_ptr, T> &value)
			{
				std::unique_lock<srwlock> l{ queue_lock };
				if (is_cancelled)
				{
					value = std::make_exception_ptr(operation_cancelled{});
					return true;
				}
				if (!queue.empty())
				{
					value = std::move(queue.front());
					queue.pop();
					return true;
				}
				return false;
			}

			bool set_awaitable(awaitable *pointer)
			{
				std::unique_lock<srwlock> l{ queue_lock };
				if (is_cancelled)
					throw operation_cancelled{};
				if (!queue.empty())
				{
					pointer->set_result(std::move(queue.front()));
					queue.pop();
					return false;
				}
				current = pointer;
				return true;
			}

			void drain(std::unique_lock<srwlock> &&lock)
			{
				if (current)
				{
					auto cur = std::exchange(current, nullptr);
					if (is_cancelled)
						cur->set_exception(std::make_exception_ptr(operation_cancelled{}));
					else
					{
						auto v = std::move(queue.front());
						cur->set_result(std::move(v));
						queue.pop();
					}
					resume_on_background(cur->handle);
				}
			}

		public:
			void push(T &&item)
			{
				std::unique_lock<srwlock> l{ queue_lock };
				if (!is_cancelled)
					queue.emplace(std::move(item));
				drain(std::move(l));
			}

			template<class...Args>
			void emplace(Args &&...args)
			{
				push(T(std::forward<Args>(args)...));
			}

			void cancel()
			{
				std::unique_lock<srwlock> l{ queue_lock };
				is_cancelled = true;
				drain(std::move(l));
			}

			awaitable next() noexcept
			{
				return{ this };
			}
		};
	}

	using details::async_queue;
}
