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
		enum class status_t
		{
			running,
			ready,
			exception
		};

		struct promise_base0
		{
			srwlock lock;
			std::experimental::coroutine_handle<> resume{};
			std::exception_ptr exception;
			std::atomic<int> use_count{ 0 };
			std::atomic<bool> cancelled{ false };

			status_t status{ status_t::running };

			bool is_ready(std::unique_lock<srwlock> &) const noexcept
			{
				return status != status_t::running;
			}

			bool is_cancelled() const noexcept
			{
				return cancelled.load(std::memory_order_relaxed);
			}

			void cancel() noexcept
			{
				cancelled.store(true, std::memory_order_relaxed);
			}

			void check_resume(std::unique_lock<srwlock> &&l) noexcept
			{
				if (resume)
				{
					l.unlock();
					resume();
				}
			}

			void set_exception(std::exception_ptr exception_) noexcept
			{
				std::unique_lock<srwlock> l{ lock };
				exception = exception_;
				status = status_t::exception;
				check_resume(std::move(l));
			}

			void check_exception()
			{
				if (status == status_t::exception && exception)
					std::rethrow_exception(exception);
			}
		};

		class cancellation_token_source;
		struct cancellation_token_transport
		{
			const cancellation_token_source &source;
			promise_base0 *promise;

			bool await_ready() const noexcept
			{
				return true;
			}

			void await_suspend(std::experimental::coroutine_handle<>) const noexcept
			{
			}

			cancellation_token_transport await_resume()
			{
				return *this;
			}
		};
	}
}
