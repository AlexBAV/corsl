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

namespace corsl
{
	namespace details
	{
		template<class T>
		class async_generator;

		template<class T>
		struct __declspec(empty_bases)promise_type : public promise_base0
		{
			mutable srwlock lock;
			using variant_t = std::variant<std::monostate, T, std::exception_ptr>;
			variant_t value;
			std::experimental::coroutine_handle<> continuation;

			bool is_ready_impl() const noexcept
			{
				return value.index() != 0;
			}

			bool is_ready() const noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				if (is_ready_impl())
					return true;
				else
				{
					l.release();
					return false;
				}
			}

			void suspend(std::experimental::coroutine_handle<> continuation_)
			{
				std::unique_lock<srwlock> l{ lock, std::adopt_lock };
				assert(!continuation && "continuation must be empty at this time");
				continuation = continuation_;
			}

			template<class V>
			auto yield_value(V &&value_) noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				value = std::forward<V>(value_);

				struct awaitable
				{
					std::unique_lock<srwlock> l;
					promise_type *promise;

					awaitable(std::unique_lock<srwlock> &&l, promise_type *promise) noexcept :
					l{ std::move(l) },
						promise{ promise }
					{
					}

					bool await_ready() const noexcept
					{
						return false;
					}

					void await_suspend(std::experimental::coroutine_handle<>)
					{
						promise->check_resume(std::move(l));
					}

					void await_resume() const noexcept
					{
					}
				};
				return awaitable{ std::move(l), this };
				//				check_resume(std::move(l));
			}

			template<class V>
			void return_value(V &&value_) noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				value = std::forward<V>(value_);
				// no resuming right now - wait until final suspend
			}

			void unhandled_exception() noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				value = std::current_exception();
				// no resuming right now - wait until final suspend
			}

			void check_resume(std::unique_lock<srwlock> l) noexcept
			{
				if (continuation)
				{
					auto resume = std::exchange(continuation, nullptr);
					l.unlock();
					resume();
				}
			}

			void check_exception()
			{
				if (std::holds_alternative<std::exception_ptr>(value))
					std::rethrow_exception(std::get<std::exception_ptr>(std::move(value)));
			}

			T get_value()
			{
				return std::get<T>(std::exchange(value, variant_t{}));
			}

			static std::experimental::suspend_always initial_suspend() noexcept
			{
				return {};
			}

			auto final_suspend() noexcept
			{
				struct final_suspend_t
				{
					promise_type *promise;

					bool await_ready() const noexcept
					{
						if (promise->is_ready_impl() && promise->continuation)
							return false;
						else
							return true;
					}

					void await_suspend(std::experimental::coroutine_handle<>) noexcept
					{
						promise->continuation();
					}

					void await_resume() const noexcept
					{
					}
				};
				return final_suspend_t{this};
			}

			async_generator<T> get_return_object() noexcept;

			template<class T>
			T &&await_transform(T &&expr)
			{
				if (is_cancelled())
					throw operation_cancelled{};
				else
					return std::forward<T>(expr);
			}

			corsl::details::cancellation_token_transport await_transform(corsl::details::cancellation_source &source) noexcept
			{
				return { source, std::experimental::coroutine_handle<promise_base0>::from_promise(*this) };
			}

			corsl::details::cancellation_token_transport await_transform(const corsl::details::cancellation_source &source) noexcept
			{
				return { source, std::experimental::coroutine_handle<promise_base0>::from_promise(*this) };
			}
		};

		template<class T>
		class iterator;

		template<class T>
		struct awaitable
		{
			std::experimental::coroutine_handle<promise_type<T>> coro;

			bool await_ready() const noexcept
			{
				return coro.done() || coro.promise().is_ready();
			}

			void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept
			{
				coro.promise().suspend(continuation);
			}

			iterator<T> await_resume() const;
		};

		struct sentinel {};

		template<class T>
		class iterator
		{
			friend class async_generator<T>;
			friend struct awaitable<T>;

			std::experimental::coroutine_handle<promise_type<T>> coro;

			iterator(std::experimental::coroutine_handle<promise_type<T>> coro) noexcept :
			coro{ coro }
			{}

		public:
			using value_type = T;
			using reference = T;
			using iterator_category = std::input_iterator_tag;
			using difference_type = ptrdiff_t;

			bool operator ==(sentinel) const noexcept
			{
				return coro.done();
			}

			bool operator ==(const iterator &o) const noexcept
			{
				return coro == o.coro;
			}

			bool operator !=(sentinel) const noexcept
			{
				return !coro.done();
			}

			bool operator !=(const iterator &o) const noexcept
			{
				return coro != o.coro;
			}

			auto operator ++()
			{
				coro.resume();
				return awaitable<T>{ coro };
			}

			reference operator *()
			{
				return coro.promise().get_value();
			}
		};

		template<class T>
		inline iterator<T> awaitable<T>::await_resume() const
		{
			coro.promise().check_exception();
			return { coro };
		}

		template<class T>
		class async_generator
		{
			friend struct promise_type<T>;

		public:
			using promise_type = promise_type<T>;
		private:
			using iterator = iterator<T>;
			using awaitable = awaitable<T>;
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
			auto begin() const noexcept
			{
				assert(coro);
				coro.resume();
				return awaitable{ coro };
			}

			sentinel end() const noexcept
			{
				return {};
			}
		};

		template<class T>
		inline async_generator<T> promise_type<T>::get_return_object() noexcept
		{
			return { std::experimental::coroutine_handle<promise_type>::from_promise(*this) };
		}

	}

	using details::async_generator;
}
