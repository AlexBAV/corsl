//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/dependencies.h"

namespace corsl
{
	namespace details
	{
		class async_timer
		{
			struct timer_traits : winrt::impl::handle_traits<PTP_TIMER>
			{
				static void close(type value) noexcept
				{
					CloseThreadpoolTimer(value);
				}
			};

			winrt::impl::handle<timer_traits> timer
			{
				CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, void * context, PTP_TIMER) noexcept
			{
				static_cast<async_timer *>(context)->resume();
			}, this, nullptr)
			};

			std::atomic_flag resumed{ false };
			std::atomic<bool> cancelled{ false };
			std::experimental::coroutine_handle<> resume_location{ nullptr };

			//
			auto get() const noexcept
			{
				return timer.get();
			}

			bool is_cancelled() const noexcept
			{
				return cancelled.load(std::memory_order_acquire);
			}

			void resume() noexcept
			{
				// Waiting for callback completion may lock if the timer is cancelled
				// We need to execute continuation on another thread
				if (resume_location && !resumed.test_and_set())
					resume_on_background(resume_location);
			}

			void set_handle(std::experimental::coroutine_handle<> handle)
			{
				resume_location = handle;
			}

		public:
			auto wait(winrt::Windows::Foundation::TimeSpan duration) noexcept
			{
				class awaiter
				{
					async_timer *timer;
					winrt::Windows::Foundation::TimeSpan duration;

				public:
					awaiter(async_timer *timer, winrt::Windows::Foundation::TimeSpan duration) noexcept :
						timer{ timer },
						duration{ duration }
					{}

					bool await_ready() const noexcept
					{
						return duration.count() <= 0;
					}

					void await_suspend(std::experimental::coroutine_handle<> handle) noexcept
					{
						timer->set_handle(handle);
						int64_t relative_count = -duration.count();
						SetThreadpoolTimer(timer->get(), reinterpret_cast<PFILETIME>(&relative_count), 0, 0);
					}

					void await_resume() const
					{
						timer->set_handle(nullptr);
						if (timer->is_cancelled())
							throw timer_cancelled{};
					}
				};

				resumed.clear();
				return awaiter{ this,duration };
			}

			void cancel()
			{
				cancelled.store(true, std::memory_order_release);
				SetThreadpoolTimer(get(), nullptr, 0, 0);
				WaitForThreadpoolTimerCallbacks(get(), TRUE);
				resume();
			}
		};
	}
	using details::async_timer;
}
