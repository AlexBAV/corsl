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
		// Windows SRW lock wrapped in shared_mutex-friendly class
		// While cppwinrt provides one, it does not follow shared_mutex interface and cannot
		// be used with STL std::lock_guard and std::unique_lock
		class srwlock
		{
			SRWLOCK m_lock{};
		public:
			srwlock(const srwlock &) = delete;
			srwlock & operator=(const srwlock &) = delete;
			srwlock() noexcept = default;

			void lock() noexcept
			{
				AcquireSRWLockExclusive(&m_lock);
			}

			void lock_shared() noexcept
			{
				AcquireSRWLockShared(&m_lock);
			}

			bool try_lock() noexcept
			{
				return 0 != TryAcquireSRWLockExclusive(&m_lock);
			}

			void unlock() noexcept
			{
				ReleaseSRWLockExclusive(&m_lock);
			}

			void unlock_shared() noexcept
			{
				ReleaseSRWLockShared(&m_lock);
			}
		};
	}

	using details::srwlock;
}
