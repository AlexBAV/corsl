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

#include "thread_pool.h"

namespace corsl
{
	namespace details
	{
		// The following resumeables are copied from winrt namespace but use
		// corsl::hresult_error
		// which allows them to be used in Vista+

		template<typename T>
		T* check_pointer(T* pointer)
		{
			if (!pointer)
				throw_last_error();

			return pointer;
		}

		template<bool is_long = false>
		struct __declspec(empty_bases) resume_background_
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
					if constexpr (is_long)
						CallbackMayRunLong(pci);
					std::experimental::coroutine_handle<>::from_address(context)();
				};

				if (!TrySubmitThreadpoolCallback(callback, handle.address(), nullptr))
					throw_last_error();
			}
		};

		// A version that takes environment variable
		template<bool is_long = false>
		struct __declspec(empty_bases) env_resume_background_ : resume_background_<is_long>
		{
			PTP_CALLBACK_ENVIRON env;

			env_resume_background_(PTP_CALLBACK_ENVIRON env) noexcept :
			env{ env }
			{}

			void await_suspend(std::experimental::coroutine_handle<> handle) const
			{
				auto callback = [](PTP_CALLBACK_INSTANCE pci, void * context)
				{
					if constexpr (is_long)
						CallbackMayRunLong(pci);
					std::experimental::coroutine_handle<>::from_address(context)();
				};

				if (!TrySubmitThreadpoolCallback(callback, handle.address(), env))
					throw_last_error();
			}
		};

		inline auto resume_background() noexcept
		{
			return resume_background_<false>{};
		}

		inline auto resume_background_long() noexcept
		{
			return resume_background_<true>{};
		}

		inline auto resume_background(callback_environment &ce) noexcept
		{
			return env_resume_background_<false>{ce.get()};
		}

		inline auto resume_background_long(callback_environment &ce) noexcept
		{
			return env_resume_background_<true>{ce.get()};
		}

		struct timer_traits
		{
			using type = PTP_TIMER;

			static void close(type value) noexcept
			{
				CloseThreadpoolTimer(value);
			}

			static constexpr type invalid() noexcept
			{
				return nullptr;
			}
		};

		struct io_traits
		{
			using type = PTP_IO;

			static void close(type value) noexcept
			{
				CloseThreadpoolIo(value);
			}

			static constexpr type invalid() noexcept
			{
				return nullptr;
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
				m_timer.attach(check_pointer(CreateThreadpoolTimer(callback, handle.address(), nullptr)));
				int64_t relative_count = -m_duration.count();
				SetThreadpoolTimer(m_timer.get(), reinterpret_cast<PFILETIME>(&relative_count), 0, 0);
			}

			void await_resume() const noexcept
			{
			}

		private:
			static void __stdcall callback(PTP_CALLBACK_INSTANCE, void * context, PTP_TIMER) noexcept
			{
				std::experimental::coroutine_handle<>::from_address(context)();
			}

			winrt::handle_type<timer_traits> m_timer;
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
				m_wait.attach(check_pointer(CreateThreadpoolWait(callback, this, nullptr)));
				int64_t relative_count = -m_timeout.count();
				PFILETIME file_time = relative_count != 0 ? reinterpret_cast<PFILETIME>(&relative_count) : nullptr;
				SetThreadpoolWait(m_wait.get(), m_handle, file_time);
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

			struct wait_traits
			{
				using type = PTP_WAIT;

				static void close(type value) noexcept
				{
					CloseThreadpoolWait(value);
				}

				static constexpr type invalid() noexcept
				{
					return nullptr;
				}
			};

			winrt::handle_type<wait_traits> m_wait;
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
				m_io(check_pointer(CreateThreadpoolIo(object, awaitable_base::callback, nullptr, nullptr)))
			{
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
						if (m_result != ERROR_HANDLE_EOF)
							check_win32(m_result);

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
						if (m_result != ERROR_HANDLE_EOF)
							check_win32(m_result);

						return static_cast<uint32_t>(m_overlapped.InternalHigh);
					}

					PTP_IO m_io = nullptr;
				};

				return awaitable(get(), callback);
			}

			PTP_IO get() const noexcept
			{
				return m_io.get();
			}

			void wait_for_callbacks(bool cancel_pending_callbacks) const noexcept
			{
				WaitForThreadpoolIoCallbacks(get(), cancel_pending_callbacks);
			}
		private:
			winrt::handle_type<io_traits> m_io;
		};

		inline void resume_on_background(std::experimental::coroutine_handle<> handle, PTP_CALLBACK_ENVIRON env = nullptr)
		{
			if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE, void * context)
			{
				std::experimental::coroutine_handle<>::from_address(context)();
			}, handle.address(), env))
			{
				throw_last_error();
			}
		}

		template<bool noexcept_ = false>
		struct fire_and_forget
		{
			struct promise_type
			{
				fire_and_forget get_return_object() const noexcept
				{
					return{};
				}

				void return_void() const noexcept
				{
				}

				void unhandled_exception() const noexcept
				{
					if constexpr (noexcept_)
						std::terminate();
				}

				std::experimental::suspend_never initial_suspend() const noexcept
				{
					return{};
				}

				std::experimental::suspend_never final_suspend() const noexcept
				{
					return{};
				}
			};
		};
	}

	using details::resume_background;
	using details::resume_background_long;
	using details::resume_after;
	using details::resume_on_signal;
	using details::resumable_io;

	using details::resume_on_background;

	using fire_and_forget = details::fire_and_forget<false>;
	using fire_and_forget_noexcept = details::fire_and_forget<true>;

	namespace timer
	{
		inline auto operator co_await(winrt::Windows::Foundation::TimeSpan timeout)
		{
			return resume_after{ timeout };
		}
	}
}
