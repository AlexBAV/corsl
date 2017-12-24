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
#include "impl/promise_base.h"

#include "srwlock.h"
#include "compatible_base.h"

#include <shared_mutex>

#include <boost/intrusive/list.hpp>

namespace corsl
{
	namespace details
	{
		namespace bi = boost::intrusive;

		class cancellation_source_body;
		class cancellation_source;
		template<class F>
		class cancellation_subscription;
		struct cancellation_token_transport;

		class cancellation_subscription_base : public bi::list_base_hook<>
		{
			friend class cancellation_token;

			cancellation_token &token;
		protected:
			cancellation_subscription_base(cancellation_token &token) noexcept :
				token{ token }
			{}

			cancellation_subscription_base(const cancellation_subscription_base &) = delete;
			cancellation_subscription_base &operator =(const cancellation_subscription_base &) = delete;

			void add() noexcept;
			void remove() noexcept;

			virtual fire_and_forget<> run() = 0;
		};

		// TODO: remove std::function when MSVC starts supporting class template parameter deduction
		template<class T = void>
		class cancellation_subscription : public cancellation_subscription_base
		{
			std::function<void()> f;
			bool completed{ true };
			srwlock lock;
			condition_variable cv;

			virtual fire_and_forget<> run() override
			{
				{
					std::lock_guard<srwlock> l{ lock };
					completed = false;
				}
				co_await resume_background();
				f();
				std::lock_guard<srwlock> l{ lock };
				completed = true;
				cv.wake_one();
			}

		public:
			template<class F>
			cancellation_subscription(cancellation_token &token, F &&f) :
				cancellation_subscription_base { token },
				f{ std::forward<F>(f) }
			{
				// Exit early if token is already cancelled
				token.check_cancelled();
				add();
			}

			~cancellation_subscription()
			{
				remove();
				std::lock_guard<srwlock> l{ lock };
				cv.wait_while(lock, [this] { return !completed; });
			}
		};

		class cancellation_token : public bi::list_base_hook<>
		{
			friend class cancellation_source_body;
			friend class cancellation_subscription_base;

			std::shared_ptr<cancellation_source_body> body;
			std::experimental::coroutine_handle<promise_base0> coro{};	// associated promise

			srwlock lock;
			bi::list<cancellation_subscription_base, bi::constant_time_size<false>> callbacks;

			bool cancelled;

			//
			void cancel()
			{
				std::lock_guard<srwlock> l{ lock };
				cancelled = true;
				if (coro)
					coro.promise().cancel();

				for (auto &pair : callbacks)
					pair.run();
			}

			void add_subscription(cancellation_subscription_base &callback) noexcept
			{
				std::lock_guard<srwlock> l{ lock };
				callbacks.push_front(callback);
			}

			void remove_subscription(cancellation_subscription_base &callback) noexcept
			{
				std::lock_guard<srwlock> l{ lock };
				callbacks.erase(callbacks.s_iterator_to(callback));
			}

		public:
			cancellation_token(cancellation_token_transport &&transport);
			~cancellation_token();

			cancellation_token(const cancellation_token &) = delete;
			cancellation_token &operator =(const cancellation_token &) = delete;

			explicit operator bool() const noexcept
			{
				return is_cancelled();
			}

			bool is_cancelled() const noexcept
			{
				return cancelled;
			}

			void check_cancelled() const
			{
				if (is_cancelled())
					throw operation_cancelled{};
			}
		};

		class cancellation_source_body
		{
			friend class cancellation_token;
			friend class cancellation_source;

			mutable srwlock lock;
			bi::list<cancellation_token, bi::constant_time_size<false>> tokens;
			std::vector<std::weak_ptr<cancellation_source_body>> related_sources;
			bool cancelled{ false };

			void add_token(cancellation_token &token) noexcept
			{
				std::lock_guard<srwlock> l{ lock };
				tokens.push_back(token);
			}

			void remove_token(cancellation_token &token) noexcept
			{
				std::lock_guard<srwlock> l{ lock };
				tokens.erase(tokens.s_iterator_to(token));
			}

			void add_related(std::shared_ptr<cancellation_source_body> &related)
			{
				std::lock_guard<srwlock> l{ lock };
				related_sources.emplace_back(related);
			}

		public:
			bool is_cancelled() const noexcept
			{
				std::shared_lock<srwlock> l{ lock };
				return cancelled;
			}

			void cancel() noexcept
			{
				std::lock_guard<srwlock> l{ lock };
				if (!cancelled)
				{
					cancelled = true;
					for (auto &token : tokens)
						token.cancel();
					for (auto &related : related_sources)
					{
						auto pr = related.lock();
						if (pr)
							pr->cancel();
					}
					related_sources.clear();
				}
			}
		};

		class cancellation_source
		{
			friend class cancellation_token;
			std::shared_ptr<cancellation_source_body> body{ std::make_shared<cancellation_source_body>() };

			struct internal_t {};

			cancellation_source(internal_t, cancellation_source_body *parent)
			{
				parent->add_related(body);
			}

		public:
			cancellation_source() = default;

			void cancel() const noexcept
			{
				body->cancel();
			}

			cancellation_source create_connected_source() const
			{
				return { internal_t{}, body.get() };
			}

			bool is_cancelled() const noexcept
			{
				return body->is_cancelled();
			}
		};

		// implementations
		inline cancellation_token::cancellation_token(cancellation_token_transport &&transport) :
			body{ transport.source.body },
			cancelled{ body->is_cancelled() },
			coro{ transport.coro }
		{
			// Throw immediately if source is already cancelled
			check_cancelled();
			body->add_token(*this);
		}
			
		inline cancellation_token::~cancellation_token()
		{
			body->remove_token(*this);
		}

		inline void cancellation_subscription_base::add() noexcept
		{
			token.add_subscription(*this);
		}

		inline void cancellation_subscription_base::remove() noexcept
		{
			token.remove_subscription(*this);
		}
	}

	using details::cancellation_source;
	using details::cancellation_token;
	using details::cancellation_subscription;
}
