/******************************************************************************
* Copyright (c) 2016, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#include <entwine/tree/sleepy-tree.hpp>

#include <limits>
#include <cmath>
#include <memory>
#include <string>
#include <thread>

#include <pdal/Compression.hpp>
#include <pdal/Dimension.hpp>
#include <pdal/PointBuffer.hpp>
#include <pdal/Utils.hpp>

#include <entwine/compression/stream.hpp>
#include <entwine/compression/util.hpp>
#include <entwine/http/collector.hpp>
#include <entwine/tree/roller.hpp>
#include <entwine/tree/registry.hpp>
#include <entwine/types/bbox.hpp>
#include <entwine/types/schema.hpp>

namespace entwine
{

SleepyTree::SleepyTree(
        const std::string& dir,
        const BBox& bbox,
        const Schema& schema,
        const std::size_t baseDepth,
        const std::size_t flatDepth,
        const std::size_t diskDepth)
    : m_dir(dir)
    , m_bbox(new BBox(bbox))
    , m_schema(new Schema(schema))
    , m_numPoints(0)
    , m_registry(
            new Registry(
                *m_schema.get(),
                baseDepth,
                flatDepth,
                diskDepth))
{ }

SleepyTree::SleepyTree(const std::string& dir)
    : m_dir(dir)
    , m_bbox()
    , m_schema()
    , m_numPoints(0)
    , m_registry()
{
    load();
}

SleepyTree::~SleepyTree()
{ }

void SleepyTree::insert(const pdal::PointBuffer* pointBuffer, Origin origin)
{
    Point point;

    for (std::size_t i = 0; i < pointBuffer->size(); ++i)
    {
        point.x = pointBuffer->getFieldAs<double>(pdal::Dimension::Id::X, i);
        point.y = pointBuffer->getFieldAs<double>(pdal::Dimension::Id::Y, i);

        if (m_bbox->contains(point))
        {
            Roller roller(*m_bbox.get());

            PointInfo* pointInfo(
                    new PointInfo(
                        m_schema->pointContext(),
                        pointBuffer,
                        i,
                        origin));

            m_registry->put(&pointInfo, roller);
            ++m_numPoints;
        }
    }
}

void SleepyTree::save()
{
    Json::Value jsonMeta;
    addMeta(jsonMeta);
    m_registry->save(m_dir, jsonMeta["registry"]);

    std::ofstream metaStream(
            metaPath(),
            std::ofstream::out | std::ofstream::trunc);
    if (!metaStream.good())
    {
        throw std::runtime_error("Could not open " + metaPath());
    }

    const std::string metaString(jsonMeta.toStyledString());
    metaStream.write(metaString.data(), metaString.size());
}

void SleepyTree::load()
{
    Json::Value meta;

    {
        Json::Reader reader;
        std::ifstream metaStream(metaPath());
        if (!metaStream.good())
        {
            throw std::runtime_error("Could not open " + metaPath());
        }

        reader.parse(metaStream, meta, false);
    }

    m_bbox.reset(new BBox(BBox::fromJson(meta["bbox"])));
    m_schema.reset(new Schema(Schema::fromJson(meta["schema"])));

    const Json::Value& treeMeta(meta["tree"]);
    m_registry.reset(
            new Registry(
                *m_schema.get(),
                treeMeta["baseDepth"].asUInt64(),
                treeMeta["flatDepth"].asUInt64(),
                treeMeta["diskDepth"].asUInt64()));

    m_registry->load(m_dir, meta["registry"]);
}

const BBox& SleepyTree::getBounds() const
{
    return *m_bbox.get();
}

MultiResults SleepyTree::getPoints(
        const std::size_t depthBegin,
        const std::size_t depthEnd)
{
    Roller roller(*m_bbox.get());
    MultiResults results;
    m_registry->getPoints(
            roller,
            results,
            depthBegin,
            depthEnd);

    return results;
}

MultiResults SleepyTree::getPoints(
        const BBox& bbox,
        const std::size_t depthBegin,
        const std::size_t depthEnd)
{
    Roller roller(*m_bbox.get());
    MultiResults results;
    m_registry->getPoints(
            roller,
            results,
            bbox,
            depthBegin,
            depthEnd);

    return results;
}

pdal::PointContext SleepyTree::pointContext() const
{
    return m_schema->pointContext();
}

std::size_t SleepyTree::numPoints() const
{
    return m_numPoints;
}

const std::string& SleepyTree::dir() const
{
    return m_dir;
}

void SleepyTree::addMeta(Json::Value& meta) const
{
    meta["bbox"] = m_bbox->toJson();
    meta["schema"] = m_schema->toJson();
}

std::string SleepyTree::metaPath() const
{
    return m_dir + "/meta";
}

} // namespace entwine
