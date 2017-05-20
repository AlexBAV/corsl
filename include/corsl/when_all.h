//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/when_all_when_any_base.h"

namespace corsl
{
	namespace details
	{
		// when_all

		// 1. Variadic case
		// Awaitables are passed as direct arguments to when_all
		template<size_t Index, class Master, class Awaitable>
		inline auto when_all_helper_single(Master &master, Awaitable task) noexcept ->
			std::enable_if_t<
			std::is_same_v<
			result_type<void>,
			decltype(get_result_type(task))
			>,
			winrt::fire_and_forget
			>
		{
			try
			{
				co_await task;
				master.finished<Index>();
			}
			catch (...)
			{
				master.finished_exception();
			}
		}

		template<size_t Index, class Master, class Awaitable>
		inline auto when_all_helper_single(Master &master, Awaitable task) noexcept ->
			std::enable_if_t<
			!std::is_same_v<
			result_type<void>,
			decltype(get_result_type(task))
			>,
			winrt::fire_and_forget
			>
		{
			try
			{
				master.finished<Index>(co_await task);
			}
			catch (...)
			{
				master.finished_exception();
			};
		}

		template<class Master, class Tuple, size_t...I>
		inline void when_all_helper(Master &master, Tuple &&tuple, std::index_sequence<I...>) noexcept
		{
			[[maybe_unused]] auto x = { when_all_helper_single<I>(master, std::get<I>(std::move(tuple)))... };
		}

		template<class...Awaitables>
		struct when_all_awaitable_base
		{
			std::exception_ptr exception;
			std::atomic<int> counter;
			std::experimental::coroutine_handle<> resume;
			std::tuple<std::decay_t<Awaitables>...> awaitables;

			when_all_awaitable_base(Awaitables &&...awaitables) noexcept :
			awaitables{ std::forward<Awaitables>(awaitables)... },
				counter{ sizeof...(awaitables) }
			{}

			when_all_awaitable_base(when_all_awaitable_base &&o) noexcept :
				exception{ std::move(o.exception) },
				counter{ o.counter.load(std::memory_order_relaxed) },	// it is safe to "move" atomic this way because we don't "use" it until the final instance is allocated
				resume{ o.resume },
				awaitables{ std::move(o.awaitables) }
			{}

			void finished_exception() noexcept
			{
				if (!exception)
					exception = std::current_exception();
				check_resume();
			}

			void check_resume() noexcept
			{
				if (0 == counter.fetch_sub(1, std::memory_order_relaxed) - 1)
					resume();
			}

			bool await_ready() const noexcept
			{
				return false;
			}
		};

		// void case
		template<class...Awaitables>
		struct when_all_awaitable_void : when_all_awaitable_base<Awaitables...>
		{
			when_all_awaitable_void(Awaitables &&...awaitables) noexcept :
			when_all_awaitable_base{ std::forward<Awaitables>(awaitables)... }
			{}

			when_all_awaitable_void(when_all_awaitable_void &&o) noexcept :
				when_all_awaitable_base{ static_cast<when_all_awaitable_base &&>(o) }
			{}

			template<size_t, class T>
			void finished(const T &) noexcept
			{
				check_resume();
			}

			template<size_t>
			void finished() noexcept
			{
				check_resume();
			}

			void await_suspend(std::experimental::coroutine_handle<> handle) noexcept
			{
				resume = handle;
				using index_t = std::make_index_sequence<sizeof...(Awaitables)>;
				when_all_helper(*this, std::move(awaitables), index_t{});
			}

			void await_resume() const
			{
				if (exception)
					std::rethrow_exception(exception);
			}
		};

		template<class...Awaitables>
		inline auto when_all_impl(std::true_type, Awaitables &&...awaitables)
		{
			return when_all_awaitable_void<Awaitables...> { std::forward<Awaitables>(awaitables)... };
		}

		// non-void case
		template<class...Awaitables>
		struct when_all_awaitable_value : when_all_awaitable_base<Awaitables...>
		{
			template<class T>
			struct transform
			{
				using type = std::conditional_t<std::is_same_v<result_type<void>, T>, no_result, std::decay_t<typename T::type>>;
			};

			template<class T>
			using transform_t = typename transform<T>::type;

			template<class...Ts>
			struct get_results_type
			{
				using type = std::tuple<transform_t<Ts>...>;
			};

			using results_t = typename get_results_type<decltype(get_result_type(std::declval<Awaitables>()))...>::type;
			results_t results;

			when_all_awaitable_value(Awaitables &&...awaitables) noexcept :
			when_all_awaitable_base{ std::forward<Awaitables>(awaitables)... }
			{}

			when_all_awaitable_value(when_all_awaitable_value &&o) noexcept :
				when_all_awaitables_base{ static_cast<when_all_awaitable_base &&>(o) }
			{}

			template<size_t index, class T>
			void finished(T &&result) noexcept
			{
				std::get<index>(results) = std::forward<T>(result);
				check_resume();
			}

			template<size_t>
			void finished() noexcept
			{
				check_resume();
			}

			void await_suspend(std::experimental::coroutine_handle<> handle) noexcept
			{
				resume = handle;
				using index_t = std::make_index_sequence<sizeof...(Awaitables)>;
				when_all_helper(*this, awaitables, index_t{});
			}

			results_t await_resume()
			{
				if (exception)
					std::rethrow_exception(exception);
				else
					return std::move(results);
			}
		};

		template<class...Awaitables>
		inline auto when_all_impl(std::false_type, Awaitables &&...awaitables)
		{
			return when_all_awaitable_value<Awaitables...> { std::forward<Awaitables>(awaitables)... };
		}

		template<class...Awaitables>
		inline auto when_all(Awaitables &&...awaitables)
		{
			static_assert(sizeof...(Awaitables) >= 2, "when_all must be passed at least two arguments");
			using first_type = decltype(get_first_result_type(awaitables...));

			return when_all_impl(
				std::conjunction<
				std::is_same<result_type<void>, first_type>,
				are_all_same_t<decltype(get_result_type(awaitables))...>
				>{}, std::forward<Awaitables>(awaitables)...);
		}

		// 2. Range version
		// Awaitables are passed as a pair of iterators

		// void case
		template<class Iterator>
		struct range_when_all_awaitable_base
		{
			using value_type = typename std::iterator_traits<Iterator>::value_type;

			std::exception_ptr exception;
			std::vector<value_type> tasks_;
			std::atomic<int> counter;
			std::experimental::coroutine_handle<> resume;

			range_when_all_awaitable_base(const Iterator &begin, const Iterator &end) :
				tasks_{ begin,end },
				counter{ static_cast<int>(tasks_.size()) }
			{}

			range_when_all_awaitable_base(range_when_all_awaitable_base &&o) noexcept :
				exception{ std::move(o.exception) },
				counter{ o.counter.load(std::memory_order_relaxed) },	// it is safe to "move" atomic this way because we don't "use" it until the final instance is allocated
				resume{ o.resume },
				tasks_{ std::move(o.tasks_) }
			{}

			void finished_exception() noexcept
			{
				if (!exception)
					exception = std::current_exception();
				check_resume();
			}

			void check_resume() noexcept
			{
				if (0 == counter.fetch_sub(1, std::memory_order_relaxed) - 1)
					resume();
			}

			bool await_ready() const noexcept
			{
				return tasks_.empty();
			}
		};

		template<class Iterator>
		struct range_when_all_awaitable_void : range_when_all_awaitable_base<Iterator>
		{
			range_when_all_awaitable_void(const Iterator &begin, const Iterator &end) :
				range_when_all_awaitable_base{ begin,end }
			{}

			range_when_all_awaitable_void(range_when_all_awaitable_void &&o) noexcept :
				range_when_all_awaitable_base{ static_cast<range_when_all_awaitable_base &&>(o) }
			{}

			template<size_t>
			void finished() noexcept
			{
				check_resume();
			}

			void await_suspend(std::experimental::coroutine_handle<> handle) noexcept
			{
				auto tasks{ std::move(tasks_) };
				resume = handle;
				for (auto &task : tasks)
					when_all_helper_single<0>(*this, std::move(task));
			}

			void await_resume() const
			{
				if (exception)
					std::rethrow_exception(exception);
			}
		};

		// non-void
		template<class Master, class Awaitable>
		inline winrt::fire_and_forget range_when_all_helper_single(Master &master, Awaitable task, size_t index) noexcept
		{
			try
			{
				master.finished(index, co_await task);
			}
			catch (...)
			{
				master.finished_exception();
			};
		}

		template<class Iterator>
		struct range_when_all_awaitable_value : range_when_all_awaitable_base<Iterator>
		{
			using result_type = decltype(get_result_type(*tasks_.begin()));
			using results_t = std::vector<std::decay_t<typename result_type::type>>;
			results_t results;

			range_when_all_awaitable_value(const Iterator &begin, const Iterator &end) :
				range_when_all_awaitable_base{ begin,end },
				results(tasks_.size())
			{}

			range_when_all_awaitable_value(range_when_all_awaitable_value &&o) noexcept :
				range_when_all_awaitables_base{ static_cast<range_when_all_awaitable_base &&>(o) }
			{}

			template<class T>
			void finished(size_t index, T &&result) noexcept
			{
				results[index] = std::forward<T>(result);
				check_resume();
			}

			void await_suspend(std::experimental::coroutine_handle<> handle) noexcept
			{
				resume = handle;
				auto tasks{ std::move(tasks_) };
				resume = handle;
				for (size_t i = 0; i < tasks.size(); ++i)
					range_when_all_helper_single(*this, std::move(tasks[i]), i);
			}

			results_t await_resume()
			{
				if (exception)
					std::rethrow_exception(exception);
				else
					return std::move(results);
			}
		};

		template<class Iterator>
		inline auto when_all_range_impl(std::true_type, const Iterator &begin, const Iterator &end)
		{
			return range_when_all_awaitable_void<Iterator>{ begin, end };
		}

		template<class Iterator>
		inline auto when_all_range_impl(std::false_type, const Iterator &begin, const Iterator &end)
		{
			return range_when_all_awaitable_value<Iterator>{ begin, end };
		}

		template<class Iterator>
		inline auto when_all_range(const Iterator &begin, const Iterator &end)
		{
			using type = decltype(get_result_type(*begin));

			return when_all_range_impl(std::is_same<result_type<void>, type>{}, begin, end);
		}
	}

	using details::when_all;
	using details::when_all_range;
}
