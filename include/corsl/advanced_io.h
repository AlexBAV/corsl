//-------------------------------------------------------------------------------------------------------
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
		template<class D>
		class supports_timeout
		{
			struct timer_traits : winrt::impl::handle_traits<PTP_TIMER>
			{
				static void close(type value) noexcept
				{
					CloseThreadpoolTimer(value);
				}
			};

			winrt::impl::handle<timer_traits> m_timer
			{
				CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, void * context, PTP_TIMER) noexcept
			{
				static_cast<D *>(context)->on_timeout();
			}, static_cast<D *>(this), nullptr)
			};
			winrt::Windows::Foundation::TimeSpan timeout;

		protected:
			using supports_timeout_base = supports_timeout;

			supports_timeout(winrt::Windows::Foundation::TimeSpan timeout) :
				timeout{ timeout }
			{}

			void set_timer() const noexcept
			{
				if (timeout.count())
				{
					int64_t relative_count = -timeout.count();
					SetThreadpoolTimer(winrt::get_abi(m_timer), reinterpret_cast<PFILETIME>(&relative_count), 0, 0);
				}
			}

			void reset_timer() const noexcept
			{
				if (timeout.count())
				{
					SetThreadpoolTimer(winrt::get_abi(m_timer), nullptr, 0, 0);
					WaitForThreadpoolTimerCallbacks(winrt::get_abi(m_timer), TRUE);
				}
			}
		};

		class resumable_io_timeout
		{
			struct io_traits : winrt::impl::handle_traits<PTP_IO>
			{
				static void close(type value) noexcept
				{
					CloseThreadpoolIo(value);
				}
			};

			class my_awaitable_base : public OVERLAPPED
			{
			protected:
				uint32_t m_result{};
				std::experimental::coroutine_handle<> m_resume{ nullptr };
				virtual void resume() = 0;

				my_awaitable_base() : OVERLAPPED{}
				{}

			public:
				static void __stdcall callback(PTP_CALLBACK_INSTANCE, void *, void * overlapped, ULONG result, ULONG_PTR, PTP_IO) noexcept
				{
					auto context = static_cast<my_awaitable_base *>(static_cast<OVERLAPPED *>(overlapped));
					context->m_result = result;
					context->resume();
				}
			};

			template<class F>
			class awaitable : protected my_awaitable_base, protected F, protected supports_timeout<awaitable<F>>
			{
				PTP_IO m_io{ nullptr };
				HANDLE object;

				virtual void resume() override
				{
					reset_timer();
					m_resume();
				}

			public:
				awaitable(PTP_IO io, HANDLE object, F &&callback, winrt::Windows::Foundation::TimeSpan timeout) noexcept :
					m_io{ io },
					object{ object },
					F{ std::forward<F>(callback) },
					supports_timeout_base{ timeout }
				{}

				bool await_ready() const noexcept
				{
					return false;
				}

				auto await_suspend(std::experimental::coroutine_handle<> resume_handle)
				{
					m_resume = resume_handle;
					StartThreadpoolIo(m_io);

					try
					{
						return call(std::is_same<void, decltype((*this)(std::declval<OVERLAPPED &>()))>{});
					}
					catch (...)
					{
						CancelThreadpoolIo(m_io);
						throw;
					}
				}

				void call(std::true_type)
				{
					(*this)(*this);
					set_timer();
				}

				bool call(std::false_type)
				{
					if ((*this)(*this))
					{
						set_timer();
						return true;
					}
					else
					{
						CancelThreadpoolIo(m_io);
						return false;
					}
				}

				uint32_t await_resume()
				{
					if (m_result != NO_ERROR && m_result != ERROR_HANDLE_EOF)
					{
						if (m_result == ERROR_OPERATION_ABORTED)
							m_result = ERROR_TIMEOUT;
						throw operation_cancelled{};
					}

					return static_cast<uint32_t>(InternalHigh);
				}

				void on_timeout()
				{
					// cancel io
					CancelIoEx(object, this);
				}
			};

			winrt::impl::handle<io_traits> m_io;
			HANDLE object;

			//
		public:
			resumable_io_timeout(HANDLE object) :
				object{ object },
				m_io{ CreateThreadpoolIo(object, my_awaitable_base::callback, nullptr, nullptr) }
			{
				if (!m_io)
				{
					throw_last_error();
				}
			}

			template <typename F>
			auto start(F &&callback, winrt::Windows::Foundation::TimeSpan timeout)
			{
				return awaitable<F>{get(), object, std::forward<F>(callback), timeout};
			}

			PTP_IO get() const noexcept
			{
				return winrt::get_abi(m_io);
			}
		};
	}

	using details::supports_timeout;
	using details::resumable_io_timeout;
}
