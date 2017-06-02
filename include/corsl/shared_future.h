//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

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
			std::vector<std::experimental::coroutine_handle<>> continuations;

			winrt::fire_and_forget start()
			{
				co_await resume_background{};
				try
				{
					co_await future_;
				}
				catch (...)
				{
				}

				std::unique_lock<srwlock> l{ lock };
				mode = mode::ready;
				auto existing_continuations = std::move(continuations);
				l.unlock();
				for (auto handle : existing_continuations)
					resume_on_background(handle);
			}

		public:
			shared_future_impl(future<T> &&future_) noexcept :
				future_{ std::move(future_) }
			{
			}

			shared_future_impl(const shared_future_impl &) = delete;
			shared_future_impl &operator =(const shared_future_impl &) = delete;

			shared_future_impl(shared_future_impl &&) = delete;
			shared_future_impl &operator =(shared_future_impl &&) = delete;

			bool is_ready() const noexcept
			{
				return future_.is_ready();
			}

			auto get()
			{
				return future_.get();
			}

			void wait() noexcept
			{
				future_.wait();
			}

			// await
			bool await_ready() noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				switch (mode)
				{
				case mode::not_started:
					mode = mode::started;
					start();
					break;
				case mode::ready:
					return true;
				}
				l.release();
				return false;
			}

			void await_suspend(std::experimental::coroutine_handle<> resume)
			{
				std::unique_lock<srwlock> l{ lock, std::adopt_lock };
				continuations.emplace_back(resume);
			}

			auto await_resume()
			{
				return future_.get();
			}
		};

		template<class T>
		class shared_future
		{
			std::shared_ptr<shared_future_impl<T>> pimpl;

		public:
			shared_future() = default;
			shared_future(future<T> &&future) :
				pimpl{ std::make_shared<shared_future_impl<T>>(std::move(future)) }
			{}

			bool is_ready() const noexcept
			{
				return pimpl->is_ready();
			}

			auto get()
			{
				return pimpl->get();
			}

			void wait() noexcept
			{
				pimpl->wait();
			}

			// await
			bool await_ready() const noexcept
			{
				return pimpl->await_ready();
			}

			void await_suspend(std::experimental::coroutine_handle<> resume) const
			{
				pimpl->await_suspend(resume);
			}

			auto await_resume() const
			{
				return pimpl->await_resume();
			}
		};
	}

	using details::shared_future;
}
