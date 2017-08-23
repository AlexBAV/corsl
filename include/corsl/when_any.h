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
		///////////////////////////////////
		// when_any
		struct when_any_block_base
		{
			std::exception_ptr exception;
			std::atomic<std::experimental::coroutine_handle<>> resume{};

			void finished_exception() noexcept
			{
				if (resume.load(std::memory_order_relaxed))
				{
					auto value = resume.exchange(nullptr, std::memory_order_relaxed);
					if (value)
					{
						exception = std::current_exception();
						value();
					}
				}
			}
		};

		// void case
		struct when_any_block_void : when_any_block_base
		{
			size_t index;

			void finished(size_t index_) noexcept
			{
				if (resume.load(std::memory_order_relaxed))
				{
					auto value = resume.exchange(nullptr, std::memory_order_relaxed);
					if (value)
					{
						index = index_;
						value();
					}
				}
			}
		};

		template<class T>
		struct when_any_block_value : when_any_block_base
		{
			T result;
			size_t index;

			void finished(T &&result_, size_t index_) noexcept
			{
				if (resume.load(std::memory_order_relaxed))
				{
					auto value = resume.exchange(nullptr, std::memory_order_relaxed);
					if (value)
					{
						result = std::move(result_);
						index = index_;
						value();
					}
				}
			}
		};

		template<size_t Index, class Awaitable>
		inline fire_and_forget<> when_any_helper_single(std::shared_ptr<when_any_block_void> master, Awaitable task) noexcept
		{
			try
			{
				co_await task;
				master->finished(Index);
			}
			catch (...)
			{
				master->finished_exception();
			}
		}

		template<size_t N, class Tuple, size_t...I>
		inline void when_any_helper(std::array<std::shared_ptr<when_any_block_void>, N> &&master, Tuple &&tuple, std::index_sequence<I...>) noexcept
		{
			[[maybe_unused]] auto x = { when_any_helper_single<I>(std::get<I>(std::move(master)), std::get<I>(std::move(tuple)))... };
		}

		template<class...Awaitables>
		inline auto when_any_impl(result_type<void>, Awaitables &&...awaitables)
		{
			struct when_any_awaitable
			{
				std::shared_ptr<when_any_block_void> ptr;
				std::tuple<std::decay_t<Awaitables>...> awaitables;

				when_any_awaitable(Awaitables &&...awaitables) noexcept :
				awaitables{ std::forward<Awaitables>(awaitables)... },
					ptr{ std::make_shared<when_any_block_void>() }
				{
				}

				bool await_ready() const noexcept
				{
					return false;
				}

				void await_suspend(std::experimental::coroutine_handle<> handle)
				{
					ptr->resume.store(handle, std::memory_order_relaxed);
					std::array<std::shared_ptr<when_any_block_void>, sizeof...(Awaitables)> references;
					std::fill(references.begin(), references.end(), ptr);

					auto awaitables_local_copy = std::move(awaitables);

					using index_t = std::make_index_sequence<sizeof...(Awaitables)>;
					when_any_helper(std::move(references), std::move(awaitables_local_copy), index_t{});
				}

				size_t await_resume() const
				{
					if (ptr->exception)
						std::rethrow_exception(ptr->exception);
					else
						return ptr->index;
				}
			};

			return when_any_awaitable{ std::forward<Awaitables>(awaitables)... };
		}

		//non-void case
		template<class T, class Awaitable>
		inline fire_and_forget<> when_any_helper_single_value(std::shared_ptr<when_any_block_value<T>> master, Awaitable task, size_t index) noexcept
		{
			try
			{
				master->finished(co_await task, index);
			}
			catch (...)
			{
				master->finished_exception();
			}
		}

		template<class T, size_t N, class Tuple, size_t...I>
		inline void when_any_helper_value(std::array<std::shared_ptr<when_any_block_value<T>>, N> &&master, Tuple &&tuple, std::index_sequence<I...>) noexcept
		{
			[[maybe_unused]] auto x = { when_any_helper_single_value<T>(std::get<I>(std::move(master)), std::get<I>(std::move(tuple)), I)... };
		}

		template<class T, class...Awaitables>
		inline auto when_any_impl(result_type<T>, Awaitables &&...awaitables)
		{
			using value_type = std::decay_t<T>;
			struct when_any_awaitable
			{
				std::shared_ptr<when_any_block_value<value_type>> ptr;
				std::tuple<std::decay_t<Awaitables>...> awaitables;

				when_any_awaitable(Awaitables &&...awaitables) noexcept :
				awaitables{ std::forward<Awaitables>(awaitables)... },
					ptr{ std::make_shared<when_any_block_value<value_type>>() }
				{}

				bool await_ready() const noexcept
				{
					return false;
				}

				void await_suspend(std::experimental::coroutine_handle<> handle)
				{
					ptr->resume.store(handle, std::memory_order_relaxed);
					std::array<std::shared_ptr<when_any_block_value<value_type>>, sizeof...(Awaitables)> references;
					std::fill(references.begin(), references.end(), ptr);

					auto awaitables_copy = std::move(awaitables);
					using index_t = std::make_index_sequence<sizeof...(Awaitables)>;
					when_any_helper_value(std::move(references), std::move(awaitables_copy), index_t{});
				}

				std::pair<T, size_t> await_resume() const
				{
					if (ptr->exception)
						std::rethrow_exception(ptr->exception);
					else
						return { std::move(ptr->result), ptr->index };
				}
			};

			return when_any_awaitable{ std::forward<Awaitables>(awaitables)... };
		}

		template<class...Awaitables>
		inline auto when_any(Awaitables &&...awaitables)
		{
			static_assert(sizeof...(Awaitables) >= 2, "when_any must be passed at least two arguments");
			static_assert(are_all_same_v<decltype(get_result_type(awaitables))...>, "when_any requires all awaitables to produce the same type");

			return when_any_impl(get_first_result_type(awaitables...), std::forward<Awaitables>(awaitables)...);
		}

		// range
		// void case
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

				void await_suspend(std::experimental::coroutine_handle<> handle)
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
				std::shared_ptr<when_any_block_value<value_type>> ptr;
				std::vector<task_type> tasks;

				when_any_awaitable(const Iterator &begin, const Iterator &end) noexcept :
				tasks{ begin, end },
					ptr{ std::make_shared<when_any_block_value<value_type>>() }
				{}

				bool await_ready() const noexcept
				{
					return false;
				}

				void await_suspend(std::experimental::coroutine_handle<> handle)
				{
					ptr->resume.store(handle, std::memory_order_relaxed);
					std::vector<std::shared_ptr<when_any_block_value<value_type>>> references(tasks.size(), ptr);

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
