#!/usr/bin/env python3
"""Standalone MultiOptimizationProfile positive sample for IxRT/TensorRT."""

from __future__ import annotations

import argparse

import numpy as np
import tensorrt as trt

try:
    from cuda.bindings import runtime as cudart
except ModuleNotFoundError:
    import cuda.cudart as cudart


N_CONTEXT = 2
IDENTITY_INPUT_NAME = "inputT0"
IDENTITY_INPUT_SHAPE = (3, 4, 5)
MATMUL_INPUT_NAME = "x"
MATMUL_BATCH = 8
MATMUL_K = 128


def check_cuda(result, name):
    status = result[0] if isinstance(result, tuple) else result
    if status != cudart.cudaError_t.cudaSuccess:
        raise RuntimeError(f"{name} failed: {status}")
    return result[1:] if isinstance(result, tuple) else ()


def make_matmul_weights():
    weights = np.arange(MATMUL_K * MATMUL_K, dtype=np.float32)
    weights = weights.reshape(MATMUL_K, MATMUL_K)
    return np.ascontiguousarray((weights % 17 - 8) / 17.0)


def build_identity_network(builder):
    network = builder.create_network()
    config = builder.create_builder_config()
    tensor = network.add_input(IDENTITY_INPUT_NAME, trt.float32, [-1, -1, -1])

    profile0 = builder.create_optimization_profile()
    profile0.set_shape(tensor.name, [1, 1, 1], [3, 4, 5], [6, 8, 10])
    config.add_optimization_profile(profile0)

    profile1 = builder.create_optimization_profile()
    profile1.set_shape(tensor.name, [1, 1, 1], [6, 8, 10], [9, 12, 15])
    config.add_optimization_profile(profile1)

    identity = network.add_identity(tensor)
    network.mark_output(identity.get_output(0))

    input_data = np.arange(
        np.prod(IDENTITY_INPUT_SHAPE), dtype=np.float32).reshape(IDENTITY_INPUT_SHAPE)
    return network, config, tensor.name, input_data, input_data


def build_matmul_relu_network(builder):
    network = builder.create_network(
        flags=1 << int(trt.NetworkDefinitionCreationFlag.EXPLICIT_BATCH))
    config = builder.create_builder_config()
    config.set_memory_pool_limit(trt.MemoryPoolType.WORKSPACE, 256 << 20)

    tensor = network.add_input(MATMUL_INPUT_NAME, trt.float32, (-1, MATMUL_K))
    weights = make_matmul_weights()
    constant = network.add_constant((MATMUL_K, MATMUL_K), trt.Weights(weights))
    mm = network.add_matrix_multiply(
        tensor,
        trt.MatrixOperation.NONE,
        constant.get_output(0),
        trt.MatrixOperation.NONE,
    )
    relu = network.add_activation(mm.get_output(0), trt.ActivationType.RELU)
    network.mark_output(relu.get_output(0))

    for _ in range(N_CONTEXT):
        profile = builder.create_optimization_profile()
        profile.set_shape(
            tensor.name,
            (MATMUL_BATCH, MATMUL_K),
            (MATMUL_BATCH, MATMUL_K),
            (MATMUL_BATCH, MATMUL_K),
        )
        config.add_optimization_profile(profile)

    input_data = np.arange(
        MATMUL_BATCH * MATMUL_K, dtype=np.float32).reshape(MATMUL_BATCH, MATMUL_K)
    return network, config, tensor.name, input_data, None


def build_engine(network_name):
    logger = trt.Logger(trt.Logger.WARNING)
    builder = trt.Builder(logger)
    if network_name == "identity":
        network, config, input_name, input_data, expected_output = build_identity_network(
            builder)
    else:
        network, config, input_name, input_data, expected_output = build_matmul_relu_network(
            builder)

    plan = builder.build_serialized_network(network, config)
    if not plan:
        raise RuntimeError("build_serialized_network failed")

    runtime = trt.Runtime(logger)
    engine = runtime.deserialize_cuda_engine(plan)
    if not engine:
        raise RuntimeError("deserialize_cuda_engine failed")
    return engine, input_name, input_data, expected_output


def tensor_names(engine):
    return [engine.get_tensor_name(i) for i in range(engine.num_io_tensors)]


def allocate_buffers(engine, context, input_name, input_data):
    buffers = {}
    for name in tensor_names(engine):
        shape = tuple(context.get_tensor_shape(name))
        nbytes = int(np.prod(shape)) * np.dtype(np.float32).itemsize
        host = np.empty(shape, dtype=np.float32)
        (device,) = check_cuda(cudart.cudaMalloc(nbytes), "cudaMalloc")
        buffers[name] = [host, device, nbytes]
        if not context.set_tensor_address(name, device):
            raise RuntimeError(f"set_tensor_address({name}) failed")

    buffers[input_name][0] = np.ascontiguousarray(input_data)
    return buffers


def free_buffers(buffers):
    for _, device, _ in buffers.values():
        check_cuda(cudart.cudaFree(device), "cudaFree")


def run_context(engine, context, profile_index, input_name, input_data, expected_output):
    print(f"Use optimization-profile {profile_index}", flush=True)
    print(f"BEFORE_SET_PROFILE:{profile_index}", flush=True)
    if not context.set_optimization_profile_async(profile_index, 0):
        raise RuntimeError(
            f"set_optimization_profile_async({profile_index}) failed")
    print(f"AFTER_SET_PROFILE:{profile_index}", flush=True)

    if not context.set_input_shape(input_name, input_data.shape):
        raise RuntimeError("set_input_shape failed")
    print(f"AFTER_SET_INPUT_SHAPE:{profile_index}", flush=True)

    buffers = allocate_buffers(engine, context, input_name, input_data)
    try:
        check_cuda(
            cudart.cudaMemcpyAsync(
                buffers[input_name][1],
                buffers[input_name][0].ctypes.data,
                buffers[input_name][2],
                cudart.cudaMemcpyKind.cudaMemcpyHostToDevice,
                0,
            ),
            "cudaMemcpyAsync H2D",
        )

        print(f"BEFORE_EXECUTE:{profile_index}", flush=True)
        if not context.execute_async_v3(0):
            raise RuntimeError("execute_async_v3 failed")
        print(f"AFTER_EXECUTE:{profile_index}", flush=True)

        output_name = next(
            name for name in tensor_names(engine)
            if engine.get_tensor_mode(name) == trt.TensorIOMode.OUTPUT)
        check_cuda(
            cudart.cudaMemcpyAsync(
                buffers[output_name][0].ctypes.data,
                buffers[output_name][1],
                buffers[output_name][2],
                cudart.cudaMemcpyKind.cudaMemcpyDeviceToHost,
                0,
            ),
            "cudaMemcpyAsync D2H",
        )
        check_cuda(cudart.cudaStreamSynchronize(0), "cudaStreamSynchronize")

        if expected_output is None:
            print(f"OUTPUT_UNCHECKED:{profile_index}", flush=True)
        else:
            if not np.allclose(
                    buffers[output_name][0], expected_output, rtol=1e-5, atol=1e-5):
                raise RuntimeError(f"{output_name} does not match expected output")
            print(f"OUTPUT_MATCH:{profile_index}", flush=True)
    finally:
        free_buffers(buffers)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--network",
        choices=("identity", "matmul-relu"),
        default="identity",
        help="Network to build. identity is the positive sample; "
             "matmul-relu switches to the MatMul+ReLU boundary probe.",
    )
    args = parser.parse_args()

    print(f"NETWORK:{args.network}", flush=True)
    engine, input_name, input_data, expected_output = build_engine(args.network)
    print("BUILD_ENGINE_PASSED", flush=True)
    contexts = [engine.create_execution_context() for _ in range(N_CONTEXT)]
    print("AFTER_CREATE_CONTEXTS", flush=True)

    for profile_index, context in enumerate(contexts):
        run_context(
            engine,
            context,
            profile_index,
            input_name,
            input_data,
            expected_output,
        )

    print("PASS: MultiOptimizationProfile inference works", flush=True)


if __name__ == "__main__":
    main()
