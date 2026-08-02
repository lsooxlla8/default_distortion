/*
    SPDX-License-Identifier: GPL-3.0-only

    Scalar adaptation of the Jiles-Atherton hysteresis processor from:
    - CHOW Tape Model, Copyright (C) Jatin Chowdhury and contributors
    - BYOD, Copyright (C) Chowdhury DSP and contributors

    Original sources:
    https://github.com/jatinchowdhury18/AnalogTapeModel
    https://github.com/Chowdhury-DSP/BYOD

    This adaptation removes the original SIMD and parameter-smoothing
    dependencies so the model can live inside one independently stateful
    default_distortion stage. The numerical model and four-iteration
    Newton-Raphson integration follow the GPLv3 source above.
*/

#pragma once

#include <algorithm>
#include <cmath>

namespace dd::chowtape
{
inline constexpr double alpha = 1.6e-3;

struct State
{
    double previousMagnetisation = 0.0;
    double previousField = 0.0;
    double previousFieldDerivative = 0.0;
};

struct Model
{
    double saturation = 1.25;
    double domainScale = saturation / 3.01;
    double reversibility = 0.70;
    double pinning = 0.47875;
    double irreversible = 1.0 - reversibility;
    double saturationOverScale = saturation / domainScale;
    double inverseDomainScale = 1.0 / domainScale;
    double saturationOverScaleAlpha = alpha * saturationOverScale;
    double reversibleSlope = reversibility * saturationOverScale;
    double reversibleSlopeAlpha = alpha * reversibleSlope;
    double reversibleCurvatureAlpha =
        reversibleSlopeAlpha / domainScale;
    double reversibleCurvatureAlphaSquared =
        alpha * reversibleCurvatureAlpha;
};

inline Model makeModel (double drive, double width) noexcept
{
    Model model;
    const auto boundedDrive = std::clamp (drive, 0.0, 1.0);
    const auto boundedWidth = std::clamp (width, 0.0, 1.0);

    // Parameter mapping from BYOD's tape hysteresis processor.
    constexpr double tapeSaturation = 0.5;
    model.saturation = 0.5 + 1.5 * (1.0 - tapeSaturation);
    model.domainScale =
        model.saturation / (0.01 + 6.0 * boundedDrive);
    model.reversibility = std::sqrt (1.0 - boundedWidth) - 0.01;
    model.pinning = 0.47875;
    model.irreversible = 1.0 - model.reversibility;
    model.saturationOverScale =
        model.saturation / model.domainScale;
    model.inverseDomainScale = 1.0 / model.domainScale;
    model.saturationOverScaleAlpha =
        alpha * model.saturationOverScale;
    model.reversibleSlope =
        model.reversibility * model.saturationOverScale;
    model.reversibleSlopeAlpha =
        alpha * model.reversibleSlope;
    model.reversibleCurvatureAlpha =
        model.reversibleSlopeAlpha / model.domainScale;
    model.reversibleCurvatureAlphaSquared =
        alpha * model.reversibleCurvatureAlpha;
    return model;
}

namespace detail
{
struct Evaluation
{
    double derivative = 0.0;
    double derivativeByMagnetisation = 0.0;
};

struct IntegrationCoefficients
{
    double fieldDerivativeScale = 1.75 * 44100.0;
    double trapezoidScale = (1.0 / 44100.0) / 1.9;
};

inline IntegrationCoefficients makeIntegrationCoefficients (
    double sampleRate) noexcept
{
    const auto rate = std::max (1.0, sampleRate);
    return { 1.75 * rate, (1.0 / rate) / 1.9 };
}

inline int sign (double value) noexcept
{
    return static_cast<int> (value > 0.0)
        - static_cast<int> (value < 0.0);
}

template <bool calculateJacobian>
inline Evaluation evaluate (double magnetisation,
                            double field,
                            double fieldDerivative,
                            const Model& model) noexcept
{
    constexpr double oneThird = 1.0 / 3.0;
    constexpr double negativeTwoFifteenths = -2.0 / 15.0;

    const auto q = (field + magnetisation * alpha)
        * model.inverseDomainScale;
    const auto nearZero = std::abs (q) < 1.0e-3;

    double langevin = q * oneThird;
    double langevinPrime = oneThird;
    double langevinSecond = negativeTwoFifteenths * q;
    if (! nearZero)
    {
        const auto inverseQ = 1.0 / q;
        const auto coth = 1.0 / std::tanh (q);
        langevin = coth - inverseQ;
        langevinPrime =
            inverseQ * inverseQ - coth * coth + 1.0;
        langevinSecond =
            2.0 * coth * (coth * coth - 1.0)
            - 2.0 * inverseQ * inverseQ * inverseQ;
    }

    const auto difference =
        langevin * model.saturation - magnetisation;
    const auto direction = fieldDerivative >= 0.0 ? 1.0 : -1.0;
    const auto followsDirection =
        sign (direction) == sign (difference);
    const auto irreversibleSlope = followsDirection
        ? model.irreversible
        : 0.0;

    auto denominator =
        model.irreversible * direction * model.pinning
        - alpha * difference;
    if (std::abs (denominator) < 1.0e-12)
        denominator = std::copysign (1.0e-12, denominator);

    const auto inverseDenominator = 1.0 / denominator;
    const auto f1 = irreversibleSlope * difference * inverseDenominator;
    const auto f2 = langevinPrime * model.reversibleSlope;
    const auto f3 =
        1.0 - langevinPrime * model.reversibleSlopeAlpha;
    const auto safeF3 = std::abs (f3) < 1.0e-12
        ? std::copysign (1.0e-12, f3)
        : f3;
    const auto inverseF3 = 1.0 / safeF3;
    const auto derivative =
        fieldDerivative * (f1 + f2) * inverseF3;

    if constexpr (! calculateJacobian)
        return { derivative, 0.0 };

    const auto differencePrime =
        langevinPrime * model.saturationOverScaleAlpha - 1.0;
    const auto f1Prime = irreversibleSlope
        * (differencePrime * inverseDenominator
           + difference * alpha * differencePrime
               * inverseDenominator * inverseDenominator);
    const auto f2Prime =
        langevinSecond * model.reversibleCurvatureAlpha;
    const auto f3Prime =
        -langevinSecond * model.reversibleCurvatureAlphaSquared;

    return {
        derivative,
        fieldDerivative * (f1Prime + f2Prime) * inverseF3
            - derivative * f3Prime * inverseF3
    };
}
} // namespace detail

inline float processSample (float input,
                            State& state,
                            const Model& model,
                            const detail::IntegrationCoefficients& integration) noexcept
{
    const auto field = 2.0 * static_cast<double> (input);
    const auto fieldDerivative =
        integration.fieldDerivativeScale * (field - state.previousField)
        - 0.75 * state.previousFieldDerivative;
    const auto previousEvaluation = detail::evaluate<false> (
        state.previousMagnetisation,
        state.previousField,
        state.previousFieldDerivative,
        model);

    auto magnetisation = state.previousMagnetisation;
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        const auto evaluation = detail::evaluate<true> (
            magnetisation, field, fieldDerivative, model);
        auto denominator =
            1.0 - integration.trapezoidScale
                * evaluation.derivativeByMagnetisation;
        if (std::abs (denominator) < 1.0e-12)
            denominator = std::copysign (1.0e-12, denominator);
        const auto correction =
            (magnetisation - state.previousMagnetisation
             - integration.trapezoidScale
                 * (evaluation.derivative
                    + previousEvaluation.derivative))
            / denominator;
        magnetisation -= correction;
    }

    if (! std::isfinite (magnetisation)
        || std::abs (magnetisation) > 20.0)
    {
        magnetisation = 0.0;
        state.previousFieldDerivative = 0.0;
    }
    else
    {
        state.previousFieldDerivative = fieldDerivative;
    }

    state.previousMagnetisation = magnetisation;
    state.previousField = field;
    return static_cast<float> (magnetisation);
}

inline float processSample (float input,
                            float drive,
                            float width,
                            double sampleRate,
                            State& state) noexcept
{
    return processSample (
        input,
        state,
        makeModel (drive, width),
        detail::makeIntegrationCoefficients (sampleRate));
}
} // namespace dd::chowtape
