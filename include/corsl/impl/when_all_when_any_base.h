//-------------------------------------------------------------------------------------------------------
// Copyright (C) 2017 HHD Software Ltd.
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
		// no_result will substitute 'void' in tuple
		struct no_result {};

		// get_result_type 
		// get the coroutine result type
		// supports IAsyncAction, IAsyncOperation<T> and awaitable object that implements await_resume
		// yields result_type<T> where T is a coroutine result type
		template<class T>
		struct result_type
		{
			using type = T;
		};

		constexpr result_type<void> get_result_type(const ::winrt::Windows::Foundation::IAsyncAction &)
		{
			return {};
		}

		template<class T>
		constexpr result_type<T> get_result_type(const ::winrt::Windows::Foundation::IAsyncOperation<T> &)
		{
			return {};
		}

		// primary template handles types that do not implement await_resume:
		template<class, class = std::void_t<>>
		struct has_await_resume : std::false_type {};

		// specialization recognizes types that do implement await_resume
		template<class T>
		struct has_await_resume<T, std::void_t<decltype(std::declval<T&>().await_resume())>> : std::true_type {};

		template<class T>
		constexpr bool has_await_resume_v = has_await_resume<T>::value;

		// "external" cae
		template<class, class = std::void_t<>>
		struct has_external_await_resume : std::false_type {};

		template<class T>
		struct has_external_await_resume<T, std::void_t<decltype(await_resume(std::declval<T &>()))>> : std::true_type {};

		template<class T>
		constexpr bool has_external_await_resume_v = has_external_await_resume<T>::value;

		//
		template<class T>
		constexpr result_type<decltype(std::declval<T &>().await_resume())> get_result_type(const T &, std::enable_if_t<has_await_resume_v<T>, void *> = nullptr)
		{
			return {};
		}

		template<class T>
		constexpr result_type<decltype(await_resume(std::declval<T &>()))> get_result_type(const T &, std::enable_if_t<has_external_await_resume_v<T>, void *> = nullptr)
		{
			return {};
		}

		// Helper to get a coroutine result type for a first item in variadic sequence
		template<class First, class...Rest>
		constexpr auto get_first_result_type(const First &first, const Rest &...)
		{
			return get_result_type(first);
		}

		// Get a first type in a variadic type list
		template<class...T>
		struct get_first;

		template<class F, class...R>
		struct get_first<F, R...>
		{
			using type = F;
		};

		template<class...T>
		using get_first_t = typename get_first<T...>::type;

		template<class...Ts>
		struct are_all_same
		{
			using first_type = get_first_t<Ts...>;

			using type = std::conjunction<std::is_same<first_type, Ts>...>;
		};

		template<class...Ts>
		using are_all_same_t = typename are_all_same<Ts...>::type;

		template<class...Ts>
		constexpr bool are_all_same_v = typename are_all_same<Ts...>::type::value;
	}

	using details::no_result;
}
