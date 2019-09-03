#pragma once

#include <array>
#include <string>
#include <map>

/**
 * About:
 * This is implementation of Independent two-sample t-test
 * Read about it on https://en.wikipedia.org/wiki/Student%27s_t-test (Equal or unequal sample sizes, equal variance)
 *
 * Usage:
 * It's it used to assume with some level of confidence that two distributions don't differ.
 * Values can be added with t_test.add(0/1, value) and after compared and reported.
 */
class StudentTTest
{
private:
    struct DistributionData
    {
        size_t size = 0;
        double sum = 0;
        double squares_sum = 0;

        void add(double value)
        {
            ++size;
            sum += value;
            squares_sum += value * value;
        }

        double avg() const
        {
            return sum / size;
        }

        double var() const
        {
            return (squares_sum - (sum * sum / size)) / static_cast<double>(size - 1);
        }

        void clear()
        {
            size = 0;
            sum = 0;
            squares_sum = 0;
        }
    };

    double mean_difference = 0;
    double mean_confidence_interval = 0;

    std::array<DistributionData, 2> data {};

    void updateResults(size_t confidence_level_index = 5);

public:
    void clear();

    void add(size_t distribution, double value);

    bool empty() const;

    bool distributionsDiffer(size_t confidence_level_index = 5);

    std::string reportResults(size_t confidence_level_index = 5);
};
