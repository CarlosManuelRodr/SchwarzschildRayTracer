/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include <iostream>

/**
 * @brief Write a textual progress bar to standard output.
 * @param progress Fraction completed, normally in [0,1].
 */
void progress_bar(float progress)
{
    const int barWidth = 35;
    std::cout << "[";
    int pos = (int)(barWidth * progress);

    for (int i = 0; i < barWidth; ++i)
    {
        if (i < pos)
            std::cout << "=";
        else if (i == pos)
            std::cout << ">";
        else
            std::cout << " ";
    }

    std::cout << "] " << int(progress * 100.0) << " %\r";
    std::cout.flush();

    if (progress == 1.0)
        std::cout << std::endl;
}

/**
 * @brief Write a textual progress bar to standard output.
 * @param val Current value in [min,max].
 * @param min Lower endpoint.
 * @param max Upper endpoint.
 * @param dt Step size; completion is reported when val + dt exceeds max.
 */
void progress_bar(float val, float min, float max, float dt)
{
    double progress;

    if (val + dt > max)
        progress = 1.0;
    else
        progress = (val - min) / (max - min);

    const int barWidth = 35;
    std::cout << "[";
    int pos = (int)(barWidth * progress);

    for (int i = 0; i < barWidth; ++i)
    {
        if (i < pos)
            std::cout << "=";
        else if (i == pos)
            std::cout << ">";
        else
            std::cout << " ";
    }

    std::cout << "] " << int(progress * 100.0) << " %\r";
    std::cout.flush();

    if (progress == 1.0)
        std::cout << std::endl;
}
