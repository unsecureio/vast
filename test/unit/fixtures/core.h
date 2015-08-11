#ifndef FIXTURES_CORE_H
#define FIXTURES_CORE_H

#include <vector>

#include <caf/all.hpp>

#include "vast/actor/node.h"

using namespace caf;
using namespace vast;

namespace fixtures {

struct core {
  core() {
    if (exists(dir)) {
      MESSAGE("removing existing directory");
      REQUIRE(rm(dir));
    }
  }

  ~core() {
    self->await_all_other_actors_done();
    if (exists(dir)) {
      MESSAGE("removing created directory");
      REQUIRE(rm(dir));
    }
  }

  actor make_core() {
    auto n = self->spawn<node>(node_name, dir);
    std::vector<message> msgs = {
      make_message("spawn", "archive", "-s", "1"),
      make_message("spawn", "index", "-e", "10"),
      make_message("spawn", "importer"),
      make_message("spawn", "identifier"),
      make_message("connect", "importer", "identifier"),
      make_message("connect", "importer", "archive"),
      make_message("connect", "importer", "index"),
    };
    for (auto& msg : msgs)
      self->sync_send(n, msg).await(
        [&](error const& e) {
          FAIL(e);
        },
        others >> [] {
          // Everyting except an error is a valid return value.
        }
      );
    return n;
  }

  void stop_core(actor const& n) {
    // Assume all sources have terminated. Then we stop the IMPORTER. After
    // getting notified that it terminated, we can guarantee that ARCHIVE and
    // INDEX have received all their events.
    self->sync_send(n, store_atom::value, get_atom::value, actor_atom::value,
                    "importer").await(
      [&](actor const& a, std::string const& fqn, std::string const& type) {
        CHECK(fqn == "importer@" + node_name);
        CHECK(type == "importer");
        REQUIRE(a != invalid_actor);
        self->monitor(a);
        self->send_exit(a, exit::done);
      },
      others >> [&] {
        FAIL("unexpected message: " << to_string(self->current_message()));
      }
    );
    self->receive([&](down_msg const& dm) { CHECK(dm.reason == exit::done); });
    self->sync_send(n, "stop").await([](ok_atom) {});
  }

  template <typename... Args>
  void run_source(actor const& n, Args&&... args) {
    std::vector<message> msgs = {
      make_message("spawn", "source", std::forward<Args>(args)...),
      make_message("connect", "source", "importer"),
      make_message("send", "source", "run")
    };
    for (auto& msg : msgs)
      self->sync_send(n, msg).await( [&](error const& e) {
          FAIL(e);
        },
        others >> [] {
          // Everyting except error is a valid return value.
        }
      );
    MESSAGE("monitoring source");
    self->sync_send(n, store_atom::value, get_atom::value, actor_atom::value,
                    "source").await(
      [&](actor const& a, std::string const& fqn, std::string const& type) {
        CHECK(a != invalid_actor);
        CHECK(fqn == "source@" + node_name);
        CHECK(type == "source");
        self->monitor(a);
        MESSAGE("waiting for source to terminate");
        self->receive(
          [&](down_msg const& dm) { CHECK(dm.reason == exit::done); }
        );
      },
      [](vast::none) {
        // source has already terminated.
      }
    );
  }

  std::string const node_name = "test-node";
  path dir = "vast-unit-test";
  scoped_actor self;
};

} // namespace fixtures

#endif
