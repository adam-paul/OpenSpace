/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2025                                                               *
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

#ifndef __OPENSPACE_CORE___SCENEINITIALIZER___H__
#define __OPENSPACE_CORE___SCENEINITIALIZER___H__

#include <openspace/util/threadpool.h>
#include <unordered_set>
#include <vector>

namespace openspace {

class SceneGraphNode;

class SceneInitializer {
public:
    virtual ~SceneInitializer() = default;
    virtual void initializeNode(SceneGraphNode* node) = 0;
    virtual std::vector<SceneGraphNode*> takeInitializedNodes() = 0;
    virtual bool isInitializing() const = 0;
};

class SingleThreadedSceneInitializer final : public SceneInitializer {
public:
    void initializeNode(SceneGraphNode* node) override;
    std::vector<SceneGraphNode*> takeInitializedNodes() override;
    bool isInitializing() const override;

private:
    std::vector<SceneGraphNode*> _initializedNodes;
};

class MultiThreadedSceneInitializer final : public SceneInitializer {
public:
    explicit MultiThreadedSceneInitializer(unsigned int nThreads);

    void initializeNode(SceneGraphNode* node) override;
    std::vector<SceneGraphNode*> takeInitializedNodes() override;
    bool isInitializing() const override;

private:
    std::vector<SceneGraphNode*> _initializedNodes;
    std::unordered_set<SceneGraphNode*> _initializingNodes;
    ThreadPool _threadPool;
    mutable std::mutex _mutex;
};

} // namespace openspace

#endif // __OPENSPACE_CORE___SCENEINITIALIZER___H__
