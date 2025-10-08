//
// Created by Eric Gilerson on 10/7/25.
//

#pragma once
#include "rivulet/scaler.hpp"

namespace rivulet {
    // StandardScaler: (x - mean) / std
    class StandardScaler : public Scaler {
    public:
        void fit(const std::vector<double>& col) override;
        void transform(std::vector<double>& col) const override;
        ScalerParams params() const override;
    private:
        ColumnStats stats_;
    };

    // ZeroStandardScaler: (x - median) / iqr
    class ZeroStandardScaler : public Scaler {
    public:
        void fit(const std::vector<double>& col) override;
        void transform(std::vector<double>& col) const override;
        ScalerParams params() const override;
    private:
        ColumnStats stats_;
    };
}