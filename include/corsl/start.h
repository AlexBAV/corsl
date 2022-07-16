//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/when_all_when_any_base.h"
#include "future.h"

namespace corsl
{
	namespace details
	{
		// start
		// methods "starts" an asynchronous operation that only starts in await_suspend
		template<class T, class Awaitable>
		inline auto istart(result_type<T>, Awaitable task) -> future<T>
		{
			if constexpr (std::same_as<void, T>)
				co_await task;
			else
				co_return co_await task;
		}

		template<class Awaitable>
		inline auto start(Awaitable &&awaitable)
		{
			return istart(get_result_type(awaitable), std::forward<Awaitable>(awaitable));
		}

		template<class Awaitable>
		inline auto block_get(Awaitable &&awaitable)
		{
			return start(std::forward<Awaitable>(awaitable)).get();
		}

		template<class Awaitable>
		inline void block_wait(Awaitable &&awaitable) noexcept
		{
			start(std::forward<Awaitable>(awaitable)).wait();
		}
	}
	using details::start;
	using details::block_get;
	using details::block_wait;
}
