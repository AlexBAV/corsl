//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "dependencies.h"

namespace corsl
{
	namespace details
	{
		// Windows SRW lock wrapped in shared_mutex-friendly class
		class srwlock
		{
			SRWLOCK m_lock{};
		public:
			srwlock(const srwlock &) = delete;
			srwlock & operator=(const srwlock &) = delete;
			srwlock() noexcept = default;

			_Acquires_exclusive_lock_(&m_lock)
			void lock() noexcept
			{
				AcquireSRWLockExclusive(&m_lock);
			}

			_Acquires_shared_lock_(&m_lock)
			void lock_shared() noexcept
			{
				AcquireSRWLockShared(&m_lock);
			}

			_When_(return, _Acquires_exclusive_lock_(&m_lock))
			bool try_lock() noexcept
			{
				return 0 != TryAcquireSRWLockExclusive(&m_lock);
			}

			_When_(return, _Acquires_shared_lock_(&m_lock))
			bool try_lock_shared() noexcept
			{
				return 0 != TryAcquireSRWLockShared(&m_lock);
			}

			_Releases_exclusive_lock_(&m_lock)
			void unlock() noexcept
			{
				ReleaseSRWLockExclusive(&m_lock);
			}

			_Releases_shared_lock_(&m_lock)
			void unlock_shared() noexcept
			{
				ReleaseSRWLockShared(&m_lock);
			}

			PSRWLOCK get() noexcept
			{
				return &m_lock;
			}
		};

		class condition_variable
		{
			CONDITION_VARIABLE m_cv{};

		public:
			condition_variable(condition_variable const &) = delete;
			condition_variable const & operator=(condition_variable const &) = delete;
			condition_variable() noexcept = default;

			template <typename F>
			void wait_while(srwlock &x, const F &predicate) noexcept(noexcept(predicate()))
			{
				while (predicate())
				{
					WINRT_VERIFY(SleepConditionVariableSRW(&m_cv, x.get(), INFINITE, 0));
				}
			}

			template<typename F>
			bool wait_while(srwlock &x, winrt::Windows::Foundation::TimeSpan timeout, const F &predicate) noexcept(noexcept(predicate()))
			{
				while (predicate())
				{
					auto ret = SleepConditionVariableSRW(&m_cv, x.get(), static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count()), 0);
					if (!ret)
					{
						if (GetLastError() == ERROR_TIMEOUT)
							return false;
						else
							assert(false);
					}
				}
				return true;
			}

			void wake_one() noexcept
			{
				WakeConditionVariable(&m_cv);
			}

			void wake_all() noexcept
			{
				WakeAllConditionVariable(&m_cv);
			}
		};

		class mutex
		{
			std::binary_semaphore sema {1};

		public:
			void lock() noexcept
			{
				sema.acquire();
			}

			bool try_lock() noexcept
			{
				return sema.try_acquire();
			}

			void unlock() noexcept
			{
				sema.release();
			}
		};
	}

	using details::srwlock;
	using details::condition_variable;
	using details::mutex;
}
