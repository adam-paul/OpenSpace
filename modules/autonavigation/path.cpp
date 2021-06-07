/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/autonavigation/path.h>

#include <modules/autonavigation/autonavigationmodule.h>
#include <modules/autonavigation/helperfunctions.h>
#include <modules/autonavigation/pathcurve.h>
#include <modules/autonavigation/rotationinterpolator.h>
#include <modules/autonavigation/speedfunction.h>
#include <modules/autonavigation/curves/avoidcollisioncurve.h>
#include <modules/autonavigation/curves/zoomoutoverviewcurve.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/moduleengine.h>
#include <openspace/scene/scenegraphnode.h>
#include <ghoul/logging/logmanager.h>

namespace {
    constexpr const char* _loggerCat = "Path";
} // namespace

namespace openspace::autonavigation {

Path::Path(Waypoint start, Waypoint end, CurveType type,
           std::optional<double> duration)
    : _start(start), _end(end), _curveType(type)
{
    switch (_curveType) {
        case CurveType::AvoidCollision:
            _curve = std::make_unique<AvoidCollisionCurve>(_start, _end);
            _rotationInterpolator = std::make_unique<EasedSlerpInterpolator>(
                _start.rotation(),
                _end.rotation()
            );
            break;
        case CurveType::Linear:
            _curve = std::make_unique<LinearCurve>(_start, _end);
            _rotationInterpolator = std::make_unique<EasedSlerpInterpolator>(
                _start.rotation(),
                _end.rotation()
            );
            break;
        case CurveType::ZoomOutOverview:
            _curve = std::make_unique<ZoomOutOverviewCurve>(_start, _end);
            _rotationInterpolator = std::make_unique<LookAtInterpolator>(
                _start.rotation(),
                _end.rotation(),
                _start.node()->worldPosition(),
                _end.node()->worldPosition(),
                _curve.get()
            );
            break;
        default:
            LERROR("Could not create curve. Type does not exist!");
            throw ghoul::MissingCaseException();
            return;
    }

    _speedFunction = SpeedFunction(SpeedFunction::Type::DampenedQuintic);

    if (duration.has_value()) {
        _duration = duration.value();
    }
    else {
        _duration = std::log(pathLength());

        auto module = global::moduleEngine->module<AutoNavigationModule>();
        _duration /= module->AutoNavigationHandler().speedScale();
    }
}

Waypoint Path::startPoint() const { return _start; }

Waypoint Path::endPoint() const { return _end; }

double Path::duration() const { return _duration; }

double Path::pathLength() const { return _curve->length(); }

std::vector<glm::dvec3> Path::controlPoints() const {
    return _curve->points();
}

CameraPose Path::traversePath(double dt) {
    if (!_curve || !_rotationInterpolator) {
        // TODO: handle better (abort path somehow)
        return _start.pose;
    }

    AutoNavigationModule* module = global::moduleEngine->module<AutoNavigationModule>();
    AutoNavigationHandler& handler = module->AutoNavigationHandler();
    const int nSteps = handler.integrationResolutionPerFrame();

    double displacement = helpers::simpsonsRule(
        _progressedTime,
        _progressedTime + dt,
        nSteps,
        [this](double t) { return speedAtTime(t); }
    );

    _progressedTime += dt;
    _traveledDistance += displacement;

    return interpolatedPose(_traveledDistance);
}

std::string Path::currentAnchor() const {
    bool pastHalfway = (_traveledDistance / pathLength()) > 0.5;
    return (pastHalfway) ? _end.nodeDetails.identifier : _start.nodeDetails.identifier;
}

bool Path::hasReachedEnd() const {
    return (_traveledDistance / pathLength()) >= 1.0;
}

double Path::speedAtTime(double time) const {
    return _speedFunction.scaledValue(time, _duration, pathLength());
}

CameraPose Path::interpolatedPose(double distance) const {
    double u = distance / pathLength();
    CameraPose cs;
    cs.position = _curve->positionAt(u);
    cs.rotation = _rotationInterpolator->interpolate(u);
    return cs;
}

} // namespace openspace::autonavigation
