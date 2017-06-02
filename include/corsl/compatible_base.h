//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/dependencies.h"
#include "impl/errors.h"

namespace corsl
{
	namespace details
	{
		// The following resumeables are copied from winrt namespace but use
		// corsl::hresult_error
		// which allows them to be used in Vista+

		struct resume_background
		{
			bool await_ready() const noexcept
			{
				return false;
			}

			void await_resume() const noexcept
			{
			}

			void await_suspend(std::experimental::coroutine_handle<> handle) const
			{
				auto callback = [](PTP_CALLBACK_INSTANCE, void * context)
				{
					std::experimental::coroutine_handle<>::from_address(context)();
				};

				if (!TrySubmitThreadpoolCallback(callback, handle.address(), nullptr))
				{
					throw_last_error();
				}
			}
		};

		struct resume_background_long
		{
			bool await_ready() const noexcept
			{
				return false;
			}

			void await_resume() const noexcept
			{
			}

			void await_suspend(std::experimental::coroutine_handle<> handle) const
			{
				auto callback = [](PTP_CALLBACK_INSTANCE pci, void * context)
				{
					CallbackMayRunLong(pci);
					std::experimental::coroutine_handle<>::from_address(context)();
				};

				if (!TrySubmitThreadpoolCallback(callback, handle.address(), nullptr))
				{
					throw_last_error();
				}
			}
		};

		struct resume_after
		{
			explicit resume_after(winrt::Windows::Foundation::TimeSpan duration) noexcept :
			m_duration(duration)
			{
			}

			bool await_ready() const noexcept
			{
				return m_duration.count() <= 0;
			}

			void await_suspend(std::experimental::coroutine_handle<> handle)
			{
				m_timer = CreateThreadpoolTimer(callback, handle.address(), nullptr);

				if (!m_timer)
				{
					throw_last_error();
				}

				int64_t relative_count = -m_duration.count();
				SetThreadpoolTimer(winrt::get_abi(m_timer), reinterpret_cast<PFILETIME>(&relative_count), 0, 0);
			}

			void await_resume() const noexcept
			{
			}

		private:

			static void __stdcall callback(PTP_CALLBACK_INSTANCE, void * context, PTP_TIMER) noexcept
			{
				std::experimental::coroutine_handle<>::from_address(context)();
			}

			struct timer_traits : winrt::impl::handle_traits<PTP_TIMER>
			{
				static void close(type value) noexcept
				{
					CloseThreadpoolTimer(value);
				}
			};

			winrt::impl::handle<timer_traits> m_timer;
			winrt::Windows::Foundation::TimeSpan m_duration;
		};

		struct resume_on_signal
		{
			explicit resume_on_signal(HANDLE handle) noexcept :
			m_handle(handle)
			{}

			resume_on_signal(HANDLE handle, winrt::Windows::Foundation::TimeSpan timeout) noexcept :
				m_handle(handle),
				m_timeout(timeout)
			{}

			bool await_ready() const noexcept
			{
				return WaitForSingleObject(m_handle, 0) == WAIT_OBJECT_0;
			}

			void await_suspend(std::experimental::coroutine_handle<> resume)
			{
				m_resume = resume;
				m_wait = CreateThreadpoolWait(callback, this, nullptr);

				if (!m_wait)
				{
					throw_last_error();
				}

				int64_t relative_count = -m_timeout.count();
				PFILETIME file_time = relative_count != 0 ? reinterpret_cast<PFILETIME>(&relative_count) : nullptr;
				SetThreadpoolWait(winrt::get_abi(m_wait), m_handle, file_time);
			}

			bool await_resume() const noexcept
			{
				return m_result == WAIT_OBJECT_0;
			}

		private:

			static void __stdcall callback(PTP_CALLBACK_INSTANCE, void * context, PTP_WAIT, TP_WAIT_RESULT result) noexcept
			{
				auto that = static_cast<resume_on_signal *>(context);
				that->m_result = result;
				that->m_resume();
			}

			struct wait_traits : winrt::impl::handle_traits<PTP_WAIT>
			{
				static void close(type value) noexcept
				{
					CloseThreadpoolWait(value);
				}
			};

			winrt::impl::handle<wait_traits> m_wait;
			winrt::Windows::Foundation::TimeSpan m_timeout{ 0 };
			HANDLE m_handle{};
			uint32_t m_result{};
			std::experimental::coroutine_handle<> m_resume{ nullptr };
		};

		struct awaitable_base
		{
			static void __stdcall callback(PTP_CALLBACK_INSTANCE, void *, void * overlapped, ULONG result, ULONG_PTR, PTP_IO) noexcept
			{
				auto context = static_cast<awaitable_base *>(overlapped);
				context->m_result = result;
				context->m_resume();
			}

		protected:

			OVERLAPPED m_overlapped{};
			uint32_t m_result{};
			std::experimental::coroutine_handle<> m_resume{ nullptr };
		};

		struct resumable_io
		{
			resumable_io(HANDLE object) :
				m_io(CreateThreadpoolIo(object, awaitable_base::callback, nullptr, nullptr))
			{
				if (!m_io)
				{
					throw_last_error();
				}
			}

			template <typename F>
			auto start(F callback)
			{
				struct awaitable : awaitable_base, F
				{
					awaitable(PTP_IO io, F callback) noexcept :
					m_io(io),
						F(callback)
					{}

					bool await_ready() const noexcept
					{
						return false;
					}

					void await_suspend(std::experimental::coroutine_handle<> resume_handle)
					{
						m_resume = resume_handle;
						StartThreadpoolIo(m_io);

						try
						{
							(*this)(m_overlapped);
						}
						catch (...)
						{
							CancelThreadpoolIo(m_io);
							throw;
						}
					}

					uint32_t await_resume() const
					{
						if (m_result != NO_ERROR && m_result != ERROR_HANDLE_EOF)
						{
							throw hresult_error(HRESULT_FROM_WIN32(m_result));
						}

						return static_cast<uint32_t>(m_overlapped.InternalHigh);
					}

					PTP_IO m_io = nullptr;
				};

				return awaitable(get(), callback);
			}

			template <typename F>
			auto start_pending(F callback)
			{
				struct awaitable : awaitable_base, F
				{
					awaitable(PTP_IO io, F callback) noexcept :
					m_io(io),
						F(callback)
					{}

					bool await_ready() const noexcept
					{
						return false;
					}

					bool await_suspend(std::experimental::coroutine_handle<> resume_handle)
					{
						m_resume = resume_handle;
						StartThreadpoolIo(m_io);

						try
						{
							const bool pending = (*this)(m_overlapped);

							if (!pending)
							{
								CancelThreadpoolIo(m_io);
							}

							return pending;
						}
						catch (...)
						{
							CancelThreadpoolIo(m_io);
							throw;
						}
					}

					uint32_t await_resume() const
					{
						if (m_result != NO_ERROR && m_result != ERROR_HANDLE_EOF)
						{
							throw hresult_error(HRESULT_FROM_WIN32(m_result));
						}

						return static_cast<uint32_t>(m_overlapped.InternalHigh);
					}

					PTP_IO m_io = nullptr;
				};

				return awaitable(get(), callback);
			}

			PTP_IO get() const noexcept
			{
				return winrt::get_abi(m_io);
			}

		private:

			struct io_traits : winrt::impl::handle_traits<PTP_IO>
			{
				static void close(type value) noexcept
				{
					CloseThreadpoolIo(value);
				}
			};

			winrt::impl::handle<io_traits> m_io;
		};
	}

	using details::resume_background;
	using details::resume_background_long;
	using details::resume_after;
	using details::resume_on_signal;
	using details::resumable_io;

	namespace timer
	{
		inline auto operator co_await(winrt::Windows::Foundation::TimeSpan timeout)
		{
			return resume_after{ timeout };
		}
	}
}
