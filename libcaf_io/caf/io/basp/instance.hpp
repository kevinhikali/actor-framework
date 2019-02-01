/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include <limits>

#include "caf/error.hpp"
#include "caf/variant.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/binary_deserializer.hpp"

#include "caf/io/hook.hpp"
#include "caf/io/middleman.hpp"

#include "caf/io/basp/header.hpp"
#include "caf/io/basp/buffer_type.hpp"
#include "caf/io/basp/message_type.hpp"
#include "caf/io/basp/routing_table.hpp"
#include "caf/io/basp/connection_state.hpp"

namespace caf {
namespace io {
namespace basp {

/// @addtogroup BASP

/// Describes a protocol instance managing multiple connections.
class instance {
public:
  /// Provides a callback-based interface for certain BASP events.
  class callee {
  public:
    // -- member types ---------------------------------------------------------

    using buffer_type = std::vector<char>;

    using endpoint_handle = variant<connection_handle, datagram_handle>;

    // -- constructors, destructors, and assignment operators ------------------

    explicit callee(actor_system& sys);

    virtual ~callee();

    // -- pure virtual member functions ----------------------------------------

    /// Called if a server handshake was received and
    /// the connection to `nid` is established.
    virtual void finalize_handshake(const node_id& nid, actor_id aid,
                                    std::set<std::string>& sigs) = 0;

    /// Called whenever a direct connection was closed or a
    /// node became unrechable for other reasons *before*
    /// this node gets erased from the routing table.
    /// @warning The implementing class must not modify the
    ///          routing table from this callback.
    virtual void purge_state(const node_id& nid) = 0;

    /// Called whenever a remote node created a proxy
    /// for one of our local actors.
    virtual void proxy_announced(const node_id& nid, actor_id aid) = 0;

    /// Called for each `dispatch_message` without `named_receiver_flag`.
    virtual void deliver(const node_id& source_node, actor_id source_actor,
                         actor_id dest_actor, message_id mid,
                         std::vector<strong_actor_ptr>& forwarding_stack,
                         message& msg) = 0;

    /// Called for each `dispatch_message` with `named_receiver_flag`.
    virtual void deliver(const node_id& source_node, actor_id source_actor,
                         atom_value dest_actor, message_id mid,
                         std::vector<strong_actor_ptr>& forwarding_stack,
                         message& msg) = 0;

    /// Called whenever BASP learns the ID of a remote node.
    virtual void learned_new_node(const node_id& nid) = 0;

    /// Called if a heartbeat was received from `nid`
    virtual void handle_heartbeat(const node_id& nid) = 0;

    /// Send messages that were buffered while connectivity establishment
    /// was pending using `hdl`.
    virtual void send_buffered_messages(execution_unit* ctx, node_id nid,
                                        connection_handle hdl) = 0;

    /// Returns a reference to the sent buffer.
    virtual buffer_type& get_buffer(connection_handle hdl) = 0;

    /// Returns a reference to a buffer to be sent to node with `nid`.
    /// If communication with the node is esstablished, it picks the first
    /// available handle, otherwise a buffer for a pending message is returned.
    virtual buffer_type& get_buffer(node_id nid) = 0;

    /// Flushes the underlying write buffer of `hdl`.
    virtual void flush(connection_handle hdl) = 0;

    // -- properties -----------------------------------------------------------

    /// Returns the hosting actor system.
    actor_system& system() {
      return system_;
    }

    /// Returns the hosting actor system.
    proxy_registry& proxies() {
      return system_.proxies();
    }

    /// Returns the system-wide configuration.
    const actor_system_config& config() const {
      return system_.config();
    }

  protected:
    actor_system& system_;
  };

  /// Describes a function object responsible for writing
  /// the payload for a BASP message.
  using payload_writer = callback<serializer&>;

  /// Describes a callback function object for `remove_published_actor`.
  using removed_published_actor = callback<const strong_actor_ptr&, uint16_t>;

  instance(abstract_broker* parent, callee& lstnr);

  /// Handles received data and returns a config for receiving the
  /// next data or `none` if an error occured.
  connection_state handle(execution_unit* ctx,
                          new_data_msg& dm, header& hdr, bool is_payload);

  /// Sends heartbeat messages to all valid nodes those are directly connected.
  void handle_heartbeat(execution_unit* ctx);

  /// Returns a route to `target` or `none` on error.
  routing_table::lookup_result lookup(const node_id& target);

  /// Flushes the underlying buffer of `path`.
  void flush(connection_handle hdl);

  /// Sends a BASP message and implicitly flushes the output buffer of `r`.
  /// This function will update `hdr.payload_len` if a payload was written.
  void write(execution_unit* ctx, connection_handle hdl, header& hdr,
             payload_writer* writer = nullptr);

  /// Adds a new actor to the map of published actors.
  void add_published_actor(uint16_t port,
                           strong_actor_ptr published_actor,
                           std::set<std::string> published_interface);

  /// Removes the actor currently assigned to `port`.
  size_t remove_published_actor(uint16_t port,
                                removed_published_actor* cb = nullptr);

  /// Removes `whom` if it is still assigned to `port` or from all of its
  /// current ports if `port == 0`.
  size_t remove_published_actor(const actor_addr& whom, uint16_t port,
                                removed_published_actor* cb = nullptr);

  /// Returns `true` if a path to destination existed, `false` otherwise.
  bool dispatch(execution_unit* ctx, const strong_actor_ptr& sender,
                const std::vector<strong_actor_ptr>& forwarding_stack,
                const strong_actor_ptr& receiver,
                message_id mid, const message& msg);

  /// Returns the routing table of this BASP instance.
  routing_table& tbl() {
    return tbl_;
  }

  /// Stores the address of a published actor along with its publicly
  /// visible messaging interface.
  using published_actor = std::pair<strong_actor_ptr, std::set<std::string>>;

  /// Maps ports to addresses and interfaces of published actors.
  using published_actor_map = std::unordered_map<uint16_t, published_actor>;

  /// Returns the current mapping of ports to addresses
  /// and interfaces of published actors.
  const published_actor_map& published_actors() const {
    return published_actors_;
  }

  /// Writes a header followed by its payload to `storage`.
  static void write(execution_unit* ctx, buffer_type& buf, header& hdr,
                    payload_writer* pw = nullptr);

  /// Writes the server handshake containing the information of the
  /// actor published at `port` to `buf`. If `port == none` or
  /// if no actor is published at this port then a standard handshake is
  /// written (e.g. used when establishing direct connections on-the-fly).
  void write_server_handshake(execution_unit* ctx,
                              buffer_type& out_buf, optional<uint16_t> port);

  /// Writes the client handshake to `buf`.
  void write_client_handshake(execution_unit* ctx, buffer_type& buf,
                              const node_id& this_node,
                              const std::string& app_identifier);

  /// Writes the client handshake to `buf`.
  void write_client_handshake(execution_unit* ctx, buffer_type& buf);

  /// Writes an `announce_proxy` to `buf`.
  void write_announce_proxy(execution_unit* ctx, buffer_type& buf,
                            const node_id& dest_node, actor_id aid);

  /// Writes a `kill_proxy` to `buf`.
  void write_kill_proxy(execution_unit* ctx, buffer_type& buf,
                        const node_id& dest_node, actor_id aid,
                        const error& rsn);

  /// Writes a `heartbeat` to `buf`.
  void write_heartbeat(execution_unit* ctx, buffer_type& buf);

  const node_id& this_node() const {
    return this_node_;
  }

  /// Invokes the callback(s) associated with given event.
  template <hook::event_type Event, typename... Ts>
  void notify(Ts&&... xs) {
    system().middleman().template notify<Event>(std::forward<Ts>(xs)...);
  }

  actor_system& system() {
    return callee_.system();
  }

  bool handle(execution_unit* ctx, connection_handle hdl, header& hdr,
              std::vector<char>* payload);

private:
  routing_table tbl_;
  published_actor_map published_actors_;
  node_id this_node_;
  callee& callee_;
};

/// @}

} // namespace basp
} // namespace io
} // namespace caf

