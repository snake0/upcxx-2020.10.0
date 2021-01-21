#ifndef _cfdca0c3_057d_4ee9_9c82_b68e4bc96a0f
#define _cfdca0c3_057d_4ee9_9c82_b68e4bc96a0f

#include <upcxx/backend.hpp>
#include <upcxx/bind.hpp>
#include <upcxx/completion.hpp>
#include <upcxx/future.hpp>
#include <upcxx/team.hpp>

namespace upcxx {
  //////////////////////////////////////////////////////////////////////
  // detail::rpc_remote_results
  
  namespace detail {
    template<typename Call, typename = void>
    struct rpc_remote_results {
      // no `type`
    };
    template<typename Fn, typename ...Arg>
    struct rpc_remote_results<
        Fn(Arg...),
        decltype(
          std::declval<typename binding<Fn>::off_wire_type>()(
            std::declval<typename binding<Arg>::off_wire_type>()...
          ),
          void()
        )
      > {
      using type = typename decltype(
          upcxx::apply_as_future(
            std::declval<typename binding<Fn>::off_wire_type>(),
            std::declval<typename binding<Arg>::off_wire_type>()...
          )
        )::results_type;
    };
  }
  
  //////////////////////////////////////////////////////////////////////
  // detail::is_completions

  namespace detail {
    // Used to distinguish completions from function objects in rpc
    // overloads.
    template<typename T>
    struct is_completions : std::false_type {};

    template<typename ...Cxs>
    struct is_completions<completions<Cxs...>> : std::true_type {};
  }

  //////////////////////////////////////////////////////////////////////
  // rpc_ff

  namespace detail {
    struct rpc_ff_event_values {
      template<typename Event>
      using tuple_t = std::tuple<>;
    };

    template<typename Call, typename Cxs,
             typename = typename rpc_remote_results<Call>::type>
    struct rpc_ff_return;
    
    template<typename Call, typename ...Cx, typename Results>
    struct rpc_ff_return<Call, completions<Cx...>, Results> {
      using type = typename detail::completions_returner<
          /*EventPredicate=*/detail::event_is_here,
          /*EventValues=*/detail::rpc_ff_event_values,
          completions<Cx...>
        >::return_t;
    };

    // SFINAE-avoiding return-type computation for rpc_ff.
    template<typename Call, typename Cxs, typename = void>
    struct rpc_ff_return_no_sfinae {
      using type = void;
      static const bool value = false; // whether Call is valid
    };

    template<typename Fn, typename Cxs, typename ...Arg>
    struct rpc_ff_return_no_sfinae<
        Fn(Arg...), Cxs,
        decltype(
          std::declval<typename detail::rpc_ff_return<Fn(Arg...), Cxs>::type>(),
          void()
        )
      > {
      using type = typename detail::rpc_ff_return<Fn(Arg...), Cxs>::type;
      static const bool value = true;
    };
  }
  
  // defaulted completions
  template<typename Fn, typename ...Arg>
  auto rpc_ff(const team &tm, intrank_t recipient, Fn &&fn, Arg &&...args)
    // computes our return type, but SFINAE's out if fn is a completions type
    -> typename std::enable_if<
         !detail::is_completions<Fn>::value,
         typename detail::rpc_ff_return_no_sfinae<Fn(Arg...), completions<>>::type
       >::type {

    static_assert(
      detail::trait_forall<
          detail::is_not_array,
          Arg...
        >::value,
      "Arrays may not be passed as arguments to rpc_ff. "
      "To send the contents of an array, use upcxx::make_view() to construct a upcxx::view over the elements."
    );

    static_assert(
      detail::trait_forall<
          is_serializable,
          typename binding<Arg>::on_wire_type...
        >::value,
      "All rpc arguments must be Serializable."
    );

    static_assert(
      detail::rpc_ff_return_no_sfinae<Fn(Arg...), completions<>>::value,
      "function object provided to rpc_ff cannot be invoked on the given arguments as rvalue references "
      "(after deserialization of the function object and arguments). "
      "Note: make sure that the function object does not have any non-const lvalue-reference parameters."
    );
      
    static_assert(
      detail::trait_forall<
         detail::type_respects_static_size_limit,
         typename binding<Arg>::on_wire_type...
       >::value,
      UPCXX_STATIC_ASSERT_RPC_MSG(rpc_ff)
    );

    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT(recipient >= 0 && recipient < tm.rank_n(),
      "rpc_ff(team, recipient, ...) requires recipient in [0, team.rank_n()-1] == [0, " << tm.rank_n()-1 << "], but given: " << recipient);

    backend::template send_am_master<progress_level::user>(
      tm, recipient,
      upcxx::bind_rvalue_as_lvalue(std::forward<Fn>(fn), std::forward<Arg>(args)...)
    );
  }
  
  template<typename Fn, typename ...Arg>
  auto rpc_ff(intrank_t recipient, Fn &&fn, Arg &&...args)
    // computes our return type, but SFINAE's out if fn is a completions type
    -> typename std::enable_if<
         !detail::is_completions<Fn>::value,
         typename detail::rpc_ff_return_no_sfinae<Fn(Arg...), completions<>>::type
       >::type {

    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT(recipient >= 0 && recipient < world().rank_n(),
      "rpc_ff(recipient, ...) requires recipient in [0, rank_n()-1] == [0, " << world().rank_n()-1 << "], but given: " << recipient);

    return rpc_ff(world(), recipient, std::forward<Fn>(fn), std::forward<Arg>(args)...);
  }

  // explicit completions
  template<typename Cxs, typename Fn, typename ...Arg>
  UPCXX_NODISCARD
  auto rpc_ff(const team &tm, intrank_t recipient, Cxs &&cxs, Fn &&fn, Arg &&...args)
    // computes our return type, but SFINAE's out if cxs is not a completions type
    -> typename std::enable_if<
         detail::is_completions<typename std::decay<Cxs>::type>::value,
         typename detail::rpc_ff_return_no_sfinae<Fn(Arg...), typename std::decay<Cxs>::type>::type
       >::type {
    using CxsDecayed = typename std::decay<Cxs>::type;

    static_assert(
      detail::trait_forall<
          detail::is_not_array,
          Arg...
        >::value,
      "Arrays may not be passed as arguments to rpc_ff. "
      "To send the contents of an array, use upcxx::make_view() to construct a upcxx::view over the elements."
    );

    static_assert(
      detail::trait_forall<
          is_serializable,
          typename binding<Arg>::on_wire_type...
        >::value,
      "All rpc arguments must be Serializable."
    );
      
    static_assert(
      detail::rpc_ff_return_no_sfinae<Fn(Arg...), CxsDecayed>::value,
      "function object provided to rpc_ff cannot be invoked on the given arguments as rvalue references "
      "(after deserialization of the function object and arguments). "
      "Note: make sure that the function object does not have any non-const lvalue-reference parameters."
    );

    static_assert(
      detail::trait_forall<
         detail::type_respects_static_size_limit,
         typename binding<Arg>::on_wire_type...
       >::value,
      UPCXX_STATIC_ASSERT_RPC_MSG(rpc_ff)
    );

    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT(recipient >= 0 && recipient < tm.rank_n(),
      "rpc_ff(team, recipient, ...) requires recipient in [0, team.rank_n()-1] == [0, " << tm.rank_n()-1 << "], but given: " << recipient);

    UPCXX_ASSERT_ALWAYS(
      (!detail::completions_has_event<CxsDecayed, remote_cx_event>::value &&
       !detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
      "rpc_ff does not support remote or operation completion."
    );

    auto state = detail::completions_state<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rpc_ff_event_values,
        CxsDecayed
      >{std::forward<Cxs>(cxs)};
    
    auto returner = detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::rpc_ff_event_values,
        CxsDecayed
      >{state};
    
    backend::template send_am_master<progress_level::user>(
      tm, recipient,
      upcxx::bind_rvalue_as_lvalue(std::forward<Fn>(fn), std::forward<Arg>(args)...)
    );
    
    // send_am_master doesn't support async source-completion, so we know
    // its trivially satisfied.
    state.template operator()<source_cx_event>();
    
    return returner();
  }
  
  template<typename Cxs, typename Fn, typename ...Arg>
  UPCXX_NODISCARD
  auto rpc_ff(intrank_t recipient, Cxs &&cxs, Fn &&fn, Arg &&...args)
    // computes our return type, but SFINAE's out if cxs is not a completions type
    -> typename std::enable_if<
         detail::is_completions<typename std::decay<Cxs>::type>::value,
         typename detail::rpc_ff_return_no_sfinae<Fn(Arg...), typename std::decay<Cxs>::type>::type
       >::type {
  
    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT(recipient >= 0 && recipient < world().rank_n(),
      "rpc_ff(recipient, ...) requires recipient in [0, rank_n()-1] == [0, " << world().rank_n()-1 << "], but given: " << recipient);

    return rpc_ff(world(), recipient, std::forward<Cxs>(cxs), std::forward<Fn>(fn), std::forward<Arg>(args)...);
  }
  
  //////////////////////////////////////////////////////////////////////
  // rpc
  
  namespace detail {
    template<typename PointerToLpcStalled>
    struct rpc_recipient_after;

    template<typename ...T>
    struct rpc_recipient_after<detail::lpc_dormant<T...>*> {
      intrank_t initiator;
      detail::lpc_dormant<T...> *remote_lpc;
      
      template<typename ...Arg>
      void operator()(Arg &&...arg) const {
        backend::template send_awaken_lpc(
          upcxx::world(), initiator,
          remote_lpc, std::tuple<Arg&&...>(std::forward<Arg>(arg)...)
        );
      }
    };

    template<typename Call>
    struct rpc_event_values;

    template<typename Fn, typename ...Arg>
    struct rpc_event_values<Fn(Arg...)> {
      using results_tuple = typename rpc_remote_results<Fn(Arg...)>::type;
      
      static_assert(
        is_serializable<results_tuple>::value,
        "rpc return values must be Serializable."
      );
      
      static_assert(
        (detail::binding_all_immediate<results_tuple>::value &&
         detail::trait_forall_tupled<serialization_references_buffer_not, results_tuple>::value),
        "rpc return values may not have type dist_object<T>&, team&, or view<T>."
      );
      
      template<typename Event>
      using tuple_t = typename std::conditional<
          std::is_same<Event, operation_cx_event>::value,
          /*Event == operation_cx_event:*/deserialized_type_t<results_tuple>,
          /*Event != operation_cx_event:*/std::tuple<>
        >::type;
    };
    
    // Computes return type for rpc.
    template<typename Call, typename Cxs,
             typename = typename rpc_remote_results<Call>::type>
    struct rpc_return;
    
    template<typename Call, typename ...Cx, typename Result>
    struct rpc_return<Call, completions<Cx...>, Result> {
      using type = typename detail::completions_returner<
          /*EventPredicate=*/detail::event_is_here,
          /*EventValues=*/detail::rpc_event_values<Call>,
          completions<Cx...>
        >::return_t;
    };

    // SFINAE-avoiding return-type computation for rpc.
    template<typename Call, typename Cxs, typename = void>
    struct rpc_return_no_sfinae {
      using type = future<>;
      static const bool value = false; // whether Call is valid
    };

    template<typename Fn, typename Cxs, typename ...Arg>
    struct rpc_return_no_sfinae<
        Fn(Arg...), Cxs,
        decltype(
          std::declval<typename detail::rpc_return<Fn(Arg...), Cxs>::type>(),
          void()
        )
      > {
      using type = typename detail::rpc_return<Fn(Arg...), Cxs>::type;
      static const bool value = true;
    };
  }
  
  namespace detail {
    template<typename Cxs, typename Fn, typename ...Arg>
    auto rpc(const team &tm, intrank_t recipient, Fn &&fn, Arg &&...args, Cxs &&cxs, int /*dummy*/)
      // computes our return type, but SFINAE's out if fn(args...) is ill-formed
      -> typename detail::rpc_return<Fn(Arg...), typename std::decay<Cxs>::type>::type {
      using CxsDecayed = typename std::decay<Cxs>::type;
      
      static_assert(
        detail::trait_forall<
            detail::is_not_array,
            Arg...
          >::value,
        "Arrays may not be passed as arguments to rpc. "
        "To send the contents of an array, use upcxx::make_view() to construct a upcxx::view over the elements."
      );

      static_assert(
        detail::trait_forall<
            is_serializable,
            typename binding<Arg>::on_wire_type...
          >::value,
        "All rpc arguments must be Serializable."
      );
        
      static_assert(
        detail::trait_forall<
            detail::type_respects_static_size_limit,
            typename binding<Arg>::on_wire_type...
          >::value,
        UPCXX_STATIC_ASSERT_RPC_MSG(rpc)
      );

      UPCXX_ASSERT_ALWAYS(
        (!detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
        "rpc does not support remote completion."
      );

      using cxs_state_t = detail::completions_state<
          /*EventPredicate=*/detail::event_is_here,
          /*EventValues=*/detail::rpc_event_values<Fn&&(Arg&&...)>,
          CxsDecayed
        >;
      
      cxs_state_t state(std::forward<Cxs>(cxs));
      
      auto returner = detail::completions_returner<
          /*EventPredicate=*/detail::event_is_here,
          /*EventValues=*/detail::rpc_event_values<Fn&&(Arg&&...)>,
          CxsDecayed
        >(state);
      
      intrank_t initiator = backend::rank_me;
      auto *op_lpc = static_cast<cxs_state_t&&>(state).template to_lpc_dormant<operation_cx_event>();
      
      using fn_bound_t = typename detail::bind<const Fn&, const Arg&...>::return_type;

      backend::template send_am_master<progress_level::user>(
        tm, recipient,
        upcxx::bind_rvalue_as_lvalue(
          [=](deserialized_type_t<fn_bound_t> &&fn_bound) {
            return upcxx::apply_as_future(
                static_cast<deserialized_type_t<fn_bound_t>&&>(fn_bound)
              ).then_lazy(
                // Wish we could just use a lambda here, but since it has
                // to take variadic Arg... we have to call to an outlined
                // class. I'm not sure if even C++14's allowance of `auto`
                // lambda args would be enough.
                detail::rpc_recipient_after<decltype(op_lpc)>{
                  initiator, op_lpc
                }
              );
          },
          upcxx::bind_rvalue_as_lvalue(static_cast<Fn&&>(fn), static_cast<Arg&&>(args)...)
        )
      );
      
      // send_am_master doesn't support async source-completion, so we know
      // its trivially satisfied.
      state.template operator()<source_cx_event>();
      
      return returner();
    }

    // Overload replaces SFINAE with a static assertion failure.
    // Note: cxs comes after args to prevent the dummy int from being
    // folded into the parameter pack for ...Arg, forcing it into the
    // variadic arguments here.
    template<typename Cxs, typename Fn, typename ...Arg>
    future<> rpc(const team &, intrank_t, Fn &&, Arg &&..., Cxs&&, ...) {
      using CxsDecayed = typename std::decay<Cxs>::type;
      // check that this overload is not unintentionally invoked
      static_assert(
        !detail::rpc_return_no_sfinae<Fn(Arg...), CxsDecayed>::value,
        "internal error"
      );
      // friendlier error message for when Fn(Arg...) is invalid
      static_assert(
        detail::rpc_return_no_sfinae<Fn(Arg...), CxsDecayed>::value,
        "function object provided to rpc cannot be invoked on the given arguments as rvalue references "
        "(after deserialization of the function object and arguments). "
        "Note: make sure that the function object does not have any non-const lvalue-reference parameters."
      );
      return make_future();
    }
  }

  template<typename Cxs, typename Fn, typename ...Arg>
  UPCXX_NODISCARD
  auto rpc(const team &tm, intrank_t recipient, Cxs &&cxs, Fn &&fn, Arg &&...args)
    // computes our return type, but SFINAE's out if cxs is not a completions type
    -> typename std::enable_if<
         detail::is_completions<typename std::decay<Cxs>::type>::value,
         typename detail::rpc_return_no_sfinae<Fn(Arg...), typename std::decay<Cxs>::type>::type
       >::type {

    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT(recipient >= 0 && recipient < tm.rank_n(),
      "rpc(team, recipient, ...) requires recipient in [0, team.rank_n()-1] == [0, " << tm.rank_n()-1 << "], but given: " << recipient);

    return detail::template rpc<Cxs, Fn&&, Arg&&...>(
        tm, recipient, std::forward<Fn>(fn), std::forward<Arg>(args)...,
        std::forward<Cxs>(cxs), 0
      );
  }
  
  template<typename Cxs, typename Fn, typename ...Arg>
  UPCXX_NODISCARD
  auto rpc(intrank_t recipient, Cxs &&cxs, Fn &&fn, Arg &&...args)
    // computes our return type, but SFINAE's out if cxs is not a completions type
    -> typename std::enable_if<
         detail::is_completions<typename std::decay<Cxs>::type>::value,
         typename detail::rpc_return_no_sfinae<Fn(Arg...), typename std::decay<Cxs>::type>::type
       >::type {

    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT(recipient >= 0 && recipient < world().rank_n(),
      "rpc(recipient, ...) requires recipient in [0, rank_n()-1] == [0, " << world().rank_n()-1 << "], but given: " << recipient);

    return detail::template rpc<Cxs, Fn&&, Arg&&...>(
        world(), recipient, std::forward<Fn>(fn), std::forward<Arg>(args)...,
        std::forward<Cxs>(cxs), 0
      );
  }
  
  // rpc: default completions variant
  template<typename Fn, typename ...Arg>
  UPCXX_NODISCARD
  auto rpc(const team &tm, intrank_t recipient, Fn &&fn, Arg &&...args)
    // computes our return type, but SFINAE's out if fn is a completions type
    -> typename std::enable_if<
         !detail::is_completions<Fn>::value,
         typename detail::rpc_return_no_sfinae<Fn(Arg...), completions<future_cx<operation_cx_event>>>::type
       >::type {

    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT(recipient >= 0 && recipient < tm.rank_n(),
      "rpc(team, recipient, ...) requires recipient in [0, team.rank_n()-1] == [0, " << tm.rank_n()-1 << "], but given: " << recipient);

    return detail::template rpc<completions<future_cx<operation_cx_event>>, Fn&&, Arg&&...>(
      tm, recipient, std::forward<Fn>(fn), std::forward<Arg>(args)...,
      operation_cx::as_future(), 0
    );
  }
  
  template<typename Fn, typename ...Arg>
  UPCXX_NODISCARD
  auto rpc(intrank_t recipient, Fn &&fn, Arg &&...args)
    // computes our return type, but SFINAE's out if fn is a completions type
    -> typename std::enable_if<
         !detail::is_completions<Fn>::value,
         typename detail::rpc_return_no_sfinae<Fn(Arg...), completions<future_cx<operation_cx_event>>>::type
       >::type {

    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT(recipient >= 0 && recipient < world().rank_n(),
      "rpc(recipient, ...) requires recipient in [0, rank_n()-1] == [0, " << world().rank_n()-1 << "], but given: " << recipient);

    return detail::template rpc<completions<future_cx<operation_cx_event>>, Fn&&, Arg&&...>(
      world(), recipient, std::forward<Fn>(fn), std::forward<Arg>(args)...,
      operation_cx::as_future(), 0
    );
  }
}
#endif
