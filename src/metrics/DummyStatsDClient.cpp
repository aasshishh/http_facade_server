#include <iostream>

#include "DummyStatsDClient.hpp"

// Define static members
std::shared_ptr<DummyStatsDClient> DummyStatsDClient::instance = nullptr;
std::once_flag DummyStatsDClient::init_flag;

std::shared_ptr<DummyStatsDClient> DummyStatsDClient::getInstance() {
    std::call_once(init_flag, []() {
        instance = std::shared_ptr<DummyStatsDClient>(new DummyStatsDClient());
    });
    return instance;
}

// Implement the virtual methods
void DummyStatsDClient::increment(const std::string& /* key */, int /* value */) {
    // No-op implementation
}

void DummyStatsDClient::decrement(const std::string& /* key */, int /* value */) {
    // No-op implementation
}

void DummyStatsDClient::gauge(const std::string& /* key */, double /* value */) {
    // No-op implementation
}

void DummyStatsDClient::timing(const std::string& /* key */, std::chrono::milliseconds /* value */) {
    // No-op implementation
}

void DummyStatsDClient::set(const std::string& /* key */, const std::string& /* value */) {
    // No-op implementation
}