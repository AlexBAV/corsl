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
		class thread_pool
		{
			struct pool_handle : winrt::impl::handle_traits<PTP_POOL>
			{
				constexpr static type invalid() noexcept
				{
					return nullptr;
				}

				static void close(type handle) noexcept
				{
					CloseThreadpool(handle);
				}
			};

			winrt::impl::handle<pool_handle> pool{ CreateThreadpool(nullptr) };

		public:
			thread_pool()
			{
				if (!pool)
					throw std::bad_alloc();
			}

			thread_pool(unsigned min_threads, unsigned max_threads) : thread_pool()
			{
				assert(min_threads <= max_threads && "Invalid arguments passed");
				check_win32_api(SetThreadpoolThreadMinimum(pool.get(), min_threads));
				SetThreadpoolThreadMaximum(pool.get(), max_threads);
			}

			PTP_POOL get() const noexcept
			{
				return pool.get();
			}
		};

		class callback_environment
		{
			TP_CALLBACK_ENVIRON tphandle;

		public:
			callback_environment() noexcept
			{
				InitializeThreadpoolEnvironment(&tphandle);
			}

			callback_environment(const thread_pool &pool, TP_CALLBACK_PRIORITY priority = TP_CALLBACK_PRIORITY_NORMAL) noexcept : callback_environment()
			{
				set_pool(pool);
				if (priority != TP_CALLBACK_PRIORITY_NORMAL)
					set_callback_priority(priority);
			}

			~callback_environment()
			{
				DestroyThreadpoolEnvironment(&tphandle);
			}

			// The thread pool is not copiable, nor moveable
			callback_environment(const callback_environment &) = delete;
			callback_environment &operator =(const callback_environment &) = delete;

			PTP_CALLBACK_ENVIRON get() noexcept
			{
				return &tphandle;
			}

			void set_library(PVOID library) noexcept
			{
				SetThreadpoolCallbackLibrary(&tphandle, library);
			}

			void set_callback_priority(TP_CALLBACK_PRIORITY priority) noexcept
			{
				SetThreadpoolCallbackPriority(&tphandle, priority);
			}

			void set_pool(const thread_pool &pool) noexcept
			{
				SetThreadpoolCallbackPool(&tphandle, pool.get());
			}
		};
	}

	using details::thread_pool;
	using details::callback_environment;
}
