/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "RenderValuesCache.h"

#include "Engine/Bezier.h"
#include "Engine/Curve.h"
#include "Engine/EffectInstance.h"
#include "Engine/Node.h"
#include "Engine/NodeMetadata.h"

NATRON_NAMESPACE_ENTER;


struct DimTimeView
{
    double time;
    DimIdx dimension;
    ViewIdx view;
};


struct ValueDimTimeViewCompareLess
{

    bool operator() (const DimTimeView& lhs,
                     const DimTimeView& rhs) const
    {
        if (lhs.view < rhs.view) {
            return true;
        } else if (lhs.view > rhs.view) {
            return false;
        } else {
            if (lhs.dimension < rhs.dimension) {
                return true;
            } else if (lhs.dimension > rhs.dimension) {
                return false;
            } else {
                return lhs.time < rhs.time;
            }
        }
    }
};

typedef std::map<DimTimeView, bool, ValueDimTimeViewCompareLess> BoolValueDimTimeViewMap;
typedef std::map<DimTimeView, int, ValueDimTimeViewCompareLess > IntValueDimTimeViewMap;
typedef std::map<DimTimeView, double, ValueDimTimeViewCompareLess > DoubleValueDimTimeViewMap;
typedef std::map<DimTimeView, std::string, ValueDimTimeViewCompareLess > StringValueDimTimeViewMap;

typedef std::map<int, CurvePtr> PerDimensionParametricCurve;

template <typename T>
bool findCachedKnobValueInternal(const boost::shared_ptr<Knob<T> >& knob,
                         const typename std::map<boost::shared_ptr<Knob<T> >, std::map<DimTimeView, T, ValueDimTimeViewCompareLess> >& cache,
                         double time,
                         DimIdx dim,
                         ViewIdx view,
                         T* value)
{

    typename  std::map<boost::shared_ptr<Knob<T> >, std::map<DimTimeView, T, ValueDimTimeViewCompareLess> >::const_iterator foundKnob = cache.find(knob);
    if (foundKnob == cache.end()) {
        return false;
    }
    DimTimeView key = {time, dim, view};
    typename std::map<DimTimeView, T, ValueDimTimeViewCompareLess>::const_iterator foundValue = foundKnob->second.find(key);
    if (foundValue == foundKnob->second.end()) {
        return false;
    }
    *value = foundValue->second;
    return true;
}

template <typename T>
void setCachedKnobValueInternal(const boost::shared_ptr<Knob<T> >& knob,
                        typename std::map<boost::shared_ptr<Knob<T> >, std::map<DimTimeView, T, ValueDimTimeViewCompareLess> >& cache,
                        double time,
                        DimIdx dim,
                        ViewIdx view,
                        const T& value)
{
    std::map<DimTimeView, T, ValueDimTimeViewCompareLess>& values = cache[knob];
    DimTimeView key = {time, dim, view};
    values[key] = value;
}

struct RenderValuesCachePrivate
{

    // Knob getValue/getValueAtTime calls are cached against dimension/time/view
    std::map<KnobBoolBasePtr, BoolValueDimTimeViewMap> boolKnobValues;
    std::map<KnobIntBasePtr, IntValueDimTimeViewMap> intKnobValues;
    std::map<KnobDoubleBasePtr, DoubleValueDimTimeViewMap> doubleKnobValues;
    std::map<KnobStringBasePtr, StringValueDimTimeViewMap> stringKnobValues;
    std::map<KnobParametricPtr, PerDimensionParametricCurve> parametricKnobCurves;

    // Input nodes at the time of render
    std::vector<NodePtr> inputs;

    // Node metadatas at the time of render
    boost::shared_ptr<NodeMetadata> metadatas;

    // All roto shapes. They are plain copies of originals.
    // RotoPaint strokes don't need to be copied as they only change while drawing
    // and drawing is handled separatly (see getMostRecentStrokeChangesSinceAge).
    std::map<BezierPtr, BezierPtr> bezierShapes;


    RenderValuesCachePrivate()
    : boolKnobValues()
    , intKnobValues()
    , doubleKnobValues()
    , stringKnobValues()
    , parametricKnobCurves()
    {
        
    }


};

RenderValuesCache::RenderValuesCache()
: _imp(new RenderValuesCachePrivate())
{

}

RenderValuesCache::~RenderValuesCache()
{

}

void
RenderValuesCache::setCachedNodeMetadatas(const NodeMetadata& data)
{
    _imp->metadatas.reset(new NodeMetadata(data));
}

boost::shared_ptr<NodeMetadata>
RenderValuesCache::getCachedMetadatas() const
{
    return _imp->metadatas;
}

NodePtr
RenderValuesCache::getCachedInput(int inputNb) const
{
    if (inputNb < 0 || inputNb >= (int)_imp->inputs.size()) {
        return NodePtr();
    }
    return _imp->inputs[inputNb];
}

void
RenderValuesCache::setCachedInput(int inputNb, const NodePtr& node)
{
    if (inputNb >= (int)_imp->inputs.size()) {
        _imp->inputs.resize(inputNb + 1);
    }
    _imp->inputs[inputNb] = node;
}

template <>
bool
RenderValuesCache::getCachedKnobValue(const boost::shared_ptr<Knob<bool> >& knob, double time, DimIdx dimension, ViewIdx view, bool* value) const
{
    return findCachedKnobValueInternal<bool>(knob, _imp->boolKnobValues, time, dimension, view, value);
}

template <>
bool
RenderValuesCache::getCachedKnobValue(const boost::shared_ptr<Knob<int> >& knob, double time, DimIdx dimension, ViewIdx view, int* value) const
{
    return findCachedKnobValueInternal<int>(knob, _imp->intKnobValues, time, dimension, view, value);
}


template <>
bool
RenderValuesCache::getCachedKnobValue(const boost::shared_ptr<Knob<double> >& knob, double time, DimIdx dimension, ViewIdx view, double* value) const
{
    return findCachedKnobValueInternal<double>(knob, _imp->doubleKnobValues, time, dimension, view, value);
}


template <>
bool
RenderValuesCache::getCachedKnobValue(const boost::shared_ptr<Knob<std::string> >& knob, double time, DimIdx dimension, ViewIdx view, std::string* value) const
{
    return findCachedKnobValueInternal<std::string>(knob, _imp->stringKnobValues, time, dimension, view, value);
}

template <>
void
RenderValuesCache::setCachedKnobValue(const boost::shared_ptr<Knob<bool> >& knob, double time, DimIdx dimension, ViewIdx view, const bool& value)
{
    setCachedKnobValueInternal<bool>(knob, _imp->boolKnobValues, time, dimension, view, value);
}

template <>
void
RenderValuesCache::setCachedKnobValue(const boost::shared_ptr<Knob<int> >& knob, double time, DimIdx dimension, ViewIdx view, const int& value)
{
    setCachedKnobValueInternal<int>(knob, _imp->intKnobValues, time, dimension, view, value);
}


template <>
void
RenderValuesCache::setCachedKnobValue(const boost::shared_ptr<Knob<double> >& knob, double time, DimIdx dimension, ViewIdx view, const double& value)
{
    setCachedKnobValueInternal<double>(knob, _imp->doubleKnobValues, time, dimension, view, value);
}


template <>
void
RenderValuesCache::setCachedKnobValue(const boost::shared_ptr<Knob<std::string> >& knob, double time, DimIdx dimension, ViewIdx view, const std::string& value)
{
    setCachedKnobValueInternal<std::string>(knob, _imp->stringKnobValues, time, dimension, view, value);
}




CurvePtr
RenderValuesCache::getCachedParametricKnobCurve(const KnobParametricPtr& knob, DimIdx dimension) const
{
    std::map<KnobParametricPtr, PerDimensionParametricCurve>::const_iterator foundKnob = _imp->parametricKnobCurves.find(knob);
    if (foundKnob == _imp->parametricKnobCurves.end()) {
        return CurvePtr();
    }
    PerDimensionParametricCurve::const_iterator foundCurve = foundKnob->second.find(dimension);
    if (foundCurve == foundKnob->second.end()) {
        return CurvePtr();
    }
    return foundCurve->second;
}

void
RenderValuesCache::setCachedParametricKnobCurve(const KnobParametricPtr& knob, DimIdx dimension, const CurvePtr& curve) const
{
    CurvePtr copy(new Curve);
    copy->clone(*curve);
    _imp->parametricKnobCurves[knob][dimension] = copy;
}

BezierPtr
RenderValuesCache::getCachedBezier(const BezierPtr& bezier) const
{
    std::map<BezierPtr, BezierPtr>::const_iterator foundBezier = _imp->bezierShapes.find(bezier);
    if (foundBezier == _imp->bezierShapes.end()) {
        return BezierPtr();
    }
    return foundBezier->second;
}

void
RenderValuesCache::setCachedBezier(const BezierPtr& bezier)
{
    BezierPtr copy(new Bezier(*bezier));
    _imp->bezierShapes[bezier] = copy;

}

NATRON_NAMESPACE_EXIT;
