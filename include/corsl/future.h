//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/dependencies.h"
#include "impl/errors.h"
#include "impl/promise_base.h"

#include "srwlock.h"
#include "compatible_base.h"

namespace corsl
{
	namespace details
	{
		template<class T = void>
		class future;

		template<class value_type>
		struct __declspec(empty_bases)promise_common : promise_base0
		{
			mutex lock;
			std::variant<std::monostate, std::exception_ptr, value_type> value;
			std::coroutine_handle<> resume{};
			std::atomic<int> use_count{ 0 };

			bool is_ready(std::unique_lock<mutex> &) const noexcept
			{
				return value.index() != 0;
			}

			void check_resume(std::unique_lock<mutex> &&l) noexcept
			{
				if (auto resume_ = std::exchange(resume, std::coroutine_handle<>{}))
				{
					l.unlock();
					resume_();
				}
			}

			void internal_set_exception(std::exception_ptr &&exception) noexcept
			{
				lock.lock();	// will be released in final_suspend
				value = std::move(exception);
			}

			fire_and_forget<> internal_set_exception_async(std::exception_ptr &&exception) noexcept
			{
				std::unique_lock<mutex> l{ lock };
				value = std::move(exception);
				co_await resume_background();
				check_resume(std::move(l));
			}

			void unhandled_exception() noexcept
			{
				lock.lock();	// will be released in final_suspend
				value = std::current_exception();
			}

			void check_exception()
			{
				if (std::holds_alternative<std::exception_ptr>(value)) [[unlikely]]
					std::rethrow_exception(std::get<std::exception_ptr>(std::move(value)));
			}

			value_type &get()
			{
				check_exception();
				return *std::get_if<value_type>(&value);
			}
		};

		// future
		template<class T>
		struct  __declspec(empty_bases) promise_base : promise_common<T>
		{
			template<class V>
			void return_value(V &&v) noexcept
			{
				this->lock.lock();	// will be released in final_suspend
				this->value = std::forward<V>(v);
			}

			future<> return_value_async(T v) noexcept;
		};

		struct empty_type {};

		template<>
		struct  __declspec(empty_bases) promise_base<void> : promise_common<empty_type>
		{
			void return_void() noexcept
			{
				lock.lock();	// will be released in final_suspend::await_ready
				value = empty_type{};
			}

			future<> return_void_async() noexcept;
		};

		template<class T>
		class  __declspec(empty_bases) future_base
		{
		protected:
			const T &iget(T &value) const & noexcept
			{
				return value;
			}

			T &&iget(T &value) && noexcept
			{
				return std::move(value);
			}
		};

		template<>
		class  __declspec(empty_bases) future_base<void>
		{
		protected:
			template<class T>
			void iget(T &) const noexcept
			{
			}
		};

		template<class T>
		struct  __declspec(empty_bases) promise_type_ : promise_base<T>
		{
			std::coroutine_handle<> destroy_resume{};
			bool future_exists{ true };

			//
			bool start_async(std::coroutine_handle<> resume_, std::unique_lock<mutex> &&l) noexcept
			{
				if (this->is_ready(l))
					return false;	// we already have a result
				assert(!this->resume && "future cannot be awaited multiple times");
				this->resume = resume_;
				return true;
			}

			static std::suspend_never initial_suspend() noexcept
			{
				return {};
			}

			struct awaiter
			{
				promise_type_ *pthis;

				bool await_ready() const noexcept
				{
					auto is_ready = !pthis->future_exists;
					if (is_ready)
					{
						auto resume = std::exchange(pthis->resume, {});
						pthis->lock.unlock();
						if (resume)
							resume();
					}
					return is_ready;
				}

				auto await_suspend(std::coroutine_handle<> resume_) noexcept
				{
					pthis->destroy_resume = resume_;
#pragma warning(suppress: 26110)
					auto result = std::exchange(pthis->resume, {});
					pthis->lock.unlock();
					return result ? result : std::noop_coroutine();
				}

				static void await_resume() noexcept
				{
				}
			};

			auto final_suspend() noexcept
			{
				return awaiter{ this };
			}

			future<T> get_return_object() noexcept
			{
				add_ref();
				return { std::coroutine_handle<promise_type_>::from_promise(*this) };
			}

			void add_ref() noexcept
			{
				this->use_count.fetch_add(1, std::memory_order_relaxed);
			}

			void release() noexcept
			{
				if (1 == this->use_count.fetch_sub(1, std::memory_order_relaxed))
					destroy();
			}

			void destroy() noexcept
			{
				std::unique_lock l{ this->lock };
				future_exists = false;
				if (destroy_resume)
				{
					l.unlock();
					destroy_resume.destroy();
				}
			}

			template<class V>
			V &&await_transform(V &&expr)
			{
				if (this->is_cancelled()) [[unlikely]]
					throw operation_cancelled{};
				else
					return std::forward<V>(expr);
			}

			corsl::details::cancellation_token_transport await_transform(corsl::details::cancellation_source &source) noexcept
			{
				return { source, std::coroutine_handle<promise_base0>::from_promise(*this) };
			}

			corsl::details::cancellation_token_transport await_transform(const corsl::details::cancellation_source &source) noexcept
			{
				return { source, std::coroutine_handle<promise_base0>::from_promise(*this) };
			}
		};

		template<class T>
		class  __declspec(empty_bases) future : public future_base<T>
		{
			friend struct promise_type_<T>;
			static_assert(!std::is_reference_v<T>, "future<T> is not allowed for reference types");

			using promise_type_ = promise_type_<T>;
			using coro_type = std::coroutine_handle<promise_type_>;
			coro_type coro;

			future(coro_type coro) noexcept :
				coro{ coro }
			{}

			struct special_await
			{
				coro_type coro;

				bool await_ready() const noexcept
				{
					std::unique_lock<mutex> l{ coro.promise().lock };
					auto ready = coro.promise().is_ready(l);
					if (!ready)
						l.release();
					return ready;
				}

				bool await_suspend(std::coroutine_handle<> resume) noexcept
				{
					std::unique_lock<mutex> l{ coro.promise().lock, std::adopt_lock };
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

			future(const future &o) = delete;
			future &operator =(const future &o) = delete;

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
				assert(coro && "Calling get() or wait() on uninitialized future is prohibited");
				{
					std::unique_lock l{ coro.promise().lock };
					if (coro.promise().is_ready(l))
						return;
				}

				srwlock x;
				condition_variable cv;
				bool completed = false;

				[&]() noexcept -> fire_and_forget<>
				{
					co_await special_await{ coro };
					const std::lock_guard guard{ x };
					completed = true;
					cv.wake_one();
				}();

				const std::lock_guard guard{ x };
				cv.wait_while(x, [&] { return !completed; });
			}

			decltype(auto) get() const &
			{
				wait();
				return this->iget(coro.promise().get());
			}

			decltype(auto) get() &&
			{
				wait();
				return std::move(*this).iget(coro.promise().get());
			}

			bool is_ready() const noexcept
			{
				assert(coro && "Calling is_ready for uninitialized future is invalid");
				std::unique_lock<mutex> l{ coro.promise().lock };
				return coro.promise().is_ready(l);
			}

			// await
			bool await_ready() const noexcept
			{
				assert(coro && "co_await with uninitialized future is invalid");
				std::unique_lock<mutex> l{ coro.promise().lock };
				auto ready = coro.promise().is_ready(l);
				if (!ready)
					l.release();
				return ready;
			}

			bool await_suspend(std::coroutine_handle<> resume) noexcept
			{
				std::unique_lock<mutex> l{ coro.promise().lock, std::adopt_lock };
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

			template<class F>
			auto then(F continuation) && -> std::invoke_result_t<F, result_type>
			{
				co_return co_await continuation(co_await std::move(*this));
			}

			template<class F>
			auto then(F continuation) && -> std::invoke_result_t<F, future<result_type>>
			{
				co_return co_await continuation(std::move(*this));
			}

			template<class F>
			auto then(F continuation) && -> std::invoke_result_t<F>
			{
				co_await *this;
				co_await continuation();
			}
		};

		inline future<> promise_base<void>::return_void_async() noexcept
		{
			std::unique_lock<mutex> l{ lock };
			value = empty_type{};
			co_await resume_background();
			check_resume(std::move(l));
		}

		template<class T>
		inline future<> promise_base<T>::return_value_async(T v) noexcept
		{
			std::unique_lock l{ this->lock };
			this->value = std::move(v);
			co_await resume_background();
			this->check_resume(std::move(l));
		}

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
