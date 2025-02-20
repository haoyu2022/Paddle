cc_library(
  host_tracer
  SRCS host_tracer.cc
  DEPS framework_proto phi common var_type_traits)
cc_library(
  cuda_tracer
  SRCS cuda_tracer.cc cupti_data_process.cc
  DEPS standalone_executor phi glog common)
cc_library(
  xpu_tracer
  SRCS xpu_tracer.cc
  DEPS phi glog common)
add_subdirectory(custom_device)
cc_library(
  event_node
  SRCS event_node.cc
  DEPS phi common)
cc_library(
  profiler_utils
  SRCS utils.cc
  DEPS phi glog common)
add_subdirectory(dump)
cc_library(
  profiler_logger
  SRCS chrometracing_logger.cc dump/serialization_logger.cc
       dump/deserialization_reader.cc
  DEPS nodetreeproto event_node profiler_utils)
cc_library(
  event_bind
  SRCS event_python.cc
  DEPS profiler_logger)
cc_library(
  cpu_utilization
  SRCS cpu_utilization.cc
  DEPS phi common glog)
cc_library(
  new_profiler
  SRCS profiler.cc
  DEPS host_tracer
       cuda_tracer
       xpu_tracer
       profiler_utils
       cpu_utilization
       event_bind
       custom_tracer)
cc_test(
  test_event_node
  SRCS test_event_node.cc
  DEPS event_node profiler_logger)
cc_test(
  test_extra_info
  SRCS test_extra_info.cc
  DEPS profiler_utils)
cc_test(
  test_serialization_logger
  SRCS dump/test_serialization_logger.cc
  DEPS event_bind)
cc_test(
  new_profiler_test
  SRCS profiler_test.cc
  DEPS new_profiler)
