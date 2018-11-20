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
		template<class T, class Queue = std::queue<T>, class CallbackPolicy = callback_policy::empty>
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

				void set_exception(std::exception_ptr ptr) noexcept
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

			mutable srwlock queue_lock;
			queue_t queue;
			awaitable *current{ nullptr };
			std::exception_ptr exception{};

			bool is_ready(std::variant<std::exception_ptr, T> &value)
			{
				std::unique_lock<srwlock> l{ queue_lock };
				if (exception)
				{
					value = exception;
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
				if (exception)
					std::rethrow_exception(exception);
				if (!queue.empty())
				{
					pointer->set_result(std::move(queue.front()));
					queue.pop();
					return false;
				}
				current = pointer;
				return true;
			}

			void drain([[maybe_unused]] std::unique_lock<srwlock> &&lock)
			{
				lock;
				if (current)
				{
					auto cur = std::exchange(current, nullptr);
					if (exception)
						cur->set_exception(exception);
					else
					{
						auto v = std::move(queue.front());
						cur->set_result(std::move(v));
						queue.pop();
					}
					resume_on_background<CallbackPolicy>(cur->handle);
				}
			}

		public:
			template<class V>
			void push(V &&item)
			{
				std::unique_lock<srwlock> l{ queue_lock };
				if (!exception)
					queue.emplace(std::forward<V>(item));
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
				exception = std::make_exception_ptr(operation_cancelled{});
				drain(std::move(l));
			}

			void push_exception(std::exception_ptr exception_)
			{
				std::unique_lock<srwlock> l{ queue_lock };
				exception = std::move(exception_);
				drain(std::move(l));
			}

			awaitable next() noexcept
			{
				return{ this };
			}

			void clear() noexcept
			{
				queue_t empty_queue;
				std::scoped_lock<srwlock> l{ queue_lock };
				queue.swap(empty_queue);
				exception = {};
			}

			[[nodiscard]]
			bool empty() const noexcept
			{
				std::scoped_lock<srwlock> l{ queue_lock };
				return queue.empty();
			}
		};
	}

	using details::async_queue;
}
