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
#include <array>
#include <cmath>

namespace dd::chowtape
{
inline constexpr double alpha = 1.6e-3;

struct State
{
    struct BiquadState
    {
        double x1 = 0.0;
        double x2 = 0.0;
        double y1 = 0.0;
        double y2 = 0.0;
    };

    double previousMagnetisation = 0.0;
    double previousField = 0.0;
    double previousFieldDerivative = 0.0;
    std::array<BiquadState, 2> dcBlocker {};
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
    double outputMakeup = 1.0;
};

inline Model makeModel (double drive,
                        double saturation,
                        double width) noexcept
{
    Model model;
    const auto boundedDrive = std::clamp (drive, 0.0, 1.0);
    const auto boundedSaturation = std::clamp (saturation, 0.0, 1.0);
    const auto boundedWidth = std::clamp (width, 0.0, 1.0);

    // Parameter mapping from CHOW Tape Model's NR4 hysteresis processor.
    model.saturation = 0.5 + 1.5 * (1.0 - boundedSaturation);
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
    model.outputMakeup =
        (1.0 + 0.6 * boundedWidth) / model.saturation;
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
    struct BiquadCoefficients
    {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
    };

    double fieldDerivativeScale = 1.75 * 44100.0;
    double trapezoidScale = (1.0 / 44100.0) / 1.9;
    std::array<BiquadCoefficients, 2> dcBlocker {};
};

inline IntegrationCoefficients makeIntegrationCoefficients (
    double sampleRate) noexcept
{
    const auto rate = std::max (1.0, sampleRate);
    IntegrationCoefficients result;
    result.fieldDerivativeScale = 1.75 * rate;
    result.trapezoidScale = (1.0 / rate) / 1.9;

    constexpr std::array<double, 2> butterworthQ {
        0.541196100146197,
        1.306562964876377
    };
    const auto c = 1.0 / std::tan (
        3.14159265358979323846 * 35.0 / rate);
    const auto phi = c * c;
    for (size_t stage = 0; stage < result.dcBlocker.size(); ++stage)
    {
        const auto k = c / butterworthQ[stage];
        const auto a0 = phi + k + 1.0;
        auto& coefficients = result.dcBlocker[stage];
        coefficients.b0 = phi / a0;
        coefficients.b1 = -2.0 * coefficients.b0;
        coefficients.b2 = coefficients.b0;
        coefficients.a1 = 2.0 * (1.0 - phi) / a0;
        coefficients.a2 = (phi - k + 1.0) / a0;
    }
    return result;
}

inline double processDcBlocker (
    double input,
    State::BiquadState& state,
    const IntegrationCoefficients::BiquadCoefficients& coefficients) noexcept
{
    const auto output = coefficients.b0 * input
        + coefficients.b1 * state.x1
        + coefficients.b2 * state.x2
        - coefficients.a1 * state.y1
        - coefficients.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = input;
    state.y2 = state.y1;
    state.y1 = output;
    return output;
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

inline float processSampleWithoutDc (
    float input,
    State& state,
    const Model& model,
    const detail::IntegrationCoefficients& integration) noexcept
{
    const auto field = std::clamp (
        static_cast<double> (input), -12.5, 12.5);
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
    const auto output = magnetisation * model.outputMakeup;
    return std::isfinite (output) ? static_cast<float> (output) : 0.0f;
}

inline float processDcBlocker (
    float input,
    State& state,
    const detail::IntegrationCoefficients& integration) noexcept
{
    auto output = static_cast<double> (input);
    for (size_t stage = 0; stage < state.dcBlocker.size(); ++stage)
        output = detail::processDcBlocker (
            output,
            state.dcBlocker[stage],
            integration.dcBlocker[stage]);
    return std::isfinite (output) ? static_cast<float> (output) : 0.0f;
}

inline float processSample (float input,
                            State& state,
                            const Model& model,
                            const detail::IntegrationCoefficients& integration) noexcept
{
    return processDcBlocker (
        processSampleWithoutDc (input, state, model, integration),
        state,
        integration);
}

inline float processSample (float input,
                            float drive,
                            float saturation,
                            float width,
                            double sampleRate,
                            State& state) noexcept
{
    return processSample (
        input,
        state,
        makeModel (drive, saturation, width),
        detail::makeIntegrationCoefficients (sampleRate));
}
} // namespace dd::chowtape
