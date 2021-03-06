load("//bazel:base_cc.bzl", "base_cc_library", "base_cc_test", "base_objc_library")

package_group(
    name = "internal",
    packages = [
        "//base/...",
    ],
)

base_cc_library(
    name = "auto_reset",
    hdrs = ["auto_reset.h"],
    visibility = ["//visibility:public"],
)

base_cc_library(
    name = "bits",
    hdrs = ["bits.h"],
    visibility = ["//visibility:public"],
)

base_cc_library(
    name = "build_config",
    hdrs = ["build_config.h"],
    visibility = ["//visibility:public"],
)

base_cc_library(
    name = "callback",
    hdrs = [
        "callback.h",
        "callback_forward.h",
    ],
    visibility = ["//visibility:public"],
    deps = ["@com_google_absl//absl/functional:function_ref"],
)

base_cc_library(
    name = "compiler_specific",
    hdrs = ["compiler_specific.h"],
    visibility = ["//visibility:public"],
    deps = [":build_config"],
)

base_cc_library(
    name = "completion_once_callback",
    hdrs = ["completion_once_callback.h"],
    visibility = ["//visibility:public"],
    deps = [":callback"],
)

base_cc_library(
    name = "data_view",
    srcs = ["data_view.cc"],
    hdrs = ["data_view.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":export",
        ":sys_byteorder",
    ],
)

base_cc_library(
    name = "environment",
    srcs = ["environment.cc"],
    hdrs = ["environment.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":build_config",
        ":export",
        "@com_google_absl//absl/strings",
    ],
)

base_cc_library(
    name = "export",
    hdrs = ["export.h"],
    visibility = ["//visibility:public"],
)

base_cc_library(
    name = "file_descriptor_posix",
    hdrs = ["file_descriptor_posix.h"],
    visibility = ["//visibility:public"],
    deps = ["//base/files:file"],
)

base_cc_library(
    name = "guid",
    srcs = ["guid.cc"],
    hdrs = ["guid.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":build_config",
        ":export",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

base_cc_library(
    name = "logging",
    hdrs = ["logging.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":immediate_crash",
        "@com_github_google_glog//:glog",
    ],
)

base_cc_library(
    name = "immediate_crash",
    hdrs = ["immediate_crash.h"],
    deps = [":build_config"],
)

base_cc_library(
    name = "io_buffer",
    srcs = ["io_buffer.cc"],
    hdrs = ["io_buffer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":export",
        ":logging",
        "//base/memory:free_deleter",
    ],
)

base_cc_library(
    name = "macros",
    hdrs = ["macros.h"],
    visibility = ["//visibility:public"],
)

NATIVE_LIBRARY_HDRS = ["native_library.h"]

# TODO: Should deal with native_library_fuchsia.cc
base_cc_library(
    name = "native_library",
    srcs = [
        "native_library.cc",
        "scoped_native_library.cc",
    ] + select({
        "@com_chokobole_bazel_utils//:ios": [],
        "@com_chokobole_bazel_utils//:mac": [],
        "@com_chokobole_bazel_utils//:windows": ["native_library_win.cc"],
        "//conditions:default": ["native_library_posix.cc"],
    }),
    hdrs = NATIVE_LIBRARY_HDRS + [
        "scoped_native_library.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":scoped_generic",
        "//base/files:file_path",
    ] + select({
        "@com_chokobole_bazel_utils//:ios": [":native_library_ios"],
        "@com_chokobole_bazel_utils//:mac": [":native_library_mac"],
        "@com_chokobole_bazel_utils//:windows": [
            "//base/files:file_util",
            "//base/strings",
        ],
        "//conditions:default": [],
    }),
)

base_objc_library(
    name = "native_library_ios",
    srcs = ["native_library_ios.mm"],
    hdrs = NATIVE_LIBRARY_HDRS,
    deps = ["//base/files:file_path"],
)

base_objc_library(
    name = "native_library_mac",
    srcs = ["native_library_mac.mm"],
    hdrs = NATIVE_LIBRARY_HDRS,
    deps = [
        "//base/files:file_util",
        "//base/mac:scoped_cftyperef",
    ],
)

base_cc_library(
    name = "no_destructor",
    hdrs = ["no_destructor.h"],
    visibility = ["//visibility:public"],
)

base_cc_library(
    name = "scoped_generic",
    hdrs = ["scoped_generic.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":compiler_specific",
        ":logging",
    ],
)

base_cc_library(
    name = "stl_util",
    hdrs = ["stl_util.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":template_util",
        "@com_google_absl//absl/types:optional",
    ],
)

base_cc_library(
    name = "system_error_code",
    srcs = ["system_error_code.cc"],
    hdrs = ["system_error_code.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":build_config",
        ":export",
        ":logging",
        ":stl_util",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

base_cc_library(
    name = "sys_byteorder",
    hdrs = ["sys_byteorder.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":build_config",
        ":logging",
    ],
)

base_cc_library(
    name = "template_util",
    hdrs = ["template_util.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":build_config",
    ],
)

base_cc_library(
    name = "token",
    srcs = ["token.cc"],
    hdrs = ["token.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":export",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:optional",
    ],
)

base_cc_library(
    name = "unguessable_token",
    srcs = ["unguessable_token.cc"],
    hdrs = ["unguessable_token.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":logging",
        ":no_destructor",
        ":token",
    ],
)

base_cc_test(
    name = "base_unittests",
    srcs = [
        "auto_reset_unittest.cc",
        "bits_unittest.cc",
        "data_view_unittest.cc",
        "environment_unittest.cc",
        "guid_unittest.cc",
        "scoped_generic_unittest.cc",
        "sys_byteorder_unittest.cc",
    ],
    deps = [
        ":auto_reset",
        ":bits",
        ":data_view",
        ":environment",
        ":guid",
        ":scoped_generic",
        ":stl_util",
        ":sys_byteorder",
        "@com_google_googletest//:gtest_main",
    ],
)
