//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/when_all_when_any_base.h"

#include <variant>

#include <boost/mp11/list.hpp>
#include <boost/mp11/algorithm.hpp>

namespace corsl
{
	namespace details
	{
		///////////////////////////////////
		// when_any
		template<class Result>
		struct when_any_block
		{
			std::atomic<std::coroutine_handle<>> resume{};

			Result result;
			std::exception_ptr exception;
			size_t index;

			void finished_exception() noexcept
			{
				auto continuation = resume.exchange(nullptr, std::memory_order_relaxed);
				if (continuation)
				{
					exception = std::current_exception();
					continuation();
				}
			}

			void finished(size_t index_) noexcept
			{
				auto continuation = resume.exchange(nullptr, std::memory_order_relaxed);
				if (continuation)
				{
					index = index_;
					continuation();
				}
			}

			template<class V>
			void finished(V &&result_, size_t index_) noexcept
			{
				auto continuation = resume.exchange(nullptr, std::memory_order_relaxed);
				if (continuation)
				{
					result = std::forward<V>(result_);
					index = index_;
					continuation();
				}
			}
		};

		template<class T, class Awaitable>
		inline fire_and_forget<> when_any_helper_single(std::shared_ptr<when_any_block<T>> master, Awaitable task, size_t index) noexcept
		{
			try
			{
				if constexpr (std::is_same_v<result_type<void>, decltype(get_result_type(task))>)
				{
					co_await task;
					master->finished(index);
				}
				else
				{
					master->finished(co_await task, index);
				}
			}
			catch (...)
			{
				master->finished_exception();
			}
		}

		template<class T, size_t N, class Tuple, size_t...I>
		inline void when_any_helper(std::array<std::shared_ptr<when_any_block<T>>, N> &&master, Tuple &&tuple, std::index_sequence<I...>) noexcept
		{
			(..., when_any_helper_single<T>(std::get<I>(std::move(master)), std::get<I>(std::move(tuple)), I));
		}

		template<class Result, class... Awaitables>
		struct when_any_awaitable
		{
			using any_block_t = when_any_block<Result>;
			static constexpr size_t N = sizeof...(Awaitables);
			static constexpr bool is_void = std::is_same_v<Result, result_type<void>>;

			std::shared_ptr<any_block_t> ptr{ std::make_shared<any_block_t>() };
			std::tuple<std::decay_t<Awaitables>...> awaitables;

			// If you see the compilation error on the constructor, check if you are trying to pass corsl::future as NOT an rvalue
			template<class...Args>
			when_any_awaitable(Args &&...args) noexcept :
				awaitables{ std::forward<Args>(args)... }
			{}

			bool await_ready() const noexcept
			{
				return false;
			}

			void await_suspend(std::coroutine_handle<> handle)
			{
				ptr->resume.store(handle, std::memory_order_relaxed);
				std::array<std::shared_ptr<any_block_t>, N> references;
				std::fill(references.begin(), references.end(), ptr);

				auto awaitables_copy = std::move(awaitables);
				using index_t = std::make_index_sequence<N>;
				when_any_helper(std::move(references), std::move(awaitables_copy), index_t{});
			}

			template<bool cond = is_void>
			std::enable_if_t<cond, size_t> iresume() const
			{
				return ptr->index;
			}

			template<bool cond = is_void>
			std::enable_if_t<!cond, std::pair<size_t, Result>> iresume() const
			{
				return { ptr->index, std::move(ptr->result) };
			}

			auto await_resume() const
			{
				if (ptr->exception)
					std::rethrow_exception(ptr->exception);
				else
					return iresume<>();
			}
		};

		template<class Result, class...Awaitables>
		inline auto when_any_impl(Awaitables &&...awaitables)
		{
			return when_any_awaitable<Result, Awaitables...>{ std::forward<Awaitables>(awaitables)... };
		}

		template<class...Awaitables>
		inline auto when_any(Awaitables &&...awaitables)
		{
			static_assert(sizeof...(Awaitables) >= 2, "when_any must be passed at least two arguments");

			using namespace boost::mp11;
			using result_types = mp_list<decltype(get_result_type(awaitables))...>;

			if constexpr (mp_apply<mp_same, result_types>{})
			{
				if constexpr(std::is_same_v<void, invoke_result<mp_first<result_types>>>)
					return when_any_impl<mp_first<result_types>>(std::forward<Awaitables>(awaitables)...);
				else
					return when_any_impl<invoke_result<mp_first<result_types>>>(std::forward<Awaitables>(awaitables)...);
			}
			else
			{
				using filtered_types = mp_transform<
					invoke_result,
					mp_unique<mp_remove<result_types, result_type<void>>>
				>;
				using value_type = std::conditional_t<mp_size<filtered_types>::value == 1, mp_first<filtered_types>, std::variant<filtered_types>>;
				return when_any_impl<value_type>(std::forward<Awaitables>(awaitables)...);
			}
		}

		// range
		// void case
		using when_any_block_void = when_any_block<result_type<void>>;

		template<class Awaitable>
		inline fire_and_forget<> range_when_any_helper_single(std::shared_ptr<when_any_block_void> master, Awaitable task, size_t index) noexcept
		{
			try
			{
				co_await task;
				master->finished(index);
			}
			catch (...)
			{
				master->finished_exception();
			}
		}

		template<class Iterator>
		inline auto range_when_any_impl(result_type<void>, const Iterator &begin, const Iterator &end)
		{
			struct when_any_awaitable
			{
				using value_type = std::decay_t<typename std::iterator_traits<Iterator>::value_type>;

				std::shared_ptr<when_any_block_void> ptr;
				std::vector<value_type> awaitables;

				when_any_awaitable(const Iterator &begin, const Iterator &end) :
					awaitables{ begin, end },
					ptr{ std::make_shared<when_any_block_void>() }
				{
				}

				bool await_ready() const noexcept
				{
					return false;
				}

				void await_suspend(std::coroutine_handle<> handle)
				{
					ptr->resume.store(handle, std::memory_order_relaxed);
					std::vector<std::shared_ptr<when_any_block_void>> references(awaitables.size(), ptr);

					auto awaitables_local_copy = std::move(awaitables);
					for (size_t i = 0; i < references.size(); ++i)
					{
						range_when_any_helper_single(std::move(references[i]), std::move(awaitables_local_copy[i]), i);
					}
				}

				size_t await_resume() const
				{
					if (ptr->exception)
						std::rethrow_exception(ptr->exception);
					else
						return ptr->index;
				}
			};

			return when_any_awaitable{ begin, end };
		}

		//non-void case
		template<class T, class Iterator>
		inline auto range_when_any_impl(result_type<T>, const Iterator &begin, const Iterator &end)
		{
			using task_type = typename std::iterator_traits<Iterator>::value_type;
			using value_type = std::decay_t<T>;
			struct when_any_awaitable
			{
				std::shared_ptr<when_any_block<value_type>> ptr;
				std::vector<task_type> tasks;

				when_any_awaitable(const Iterator &begin, const Iterator &end) noexcept :
				tasks{ begin, end },
					ptr{ std::make_shared<when_any_block<value_type>>() }
				{}

				bool await_ready() const noexcept
				{
					return false;
				}

				void await_suspend(std::coroutine_handle<> handle)
				{
					ptr->resume.store(handle, std::memory_order_relaxed);
					std::vector<std::shared_ptr<when_any_block<value_type>>> references(tasks.size(), ptr);

					auto tasks_copy = std::move(tasks);
					for (size_t i = 0; i < tasks_copy.size(); ++i)
						when_any_helper_single_value(std::move(references[i]), std::move(tasks_copy[i]), i);
				}

				std::pair<T, size_t> await_resume() const
				{
					if (ptr->exception)
						std::rethrow_exception(ptr->exception);
					else
						return { std::move(ptr->result), ptr->index };
				}
			};

			return when_any_awaitable{ begin, end };
		}

		//

		template<class Iterator>
		inline auto when_any_range(const Iterator &begin, const Iterator &end)
		{
			return range_when_any_impl(get_result_type(*begin), begin, end);
		}
	}

	using details::when_any;
	using details::when_any_range;
}
