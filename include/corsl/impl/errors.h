//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "dependencies.h"

namespace corsl
{
	namespace details
	{
		using namespace std::literals;
		// Simplified version of cppwinrt's hresult_error
		// May be used on Vista+
		class hresult_error
		{
			HRESULT m_code{ E_FAIL };

		public:
			constexpr hresult_error() = default;

			explicit constexpr hresult_error(const HRESULT code) noexcept :
				m_code{ code }
			{
			}

			constexpr HRESULT code() const noexcept
			{
				return m_code;
			}

			constexpr bool is_aborted() const noexcept
			{
				return m_code == HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
			}

			std::wstring message() const noexcept
			{
				winrt::handle_type<winrt::impl::heap_traits> message;

				const uint32_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					nullptr,
					m_code,
					0,//MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					reinterpret_cast<wchar_t *>(winrt::put_abi(message)),
					0,
					nullptr);

				std::wstring_view error{ message.get(), size };
				if (auto pos = error.find_first_not_of(L" \t"sv); pos != std::wstring_view::npos)
					error.remove_prefix(pos);
				if (auto pos = error.find_last_not_of(L" \t"sv); pos != std::wstring_view::npos)
					error.remove_suffix(error.size() - pos - 1);
				return std::wstring{ error };
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

		[[noreturn]] inline void throw_error(HRESULT hr)
		{
			if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
				throw operation_cancelled{};
			else
				throw hresult_error{ hr };
		}

		[[noreturn]] inline void throw_win32_error(DWORD err)
		{
			throw_error(HRESULT_FROM_WIN32(err));
		}

		[[noreturn]] inline void throw_last_error()
		{
			throw_win32_error(GetLastError());
		}

		inline void check_hresult(HRESULT error)
		{
			if (FAILED(error))
				throw_error(error);
		}

		inline void check_win32(DWORD error)
		{
			if (error)
				throw hresult_error{ HRESULT_FROM_WIN32(error) };
		}

		inline void check_io(BOOL result)
		{
			if (!result)
			{
				auto err = GetLastError();
				if (err != ERROR_IO_PENDING)
					throw hresult_error{ HRESULT_FROM_WIN32(err) };
			}
		}

		inline void check_win32_api(BOOL res)
		{
			if (!res)
				throw hresult_error{ HRESULT_FROM_WIN32(GetLastError()) };
		}

		class timer_cancelled : public operation_cancelled
		{
		};
	}

	using details::hresult_error;
	using details::operation_cancelled;
	using details::timer_cancelled;

	using details::throw_error;
	using details::throw_win32_error;
	using details::throw_last_error;
	using details::check_hresult;
	using details::check_win32;
	using details::check_io;
	using details::check_win32_api;
}
