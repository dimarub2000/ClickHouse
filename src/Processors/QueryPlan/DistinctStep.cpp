#include <Processors/QueryPipeline.h>
#include <Processors/QueryPlan/DistinctStep.h>
#include <Processors/Transforms/DistinctSortedTransform.h>
#include <Processors/Transforms/DistinctTransform.h>

namespace DB
{

static bool checkColumnsAlreadyDistinct(const Names & columns, const NameSet & distinct_names)
{
    bool columns_already_distinct = true;
    for (const auto & name : columns)
        if (distinct_names.count(name) == 0)
            columns_already_distinct = false;

    return columns_already_distinct;
}

static ITransformingStep::DataStreamTraits getTraits(bool pre_distinct, bool already_distinct_columns)
{
    return ITransformingStep::DataStreamTraits
    {
            .preserves_distinct_columns = already_distinct_columns, /// Will be calculated separately otherwise
            .returns_single_stream = !pre_distinct && !already_distinct_columns,
            .preserves_number_of_streams = pre_distinct || already_distinct_columns,
    };
}


DistinctStep::DistinctStep(
    const DataStream & input_stream_,
    const SizeLimits & set_size_limits_,
    UInt64 limit_hint_,
    const Names & columns_,
    bool pre_distinct_,
    InputOrderInfoPtr distinct_info_)
    : ITransformingStep(
            input_stream_,
            input_stream_.header,
            getTraits(pre_distinct_, checkColumnsAlreadyDistinct(columns_, input_stream_.distinct_columns)))
    , set_size_limits(set_size_limits_)
    , limit_hint(limit_hint_)
    , columns(columns_)
    , pre_distinct(pre_distinct_)
    , distinct_info(distinct_info_)
{
    if (!output_stream->distinct_columns.empty() /// Columns already distinct, do nothing
        && (!pre_distinct /// Main distinct
            || input_stream_.has_single_port)) /// pre_distinct for single port works as usual one
    {
        /// Build distinct set.
        for (const auto & name : columns)
            output_stream->distinct_columns.insert(name);
    }
}

void DistinctStep::transformPipeline(QueryPipeline & pipeline)
{
    if (checkColumnsAlreadyDistinct(columns, input_streams.front().distinct_columns))
        return;

    if (!pre_distinct)
        pipeline.resize(1);

    if (pre_distinct && distinct_info)
    {
        pipeline.addSimpleTransform([&](const Block & header, QueryPipeline::StreamType stream_type) -> ProcessorPtr
        {
            if (stream_type != QueryPipeline::StreamType::Main)
                return nullptr;

            return std::make_shared<DistinctSortedTransform>(header, set_size_limits, limit_hint, distinct_info->order_key_prefix_descr, columns);
        });
    }
    else
    {
        pipeline.addSimpleTransform([&](const Block & header, QueryPipeline::StreamType stream_type) -> ProcessorPtr
        {
            if (stream_type != QueryPipeline::StreamType::Main)
                return nullptr;

            return std::make_shared<DistinctTransform>(header, set_size_limits, limit_hint, columns);
        });
    }
}

}
