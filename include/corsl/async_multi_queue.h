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

#include <boost/intrusive/list.hpp>

#include "async_queue.h"
#include "thread_pool.h"

namespace corsl
{
	namespace details
	{
		namespace bi = boost::intrusive;

#if defined(_DEBUG)
#define BL_LINK_MODE bi::link_mode<bi::safe_link>
#else
#define BL_LINK_MODE bi::link_mode<bi::normal_link>
#endif

		template<class T, class Queue = std::queue<T>, class CallbackPolicy = callback_policy::empty>
		class async_multi_consumer_queue
		{
			using queue_t = Queue;
			struct awaitable_base : public boost::intrusive::list_base_hook<BL_LINK_MODE>
			{
			};

			using awaitable = aq_awaitable<async_multi_consumer_queue, T, awaitable_base>;
			friend typename awaitable;

			mutable srwlock queue_lock;
			queue_t queue;
			bi::list<awaitable, bi::constant_time_size<true>> clients;
			std::exception_ptr exception{};
			PTP_CALLBACK_ENVIRON pce;

			bool is_ready(std::variant<std::exception_ptr, T> &value)
			{
				std::unique_lock l{ queue_lock };
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
				std::unique_lock l{ queue_lock };
				if (exception)
					std::rethrow_exception(exception);
				if (!queue.empty())
				{
					pointer->set_result(std::move(queue.front()));
					queue.pop();
					return false;
				}
				clients.push_back(*pointer);
				return true;
			}

			bool drain([[maybe_unused]] std::unique_lock<srwlock> &&lock)
			{
				lock;
				if (!clients.empty())
				{
					auto it = clients.begin();
					auto *cur = std::addressof(*it);
					clients.erase(it);

					if (exception)
						cur->set_exception(exception);
					else
					{
						auto v = std::move(queue.front());
						cur->set_result(std::move(v));
						queue.pop();
					}
					resume_on_background<CallbackPolicy>(cur->handle);
					return true;
				}
				else
					return false;
			}

		public:
			async_multi_consumer_queue(PTP_CALLBACK_ENVIRON pce = nullptr) noexcept :
				pce{ pce }
			{}

			async_multi_consumer_queue(callback_environment &ce) noexcept :
				pce{ ce.get() }
			{}

			async_multi_consumer_queue(const async_multi_consumer_queue &) = delete;
			async_multi_consumer_queue &operator =(const async_multi_consumer_queue &) = delete;

			//
			template<class V>
			void push(V &&item)
			{
				std::unique_lock l{ queue_lock };
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
				std::unique_lock l{ queue_lock };
				exception = std::make_exception_ptr(operation_cancelled{});
				drain(std::move(l));
			}

			void push_exception(std::exception_ptr exception_)
			{
				std::unique_lock l{ queue_lock };
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
				std::unique_lock l{ queue_lock };
				queue.swap(empty_queue);
				exception = std::make_exception_ptr(operation_cancelled{});

				while (drain(std::move(l)))
					;
				exception = {};
			}

			[[nodiscard]]
			bool empty() const noexcept
			{
				std::scoped_lock<srwlock> l{ queue_lock };
				return queue.empty();
			}
		};

#undef BL_LINK_MODE
	}

	using details::async_multi_consumer_queue;
}
