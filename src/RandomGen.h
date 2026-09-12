/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include <numeric>
#include <random>

// Random number generation.

/**
 * @enum RandomAlgorithm
 * @brief Random engine choices for the historical CPU renderer.
 */
enum class RandomAlgorithm
{
    LCG,
    MT19937,
    RANLUX24,
    RANLUX48
};

/**
 * @class RandomGen
 * @brief Process-wide random engine wrapper; shared mutable state is not thread-safe.
 */
class RandomGen
{
    static RandomAlgorithm m_ra;
    static std::mt19937 mt;
    static std::ranlux24 rl24;
    static std::ranlux48 rl48;

public:
    /** @brief Select the process-wide random engine; this does not reseed it. */
    static void SetAlgorithm(RandomAlgorithm ra);

    /** @brief Seed the selected engine; -1 uses the current system-clock count. */
    static void Seed(int seed = -1);

    /** @brief Return a random integer in [0,i), using modulo reduction; i must be positive. */
    static int GetInt(int i);

    /** @brief Return an engine sample scaled to [0,1], including the endpoints. */
    static float Getfloat();
};
