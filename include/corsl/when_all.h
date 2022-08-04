//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
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
		inline fire_and_forget<> when_all_helper_single(Master &master, Awaitable task) noexcept
		{
			try
			{
				if constexpr (std::same_as<result_type<void>, get_result_type_t<Awaitable>>)
				{
					co_await task;
					master.finished(std::integral_constant<size_t, Index>{});
				}
				else
				{
					master.finished(std::integral_constant<size_t, Index>{}, co_await task);
				}
			}
			catch (...)
			{
				master.finished_exception();
			}
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
			std::coroutine_handle<> resume;
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
				when_all_awaitable_base<Awaitables...>{ std::forward<Awaitables>(awaitables)... }
			{}

			template<size_t N, class T>
			void finished(std::integral_constant<size_t, N>, const T &) noexcept
			{
				this->check_resume();
			}

			template<size_t N>
			void finished(std::integral_constant<size_t, N>) noexcept
			{
				this->check_resume();
			}

			void await_suspend(std::coroutine_handle<> handle) noexcept
			{
				this->resume = handle;
				when_all_helper(*this, std::move(this->awaitables), std::make_index_sequence<sizeof...(Awaitables)>{});
			}

			void await_resume() const
			{
				if (this->exception)
					std::rethrow_exception(std::move(this->exception));
			}
		};

		// non-void case
		template<class...Awaitables>
		struct when_all_awaitable_value : when_all_awaitable_base<Awaitables...>
		{
			template<class T>
			using transform_t = std::conditional_t<std::same_as<result_type<void>, T>, no_result, std::decay_t<typename T::type>>;

			using results_t = std::tuple<transform_t<get_result_type_t<Awaitables>>...>;
			results_t results;

			when_all_awaitable_value(Awaitables &&...awaitables) noexcept :
				when_all_awaitable_base<Awaitables...>{ std::forward<Awaitables>(awaitables)... }
			{}

			when_all_awaitable_value(when_all_awaitable_value &&o) noexcept :
				when_all_awaitable_base{ static_cast<when_all_awaitable_base &&>(o) }
			{}

			template<size_t N, class T>
			void finished(std::integral_constant<size_t, N>, T &&result) noexcept
			{
				std::get<N>(results) = std::forward<T>(result);
				this->check_resume();
			}

			template<size_t N>
			void finished(std::integral_constant<size_t, N>) noexcept
			{
				this->check_resume();
			}

			void await_suspend(std::coroutine_handle<> handle) noexcept
			{
				this->resume = handle;
				when_all_helper(*this, this->awaitables, std::make_index_sequence<sizeof...(Awaitables)>{});
			}

			results_t await_resume()
			{
				if (this->exception)
					std::rethrow_exception(std::move(this->exception));
				else
					return std::move(results);
			}
		};

		template<class...Awaitables>
		inline auto when_all(Awaitables &&...awaitables)
		{
			static_assert(sizeof...(Awaitables) >= 2, "when_all must be passed at least two arguments");

			using result_types = mp11::mp_list<get_result_type_t<Awaitables>...>;

			if constexpr (std::same_as<result_type<void>, mp11::mp_first<result_types>> && mp11::mp_apply<mp11::mp_same, result_types>::value)
				return when_all_awaitable_void{ std::forward<Awaitables>(awaitables)... };
			else
				return when_all_awaitable_value{ std::forward<Awaitables>(awaitables)... };
		}

		// 2. Range version
		// Awaitables are passed as ranges
		template<class Awaitable>
		struct range_when_all_awaitable_base
		{
			std::exception_ptr exception;
			std::vector<Awaitable> tasks_;
			std::atomic<int> counter;
			std::coroutine_handle<> resume;

			template<sr::range Range>
			range_when_all_awaitable_base(Range &&range) requires !std::copyable<Awaitable> :
				tasks_{ std::make_move_iterator(sr::begin(range)), std::make_move_iterator(sr::end(range)) },
				counter{ static_cast<int>(tasks_.size()) }
			{}

			template<sr::range Range>
			range_when_all_awaitable_base(Range &&range) requires std::copyable<Awaitable> :
				tasks_{ sr::begin(range), sr::end(range) },
				counter{ static_cast<int>(tasks_.size()) }
			{}

			range_when_all_awaitable_base(range_when_all_awaitable_base &&o) noexcept :
				exception{ std::move(o.exception) },
				tasks_{ std::move(o.tasks_) },
				counter{ o.counter.load(std::memory_order_relaxed) },
				resume{ std::move(o.resume) }
			{}

			range_when_all_awaitable_base &operator =(range_when_all_awaitable_base &&o) noexcept
			{
				using std::swap;
				swap(exception, o.exception);
				swap(tasks_, o.tasks_);
				swap(resume, o.resume);
				counter.store(o.load(std::memory_order_relaxed), std::memory_order_relaxed);

				return *this;
			}

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

		template<class Awaitable>
		struct range_when_all_awaitable_void : range_when_all_awaitable_base<Awaitable>
		{
			template<sr::range Range>
			range_when_all_awaitable_void(Range &&range) :
				range_when_all_awaitable_base<Awaitable>{ std::forward<Range>(range) }
			{}

			template<size_t N>
			void finished(std::integral_constant<size_t, N>) noexcept
			{
				this->check_resume();
			}

			void await_suspend(std::coroutine_handle<> handle) noexcept
			{
				auto tasks{ std::move(this->tasks_) };
				this->resume = handle;
				for (auto &task : tasks)
					when_all_helper_single<0>(*this, std::move(task));
			}

			void await_resume() const
			{
				if (this->exception)
					std::rethrow_exception(this->exception);
			}
		};

		// non-void
		template<class Master, class Awaitable>
		inline fire_and_forget<> range_when_all_helper_single(Master &master, Awaitable task, size_t index) noexcept
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

		template<class Awaitable>
		struct range_when_all_awaitable_value : range_when_all_awaitable_base<Awaitable>
		{
			using result_type = get_result_type_t<Awaitable>;
			using results_t = std::vector<std::decay_t<typename result_type::type>>;
			results_t results;

			template<sr::range Range>
			range_when_all_awaitable_value(Range &&range) :
				range_when_all_awaitable_base<Awaitable>{ std::forward<Range>(range) },
				results(this->tasks_.size())
			{}

			template<class T>
			void finished(size_t index, T &&result) noexcept
			{
				results[index] = std::forward<T>(result);
				this->check_resume();
			}

			void await_suspend(std::coroutine_handle<> handle) noexcept
			{
				this->resume = handle;
				auto tasks{ std::move(this->tasks_) };
				this->resume = handle;
				for (size_t i = 0; i < tasks.size(); ++i)
					range_when_all_helper_single(*this, std::move(tasks[i]), i);
			}

			results_t await_resume()
			{
				if (this->exception)
					std::rethrow_exception(this->exception);
				else
					return std::move(results);
			}
		};

		template<sr::range Range>
		inline auto when_all_range(Range &&range)
		{
			using future_type = std::decay_t<sr::range_value_t<Range>>;
			using type = get_result_type_t<future_type>;
			if constexpr (std::same_as<result_type<void>, type>)
				return range_when_all_awaitable_void<future_type> { std::forward<Range>(range) };
			else
				return range_when_all_awaitable_value<future_type> { std::forward<Range>(range) };
		}
	}

	using details::when_all;
	using details::when_all_range;
}
