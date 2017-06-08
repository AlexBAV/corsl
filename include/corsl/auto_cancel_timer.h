//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "async_timer.h"
#include "cancel.h"

namespace corsl
{
	namespace details
	{
		template<class Derived>
		struct auto_cancellation_base
		{
			using base_type = auto_cancellation_base;
			cancellation_subscription<> subscription;

			auto_cancellation_base(cancellation_token &token) :
				subscription{ token,[this]() noexcept {
					static_cast<Derived &>(*this).cancel();
				}
			}
			{
			}
		};
	}

	using auto_cancel_timer = details::async_timer<details::auto_cancellation_base>;
}
