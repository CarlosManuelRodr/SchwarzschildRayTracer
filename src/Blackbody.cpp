#include "SceneData.h"
#include <algorithm>
#include <cmath>

namespace rt
{
namespace
{
double gaussian(double wavelength, double center, double left, double right)
{
    double t = (wavelength - center) * (wavelength < center ? left : right);
    return std::exp(-0.5 * t * t);
}

Vec3 spectrumToXyz(double temperature)
{
    Vec3 xyz;

    // Planck radiance integrated against Wyman/Sloan/Shirley (2013), Eq. 4.
    // The common physical prefactor cancels against the 5778 K normalization.
    for (double wavelength = 380; wavelength <= 780; wavelength += 5)
    {
        double radiance =
            std::pow(550.0 / wavelength, 5) / std::expm1(1.438776877e7 / (wavelength * temperature));
        double x = 1.056 * gaussian(wavelength, 599.8, 0.0264, 0.0323) +
                   0.362 * gaussian(wavelength, 442.0, 0.0624, 0.0374) -
                   0.065 * gaussian(wavelength, 501.1, 0.0490, 0.0382);
        double y = 0.821 * gaussian(wavelength, 568.8, 0.0213, 0.0247) +
                   0.286 * gaussian(wavelength, 530.9, 0.0613, 0.0322);
        double z = 1.217 * gaussian(wavelength, 437.0, 0.0845, 0.0278) +
                   0.681 * gaussian(wavelength, 459.0, 0.0385, 0.0725);

        xyz += radiance * Vec3(x, y, z);
    }

    return xyz;
}
} // namespace

const std::vector<Float4>& blackbodyTable()
{
    static const std::vector<Float4> table = []
    {
        std::vector<Float4> values;
        double normalization = spectrumToXyz(5778).y;

        // Includes intensity, not just normalized chromaticity. Evaluating at
        // T_observed = g*T_emitted accounts for both spectral shift and beaming.
        for (int i = 0; i <= 2048; ++i)
        {
            double temperature = 500.0 * std::exp(i * std::log(100000.0) / 2048.0);
            auto xyz = spectrumToXyz(temperature) / normalization;
            values.push_back({float(std::max(0.0, 3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z)),
                              float(std::max(0.0, -0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z)),
                              float(std::max(0.0, 0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z)),
                              0});
        }

        return values;
    }();

    return table;
}
} // namespace rt
