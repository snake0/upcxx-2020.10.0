#ifndef _f2c7c1fc_cd4a_4123_bf50_a6542f8efa2c
#define _f2c7c1fc_cd4a_4123_bf50_a6542f8efa2c

#include <upcxx/backend.hpp>
#include <upcxx/completion.hpp>
#include <upcxx/future.hpp>
#include <upcxx/team.hpp>

namespace upcxx {
  namespace detail {
    ////////////////////////////////////////////////////////////////////
    // barrier_event_values: Value for completions_state's EventValues
    // template argument. barrier events always report no values.
    struct barrier_event_values {
      template<typename Event>
      using tuple_t = std::tuple<>;
    };

    void barrier_async_inject(const team &tm, backend::gasnet::handle_cb *cb);
  }
  
  void barrier(const team &tm = upcxx::world());
  
  template<typename Cxs = completions<future_cx<operation_cx_event>>>
  UPCXX_NODISCARD
  typename detail::completions_returner<
      /*EventPredicate=*/detail::event_is_here,
      /*EventValues=*/detail::barrier_event_values,
      typename std::decay<Cxs>::type
    >::return_t
  barrier_async(
      const team &tm = upcxx::world(),
      Cxs &&cxs = completions<future_cx<operation_cx_event>>({})
    ) {
    using CxsDecayed = typename std::decay<Cxs>::type;
    UPCXX_ASSERT_INIT();
    UPCXX_ASSERT_MASTER();
    UPCXX_ASSERT_COLLECTIVE_SAFE_NAMED("upcxx::barrier_async()", entry_barrier::internal);
    UPCXX_ASSERT_ALWAYS(
      (detail::completions_has_event<CxsDecayed, operation_cx_event>::value),
      "Not requesting operation completion is surely an error."
    );
    UPCXX_ASSERT_ALWAYS(
      (!detail::completions_has_event<CxsDecayed, source_cx_event>::value &&
       !detail::completions_has_event<CxsDecayed, remote_cx_event>::value),
      "barrier_async does not support source or remote completion."
    );

    struct barrier_cb final: backend::gasnet::handle_cb {
      detail::completions_state<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::barrier_event_values,
        CxsDecayed> state;

      barrier_cb(Cxs &&cxs): state(std::forward<Cxs>(cxs)) {}
      
      void execute_and_delete(backend::gasnet::handle_cb_successor) {
        state.template operator()<operation_cx_event>();
        delete this;
      }
    };

    barrier_cb *cb = new barrier_cb(std::forward<Cxs>(cxs));
    
    auto returner = detail::completions_returner<
        /*EventPredicate=*/detail::event_is_here,
        /*EventValues=*/detail::barrier_event_values,
        CxsDecayed
      >(cb->state);
    
    detail::barrier_async_inject(tm, cb);
    
    return returner();
  }
}
#endif
