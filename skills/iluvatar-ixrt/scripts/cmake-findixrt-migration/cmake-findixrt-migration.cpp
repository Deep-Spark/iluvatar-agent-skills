#include <NvInfer.h>

#include <iostream>
#include <memory>

class Logger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char *msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            std::cerr << (msg ? msg : "") << '\n';
        }
    }
};

int main() {
    Logger logger;
    std::unique_ptr<nvinfer1::IRuntime> runtime{nvinfer1::createInferRuntime(logger)};
    if (!runtime) {
        std::cerr << "createInferRuntime failed\n";
        return 1;
    }
    std::cout << "CMake FindIxRT migration: TensorRT::TensorRT target runtime OK\n";
    return 0;
}
