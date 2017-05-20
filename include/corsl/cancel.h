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

#include "srwlock.h"
#include "compatible_base.h"

#include <boost/intrusive/list.hpp>

namespace corsl
{
	namespace details
	{
		namespace bi = boost::intrusive;
		class cancellation_token_source_body;
		class cancellation_token_source;

		class cancellation_token : public bi::list_base_hook<>
		{
			friend class cancellation_token_source_body;
			std::shared_ptr<cancellation_token_source_body> body;
			std::unordered_map<int, std::function<void()>> callbacks;
			srwlock lock;
			int last_id{ 0 };
			bool cancelled;

			//
			static winrt::fire_and_forget run_callback(std::function<void()> f)
			{
				co_await resume_background{};
				f();
			}

			void cancel()
			{
				std::lock_guard<srwlock> l(lock);
				cancelled = true;

				for (auto &pair : callbacks)
				{
					run_callback(std::move(pair.second));
				}
				callbacks.clear();	// callbacks are not needed anymore, as they have already fired
			}

		public:
			cancellation_token(const cancellation_token_source &) noexcept;
			~cancellation_token();

			cancellation_token(const cancellation_token &) = delete;
			cancellation_token &operator =(const cancellation_token &) = delete;

			explicit operator bool() const noexcept
			{
				return cancelled;
			}

			bool is_cancelled() const noexcept
			{
				return cancelled;
			}

			void check_cancelled() const
			{
				if (cancelled)
					throw operation_cancelled{};
			}

			template<class F>
			int subscribe(F &&f)
			{
				std::lock_guard<srwlock> l(lock);
				callbacks.emplace(last_id, std::forward<F>(f));
				return last_id++;
			}

			void unsubscribe(int handle)
			{
				std::lock_guard<srwlock> l(lock);
				callbacks.erase(handle);
			}
		};

		class cancellation_token_source_body
		{
			friend class cancellation_token;

			srwlock lock;
			bi::list<cancellation_token, bi::constant_time_size<false>> tokens;
			bool cancelled{ false };

			void add_token(cancellation_token &token) noexcept
			{
				std::lock_guard<srwlock> l(lock);
				tokens.push_back(token);
			}

			void remove_token(cancellation_token &token) noexcept
			{
				std::lock_guard<srwlock> l(lock);
				tokens.erase(tokens.s_iterator_to(token));
			}

		public:
			bool is_cancelled() const noexcept
			{
				return cancelled;
			}

			void cancel() noexcept
			{
				std::lock_guard<srwlock> l(lock);
				if (!cancelled)
				{
					cancelled = true;
					for (auto &token : tokens)
						token.cancel();
				}
			}
		};

		class cancellation_token_source
		{
			friend class cancellation_token;
			std::shared_ptr<cancellation_token_source_body> body{ std::make_shared<cancellation_token_source_body>() };

		public:
			void cancel() noexcept
			{
				body->cancel();
			}
		};

		// implementations
		inline cancellation_token::cancellation_token(const cancellation_token_source &src) noexcept :
			body{ src.body },
			cancelled{ body->is_cancelled() }
		{
			body->add_token(*this);
		}

		inline cancellation_token::~cancellation_token()
		{
			body->remove_token(*this);
		}
	}

	using details::cancellation_token_source;
	using details::cancellation_token;
}
