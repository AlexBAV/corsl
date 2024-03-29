//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include <vector>

#include "future.h"
#include "compatible_base.h"

namespace corsl
{
	namespace details
	{
		template<class T>
		class shared_future_impl
		{
			enum class mode
			{
				not_started,
				started,
				ready
			};

			future<T> future_;
			srwlock lock;
			mode mode { mode::not_started };
			std::vector<std::coroutine_handle<>> continuations;

			fire_and_forget<> start()
			{
				co_await resume_background();
				try
				{
					co_await future_;
				}
				catch (...)
				{
				}

				std::unique_lock<srwlock> l{ lock };
				mode = mode::ready;
				auto existing_continuations{ std::move(continuations) };
				l.unlock();
				for (auto handle : existing_continuations)
					resume_on_background(handle);
			}

			corsl::future<> get_wait_future()
			{
				try
				{
					co_await *this;
				}
				catch (...)
				{
				}
			}

		public:
			shared_future_impl(future<T> &&future_) noexcept :
				future_{ std::move(future_) }
			{
			}

			shared_future_impl(const shared_future_impl &) = delete;
			shared_future_impl &operator =(const shared_future_impl &) = delete;

			bool is_ready() const noexcept
			{
				return future_.is_ready();
			}

			auto get()
			{
				wait();
				return future_.get();
			}

			void wait() noexcept
			{
				if (!is_ready())
					get_wait_future().wait();
			}

			// await
			bool await_ready() noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				if (mode == mode::ready)
					return true;
				else
				{
					l.release();
					return false;
				}
			}

			void await_suspend(std::coroutine_handle<> resume)
			{
				std::unique_lock<srwlock> l{ lock, std::adopt_lock };
				continuations.emplace_back(resume);
				if (mode == mode::not_started)
				{
					mode = mode::started;
					start();
				}
			}

			auto await_resume()
			{
				return future_.get();
			}
		};

		template<class T = void>
		class shared_future
		{
			std::shared_ptr<shared_future_impl<T>> pimpl;

		public:
			using result_type = T;

			shared_future() = default;
			shared_future(future<T> &&future) :
				pimpl{ std::make_shared<shared_future_impl<T>>(std::move(future)) }
			{}

			explicit operator bool() const noexcept
			{
				return !!pimpl;
			}

			bool is_ready() const noexcept
			{
				return pimpl->is_ready();
			}

			decltype(auto) get()
			{
				return pimpl->get();
			}

			decltype(auto) get() const
			{
				return pimpl->get();
			}

			void wait() const noexcept
			{
				pimpl->wait();
			}

			// await
			bool await_ready() const noexcept
			{
				return pimpl->await_ready();
			}

			void await_suspend(std::coroutine_handle<> resume) const
			{
				pimpl->await_suspend(resume);
			}

			auto await_resume() const
			{
				return pimpl->await_resume();
			}

			template<class F>
			auto then(F continuation) ->std::invoke_result_t<F, result_type>
			{
				co_return co_await continuation(co_await *this);
			}

			template<class F>
			auto then(F continuation) ->std::invoke_result_t<F, future<result_type>>
			{
				co_return co_await continuation(*this);
			}

			template<class F>
			auto then(F continuation) ->std::invoke_result_t<F>
			{
				co_await *this;
				co_await continuation();
			}
		};
	}

	using details::shared_future;
}
