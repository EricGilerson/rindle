//
// Created by Eric Gilerson on 10/6/25.
//

#ifndef RIVULET_ENGINE_HPP
#define RIVULET_ENGINE_HPP

#endif //RIVULET_ENGINE_HPP

#pragma once
#include "rivulet/types.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

namespace rivulet {

    class Engine {
    public:
        explicit Engine(EngineConfig cfg);

        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        Engine(Engine&&) noexcept = default;
        Engine& operator=(Engine&&) noexcept = default;

        bool load_feature_spec(std::string_view json_spec, std::string& error_msg);
        bool ingest(const Event& e, std::string& error_msg);

        std::optional<FeatureVector> feature_at(std::string_view key, Timestamp t, std::string& error_msg) const;

        void flush();

    private:
        struct KeyState;
        EngineConfig cfg_;
        std::unordered_map<std::string, KeyState> state_;
    };

} // namespace rivulet

