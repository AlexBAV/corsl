//-------------------------------------------------------------------------------------------------------
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
		template<class Awaitable>
		inline future<void> istart(result_type<void>, Awaitable task)
		{
			co_await task;
		}

		template<class T, class Awaitable>
		inline auto istart(result_type<T>, Awaitable task) -> future<T>
		{
			co_return co_await task;
		}

		template<class Awaitable>
		inline auto start(Awaitable &&awaitable)
		{
			return istart(get_result_type(awaitable), std::forward<Awaitable>(awaitable));
		}
	}
	using details::start;
}
