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
		template<class T = void>
		class shared_future
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
				co_await future_;

				std::unique_lock<srwlock> l{ lock };
				mode = mode::ready;
				auto existing_continuations = std::move(continuations);
				l.unlock();
				for (auto handle : existing_continuations)
				{
					[](std::experimental::coroutine_handle<> handle) noexcept -> winrt::fire_and_forget
					{
						co_await resume_background{};
						handle();
					}(handle);
				}
			}

		public:
			shared_future(future<T> &&future_) noexcept :
				future_{ std::move(future_) }
			{
			}

			shared_future(const shared_future &) = delete;
			shared_future &operator =(const shared_future &) = delete;

			shared_future(shared_future &&) = default;
			shared_future &operator =(shared_future &&) = default;

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
	}

	using details::shared_future;
}
