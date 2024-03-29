//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "async_timer.h"
#include "tp_timer.h"
#include "cancel.h"

namespace corsl
{
	namespace details
	{
		template<class timer_t>
		class auto_cancel_timer : public timer_t
		{
			cancellation_subscription_generic subscription;

		public:
			template<class...Args>
			auto_cancel_timer(cancellation_token &token, Args &&...args) :
				timer_t{ std::forward<Args>(args)... },
				subscription{ token,[this]() noexcept {
					this->cancel();
				}
			}
			{
			}
		};
	}

	using auto_cancel_timer = details::auto_cancel_timer<details::async_timer<>>;
	using auto_cancel_tp_timer = details::auto_cancel_timer<details::tp_timer<>>;

	template<class CallbackPolicy>
	using auto_cancel_timer_ex = details::auto_cancel_timer<details::async_timer<CallbackPolicy>>;

	template<class CallbackPolicy>
	using auto_cancel_tp_timer_ex = details::auto_cancel_timer<details::tp_timer<>>;
}
