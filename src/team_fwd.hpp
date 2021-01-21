#ifndef _ba798e6d_cac8_4c55_839e_7b4ba217212c
#define _ba798e6d_cac8_4c55_839e_7b4ba217212c

#include <upcxx/bind.hpp>
#include <upcxx/backend_fwd.hpp>
#include <upcxx/digest.hpp>
#include <upcxx/utility.hpp>

#include <unordered_map>

/* This is the forward declaration(s) of upcxx::team and friends. It does not
 * define the function bodies nor does it pull in the full backend header.
 */

////////////////////////////////////////////////////////////////////////////////

namespace upcxx {
  namespace detail {
    extern std::unordered_map<digest, void*> registry;
    
    // Get the promise pointer from the master map.
    template<typename T>
    future_header_promise<T>* registered_promise(digest id, int initial_anon=0);

    template<typename T, typename ...U>
    T* registered_state(digest id, U &&...ctor_args);
  }
  
  class team;
  
  struct team_id {
  //private:
    digest dig_;
    team_id(digest id) : dig_(id) {}
    
  //public:
    team_id() : dig_(digest::zero()) {} // issue 343: disable trivial default construction

    team& here() const {
      UPCXX_ASSERT_INIT();
      team *presult = static_cast<team*>(detail::registry[dig_]);
      UPCXX_ASSERT(presult, "team_id::here() called for an invalid id or team (possibly outside its lifetime)");
      return *presult;
    }

    future<team&> when_here() const {
      UPCXX_ASSERT_INIT();
      team *pteam = static_cast<team*>(detail::registry[dig_]);
      // issue170: Currently the only form of team construction has barrier semantics,
      // such that a newly created team_id cannot arrive at user-level progress anywhere
      // until after the local representative has been constructed.
      UPCXX_ASSERT(pteam != nullptr);
      return make_future<team&>(*pteam);
    }
    
    #define UPCXX_COMPARATOR(op) \
      friend bool operator op(team_id a, team_id b) {\
        return a.dig_ op b.dig_; \
      }
    UPCXX_COMPARATOR(==)
    UPCXX_COMPARATOR(!=)
    UPCXX_COMPARATOR(<)
    UPCXX_COMPARATOR(<=)
    UPCXX_COMPARATOR(>)
    UPCXX_COMPARATOR(>=)
    #undef UPCXX_COMPARATOR
  };
  
  inline std::ostream& operator<<(std::ostream &o, team_id x) {
    return o << x.dig_;
  }
}

namespace std {
  template<>
  struct hash<upcxx::team_id> {
    size_t operator()(upcxx::team_id id) const {
      return hash<upcxx::digest>()(id.dig_);
    }
  };
}

namespace upcxx {
  class team:
      backend::team_base /* defined by <backend>/runtime_fwd.hpp */ {
    digest id_;
    std::uint64_t coll_counter_;
    intrank_t n_, me_;
    
  public:
    team(detail::internal_only, backend::team_base &&base, digest id, intrank_t n, intrank_t me);
    team(team const&) = delete;
    team(team &&that);
    ~team();
    
    intrank_t rank_n() const { UPCXX_ASSERT_INIT(); return n_; }
    intrank_t rank_me() const { UPCXX_ASSERT_INIT(); return me_; }
    
    intrank_t from_world(intrank_t rank) const {
      UPCXX_ASSERT_INIT();
      UPCXX_ASSERT(rank >= 0 && rank < upcxx::rank_n(), 
                   "team::from_world(rank) requires rank in [0, world().rank_n()-1] == [0, " << upcxx::rank_n()-1 << "], but given: " << rank);
      return backend::team_rank_from_world(*this, rank);
    }
    intrank_t from_world(intrank_t rank, intrank_t otherwise) const {
      UPCXX_ASSERT_INIT();
      UPCXX_ASSERT(rank >= 0 && rank < upcxx::rank_n(), 
                   "team::from_world(rank, otherwise) requires rank in [0, world().rank_n()-1] == [0, " << upcxx::rank_n()-1 << "], but given: " << rank);
      return backend::team_rank_from_world(*this, rank, otherwise);
    }
    
    intrank_t operator[](intrank_t peer) const {
      UPCXX_ASSERT_INIT();
      UPCXX_ASSERT(peer >= 0 && peer < this->rank_n(), 
                   "team[peer_index] requires peer_index in [0, rank_n()-1] == [0, " << this->rank_n()-1 << "], but given: " << peer);
      return backend::team_rank_to_world(*this, peer);
    }
    
    team_id id() const {
      return team_id{id_};
    }
    
    static constexpr intrank_t color_none = -0xbad;
    
    team split(intrank_t color, intrank_t key) const;
    
    void destroy(entry_barrier eb = entry_barrier::user);
    
    ////////////////////////////////////////////////////////////////////////////
    // internal only
    
    const team_base& base(detail::internal_only) const {
      return *this;
    }
    
    digest next_collective_id(detail::internal_only) {
      return id_.eat(coll_counter_++);
    }

    void destroy(detail::internal_only, entry_barrier eb = entry_barrier::user);
  };
  
  team& world();
  team& local_team();
  
  namespace detail {
    extern detail::raw_storage<team> the_world_team;
    extern detail::raw_storage<team> the_local_team;    
  }
}
#endif
