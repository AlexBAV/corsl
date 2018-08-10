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
		class auto_cancel_timer : public async_timer
		{
			cancellation_subscription_generic subscription;

		public:
			auto_cancel_timer(cancellation_token &token) :
				subscription{ token,[this]() noexcept {
					this->cancel();
				}
			}
			{
			}
		};
	}

	using details::auto_cancel_timer;
}
