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
		// Some of the following classes are based on winrt implementation but use corsl::hresult_error,
		// which allows them to be used in Vista+

		template<typename T>
		T* check_pointer(T* pointer)
		{
			if (!pointer)
				throw_last_error();

			return pointer;
		}

		namespace callback_policy
		{
			struct empty
			{
				static constexpr void init_callback(PTP_CALLBACK_INSTANCE) noexcept
				{
				}
			};

			struct store
			{
				inline static thread_local PTP_CALLBACK_INSTANCE current_callback{};

				static void init_callback(PTP_CALLBACK_INSTANCE pci) noexcept
				{
					current_callback = pci;
				}
			};
		}

		template<bool is_long = false, class CallbackPolicy = callback_policy::empty>
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
					CallbackPolicy::init_callback(pci);
					std::experimental::coroutine_handle<>::from_address(context)();
				};

				if (!TrySubmitThreadpoolCallback(callback, handle.address(), nullptr))
					throw_last_error();
			}
		};

		// A version that takes environment variable
		template<bool is_long = false, class CallbackPolicy = callback_policy::empty>
		struct __declspec(empty_bases) env_resume_background_ : resume_background_<is_long, CallbackPolicy>
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
					CallbackPolicy::init_callback(pci);
					std::experimental::coroutine_handle<>::from_address(context)();
				};

				if (!TrySubmitThreadpoolCallback(callback, handle.address(), env))
					throw_last_error();
			}
		};

		template<class CallbackPolicy>
		inline auto resume_background() noexcept
		{
			return resume_background_<false, CallbackPolicy>{};
		}

		template<class CallbackPolicy>
		inline auto resume_background_long() noexcept
		{
			return resume_background_<true, CallbackPolicy>{};
		}

		inline auto resume_background() noexcept
		{
			return resume_background<callback_policy::empty>();
		}

		inline auto resume_background_long() noexcept
		{
			return resume_background_long<callback_policy::empty>();
		}

		template<class CallbackPolicy>
		inline auto resume_background(callback_environment &ce) noexcept
		{
			return env_resume_background_<false, CallbackPolicy>{ce.get()};
		}

		template<class CallbackPolicy>
		inline auto resume_background_long(callback_environment &ce) noexcept
		{
			return env_resume_background_<true, CallbackPolicy>{ce.get()};
		}

		inline auto resume_background(callback_environment &ce) noexcept
		{
			return resume_background<callback_policy::empty>(ce);
		}

		inline auto resume_background_long(callback_environment &ce) noexcept
		{
			return resume_background_long<callback_policy::empty>(ce);
		}

		// Tmers

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

		template<class CallbackPolicy>
		struct awaitable_base
		{
			static void __stdcall callback(PTP_CALLBACK_INSTANCE pci, void *, void * overlapped, ULONG result, ULONG_PTR, PTP_IO) noexcept
			{
				CallbackPolicy::init_callback(pci);
				auto context = static_cast<awaitable_base *>(overlapped);
				context->m_result = result;
				context->m_resume();
			}

		protected:

			OVERLAPPED m_overlapped{};
			uint32_t m_result{};
			std::experimental::coroutine_handle<> m_resume{ nullptr };
		};

		template<class CallbackPolicy = callback_policy::empty>
		struct resumable_io
		{
			resumable_io(HANDLE object) :
				m_io(check_pointer(CreateThreadpoolIo(object, awaitable_base<CallbackPolicy>::callback, nullptr, nullptr)))
			{
			}

			template <class F>
			auto start(F &&callback)
			{
				struct awaitable : awaitable_base<CallbackPolicy>, F
				{
					awaitable(PTP_IO io, F &&callback) noexcept :
						m_io{ io },
						F{ std::move(callback) }
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

				return awaitable(get(), std::move(callback));
			}

			template<class F>
			auto start_pending(F &&callback)
			{
				struct awaitable : awaitable_base<CallbackPolicy>, F
				{
					awaitable(PTP_IO io, F &&callback) noexcept :
						m_io{ io },
						F{ std::move(callback) }
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

				return awaitable(get(), std::move(callback));
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

		template<class CallbackPolicy>
		inline void resume_on_background(std::experimental::coroutine_handle<> handle, PTP_CALLBACK_ENVIRON env = nullptr)
		{
			if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE pci, void * context)
			{
				CallbackPolicy::init_callback(pci);
				std::experimental::coroutine_handle<>::from_address(context)();
			}, handle.address(), env))
			{
				throw_last_error();
			}
		}

		inline void resume_on_background(std::experimental::coroutine_handle<> handle, PTP_CALLBACK_ENVIRON env = nullptr)
		{
			resume_on_background<callback_policy::empty>(handle, env);
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

		class callback
		{
			PTP_CALLBACK_INSTANCE pci;

		public:
			callback(PTP_CALLBACK_INSTANCE pci) noexcept :
				pci{ pci }
			{}

			void disassociate_current_thread() const noexcept
			{
				DisassociateCurrentThreadFromCallback(pci);
			}

			void free_library_when_callback_exits(HMODULE lib) const noexcept
			{
				FreeLibraryWhenCallbackReturns(pci, lib);
			}

			void leave_critical_section_when_callback_returns(PCRITICAL_SECTION ps) const noexcept
			{
				LeaveCriticalSectionWhenCallbackReturns(pci, ps);
			}

			void release_mutex_when_callback_returns(HANDLE mutex) const noexcept
			{
				ReleaseMutexWhenCallbackReturns(pci, mutex);
			}

			void release_mutex_when_callback_returns(const winrt::handle &mutex) const noexcept
			{
				release_mutex_when_callback_returns(mutex.get());
			}

			void release_semaphore_when_callback_returns(HANDLE semaphore, DWORD crel) const noexcept
			{
				ReleaseSemaphoreWhenCallbackReturns(pci, semaphore, crel);
			}

			void release_semaphore_when_callback_returns(const winrt::handle &semaphore, DWORD crel) const noexcept
			{
				release_semaphore_when_callback_returns(semaphore.get(), crel);
			}

			void set_event_when_callback_returns(HANDLE event) const noexcept
			{
				SetEventWhenCallbackReturns(pci, event);
			}

			void set_event_when_callback_returns(const winrt::handle &event) const noexcept
			{
				set_event_when_callback_returns(event.get());
			}

			void may_run_long() const noexcept
			{
				CallbackMayRunLong(pci);
			}
		};
	}

	using details::resume_after;
	using details::resume_on_signal;

	template<class CallbackPolicy>
	using resumable_io_ex = details::resumable_io<CallbackPolicy>;

	using resumable_io = details::resumable_io<>;

	using details::resume_background;
	using details::resume_background_long;
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

	namespace callback_policy = details::callback_policy;

	[[nodiscard]] inline details::callback get_current_callback() noexcept
	{
		return { details::callback_policy::store::current_callback };
	}
}
