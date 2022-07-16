//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "dependencies.h"
#include "srwlock.h"

namespace corsl
{
	namespace details
	{
		struct __declspec(empty_bases) promise_base0
		{
			std::atomic<bool> cancelled{ false };

			bool is_cancelled() const noexcept
			{
				return cancelled.load(std::memory_order_relaxed);
			}

			void cancel() noexcept
			{
				cancelled.store(true, std::memory_order_relaxed);
			}
		};

		class cancellation_source;
		struct cancellation_token_transport
		{
			const cancellation_source &source;
			std::coroutine_handle<promise_base0> coro;

			bool await_ready() const noexcept
			{
				return true;
			}

			void await_suspend(std::coroutine_handle<>) const noexcept
			{
			}

			cancellation_token_transport await_resume()
			{
				return *this;
			}
		};
	}
}
