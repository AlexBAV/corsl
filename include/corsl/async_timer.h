//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/dependencies.h"
#include "compatible_base.h"

namespace corsl
{
	namespace details
	{
		template<class CallbackPolicy = callback_policy::empty>
		class async_timer
		{
			winrt::handle_type<timer_traits> timer
			{
				CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE pci, void * context, PTP_TIMER) noexcept
			{
				CallbackPolicy::init_callback(pci);
				static_cast<async_timer *>(context)->resume(false);
			}, this, nullptr)
			};

			srwlock lock;
			std::coroutine_handle<> resume_location{};
			bool cancellation_requested{ false };

			//
			void resume(bool background) noexcept
			{
				std::unique_lock l{ lock };
				if (auto continuation = std::exchange(resume_location, std::coroutine_handle<>{}))
				{
					l.unlock();
					if (background)
						resume_on_background<CallbackPolicy>(continuation);
					else
						continuation();
				}
			}

			void check_cancellation(std::unique_lock<srwlock> &l)
			{
				if (cancellation_requested)
				{
					cancellation_requested = false;
					l.unlock();
					throw timer_cancelled{};
				}
			}

			void check_exception()
			{
				lock.lock();
				if (cancellation_requested)
				{
					cancellation_requested = false;
					lock.unlock();
					throw timer_cancelled{};
				}
				else
					lock.unlock();
			}

			void suspend(std::coroutine_handle<> handle, winrt::Windows::Foundation::TimeSpan duration)
			{
				{
					std::unique_lock l{ lock };
					check_cancellation(l);
					assert(!resume_location);
					resume_location = handle;
				}
				int64_t relative_count = -duration.count();
				SetThreadpoolTimer(timer.get(), reinterpret_cast<PFILETIME>(&relative_count), 0, 0);
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

					void await_suspend(std::coroutine_handle<> handle) noexcept
					{
						timer->suspend(handle, duration);
					}

					void await_resume() const
					{
						timer->check_exception();
					}
				};

				std::scoped_lock l{ lock };
				cancellation_requested = false;
				return awaiter{ this,duration };
			}

			void cancel() noexcept
			{
				bool wait = false;
				{
					std::scoped_lock l{ lock };
					cancellation_requested = true;
					SetThreadpoolTimer(timer.get(), nullptr, 0, 0);
					if (resume_location)
						wait = true;
				}
				if (wait)
					WaitForThreadpoolTimerCallbacks(timer.get(), TRUE);
				resume(true);
			}
		};
	}

	template<class CallbackPolicy>
	using async_timer_ex = details::async_timer<CallbackPolicy>;

	using async_timer = details::async_timer<>;
}
