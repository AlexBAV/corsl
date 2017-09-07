//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/dependencies.h"
#include "impl/errors.h"
#include "impl/promise_base.h"

#include "srwlock.h"

namespace corsl
{
	namespace details
	{
		// future
		template<class T>
		struct  __declspec(empty_bases) promise_base : promise_base0
		{
			T value;

			template<class V>
			void return_value(V &&v) noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				value = std::forward<V>(v);
				status = status_t::ready;
				check_resume(std::move(l));
			}

			T &get()
			{
				check_exception();
				return value;
			}
		};

		template<>
		struct  __declspec(empty_bases) promise_base<void> : promise_base0
		{
			struct empty_type {};
			empty_type value;

			void return_void() noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				status = status_t::ready;
				check_resume(std::move(l));
			}

			empty_type &get()
			{
				check_exception();
				return value;
			}
		};

		template<class T>
		class  __declspec(empty_bases) future_base
		{
		protected:
			const T &iget(T &value) const &
			{
				return value;
			}

			T &&iget(T &value) &&
			{
				return std::move(value);
			}
		};

		template<>
		class  __declspec(empty_bases) future_base<void>
		{
		protected:
			template<class T>
			void iget(T &) const
			{
			}
		};

		template<class T>
		class future;

		template<class T>
		struct  __declspec(empty_bases) promise_type_ : promise_base<T>
		{
			std::experimental::coroutine_handle<> destroy_resume{};
			bool future_exists{ true };

			//
			bool start_async(std::experimental::coroutine_handle<> resume_, std::unique_lock<srwlock> &&l) noexcept
			{
				if (is_ready(l))
					return false;	// we already have a result
				assert(!resume && "future cannot be awaited multiple times");
				resume = resume_;
				return true;
			}

			static std::experimental::suspend_never initial_suspend() noexcept
			{
				return {};
			}

			auto final_suspend() noexcept
			{
				struct awaiter
				{
					promise_type_ *pthis;

					bool await_ready() const noexcept
					{
						pthis->lock.lock();
						auto is_ready = !pthis->future_exists;
						if (is_ready)
							pthis->lock.unlock();
						return is_ready;
					}

					void await_suspend(std::experimental::coroutine_handle<> resume_) noexcept
					{
						pthis->destroy_resume = resume_;
						pthis->lock.unlock();
					}

					static void await_resume() noexcept
					{
					}
				};
				return awaiter{ this };
			}

			future<T> get_return_object() noexcept
			{
				add_ref();
				return { std::experimental::coroutine_handle<promise_type_>::from_promise(*this) };
			}

			void add_ref() noexcept
			{
				use_count.fetch_add(1, std::memory_order_relaxed);
			}

			void release() noexcept
			{
				if (1 == use_count.fetch_sub(1, std::memory_order_relaxed))
					destroy();
			}

			void destroy() noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				future_exists = false;
				if (destroy_resume)
				{
					l.unlock();
					destroy_resume.destroy();
				}
			}

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

		template<class T = void>
		class  __declspec(empty_bases) future : public future_base<T>
		{
			friend struct promise_type_<T>;
			static_assert(!std::is_reference_v<T>, "future<T> is not allowed for reference types");

			using promise_type_ = promise_type_<T>;
			using coro_type = std::experimental::coroutine_handle<promise_type_>;
			coro_type coro;

			future(coro_type coro) noexcept :
				coro{ coro }
			{}

			struct special_await
			{
				coro_type coro;

				bool await_ready() const noexcept
				{
					std::unique_lock<srwlock> l{ coro.promise().lock };
					auto ready = coro.promise().is_ready(l);
					if (!ready)
						l.release();
					return ready;
				}

				bool await_suspend(std::experimental::coroutine_handle<> resume) noexcept
				{
					std::unique_lock<srwlock> l{ coro.promise().lock, std::adopt_lock };
					return coro.promise().start_async(resume, std::move(l));
				}

				void await_resume() noexcept
				{
				}
			};

		public:
			using promise_type = promise_type_;
			using result_type = T;

			future() noexcept :
				coro{ nullptr }
			{}

			~future()
			{
				if (coro)
					coro.promise().release();
			}

			explicit operator bool()const noexcept
			{
				return !!coro;
			}

			future(const future &o) noexcept :
				coro{ o.coro }
			{
				if (coro)
					coro.promise().add_ref();
			}

			future &operator =(const future &o) noexcept
			{
				if (coro)
					coro.promise().release();
				coro = o.coro;
				if (coro)
					coro.promise().add_ref();
			}

			future(future &&o) noexcept :
				coro{ o.coro }
			{
				o.coro = nullptr;
			}

			future &operator =(future &&o) noexcept
			{
				using std::swap;
				swap(coro, o.coro);
				return *this;
			}

			void wait() const noexcept
			{
				assert(coro && "Calling get() or wait() for uninitialized future is incorrect");
				{
					std::unique_lock<srwlock> l{ coro.promise().lock };
					if (coro.promise().is_ready(l))
						return;
				}

				srwlock x;
				condition_variable cv;
				bool completed = false;

				[&]()->fire_and_forget<>
				{
					co_await special_await{ coro };
					const std::lock_guard<srwlock> guard{ x };
					completed = true;
					cv.wake_one();
				}();

				const std::lock_guard<srwlock> guard{ x };
				cv.wait_while(x, [&] { return !completed; });
			}

			decltype(auto) get() const &
			{
				wait();
				return iget(coro.promise().get());
			}

			decltype(auto) get() &&
			{
				wait();
				return std::move(*this).iget(coro.promise().get());
			}

			bool is_ready() const noexcept
			{
				assert(coro && "Calling is_ready for uninitialized future is invalid");
				std::unique_lock<srwlock> l{ coro.promise().lock };
				return coro.promise().is_ready(l);
			}

			// await
			bool await_ready() const noexcept
			{
				assert(coro && "co_await with uninitialized future is invalid");
				std::unique_lock<srwlock> l{ coro.promise().lock };
				auto ready = coro.promise().is_ready(l);
				if (!ready)
					l.release();
				return ready;
			}

			bool await_suspend(std::experimental::coroutine_handle<> resume) noexcept
			{
				std::unique_lock<srwlock> l{ coro.promise().lock, std::adopt_lock };
				return coro.promise().start_async(resume, std::move(l));
			}

			void iawait_resume(std::true_type)
			{
				coro.promise().get();
			}

			decltype(auto) iawait_resume(std::false_type)
			{
				return std::move(coro.promise().get());
			}

			decltype(auto) await_resume()
			{
				return iawait_resume(std::is_same<T, void>{});
			}
		};

		template<class F>
		struct is_future : std::false_type
		{};

		template<class T>
		struct is_future<future<T>> : std::true_type
		{};

		template<class T>
		constexpr bool is_future_v = is_future<T>::value;
	}

	using details::future;
	using details::is_future;
	using details::is_future_v;
}
