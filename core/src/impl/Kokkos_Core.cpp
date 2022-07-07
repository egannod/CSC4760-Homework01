/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 3.0
//       Copyright (2020) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#include <Kokkos_Core.hpp>
#include <impl/Kokkos_Error.hpp>
#include <impl/Kokkos_ExecSpaceInitializer.hpp>
#include <impl/Kokkos_Command_Line_Parsing.hpp>
#include <impl/Kokkos_ParseCommandLineArgumentsAndEnvironmentVariables.hpp>
#include <impl/Kokkos_DeviceManagement.hpp>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <stack>
#include <functional>
#include <list>
#include <cerrno>
#include <regex>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

//----------------------------------------------------------------------------
namespace {
bool g_is_initialized = false;
bool g_show_warnings  = true;
bool g_tune_internals = false;
// When compiling with clang/LLVM and using the GNU (GCC) C++ Standard Library
// (any recent version between GCC 7.3 and GCC 9.2), std::deque SEGV's during
// the unwinding of the atexit(3C) handlers at program termination.  However,
// this bug is not observable when building with GCC.
// As an added bonus, std::list<T> provides constant insertion and
// deletion time complexity, which translates to better run-time performance. As
// opposed to std::deque<T> which does not provide the same constant time
// complexity for inserts/removals, since std::deque<T> is implemented as a
// segmented array.
using hook_function_type = std::function<void()>;
std::stack<hook_function_type, std::list<hook_function_type>> finalize_hooks;

/**
 * The category is only used in printing, tools
 * get all metadata free of category
 */
using metadata_category_type = std::string;
using metadata_key_type      = std::string;
using metadata_value_type    = std::string;

std::map<metadata_category_type,
         std::map<metadata_key_type, metadata_value_type>>
    metadata_map;

void declare_configuration_metadata(const std::string& category,
                                    const std::string& key,
                                    const std::string& value) {
  metadata_map[category][key] = value;
}

void combine(Kokkos::InitializationSettings& out,
             Kokkos::InitializationSettings const& in) {
#define KOKKOS_IMPL_COMBINE_SETTING(NAME) \
  if (in.has_##NAME()) {                  \
    out.set_##NAME(in.get_##NAME());      \
  }                                       \
  static_assert(true, "no-op to require trailing semicolon")
  KOKKOS_IMPL_COMBINE_SETTING(num_threads);
  KOKKOS_IMPL_COMBINE_SETTING(device_id);
  KOKKOS_IMPL_COMBINE_SETTING(num_devices);
  KOKKOS_IMPL_COMBINE_SETTING(skip_device);
  KOKKOS_IMPL_COMBINE_SETTING(disable_warnings);
  KOKKOS_IMPL_COMBINE_SETTING(tune_internals);
  KOKKOS_IMPL_COMBINE_SETTING(tool_help);
  KOKKOS_IMPL_COMBINE_SETTING(tool_lib);
  KOKKOS_IMPL_COMBINE_SETTING(tool_args);
#undef KOKKOS_IMPL_COMBINE_SETTING
}

void combine(Kokkos::InitializationSettings& out,
             Kokkos::Tools::InitArguments const& in) {
  using Kokkos::Tools::InitArguments;
  if (in.tune_internals != InitArguments::PossiblyUnsetOption::unset) {
    out.set_tune_internals(in.tune_internals ==
                           InitArguments::PossiblyUnsetOption::on);
  }
  if (in.help != InitArguments::PossiblyUnsetOption::unset) {
    out.set_tool_help(in.help == InitArguments::PossiblyUnsetOption::on);
  }
  if (in.lib != InitArguments::unset_string_option) {
    out.set_tool_lib(in.lib);
  }
  if (in.args != InitArguments::unset_string_option) {
    out.set_tool_args(in.args);
  }
}

void combine(Kokkos::Tools::InitArguments& out,
             Kokkos::InitializationSettings const& in) {
  using Kokkos::Tools::InitArguments;
  if (in.has_tune_internals()) {
    out.tune_internals = in.get_tune_internals()
                             ? InitArguments::PossiblyUnsetOption::on
                             : InitArguments::PossiblyUnsetOption::off;
  }
  if (in.has_tool_help()) {
    out.help = in.get_tool_help() ? InitArguments::PossiblyUnsetOption::on
                                  : InitArguments::PossiblyUnsetOption::off;
  }
  if (in.has_tool_lib()) {
    out.lib = in.get_tool_lib();
  }
  if (in.has_tool_args()) {
    out.args = in.get_tool_args();
  }
}

}  // namespace

Kokkos::Impl::ExecSpaceManager& Kokkos::Impl::ExecSpaceManager::get_instance() {
  static ExecSpaceManager space_initializer = {};
  return space_initializer;
}

void Kokkos::Impl::ExecSpaceManager::register_space_factory(
    const std::string name, std::unique_ptr<ExecSpaceInitializerBase> space) {
  exec_space_factory_list[name] = std::move(space);
}

void Kokkos::Impl::ExecSpaceManager::initialize_spaces(
    const InitializationSettings& settings) {
  // Note: the names of the execution spaces, used as keys in the map, encode
  // the ordering of the initialization code from the old initialization stuff.
  // Eventually, we may want to do something less brittle than this, but for now
  // we're just preserving compatibility with the old implementation.
  for (auto& to_init : exec_space_factory_list) {
    to_init.second->initialize(settings);
  }
}

void Kokkos::Impl::ExecSpaceManager::finalize_spaces() {
  for (auto& to_finalize : exec_space_factory_list) {
    to_finalize.second->finalize(/*all_spaces=*/true);
  }
}

void Kokkos::Impl::ExecSpaceManager::static_fence(const std::string& name) {
  for (auto& to_fence : exec_space_factory_list) {
    to_fence.second->fence(name);
  }
}
void Kokkos::Impl::ExecSpaceManager::print_configuration(std::ostream& msg,
                                                         const bool detail) {
  for (auto& to_print : exec_space_factory_list) {
    to_print.second->print_configuration(msg, detail);
  }
}

int Kokkos::Impl::get_ctest_gpu(const char* local_rank_str) {
  auto const* ctest_kokkos_device_type =
      std::getenv("CTEST_KOKKOS_DEVICE_TYPE");
  if (!ctest_kokkos_device_type) {
    return 0;
  }

  auto const* ctest_resource_group_count_str =
      std::getenv("CTEST_RESOURCE_GROUP_COUNT");
  if (!ctest_resource_group_count_str) {
    return 0;
  }

  // Make sure rank is within bounds of resource groups specified by CTest
  auto resource_group_count = std::stoi(ctest_resource_group_count_str);
  auto local_rank           = std::stoi(local_rank_str);
  if (local_rank >= resource_group_count) {
    std::ostringstream ss;
    ss << "Error: local rank " << local_rank
       << " is outside the bounds of resource groups provided by CTest. Raised"
       << " by Kokkos::Impl::get_ctest_gpu().";
    throw_runtime_exception(ss.str());
  }

  // Get the resource types allocated to this resource group
  std::ostringstream ctest_resource_group;
  ctest_resource_group << "CTEST_RESOURCE_GROUP_" << local_rank;
  std::string ctest_resource_group_name = ctest_resource_group.str();
  auto const* ctest_resource_group_str =
      std::getenv(ctest_resource_group_name.c_str());
  if (!ctest_resource_group_str) {
    std::ostringstream ss;
    ss << "Error: " << ctest_resource_group_name << " is not specified. Raised"
       << " by Kokkos::Impl::get_ctest_gpu().";
    throw_runtime_exception(ss.str());
  }

  // Look for the device type specified in CTEST_KOKKOS_DEVICE_TYPE
  bool found_device                        = false;
  std::string ctest_resource_group_cxx_str = ctest_resource_group_str;
  std::istringstream instream(ctest_resource_group_cxx_str);
  while (true) {
    std::string devName;
    std::getline(instream, devName, ',');
    if (devName == ctest_kokkos_device_type) {
      found_device = true;
      break;
    }
    if (instream.eof() || devName.length() == 0) {
      break;
    }
  }

  if (!found_device) {
    std::ostringstream ss;
    ss << "Error: device type '" << ctest_kokkos_device_type
       << "' not included in " << ctest_resource_group_name
       << ". Raised by Kokkos::Impl::get_ctest_gpu().";
    throw_runtime_exception(ss.str());
  }

  // Get the device ID
  std::string ctest_device_type_upper = ctest_kokkos_device_type;
  for (auto& c : ctest_device_type_upper) {
    c = std::toupper(c);
  }
  ctest_resource_group << "_" << ctest_device_type_upper;

  std::string ctest_resource_group_id_name = ctest_resource_group.str();
  auto resource_str = std::getenv(ctest_resource_group_id_name.c_str());
  if (!resource_str) {
    std::ostringstream ss;
    ss << "Error: " << ctest_resource_group_id_name
       << " is not specified. Raised by Kokkos::Impl::get_ctest_gpu().";
    throw_runtime_exception(ss.str());
  }

  auto const* comma = std::strchr(resource_str, ',');
  if (!comma || strncmp(resource_str, "id:", 3)) {
    std::ostringstream ss;
    ss << "Error: invalid value of " << ctest_resource_group_id_name << ": '"
       << resource_str << "'. Raised by Kokkos::Impl::get_ctest_gpu().";
    throw_runtime_exception(ss.str());
  }

  std::string id(resource_str + 3, comma - resource_str - 3);
  return std::stoi(id.c_str());
}

// function to extract gpu # from args
int Kokkos::Impl::get_gpu(const InitializationSettings& settings) {
  int use_gpu        = settings.has_device_id() ? settings.get_device_id() : -1;
  const int ndevices = [](int num_devices) -> int {
    if (num_devices > 0) return num_devices;
#if defined(KOKKOS_ENABLE_CUDA)
    return Cuda::detect_device_count();
#elif defined(KOKKOS_ENABLE_HIP)
    return Experimental::HIP::detect_device_count();
#elif defined(KOKKOS_ENABLE_SYCL)
    return sycl::device::get_devices(sycl::info::device_type::gpu).size();
#else
    return num_devices;
#endif
  }(settings.has_num_devices() ? settings.get_num_devices() : -1);
  const int skip_device =
      settings.has_skip_device() ? settings.get_skip_device() : 9999;

  // if the exact device is not set, but ndevices was given, assign round-robin
  // using on-node MPI rank
  if (use_gpu < 0) {
    auto const* local_rank_str =
        std::getenv("OMPI_COMM_WORLD_LOCAL_RANK");  // OpenMPI
    if (!local_rank_str)
      local_rank_str = std::getenv("MV2_COMM_WORLD_LOCAL_RANK");  // MVAPICH2
    if (!local_rank_str)
      local_rank_str = std::getenv("SLURM_LOCALID");  // SLURM

    auto const* ctest_kokkos_device_type =
        std::getenv("CTEST_KOKKOS_DEVICE_TYPE");  // CTest
    auto const* ctest_resource_group_count_str =
        std::getenv("CTEST_RESOURCE_GROUP_COUNT");  // CTest
    if (ctest_kokkos_device_type && ctest_resource_group_count_str &&
        local_rank_str) {
      // Use the device assigned by CTest
      use_gpu = get_ctest_gpu(local_rank_str);
    } else if (ndevices > 0) {
      // Use the device assigned by the rank
      if (local_rank_str) {
        auto local_rank = std::stoi(local_rank_str);
        use_gpu         = local_rank % ndevices;
      } else {
        // user only gave use ndevices, but the MPI environment variable wasn't
        // set. start with GPU 0 at this point
        use_gpu = 0;
      }
    }
    // shift assignments over by one so no one is assigned to "skip_device"
    if (use_gpu >= skip_device) ++use_gpu;
  }
  return use_gpu;
}

namespace {

void initialize_backends(const Kokkos::InitializationSettings& settings) {
// This is an experimental setting
// For KNL in Flat mode this variable should be set, so that
// memkind allocates high bandwidth memory correctly.
#ifdef KOKKOS_ENABLE_HBWSPACE
  setenv("MEMKIND_HBW_NODES", "1", 0);
#endif

  Kokkos::Impl::ExecSpaceManager::get_instance().initialize_spaces(settings);
}

void initialize_profiling(const Kokkos::Tools::InitArguments& args) {
  auto initialization_status =
      Kokkos::Tools::Impl::initialize_tools_subsystem(args);
  if (initialization_status.result ==
      Kokkos::Tools::Impl::InitializationStatus::InitializationResult::
          help_request) {
    g_is_initialized = true;
    ::Kokkos::finalize();
    std::exit(EXIT_SUCCESS);
  } else if (initialization_status.result ==
             Kokkos::Tools::Impl::InitializationStatus::InitializationResult::
                 success) {
    Kokkos::Tools::parseArgs(args.args);
    for (const auto& category_value : metadata_map) {
      for (const auto& key_value : category_value.second) {
        Kokkos::Tools::declareMetadata(key_value.first, key_value.second);
      }
    }
  } else {
    std::cerr << "Error initializing Kokkos Tools subsystem" << std::endl;
    g_is_initialized = true;
    ::Kokkos::finalize();
    std::exit(EXIT_FAILURE);
  }
}

std::string version_string_from_int(int version_number) {
  std::stringstream str_builder;
  str_builder << version_number / 10000 << "." << (version_number % 10000) / 100
              << "." << version_number % 100;
  return str_builder.str();
}

void pre_initialize_internal(const Kokkos::InitializationSettings& settings) {
  if (settings.has_disable_warnings() && settings.get_disable_warnings())
    g_show_warnings = false;
  if (settings.has_tune_internals() && settings.get_tune_internals())
    g_tune_internals = true;
  declare_configuration_metadata("version_info", "Kokkos Version",
                                 version_string_from_int(KOKKOS_VERSION));
#ifdef KOKKOS_COMPILER_APPLECC
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_APPLECC",
                                 std::to_string(KOKKOS_COMPILER_APPLECC));
  declare_configuration_metadata("tools_only", "compiler_family", "apple");
#endif
#ifdef KOKKOS_COMPILER_CLANG
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_CLANG",
                                 std::to_string(KOKKOS_COMPILER_CLANG));
  declare_configuration_metadata("tools_only", "compiler_family", "clang");
#endif
#ifdef KOKKOS_COMPILER_CRAYC
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_CRAYC",
                                 std::to_string(KOKKOS_COMPILER_CRAYC));
  declare_configuration_metadata("tools_only", "compiler_family", "cray");
#endif
#ifdef KOKKOS_COMPILER_GNU
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_GNU",
                                 std::to_string(KOKKOS_COMPILER_GNU));
  declare_configuration_metadata("tools_only", "compiler_family", "gnu");
#endif
#ifdef KOKKOS_COMPILER_IBM
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_IBM",
                                 std::to_string(KOKKOS_COMPILER_IBM));
  declare_configuration_metadata("tools_only", "compiler_family", "ibm");
#endif
#ifdef KOKKOS_COMPILER_INTEL
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_INTEL",
                                 std::to_string(KOKKOS_COMPILER_INTEL));
  declare_configuration_metadata("tools_only", "compiler_family", "intel");
#endif
#ifdef KOKKOS_COMPILER_NVCC
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_NVCC",
                                 std::to_string(KOKKOS_COMPILER_NVCC));
  declare_configuration_metadata("tools_only", "compiler_family", "nvcc");
#endif
#ifdef KOKKOS_COMPILER_PGI
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_PGI",
                                 std::to_string(KOKKOS_COMPILER_PGI));
  declare_configuration_metadata("tools_only", "compiler_family", "pgi");
#endif
#ifdef KOKKOS_COMPILER_MSVC
  declare_configuration_metadata("compiler_version", "KOKKOS_COMPILER_MSVC",
                                 std::to_string(KOKKOS_COMPILER_MSVC));
  declare_configuration_metadata("tools_only", "compiler_family", "msvc");
#endif

#ifdef KOKKOS_ENABLE_GNU_ATOMICS
  declare_configuration_metadata("atomics", "KOKKOS_ENABLE_GNU_ATOMICS", "yes");
#else
  declare_configuration_metadata("atomics", "KOKKOS_ENABLE_GNU_ATOMICS", "no");
#endif
#ifdef KOKKOS_ENABLE_INTEL_ATOMICS
  declare_configuration_metadata("atomics", "KOKKOS_ENABLE_INTEL_ATOMICS",
                                 "yes");
#else
  declare_configuration_metadata("atomics", "KOKKOS_ENABLE_INTEL_ATOMICS",
                                 "no");
#endif
#ifdef KOKKOS_ENABLE_WINDOWS_ATOMICS
  declare_configuration_metadata("atomics", "KOKKOS_ENABLE_WINDOWS_ATOMICS",
                                 "yes");
#else
  declare_configuration_metadata("atomics", "KOKKOS_ENABLE_WINDOWS_ATOMICS",
                                 "no");
#endif

#ifdef KOKKOS_ENABLE_PRAGMA_IVDEP
  declare_configuration_metadata("vectorization", "KOKKOS_ENABLE_PRAGMA_IVDEP",
                                 "yes");
#else
  declare_configuration_metadata("vectorization", "KOKKOS_ENABLE_PRAGMA_IVDEP",
                                 "no");
#endif
#ifdef KOKKOS_ENABLE_PRAGMA_LOOPCOUNT
  declare_configuration_metadata("vectorization",
                                 "KOKKOS_ENABLE_PRAGMA_LOOPCOUNT", "yes");
#else
  declare_configuration_metadata("vectorization",
                                 "KOKKOS_ENABLE_PRAGMA_LOOPCOUNT", "no");
#endif
#ifdef KOKKOS_ENABLE_PRAGMA_SIMD
  declare_configuration_metadata("vectorization", "KOKKOS_ENABLE_PRAGMA_SIMD",
                                 "yes");
#else
  declare_configuration_metadata("vectorization", "KOKKOS_ENABLE_PRAGMA_SIMD",
                                 "no");
#endif
#ifdef KOKKOS_ENABLE_PRAGMA_UNROLL
  declare_configuration_metadata("vectorization", "KOKKOS_ENABLE_PRAGMA_UNROLL",
                                 "yes");
#else
  declare_configuration_metadata("vectorization", "KOKKOS_ENABLE_PRAGMA_UNROLL",
                                 "no");
#endif
#ifdef KOKKOS_ENABLE_PRAGMA_VECTOR
  declare_configuration_metadata("vectorization", "KOKKOS_ENABLE_PRAGMA_VECTOR",
                                 "yes");
#else
  declare_configuration_metadata("vectorization", "KOKKOS_ENABLE_PRAGMA_VECTOR",
                                 "no");
#endif

#ifdef KOKKOS_ENABLE_HBWSPACE
  declare_configuration_metadata("memory", "KOKKOS_ENABLE_HBWSPACE", "yes");
#else
  declare_configuration_metadata("memory", "KOKKOS_ENABLE_HBWSPACE", "no");
#endif
#ifdef KOKKOS_ENABLE_INTEL_MM_ALLOC
  declare_configuration_metadata("memory", "KOKKOS_ENABLE_INTEL_MM_ALLOC",
                                 "yes");
#else
  declare_configuration_metadata("memory", "KOKKOS_ENABLE_INTEL_MM_ALLOC",
                                 "no");
#endif

#ifdef KOKKOS_ENABLE_ASM
  declare_configuration_metadata("options", "KOKKOS_ENABLE_ASM", "yes");
#else
  declare_configuration_metadata("options", "KOKKOS_ENABLE_ASM", "no");
#endif
#ifdef KOKKOS_ENABLE_CXX14
  declare_configuration_metadata("options", "KOKKOS_ENABLE_CXX14", "yes");
#else
  declare_configuration_metadata("options", "KOKKOS_ENABLE_CXX14", "no");
#endif
#ifdef KOKKOS_ENABLE_CXX17
  declare_configuration_metadata("options", "KOKKOS_ENABLE_CXX17", "yes");
#else
  declare_configuration_metadata("options", "KOKKOS_ENABLE_CXX17", "no");
#endif
#ifdef KOKKOS_ENABLE_CXX20
  declare_configuration_metadata("options", "KOKKOS_ENABLE_CXX20", "yes");
#else
  declare_configuration_metadata("options", "KOKKOS_ENABLE_CXX20", "no");
#endif
#ifdef KOKKOS_ENABLE_DEBUG_BOUNDS_CHECK
  declare_configuration_metadata("options", "KOKKOS_ENABLE_DEBUG_BOUNDS_CHECK",
                                 "yes");
#else
  declare_configuration_metadata("options", "KOKKOS_ENABLE_DEBUG_BOUNDS_CHECK",
                                 "no");
#endif
#ifdef KOKKOS_ENABLE_HWLOC
  declare_configuration_metadata("options", "KOKKOS_ENABLE_HWLOC", "yes");
#else
  declare_configuration_metadata("options", "KOKKOS_ENABLE_HWLOC", "no");
#endif
#ifdef KOKKOS_ENABLE_LIBRT
  declare_configuration_metadata("options", "KOKKOS_ENABLE_LIBRT", "yes");
#else
  declare_configuration_metadata("options", "KOKKOS_ENABLE_LIBRT", "no");
#endif
#ifdef KOKKOS_ENABLE_LIBDL
  declare_configuration_metadata("options", "KOKKOS_ENABLE_LIBDL", "yes");
#else
  declare_configuration_metadata("options", "KOKKOS_ENABLE_LIBDL", "no");
#endif
  declare_configuration_metadata("architecture", "Default Device",
                                 typeid(Kokkos::DefaultExecutionSpace).name());
}

void post_initialize_internal(const Kokkos::InitializationSettings& settings) {
  Kokkos::Tools::InitArguments tools_init_arguments;
  combine(tools_init_arguments, settings);
  initialize_profiling(tools_init_arguments);
  g_is_initialized = true;
}

void initialize_internal(const Kokkos::InitializationSettings& settings) {
  pre_initialize_internal(settings);
  initialize_backends(settings);
  post_initialize_internal(settings);
}

void finalize_internal() {
  typename decltype(finalize_hooks)::size_type numSuccessfulCalls = 0;
  while (!finalize_hooks.empty()) {
    auto f = finalize_hooks.top();
    try {
      f();
    } catch (...) {
      std::cerr << "Kokkos::finalize: A finalize hook (set via "
                   "Kokkos::push_finalize_hook) threw an exception that it did "
                   "not catch."
                   "  Per std::atexit rules, this results in std::terminate.  "
                   "This is "
                   "finalize hook number "
                << numSuccessfulCalls
                << " (1-based indexing) "
                   "out of "
                << finalize_hooks.size()
                << " to call.  Remember that "
                   "Kokkos::finalize calls finalize hooks in reverse order "
                   "from how they "
                   "were pushed."
                << std::endl;
      std::terminate();
    }
    finalize_hooks.pop();
    ++numSuccessfulCalls;
  }

  Kokkos::Profiling::finalize();

  Kokkos::Impl::ExecSpaceManager::get_instance().finalize_spaces();

  g_is_initialized = false;
  g_show_warnings  = true;
  g_tune_internals = false;
}

void fence_internal(const std::string& name) {
  Kokkos::Impl::ExecSpaceManager::get_instance().static_fence(name);
}

unsigned get_process_id() {
#ifdef _WIN32
  return unsigned(GetCurrentProcessId());
#else
  return unsigned(getpid());
#endif
}

void print_help_message() {
  auto const help_message = R"(
--------------------------------------------------------------------------------
-------------Kokkos command line arguments--------------------------------------
--------------------------------------------------------------------------------
This program is using Kokkos.  You can use the following command line flags to
control its behavior:

Kokkos Core Options:
  --kokkos-help                  : print this message
  --kokkos-disable-warnings      : disable kokkos warning messages
  --kokkos-tune-internals        : allow Kokkos to autotune policies and declare
                                   tuning features through the tuning system. If
                                   left off, Kokkos uses heuristics
  --kokkos-num-threads=INT       : specify total number of threads to use for
                                   parallel regions on the host.
  --kokkos-device-id=INT         : specify device id to be used by Kokkos.
  --kokkos-num-devices=INT[,INT] : used when running MPI jobs. Specify number of
                                   devices per node to be used. Process to device
                                   mapping happens by obtaining the local MPI rank
                                   and assigning devices round-robin. The optional
                                   second argument allows for an existing device
                                   to be ignored. This is most useful on workstations
                                   with multiple GPUs of which one is used to drive
                                   screen output.

Kokkos Tools Options:
  --kokkos-tools-library         : Equivalent to KOKKOS_PROFILE_LIBRARY environment
                                   variable. Must either be full path to library or
                                   name of library if the path is present in the
                                   runtime library search path (e.g. LD_LIBRARY_PATH)
  --kokkos-tools-help            : Query the (loaded) kokkos-tool for its command-line
                                   option support (which should then be passed via
                                   --kokkos-tools-args="...")
  --kokkos-tools-args=STR        : A single (quoted) string of options which will be
                                   whitespace delimited and passed to the loaded
                                   kokkos-tool as command-line arguments. E.g.
                                   `<EXE> --kokkos-tools-args="-c input.txt"` will
                                   pass `<EXE> -c input.txt` as argc/argv to tool

Join us on Slack, visit https://kokkosteam.slack.com
Report bugs to https://github.com/kokkos/kokkos/issues
--------------------------------------------------------------------------------
)";
  std::cout << help_message << std::endl;
}

}  // namespace

void Kokkos::Impl::parse_command_line_arguments(
    int& argc, char* argv[], InitializationSettings& settings) {
  int num_threads;
  int ignored_numa;
  int device_id;
  int num_devices;
  int skip_device;

  bool kokkos_num_threads_found = false;
  bool kokkos_device_id_found   = false;
  bool kokkos_num_devices_found = false;

  Tools::InitArguments tools_init_arguments;
  combine(tools_init_arguments, settings);
  Tools::Impl::parse_command_line_arguments(argc, argv, tools_init_arguments);
  combine(settings, tools_init_arguments);

  bool help_flag = false;

  int iarg = 0;
  while (iarg < argc) {
    bool remove_flag = false;
    if (check_int_arg(argv[iarg], "--kokkos-num-threads", &num_threads) ||
        check_int_arg(argv[iarg], "--kokkos-threads", &num_threads)) {
      if (check_arg(argv[iarg], "--kokkos-threads")) {
        warn_deprecated_command_line_argument("--kokkos-threads",
                                              "--kokkos-num-threads");
      }
      settings.set_num_threads(num_threads);
      remove_flag              = true;
      kokkos_num_threads_found = true;
    } else if (!kokkos_num_threads_found &&
               (check_int_arg(argv[iarg], "--num-threads", &num_threads) ||
                check_int_arg(argv[iarg], "--threads", &num_threads))) {
      if (check_arg(argv[iarg], "--num-threads")) {
        warn_deprecated_command_line_argument("--num-threads",
                                              "--kokkos-num-threads");
      }
      if (check_arg(argv[iarg], "--threads")) {
        warn_deprecated_command_line_argument("--threads",
                                              "--kokkos-num-threads");
      }
      settings.set_num_threads(num_threads);
    } else if (check_int_arg(argv[iarg], "--kokkos-numa", &ignored_numa) ||
               check_int_arg(argv[iarg], "--numa", &ignored_numa)) {
      if (check_arg(argv[iarg], "--kokkos-numa")) {
        warn_deprecated_command_line_argument("--kokkos-numa");
        remove_flag = true;
      } else {
        warn_deprecated_command_line_argument("--numa");
      }
    } else if (check_int_arg(argv[iarg], "--kokkos-device-id", &device_id) ||
               check_int_arg(argv[iarg], "--kokkos-device", &device_id)) {
      if (check_arg(argv[iarg], "--kokkos-device")) {
        warn_deprecated_command_line_argument("--kokkos-device",
                                              "--kokkos-device-id");
      }
      settings.set_device_id(device_id);
      remove_flag            = true;
      kokkos_device_id_found = true;
    } else if (!kokkos_device_id_found &&
               (check_int_arg(argv[iarg], "--device-id", &device_id) ||
                check_int_arg(argv[iarg], "--device", &device_id))) {
      if (check_arg(argv[iarg], "--device-id")) {
        warn_deprecated_command_line_argument("--device-id",
                                              "--kokkos-device-id");
      }
      if (check_arg(argv[iarg], "--device")) {
        warn_deprecated_command_line_argument("--device", "--kokkos-device-id");
      }
      settings.set_device_id(device_id);
    } else if (check_arg(argv[iarg], "--kokkos-num-devices") ||
               check_arg(argv[iarg], "--num-devices") ||
               check_arg(argv[iarg], "--kokkos-ndevices") ||
               check_arg(argv[iarg], "--ndevices")) {
      if (check_arg(argv[iarg], "--num-devices")) {
        warn_deprecated_command_line_argument("--num-devices",
                                              "--kokkos-num-devices");
      }
      if (check_arg(argv[iarg], "--ndevices")) {
        warn_deprecated_command_line_argument("--ndevices",
                                              "--kokkos-num-devices");
      }
      if (check_arg(argv[iarg], "--kokkos-ndevices")) {
        warn_deprecated_command_line_argument("--kokkos-ndevices",
                                              "--kokkos-num-devices");
      }
      // Find the number of device (expecting --device=XX)
      if (!((strncmp(argv[iarg], "--kokkos-num-devices=", 21) == 0) ||
            (strncmp(argv[iarg], "--num-devices=", 14) == 0) ||
            (strncmp(argv[iarg], "--kokkos-ndevices=", 18) == 0) ||
            (strncmp(argv[iarg], "--ndevices=", 11) == 0)))
        throw_runtime_exception(
            "Error: expecting an '=INT[,INT]' after command line argument "
            "'--kokkos-num-devices'. Raised by "
            "Kokkos::initialize(int argc, char* argv[]).");

      char* num1      = strchr(argv[iarg], '=') + 1;
      char* num2      = strpbrk(num1, ",");
      int num1_len    = num2 == nullptr ? strlen(num1) : num2 - num1;
      char* num1_only = new char[num1_len + 1];
      strncpy(num1_only, num1, num1_len);
      num1_only[num1_len] = '\0';

      if (!is_unsigned_int(num1_only) || (strlen(num1_only) == 0)) {
        throw_runtime_exception(
            "Error: expecting an integer number after command line argument "
            "'--kokkos-num-devices'. Raised by "
            "Kokkos::initialize(int argc, char* argv[]).");
      }
      if (check_arg(argv[iarg], "--kokkos-num-devices") ||
          check_arg(argv[iarg], "--kokkos-ndevices") ||
          !kokkos_num_devices_found) {
        num_devices = std::stoi(num1_only);
        settings.set_num_devices(num_devices);
      }
      delete[] num1_only;

      if (num2 != nullptr) {
        if ((!is_unsigned_int(num2 + 1)) || (strlen(num2) == 1))
          throw_runtime_exception(
              "Error: expecting an integer number after command line argument "
              "'--kokkos-num-devices=XX,'. Raised by "
              "Kokkos::initialize(int argc, char* argv[]).");

        if (check_arg(argv[iarg], "--kokkos-num-devices") ||
            check_arg(argv[iarg], "--kokkos-ndevices") ||
            !kokkos_num_devices_found) {
          skip_device = std::stoi(num2 + 1);
          settings.set_skip_device(skip_device);
        }
      }

      if (check_arg(argv[iarg], "--kokkos-num-devices") ||
          check_arg(argv[iarg], "--kokkos-ndevices")) {
        remove_flag = true;
      }
    } else if (check_arg(argv[iarg], "--kokkos-disable-warnings")) {
      remove_flag = true;
      settings.set_disable_warnings(true);
    } else if (check_arg(argv[iarg], "--kokkos-tune-internals")) {
      remove_flag = true;
      settings.set_tune_internals(true);
    } else if (check_arg(argv[iarg], "--kokkos-help") ||
               check_arg(argv[iarg], "--help")) {
      help_flag = true;

      if (check_arg(argv[iarg], "--kokkos-help")) {
        remove_flag = true;
      }
    }

    if (remove_flag) {
      for (int k = iarg; k < argc - 1; k++) {
        argv[k] = argv[k + 1];
      }
      argc--;
    } else {
      iarg++;
    }
  }

  if (help_flag) {
    print_help_message();
  }

  if ((tools_init_arguments.args ==
       Kokkos::Tools::InitArguments::unset_string_option) &&
      argc > 0) {
    settings.set_tool_args(argv[0]);
  }
}

void Kokkos::Impl::parse_environment_variables(
    InitializationSettings& settings) {
  char* endptr;

  Tools::InitArguments tools_init_arguments;
  combine(tools_init_arguments, settings);
  auto init_result =
      Tools::Impl::parse_environment_variables(tools_init_arguments);
  if (init_result.result ==
      Tools::Impl::InitializationStatus::environment_argument_mismatch) {
    Impl::throw_runtime_exception(init_result.error_message);
  }
  combine(settings, tools_init_arguments);

  auto env_num_threads_str = std::getenv("KOKKOS_NUM_THREADS");
  if (env_num_threads_str != nullptr) {
    errno                = 0;
    auto env_num_threads = std::strtol(env_num_threads_str, &endptr, 10);
    if (endptr == env_num_threads_str)
      Impl::throw_runtime_exception(
          "Error: cannot convert KOKKOS_NUM_THREADS to an integer. Raised by "
          "Kokkos::initialize(int argc, char* argv[]).");
    if (errno == ERANGE)
      Impl::throw_runtime_exception(
          "Error: KOKKOS_NUM_THREADS out of range of representable values by "
          "an integer. Raised by Kokkos::initialize(int argc, char* argv[]).");
    settings.set_num_threads(env_num_threads);
  }
  auto env_numa_str = std::getenv("KOKKOS_NUMA");
  if (env_numa_str != nullptr) {
    warn_deprecated_environment_variable("KOKKOS_NUMA");
  }
  auto env_device_id_str = std::getenv("KOKKOS_DEVICE_ID");
  if (env_device_id_str != nullptr) {
    errno              = 0;
    auto env_device_id = std::strtol(env_device_id_str, &endptr, 10);
    if (endptr == env_device_id_str)
      Impl::throw_runtime_exception(
          "Error: cannot convert KOKKOS_DEVICE_ID to an integer. Raised by "
          "Kokkos::initialize(int argc, char* argv[]).");
    if (errno == ERANGE)
      Impl::throw_runtime_exception(
          "Error: KOKKOS_DEVICE_ID out of range of representable values by an "
          "integer. Raised by Kokkos::initialize(int argc, char* argv[]).");
    settings.set_device_id(env_device_id);
  }
  auto env_rdevices_str    = std::getenv("KOKKOS_RAND_DEVICES");
  auto env_num_devices_str = std::getenv("KOKKOS_NUM_DEVICES");
  if (env_num_devices_str != nullptr || env_rdevices_str != nullptr) {
    errno = 0;
    if (env_num_devices_str != nullptr && env_rdevices_str != nullptr) {
      Impl::throw_runtime_exception(
          "Error: cannot specify both KOKKOS_NUM_DEVICES and "
          "KOKKOS_RAND_DEVICES. "
          "Raised by Kokkos::initialize(int argc, char* argv[]).");
    }
    int rdevices = -1;
    if (env_num_devices_str != nullptr) {
      auto env_num_devices = std::strtol(env_num_devices_str, &endptr, 10);
      if (endptr == env_num_devices_str)
        Impl::throw_runtime_exception(
            "Error: cannot convert KOKKOS_NUM_DEVICES to an integer. Raised by "
            "Kokkos::initialize(int argc, char* argv[]).");
      if (errno == ERANGE)
        Impl::throw_runtime_exception(
            "Error: KOKKOS_NUM_DEVICES out of range of representable values by "
            "an integer. Raised by Kokkos::initialize(int argc, char* "
            "argv[]).");
      settings.set_num_devices(env_num_devices);
    } else {  // you set KOKKOS_RAND_DEVICES
      auto env_rdevices = std::strtol(env_rdevices_str, &endptr, 10);
      if (endptr == env_rdevices_str)
        Impl::throw_runtime_exception(
            "Error: cannot convert KOKKOS_RAND_DEVICES to an integer. Raised "
            "by Kokkos::initialize(int argc, char* argv[]).");
      if (errno == ERANGE)
        Impl::throw_runtime_exception(
            "Error: KOKKOS_RAND_DEVICES out of range of representable values "
            "by an integer. Raised by Kokkos::initialize(int argc, char* "
            "argv[]).");
      else
        rdevices = env_rdevices;
    }
    // Skip device
    auto env_skip_device_str = std::getenv("KOKKOS_SKIP_DEVICE");
    if (env_skip_device_str != nullptr) {
      errno                = 0;
      auto env_skip_device = std::strtol(env_skip_device_str, &endptr, 10);
      if (endptr == env_skip_device_str)
        Impl::throw_runtime_exception(
            "Error: cannot convert KOKKOS_SKIP_DEVICE to an integer. Raised by "
            "Kokkos::initialize(int argc, char* argv[]).");
      if (errno == ERANGE)
        Impl::throw_runtime_exception(
            "Error: KOKKOS_SKIP_DEVICE out of range of representable values by "
            "an integer. Raised by Kokkos::initialize(int argc, char* "
            "argv[]).");
      settings.set_skip_device(env_skip_device);
    }
    if (rdevices > 0) {
      if (settings.has_skip_device() && rdevices == 1)
        Impl::throw_runtime_exception(
            "Error: cannot KOKKOS_SKIP_DEVICE the only KOKKOS_RAND_DEVICE. "
            "Raised by Kokkos::initialize(int argc, char* argv[]).");

      std::srand(get_process_id());
      while (settings.has_device_id()) {
        int test_device_id = std::rand() % rdevices;
        if (!settings.has_skip_device() ||
            test_device_id != settings.get_skip_device()) {
          settings.set_device_id(test_device_id);
        }
      }
    }
  }
  char* env_disable_warnings_str = std::getenv("KOKKOS_DISABLE_WARNINGS");
  if (env_disable_warnings_str != nullptr) {
    auto const regex =
        std::regex("^(true|on|yes|[1-9])$",
                   std::regex_constants::icase | std::regex_constants::egrep);
    bool const disable_warnings =
        std::regex_match(env_disable_warnings_str, regex);
    settings.set_disable_warnings(disable_warnings);
  }
}

//----------------------------------------------------------------------------

void Kokkos::initialize(int& argc, char* argv[]) {
  InitializationSettings settings;
  Impl::parse_environment_variables(settings);
  Impl::parse_command_line_arguments(argc, argv, settings);
  initialize_internal(settings);
}

void Kokkos::initialize(InitializationSettings const& settings) {
  InitializationSettings tmp;
  Impl::parse_environment_variables(tmp);
  combine(tmp, settings);
  initialize_internal(tmp);
}

void Kokkos::Impl::pre_initialize(const InitializationSettings& settings) {
  pre_initialize_internal(settings);
}

void Kokkos::Impl::post_initialize(const InitializationSettings& settings) {
  post_initialize_internal(settings);
}

void Kokkos::push_finalize_hook(std::function<void()> f) {
  finalize_hooks.push(f);
}

void Kokkos::finalize() { finalize_internal(); }

#ifdef KOKKOS_ENABLE_DEPRECATED_CODE_3
KOKKOS_DEPRECATED void Kokkos::finalize_all() { finalize_internal(); }
#endif

void Kokkos::fence(const std::string& name) { fence_internal(name); }

namespace {
void print_helper(std::ostringstream& out,
                  const std::map<std::string, std::string>& print_me) {
  for (const auto& kv : print_me) {
    out << kv.first << ": " << kv.second << '\n';
  }
}
}  // namespace

void Kokkos::print_configuration(std::ostream& out, const bool detail) {
  std::ostringstream msg;

  print_helper(msg, metadata_map["version_info"]);

  msg << "Compiler:" << std::endl;
  print_helper(msg, metadata_map["compiler_version"]);

  msg << "Architecture:" << std::endl;
  print_helper(msg, metadata_map["architecture"]);

  msg << "Atomics:" << std::endl;
  print_helper(msg, metadata_map["atomics"]);

  msg << "Vectorization:" << std::endl;
  print_helper(msg, metadata_map["vectorization"]);

  msg << "Memory:" << std::endl;
  print_helper(msg, metadata_map["memory"]);

  msg << "Options:" << std::endl;
  print_helper(msg, metadata_map["options"]);

  Impl::ExecSpaceManager::get_instance().print_configuration(msg, detail);

  out << msg.str() << std::endl;
}

bool Kokkos::is_initialized() noexcept { return g_is_initialized; }

bool Kokkos::show_warnings() noexcept { return g_show_warnings; }

bool Kokkos::tune_internals() noexcept { return g_tune_internals; }

namespace Kokkos {

#ifdef KOKKOS_COMPILER_PGI
namespace Impl {
// Bizzarely, an extra jump instruction forces the PGI compiler to not have a
// bug related to (probably?) empty base optimization and/or aggregate
// construction.
void _kokkos_pgi_compiler_bug_workaround() {}
}  // end namespace Impl
#endif

}  // namespace Kokkos

Kokkos::Impl::InitializationSettingsHelper<std::string>::storage_type const
    Kokkos::Impl::InitializationSettingsHelper<std::string>::unspecified =
        "some string we don't expect user would ever provide";
