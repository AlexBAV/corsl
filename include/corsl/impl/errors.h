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
		// Simplified version of hresult_error
		// May be used on Vista+
		class hresult_error
		{
			HRESULT m_code{ E_FAIL };

		public:
			hresult_error() = default;

			explicit hresult_error(const HRESULT code) noexcept :
			m_code{ code }
			{
			}

			HRESULT code() const noexcept
			{
				return m_code;
			}

			std::wstring message() const noexcept
			{
				winrt::impl::handle<winrt::impl::heap_traits> message;

				const uint32_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					nullptr,
					m_code,
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					reinterpret_cast<wchar_t *>(winrt::put_abi(message)),
					0,
					nullptr);

				return { winrt::get_abi(message), size };
			}
		};

		// Class is used by lightweight cancellation support
		class operation_cancelled : public hresult_error
		{
		public:
			operation_cancelled() noexcept :
				hresult_error{ HRESULT_FROM_WIN32(ERROR_CANCELLED) }
			{}
		};

		[[noreturn]] inline void throw_last_error()
		{
			throw hresult_error{ HRESULT_FROM_WIN32(GetLastError()) };
		}

		[[noreturn]] inline void throw_error(HRESULT hr)
		{
			if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
				throw operation_cancelled{};
			else
				throw hresult_error{ hr };
		}

		class timer_cancelled : public operation_cancelled
		{
		};
	}

	using details::hresult_error;
	using details::operation_cancelled;
	using details::timer_cancelled;

	using details::throw_error;
	using details::throw_last_error;
}
