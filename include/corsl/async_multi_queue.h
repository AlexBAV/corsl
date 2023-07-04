//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
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

		template<class T, class Queue = std::queue<T>, class CallbackPolicy = callback_policy::empty>
		class async_multi_consumer_queue
		{
			using queue_t = Queue;
			struct awaitable_base : public boost::intrusive::list_base_hook<bi::link_mode<bi::normal_link>>
			{
			};

			using awaitable = aq_awaitable<async_multi_consumer_queue, T, awaitable_base>;
			friend typename awaitable;

			mutable srwlock queue_lock;
			queue_t queue;
			bi::list<awaitable, bi::constant_time_size<true>> clients;
			std::exception_ptr exception{};
			PTP_CALLBACK_ENVIRON pce{};

			bool is_ready(std::variant<std::monostate, std::exception_ptr, T> &value)
			{
				std::scoped_lock l{ queue_lock };
				if (exception) [[unlikely]]
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
				std::scoped_lock l{ queue_lock };
				if (exception) [[unlikely]]
					std::rethrow_exception(exception);
				if (!queue.empty())
				{
					auto v = std::move(queue.front());
					queue.pop();
					pointer->set_result(std::move(v));
					return false;
				}
				clients.push_back(*pointer);
				return true;
			}

			void drain([[maybe_unused]] std::unique_lock<srwlock> &&lock, T &&value)
			{
				lock;	// executing under lock
				if (!clients.empty())
				{
					auto it = clients.begin();
					auto *cur = std::addressof(*it);
					clients.erase(it);

					cur->set_result(std::move(value));
					resume_on_background<CallbackPolicy>(cur->handle);
				}
				else
					queue.emplace(std::move(value));
			}

		public:
			async_multi_consumer_queue(PTP_CALLBACK_ENVIRON pce = nullptr) noexcept :
				pce{ pce }
			{}

			async_multi_consumer_queue(callback_environment &ce) noexcept :
				pce{ ce.get() }
			{}

			template<class Alloc>
			requires std::uses_allocator_v<Queue, Alloc>
			explicit async_multi_consumer_queue(PTP_CALLBACK_ENVIRON pce, const Alloc &alloc) :
				pce{ pce },
				queue{ alloc }
			{}

			template<class Alloc>
			requires std::uses_allocator_v<Queue, Alloc>
			explicit async_multi_consumer_queue(callback_environment &ce, const Alloc &alloc) :
				pce{ ce.get() },
				queue{ alloc }
			{}

			template<class Alloc>
			requires std::uses_allocator_v<Queue, Alloc>
			explicit async_multi_consumer_queue(const Alloc &alloc) :
				queue{ alloc }
			{}

			async_multi_consumer_queue(const async_multi_consumer_queue &) = delete;
			async_multi_consumer_queue &operator =(const async_multi_consumer_queue &) = delete;

			//
			template<class V>
			void push(V &&item)
			{
				std::unique_lock l{ queue_lock };
				if (!exception) [[likely]]
					drain(std::move(l), T{ std::forward<V>(item) });
			}

			template<class...Args>
			void emplace(Args &&...args)
			{
				std::unique_lock l{ queue_lock };
				if (!exception) [[likely]]
					drain(std::move(l), T{ std::forward<Args>(args)... });
			}

			void cancel()
			{
				push_exception(std::make_exception_ptr(operation_cancelled{}));
			}

			void push_exception(std::exception_ptr exception_)
			{
				decltype(clients) clients_copy;

				{
					std::unique_lock l{ queue_lock };
					exception = exception_;
					std::swap(clients, clients_copy);
				}

				auto b = clients_copy.begin();
				const auto e = clients_copy.end();
				for (decltype(b) next; b != e; b = next)
				{
					b->set_exception(exception_);
					next = std::next(b);
					resume_on_background<CallbackPolicy>(b->handle);
				}
			}

			awaitable next() noexcept
			{
				return{ this };
			}

			void clear() noexcept
			{
				std::unique_lock l{ queue_lock };
				assert(clients.empty());
				std::exchange(queue, queue_t{});
				exception = {};
			}

			[[nodiscard]]
			bool empty() const noexcept
			{
				std::shared_lock l{ queue_lock };
				return queue.empty();
			}

			[[nodiscard]]
			auto size() const noexcept
			{
				std::shared_lock l{ queue_lock };
				return queue.size();
			}
		};
	}

	using details::async_multi_consumer_queue;
}
