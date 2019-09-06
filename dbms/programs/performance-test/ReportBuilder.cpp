#include "ReportBuilder.h"

#include <algorithm>
#include <regex>
#include <sstream>
#include <thread>

#include <Common/getNumberOfPhysicalCPUCores.h>
#include <Common/getFQDNOrHostName.h>
#include <common/getMemoryAmount.h>
#include <Common/StringUtils/StringUtils.h>


namespace DB
{

namespace
{

const std::regex QUOTE_REGEX{"\""};

std::string getMainMetric(const PerformanceTestInfo & test_info)
{
    std::string main_metric;
    if (test_info.main_metric.empty())
        if (test_info.exec_type == ExecutionType::Loop)
            main_metric = "min_time";
        else
            main_metric = "rows_per_second";
    else
        main_metric = test_info.main_metric;
    return main_metric;
}

bool isASCIIString(const std::string & str)
{
    return std::all_of(str.begin(), str.end(), isASCII);
}

}

std::string ReportBuilder::getCurrentTime() const
{
    return DateLUT::instance().timeToString(time(nullptr));
}

std::string ReportBuilder::buildFullReport(
    const PerformanceTestInfo & test_info,
    std::vector<TestStats> & stats,
    const std::vector<std::size_t> & queries_to_run,
    const Connections & connections,
    const ConnectionTimeouts & timeouts) const
{
    JSONString json_output;

    json_output.set("time", getCurrentTime());
    json_output.set("test_name", test_info.test_name);
    json_output.set("path", test_info.path);
    json_output.set("main_metric", getMainMetric(test_info));

    if (!test_info.substitutions.empty())
    {
        JSONString json_parameters(2); /// here, 2 is the size of \t padding

        for (auto & [parameter, values] : test_info.substitutions)
        {
            std::ostringstream array_string;
            array_string << "[";
            for (size_t i = 0; i != values.size(); ++i)
            {
                array_string << '"' << std::regex_replace(values[i], QUOTE_REGEX, "\\\"") << '"';
                if (i != values.size() - 1)
                {
                    array_string << ", ";
                }
            }
            array_string << ']';

            json_parameters.set(parameter, array_string.str());
        }

        json_output.set("parameters", json_parameters.asString());
    }

    buildRunsReport(test_info, stats, queries_to_run, connections, timeouts, json_output);

    return json_output.asString();
}

void ReportBuilder::buildRunsReport(
        const PerformanceTestInfo & test_info,
        std::vector<TestStats> & stats,
        const std::vector<std::size_t> & queries_to_run,
        const Connections & connections,
        const ConnectionTimeouts & timeouts,
        JSONString & json_output) const
{
    std::vector<std::vector<JSONString>> run_infos;

    for (size_t query_index = 0; query_index < test_info.queries.size(); ++query_index)
    {
        if (!queries_to_run.empty() && std::find(queries_to_run.begin(), queries_to_run.end(), query_index) == queries_to_run.end())
            continue;

        for (size_t run_index = 0; run_index < test_info.times_to_run; ++run_index)
        {

            std::vector<JSONString> run_info;

            for (size_t connection_index = 0; connection_index < connections.size(); ++connection_index)
            {
                size_t stat_index = run_index * test_info.queries.size() + query_index;
                ConnectionTestStats & statistics = stats[stat_index][connection_index];

                if (!statistics.ready)
                    continue;

                JSONString connection_runJSON(2);
                connection_runJSON.set("query", std::regex_replace(test_info.queries[query_index], QUOTE_REGEX, "\\\""));
                connection_runJSON.set("query_index", query_index);
                connection_runJSON.set("connection", connections[connection_index]->getDescription());
                connection_runJSON.set("server_version", connections[connection_index]->getServerVersion(timeouts));

                if (!statistics.exception.empty())
                {
                    if (isASCIIString(statistics.exception))
                        connection_runJSON.set("exception", std::regex_replace(statistics.exception, QUOTE_REGEX, "\\\""));
                    else
                        connection_runJSON.set("exception", "Some exception occurred with non ASCII message. This may produce invalid JSON. Try reproduce locally.");
                }

                if (test_info.exec_type == ExecutionType::Loop)
                {
                    /// in seconds
                    connection_runJSON.set("min_time", statistics.min_time / 1000.0);

                    if (statistics.sampler.size() != 0)
                    {
                        JSONString quantiles(5); /// here, 5 is the size of \t padding
                        for (int percent = 10; percent <= 90; percent += 10)
                        {
                            std::string quantile_key = std::to_string(percent / 100.0).substr(0, 3);
                            quantiles.set(quantile_key, statistics.sampler.quantileInterpolated(percent / 100.0));
                        }
                        quantiles.set("0.95", statistics.sampler.quantileInterpolated(95 / 100.0));
                        quantiles.set("0.99", statistics.sampler.quantileInterpolated(99 / 100.0));
                        quantiles.set("0.999", statistics.sampler.quantileInterpolated(99.9 / 100.0));
                        quantiles.set("0.9999", statistics.sampler.quantileInterpolated(99.99 / 100.0));

                        connection_runJSON.set("quantiles", quantiles.asString());
                    }

                    connection_runJSON.set("total_time", statistics.total_time);

                    if (statistics.total_time != 0)
                    {
                        connection_runJSON.set("queries_number", statistics.queries);
                        connection_runJSON.set("queries_per_second", static_cast<double>(statistics.queries) / statistics.total_time);
                        connection_runJSON.set("rows_per_second", static_cast<double>(statistics.total_rows_read) / statistics.total_time);
                        connection_runJSON.set("bytes_per_second", static_cast<double>(statistics.total_bytes_read) / statistics.total_time);
                    }
                }
                else
                {
                    connection_runJSON.set("max_rows_per_second", statistics.max_rows_speed);
                    connection_runJSON.set("max_bytes_per_second", statistics.max_bytes_speed);
                    connection_runJSON.set("avg_rows_per_second", statistics.avg_rows_speed_value);
                    connection_runJSON.set("avg_bytes_per_second", statistics.avg_bytes_speed_value);
                }

                connection_runJSON.set("memory_usage", statistics.memory_usage);

                run_info.push_back(connection_runJSON);
            }
            run_infos.push_back(run_info);
        }
    }
    json_output.set("runs", run_infos);
}

std::string ReportBuilder::buildCompactReport(
    const PerformanceTestInfo & test_info,
    std::vector<TestStats> & stats,
    const std::vector<std::size_t> & queries_to_run,
    const Connections & connections,
    const ConnectionTimeouts & /*timeouts*/) const
{
    std::ostringstream output;
    for (size_t connection_index = 0; connection_index < connections.size(); ++connection_index)
    {
        output << "connection \"" << connections[connection_index]->getDescription() << "\"\n";

        for (size_t query_index = 0; query_index < test_info.queries.size(); ++query_index)
        {
            if (!queries_to_run.empty() && std::find(queries_to_run.begin(), queries_to_run.end(), query_index) == queries_to_run.end())
                continue;

            for (size_t run_index = 0; run_index < test_info.times_to_run; ++run_index)
            {
                if (test_info.queries.size() > 1)
                    output << "query \"" << test_info.queries[query_index] << "\", ";

                output << "run " << std::to_string(run_index + 1) << ": ";

                std::string main_metric = getMainMetric(test_info);

                output << main_metric << " = ";
                size_t index = run_index * test_info.queries.size() + query_index;
                output << stats[index][connection_index].getStatisticByName(main_metric);
                output << "\n";
            }
        }
    }
    return output.str();
}

}
