//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2023 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "dependencies.h"
#include <stacktrace>

// optionally define CORSL_NO_SNAPSHOTS to disable snapshot
// optionally define CORSL_RICH_ERRORS to one of the CORSL_RICH_NONE, CORSL_RICH_STACKTRACE or CORSL_RICH_SNAPSHOT
// defaults: DEBUG = CORSL_RICH_SNAPSHOT (or CORSL_RICH_STACKTRACE)
//              RELEASE = CORSL_RICH_NONE

#define CORSL_RICH_NONE 0
#define CORSL_RICH_STACKTRACE 1
#define CORSL_RICH_SNAPSHOT 2

#if !defined(CORSL_RICH_ERRORS)
#	if defined(_DEBUG) && !defined(CORSL_NO_SNAPSHOTS)
#		define CORSL_RICH_ERRORS CORSL_RICH_SNAPSHOT
#	elif defined(_DEBUG) && defined(CORSL_NO_SNAPSHOTS)
#		define CORSL_RICH_ERRORS CORSL_RICH_STACKTRACE
#	else
#		define CORSL_RICH_ERRORS CORSL_RICH_NONE
#	endif
#endif

#if !defined(CORSL_NO_SNAPSHOTS)
#	include <filesystem>
#	include <processsnapshot.h>
#	include <Dbghelp.h>
#	include <wil/resource.h>

#	pragma comment(lib,"Dbghelp.lib")
#endif

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

		template<class Char>
		inline std::wstring utf_to_w(std::basic_string_view<Char> v, unsigned cp = CP_UTF8)
		{
			static_assert(sizeof(Char) == 1, "A string must be either std::string_view or std::u8string_view");
			const auto resulting_size = MultiByteToWideChar(cp, 0, v.data(), static_cast<int>(v.size()), nullptr, 0);
			std::wstring result;
			result.resize_and_overwrite(resulting_size, [&](wchar_t *dest, size_t size)
				{
					MultiByteToWideChar(cp, 0, v.data(), static_cast<int>(v.size()), dest, resulting_size);
					return size;
				});
			return result;
		}

		namespace bases
		{
			class empty
			{
			protected:
				static std::wstring &&append_trace(std::wstring &&text)
				{
					return std::move(text);
				}
			};

			class stacktrace
			{
				std::stacktrace trace{std::stacktrace::current(2)};	// skip this object constructor

			protected:
				std::wstring append_trace(std::wstring_view message) const
				{
					std::wstring result{message};
					result.append(L"\r\n"sv);
					for (const auto &entry : trace)
					{
						const auto e = std::to_string(entry);
						result.append(utf_to_w<char>(e));
						result.append(L"\r\n"sv);
					}
					return result;
				}
			};

#if !defined(CORSL_NO_SNAPSHOTS)
			struct unique_process_snapshot
			{
				HPSS handle;

				unique_process_snapshot(HPSS handle = nullptr) noexcept :
					handle{ handle }
				{}

				~unique_process_snapshot()
				{
					close();
				}

				void close() noexcept
				{
					if (auto h = std::exchange(handle, nullptr))
					{
						if (PSS_VA_CLONE_INFORMATION pvci; ERROR_SUCCESS == PssQuerySnapshot(h, PSS_QUERY_VA_CLONE_INFORMATION, &pvci, sizeof(pvci)))
							TerminateProcess(pvci.VaCloneHandle, 0);

						[[maybe_unused]] const auto err = PssFreeSnapshot(GetCurrentProcess(), h);
						assert(err == ERROR_SUCCESS);
					}
				}

				auto get() const noexcept
				{
					return handle;
				}

				unique_process_snapshot(const unique_process_snapshot &o) noexcept
				{
					if (o.handle)
						PssDuplicateSnapshot(GetCurrentProcess(), o.handle, GetCurrentProcess(), &handle, PSS_DUPLICATE_NONE);
					else
						handle = nullptr;
				}

				unique_process_snapshot &operator =(const unique_process_snapshot &o) noexcept
				{
					close();
					if (o.handle)
						PssDuplicateSnapshot(GetCurrentProcess(), o.handle, GetCurrentProcess(), &handle, PSS_DUPLICATE_NONE);
					return *this;
				}

				unique_process_snapshot(unique_process_snapshot &&o) noexcept
				{
					if (auto h = std::exchange(o.handle, nullptr))
						PssDuplicateSnapshot(GetCurrentProcess(), h, GetCurrentProcess(), &handle, PSS_DUPLICATE_NONE);
					else
						handle = nullptr;
				}

				unique_process_snapshot &operator =(unique_process_snapshot &&o) noexcept
				{
					if (this != &o)
						std::swap(handle, o.handle);
					return *this;
				}
			};

			inline unique_process_snapshot create_process_snapshot()
			{
				const PSS_CAPTURE_FLAGS CaptureFlags = 
					PSS_CAPTURE_VA_CLONE | PSS_CAPTURE_VA_SPACE | PSS_CAPTURE_VA_SPACE_SECTION_INFORMATION |
					PSS_CAPTURE_HANDLES |
					PSS_CAPTURE_HANDLE_NAME_INFORMATION |
					PSS_CAPTURE_HANDLE_BASIC_INFORMATION |
					PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION |
					PSS_CAPTURE_HANDLE_TRACE |
					PSS_CAPTURE_THREADS |
					PSS_CAPTURE_THREAD_CONTEXT |
					PSS_CAPTURE_THREAD_CONTEXT_EXTENDED
					;

				HPSS SnapshotHandle;
				if (DWORD dwResultCode = PssCaptureSnapshot(GetCurrentProcess(), CaptureFlags, CONTEXT_ALL, &SnapshotHandle); ERROR_SUCCESS == dwResultCode)
					return unique_process_snapshot{ SnapshotHandle };
				else
					return {};
			}

			class snapshot
			{
				unique_process_snapshot handle{ create_process_snapshot() };
			protected:
				std::wstring append_trace(std::wstring_view message) const
				{
					namespace fs = std::filesystem;
					auto path = fs::temp_directory_path() / (std::to_wstring(std::chrono::system_clock::now().time_since_epoch().count()) + L".dmp"s);

					if (wil::unique_hfile file{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)})
					{
						MINIDUMP_CALLBACK_INFORMATION CallbackInfo{};
						CallbackInfo.CallbackRoutine = [](PVOID, const PMINIDUMP_CALLBACK_INPUT CallbackInput, PMINIDUMP_CALLBACK_OUTPUT CallbackOutput) -> BOOL
						{
							switch (CallbackInput->CallbackType)
							{
							case 16: // IsProcessSnapshotCallback
								CallbackOutput->Status = S_FALSE;
								break;
							}
							return true;
						};

						if (MiniDumpWriteDump(handle.get(), GetCurrentProcessId(), file.get(),
							static_cast<MINIDUMP_TYPE>(
								MiniDumpWithUnloadedModules | 
								MiniDumpWithHandleData | 
								MiniDumpWithProcessThreadData | 
								MiniDumpWithFullMemory | 
								MiniDumpIgnoreInaccessibleMemory
								),
							nullptr, nullptr, &CallbackInfo))

							return std::wstring{message} + L" (dump "s + path.native() + L")"s;
					}

					return std::wstring{message};
				}
			};
#endif
		}

		template<class T>
		concept error_class = 
			std::default_initializable<T> &&
			std::movable<T> && 
			requires(const T & ec)
		{
			{ec.message()} -> std::same_as<std::wstring>;
		};

		class hresult_error_impl
		{
			HRESULT m_code{ E_FAIL };

		public:
			hresult_error_impl() = default;

			explicit hresult_error_impl(const HRESULT code) noexcept :
				m_code{ code }
			{
			}

			HRESULT code() const noexcept
			{
				return m_code;
			}

			bool is_aborted() const noexcept
			{
				return m_code == HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
			}

			bool is_cancelled() const noexcept
			{
				return m_code == HRESULT_FROM_WIN32(ERROR_CANCELLED);
			}

			std::wstring message() const noexcept
			{
				winrt::handle_type<winrt::impl::heap_traits> message;

				const uint32_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					nullptr,
					m_code,
					0,
					reinterpret_cast<wchar_t *>(winrt::put_abi(message)),
					0,
					nullptr);

				std::wstring_view error{ message.get(), size };
				if (auto pos = error.find_first_not_of(L" \t\r\n"sv); pos != std::wstring_view::npos)
					error.remove_prefix(pos);
				if (auto pos = error.find_last_not_of(L" \t\r\n"sv); pos != std::wstring_view::npos)
					error.remove_suffix(error.size() - pos - 1);

				return std::wstring {error};
			}
		};

		template<error_class E, class Base>
		class __declspec(empty_bases)traceable_error : public Base, public E
		{
		public:
//			constexpr traceable_error() = default;

			using E::E;

			std::wstring trace() const noexcept
			{
				return this->append_trace(this->message());
			}

			std::u8string u8message() const noexcept
			{
				return w_to_u8(this->message());
			}

			std::string utf8message() const noexcept
			{
				return w_to_utf8(this->message());
			}
		};

		//static_assert(sizeof(hresult_error_impl<bases::empty>) == sizeof(HRESULT));

#if CORSL_RICH_ERRORS == CORSL_RICH_SNAPSHOT
		using hresult_error = traceable_error<hresult_error_impl, bases::snapshot>;
#elif CORSL_RICH_ERRORS == CORSL_RICH_STACKTRACE
		using hresult_error = traceable_error<hresult_error_impl, bases::stacktrace>;
#else
		using hresult_error = traceable_error<hresult_error_impl, bases::empty>;
#endif

		using hresult_fast_error = traceable_error<hresult_error_impl, bases::empty>;
		using hresult_traced_error = traceable_error<hresult_error_impl, bases::stacktrace>;
#if !defined(CORSL_NO_SNAPSHOTS)
		using hresult_snapshot_error = traceable_error<hresult_error_impl, bases::snapshot>;
#endif

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
			return hresult_error{ HRESULT_FROM_WIN32(GetLastError()) };
		}

		inline void check_win32_api(BOOL res)
		{
			if (!res) [[unlikely]]
				throw last_error();
		}

		class timer_cancelled : public operation_cancelled
		{
		};
	}

	using details::error_class;
	using details::traceable_error;

	using details::hresult_error;
	using details::hresult_fast_error;
	using details::hresult_traced_error;
#if !defined(CORSL_NO_SNAPSHOTS)
	using details::hresult_snapshot_error;
#endif

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
