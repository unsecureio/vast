/******************************************************************************
 *                    _   _____   __________                                  *
 *                   | | / / _ | / __/_  __/     Visibility                   *
 *                   | |/ / __ |_\ \  / /          Across                     *
 *                   |___/_/ |_/___/ /_/       Space and Time                 *
 *                                                                            *
 * This file is part of VAST. It is subject to the license terms in the       *
 * LICENSE file found in the top-level directory of this distribution and at  *
 * http://vast.io/license. No part of VAST, including this file, may be       *
 * copied, modified, propagated, or distributed except according to the terms *
 * contained in the LICENSE file.                                             *
 ******************************************************************************/

#pragma once

#include "vast/config.hpp" // Needed for VAST_HAVE_ARROW

#include <caf/atom.hpp>
#include <caf/fwd.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>

namespace vast::defaults {

// -- constants for the import command and its subcommands ---------------------

/// Contains constants that are shared by two or more import subcommands.
namespace import::shared {

/// Path for reading input events or `-` for reading from STDIN.
constexpr std::string_view read = "-";

} // namespace import::shared

/// Contains constants for the import command.
namespace import {

/// Maximum size for sources that generate table slices.
constexpr size_t table_slice_size = 100;

#if VAST_HAVE_ARROW

/// The default table slice type when arrow is available.
constexpr caf::atom_value table_slice_type = caf::atom("arrow");

#else // VAST_HAVE_ARROW

/// The default table slice type when arrow is unavailable.
constexpr caf::atom_value table_slice_type = caf::atom("msgpack");

#endif // VAST_HAVE_ARROW

/// Maximum number of results.
constexpr size_t max_events = 0;

/// Read timoeut after which data is forwarded to the importer regardless of
/// batching and table slices being unfinished.
constexpr std::chrono::milliseconds read_timeout = std::chrono::seconds{10};

/// Contains settings for the zeek subcommand.
struct zeek {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "import.zeek";

  /// Path for reading input events.
  static constexpr auto read = shared::read;
};

/// Contains settings for the csv subcommand.
struct csv {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "import.csv";

  /// Path for reading input events.
  static constexpr auto read = shared::read;

  static constexpr char separator = ',';

  // TODO: agree on reasonable values
  static constexpr std::string_view set_separator = ",";

  static constexpr std::string_view kvp_separator = "=";
};

/// Contains settings for the json subcommand.
struct json {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "import.json";

  /// Path for reading input events.
  static constexpr auto read = shared::read;
};

/// Contains settings for the suricata subcommand.
struct suricata {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "import.suricata";

  /// Path for reading input events.
  static constexpr auto read = shared::read;
};

/// Contains settings for the syslog subcommand.
struct syslog {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "import.syslog";

  /// Path for reading input events.
  static constexpr auto read = shared::read;
};

/// Contains settings for the test subcommand.
struct test {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "import.test";

  /// Path for reading input events.
  static constexpr auto read = shared::read;

  /// @returns a user-defined seed if available, a randomly generated seed
  /// otherwise.
  static size_t seed(const caf::settings& options);
};

/// Contains settings for the pcap subcommand.
struct pcap {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "import.pcap";

  /// Path for reading input events.
  static constexpr auto read = shared::read;

  /// Number of bytes to keep per event.
  static constexpr size_t cutoff = std::numeric_limits<size_t>::max();

  /// Number of concurrent flows to track.
  static constexpr size_t max_flows = 1'048'576; // 1_Mi

  /// Maximum flow lifetime before eviction.
  static constexpr size_t max_flow_age = 60;

  /// Flow table expiration interval.
  static constexpr size_t flow_expiry = 10;

  /// Inverse factor by which to delay packets. For example, if 5, then for two
  /// packets spaced *t* seconds apart, the source will sleep for *t/5* seconds.
  static constexpr int64_t pseudo_realtime_factor = 0;

  /// If the snapshot length is set to snaplen, and snaplen is less than the
  /// size of a packet that is captured, only the first snaplen bytes of that
  /// packet will be captured and provided as packet data. A snapshot length
  /// of 65535 should be sufficient, on most if not all networks, to capture all
  /// the data available from the packet.
  static constexpr size_t snaplen = 65535;
};

} // namespace import

// -- constants for the explore command and its subcommands --------------------

namespace explore {

// A value of zero means 'unlimited' for all three limits below.
// If all limits are non-zero, the number of results is bounded
// by `min(max_events, max_events_query*max_events_context)`.

/// Maximum total number of results.
constexpr size_t max_events = std::numeric_limits<size_t>::max();

/// Maximum number of results for the initial query.
constexpr size_t max_events_query = 100;

/// Maximum number of results for every explored context.
constexpr size_t max_events_context = 100;

} // namespace explore

// -- constants for the export command and its subcommands ---------------------

// Unfortunately, `export` is a reserved keyword. The trailing `_` exists only
// for disambiguation.

/// Contains constants that are shared by two or more export subcommands.
namespace export_::shared {

/// Path for writing query results or `-` for writing to STDOUT.
constexpr std::string_view write = "-";

} // namespace export_::shared

/// Contains constants for the export command.
namespace export_ {

/// Path for reading reading the query or `-` for reading from STDIN.
constexpr std::string_view read = "-";

/// Maximum number of results.
constexpr size_t max_events = 0;

/// Contains settings for the zeek subcommand.
struct zeek {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "export.zeek";

  /// Path for writing query results.
  static constexpr auto write = shared::write;
};

/// Contains settings for the csv subcommand.
struct csv {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "export.csv";

  /// Path for writing query results.
  static constexpr auto write = shared::write;

  static constexpr char separator = ',';

  // TODO: agree on reasonable values
  static constexpr std::string_view set_separator = " | ";
};

/// Contains settings for the ascii subcommand.
struct ascii {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "export.ascii";

  /// Path for writing query results.
  static constexpr auto write = shared::write;
};

/// Contains settings for the json subcommand.
struct json {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "export.json";

  /// Path for writing query results.
  static constexpr auto write = shared::write;
};

/// Contains settings for the null subcommand.
struct null {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "export.null";

  /// Path for writing query results.
  static constexpr auto write = shared::write;
};

struct arrow {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "export.arrow";
  /// Path for writing query results.
  static constexpr auto write = vast::defaults::export_::shared::write;
};

/// Contains settings for the pcap subcommand.
struct pcap {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "export.pcap";

  /// Path for writing query results.
  static constexpr auto write = shared::write;

  /// Flush to disk after that many packets.
  static constexpr size_t flush_interval = 10'000;
};

} // namespace export_

// -- constants for the infer command -----------------------------------------

/// Contains settings for the csv subcommand.
struct infer {
  /// Nested category in config files for this subcommand.
  static constexpr const char* category = "infer";

  /// Path for reading input events.
  static constexpr auto read = defaults::import::shared::read;

  /// Number of bytes to buffer from input.
  static constexpr size_t buffer_size = 8'192;
};

// -- constants for the index --------------------------------------------------

/// Contains constants for value index parameterization.
namespace index {

/// The maximum length of a string before the default string index chops it off.
constexpr size_t max_string_size = 1024;

/// The maximum number elements an index for a container type (set, vector,
/// or table).
constexpr size_t max_container_elements = 256;

} // namespace index

// -- constants for the logger -------------------------------------------------
namespace logger {

// CAF uses its own string_view that does not come with conversion operators
// from std::string_view, so we use const char* instead.
// TODO: Revisit when updating to CAF 0.18.
constexpr const char* console_format = "%d %m";

constexpr const caf::atom_value console_verbosity = caf::atom("info");

constexpr const caf::atom_value file_verbosity = caf::atom("debug");

} // namespace logger

// -- constants for the entire system ------------------------------------------

/// Contains system-wide constants.
namespace system {

/// Hostname or IP address and port of a remote node.
constexpr std::string_view endpoint = "localhost:42000/tcp";

/// The unique ID of this node.
constexpr std::string_view node_id = "node";

/// Path to persistent state.
constexpr std::string_view db_directory = "vast.db";

/// Interval between two aging cycles.
constexpr caf::timespan aging_frequency = std::chrono::hours{24};

/// Maximum number of events per INDEX partition.
constexpr size_t max_partition_size = 1'048'576; // 1_Mi

/// Maximum number of in-memory INDEX partitions.
constexpr size_t max_in_mem_partitions = 10;

/// Number of immediately scheduled INDEX partitions.
constexpr size_t taste_partitions = 5;

/// Maximum number of concurrent INDEX queries.
constexpr size_t num_query_supervisors = 10;

/// Number of cached ARCHIVE segments.
constexpr size_t segments = 10;

/// Maximum size of ARCHIVE segments in MB.
constexpr size_t max_segment_size = 128;

/// Number of initial IDs to request in the IMPORTER.
constexpr size_t initially_requested_ids = 128;

/// Rate at which telemetry data is sent to the ACCOUNTANT.
constexpr std::chrono::milliseconds telemetry_rate
  = std::chrono::milliseconds{10000};

/// Interval between checks whether a signal occured.
constexpr std::chrono::milliseconds signal_monitoring_interval
  = std::chrono::milliseconds{750};

/// Timeout for initial connections to the node.
constexpr std::chrono::seconds initial_request_timeout
  = std::chrono::seconds{10};

/// The period to wait until a shutdown sequence finishes cleanly. After the
/// elapses, the shutdown procedure escalates into a "hard kill".
/// @relates shutdown_kill_timeout
constexpr std::chrono::milliseconds shutdown_grace_period
  = std::chrono::minutes{3};

/// Time to wait until receiving a DOWN from a killed actor.
/// @relates shutdown_grace_period
constexpr std::chrono::seconds shutdown_kill_timeout = std::chrono::minutes{1};

} // namespace system

} // namespace vast::defaults
