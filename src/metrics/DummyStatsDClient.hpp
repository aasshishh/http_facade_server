#pragma once

#include <memory>
#include <mutex>

#include "../interfaces/IStatsDClient.hpp"

class DummyStatsDClient : public IStatsDClient {
public:
    static std::shared_ptr<DummyStatsDClient> getInstance();
    ~DummyStatsDClient() override = default;

    void increment(const std::string& key, int value = 1) override;
    void decrement(const std::string& key, int value = 1) override;
    void gauge(const std::string& key, double value) override;
    void timing(const std::string& key, std::chrono::milliseconds value) override;
    void set(const std::string& key, const std::string& value) override;

private:
    DummyStatsDClient() = default;
    
    static std::shared_ptr<DummyStatsDClient> instance;
    static std::once_flag init_flag;

    DummyStatsDClient(const DummyStatsDClient&) = delete;
    DummyStatsDClient& operator=(const DummyStatsDClient&) = delete;
    DummyStatsDClient(DummyStatsDClient&&) = delete;
    DummyStatsDClient& operator=(DummyStatsDClient&&) = delete;
};