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

		// The following code is copied from belt/utility/charconv
		template<class Char>
		inline std::basic_string<Char> w_to_utf(std::wstring_view v, unsigned cp = CP_UTF8)
		{
			const auto resulting_size = WideCharToMultiByte(cp, 0, v.data(), static_cast<int>(v.size()), nullptr, 0, nullptr, nullptr);
			std::basic_string<Char> result;
			result.resize_and_overwrite(resulting_size, [&](Char *dest, size_t size)
				{
					WideCharToMultiByte(cp, 0, v.data(), static_cast<int>(v.size()), 
					reinterpret_cast<char *>(dest), resulting_size, nullptr, nullptr);
					return size;
				});
			return result;
		}

		inline std::string w_to_utf8(std::wstring_view v, unsigned cp = CP_UTF8)
		{
			return w_to_utf<char>(v, cp);
		}

		inline std::u8string w_to_u8(std::wstring_view v, unsigned cp = CP_UTF8)
		{
			return w_to_utf<char8_t>(v, cp);
		}

		class hresult_error
		{
			HRESULT m_code{ E_FAIL };

		public:
			constexpr hresult_error() = default;

			explicit constexpr hresult_error(const HRESULT code) noexcept :
				m_code{ code }
			{
			}

			static hresult_error last_error() noexcept
			{
				return hresult_error{ HRESULT_FROM_WIN32(GetLastError()) };
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
				if (auto pos = error.find_first_not_of(L" \t\r\n"sv); pos != std::wstring_view::npos)
					error.remove_prefix(pos);
				if (auto pos = error.find_last_not_of(L" \t\r\n"sv); pos != std::wstring_view::npos)
					error.remove_suffix(error.size() - pos - 1);
				return std::wstring{ error };
			}

			std::u8string u8message() const noexcept
			{
				return w_to_u8(message());
			}

			std::string utf8message() const noexcept
			{
				return w_to_utf8(message());
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
			if (FAILED(error)) [[unlikely]]
				throw_error(error);
		}

		inline void check_win32(DWORD error)
		{
			if (error) [[unlikely]]
				throw hresult_error{ HRESULT_FROM_WIN32(error) };
		}

		// returns false if pending, true if successful and throws on error
		inline bool check_io(BOOL result)
		{
			if (!result)
			{
				auto err = GetLastError();
				if (err != ERROR_IO_PENDING) [[unlikely]]
					throw hresult_error{ HRESULT_FROM_WIN32(err) };
				return false;
			}
			return true;
		}

		inline auto last_error() noexcept
		{
			return hresult_error::last_error();
		}

		inline void check_win32_api(BOOL res)
		{
			if (!res) [[unlikely]]
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
	using details::last_error;
}
