//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/dependencies.h"
#include "compatible_base.h"
#include "thread_pool.h"

namespace corsl
{
	namespace details
	{
		template<class CallbackPolicy = callback_policy::empty>
		class tp_timer
		{
			winrt::handle_type<timer_traits> timer;
			srwlock lock;
			std::experimental::coroutine_handle<> resume_location{};
			bool cancellation_requested{ false };

			//
			tp_timer(PTP_CALLBACK_ENVIRON pce) noexcept :
				timer{ CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE pci, void * context, PTP_TIMER) noexcept
			{
				CallbackPolicy::init_callback(pci);
				static_cast<tp_timer *>(context)->resume(false);
			}, this, pce) }
			{
			}

			//
			void resume(bool background) noexcept
			{
				std::unique_lock l{ lock };
				auto continuation = std::exchange(resume_location, std::experimental::coroutine_handle<>{});
				if (continuation)
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

			void suspend(std::experimental::coroutine_handle<> handle)
			{
				std::unique_lock l{ lock };
				check_cancellation(l);
				assert(!resume_location);
				resume_location = handle;
			}

		public:
			tp_timer(callback_environment &ce) noexcept : tp_timer(ce.get())
			{
			}

			tp_timer() noexcept : tp_timer(nullptr)
			{
			}

			auto wait() noexcept
			{
				class awaiter
				{
					tp_timer *timer;

				public:
					awaiter(tp_timer *timer) noexcept :
						timer{ timer }
					{}

					bool await_ready() const noexcept
					{
						return false;
					}

					void await_suspend(std::experimental::coroutine_handle<> handle)
					{
						timer->suspend(handle);
					}

					void await_resume() const
					{
						timer->check_exception();
					}
				};

				return awaiter{ this };
			}

			void cancel()
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

			void start(winrt::Windows::Foundation::TimeSpan duration, winrt::Windows::Foundation::TimeSpan period = {}) noexcept
			{
				int64_t relative_count = -duration.count();
				SetThreadpoolTimer(timer.get(), reinterpret_cast<PFILETIME>(&relative_count), static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(period).count()), 0);
			}

			void start(winrt::Windows::Foundation::DateTime when, winrt::Windows::Foundation::TimeSpan period = {}) noexcept
			{
				int64_t relative_count = when.time_since_epoch().count();
				SetThreadpoolTimer(timer.get(), reinterpret_cast<PFILETIME>(&relative_count), static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(period).count()), 0);
			}
		};
	}
	template<class CallbackPolicy>
	using tp_timer_ex = details::tp_timer<CallbackPolicy>;

	using tp_timer = details::tp_timer<>;
}
