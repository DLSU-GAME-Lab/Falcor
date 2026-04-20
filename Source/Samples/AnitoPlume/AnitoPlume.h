/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "Core/Plugin.h"
#include "Core/SampleApp.h"
#include "Core/AssetResolver.h"
#include "Core/Pass/RasterPass.h"
#include "RenderGraph/RenderGraph.h"
#include "ParticleSystem.h"
#include "WindStructure.h"
#include "TerrainStructure.h"
#include "SmokeLayer.h"
using namespace Falcor;

class AnitoPlume : public SampleApp
{
public:
    enum class RenderMode
    {
        Raster, RayTrace, Graph
    };

    enum class SimulatorState
    {
        Stopped, Playing, Paused
    };

    enum class EruptionVent
    {
        TaalMainCrater, BinintiangMalaki, BinintiangMunti, Pirapiraso, CaluitPoint
    };

    enum class TerrainByYear
    {
        Year2023, Year2021, Year2019, Year2015
    };

public:
    AnitoPlume(const SampleAppConfig& config);
    ~AnitoPlume();

    void onLoad(RenderContext* pRenderContext) override;
    void onShutdown() override;
    void onResize(uint32_t width, uint32_t height) override;
    void onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo) override;
    void onGuiRender(Gui* pGui) override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;
    void onHotReload(HotReloadFlags reloaded) override;

private:
    void loadScene(const std::filesystem::path& path, const Fbo* pTargetFbo);
    void setPerFrameVars(const Fbo* pTargetFbo);
    void renderRaster(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo);
    void renderRT(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo);
    void renderGraph(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo, IScene::UpdateFlags updates);

private:
    ref<Texture> createGUITexture(const std::filesystem::path& path);

    // TODO: Add to separate classes to better organize the code and separate responsibilities.
    void renderMainMenuBar(Gui* pGui);
    void renderSimulatorInput(Gui* pGui);
    void renderPlayback(Gui* pGui);
    void renderCameraSettings(Gui* pGui);
    void renderDisplaySettings(Gui* pGui);
    void renderPlumeDirectionTracker(Gui* pGui);
    void renderProfiler(Gui* pGui);

private:
    static float windIntensityGraphCallback(void*, int32_t index);
    static float windAngleGraphCallback(void*, int32_t index);

private:
    // Simulator Input : Wind Settings
    double mAltitude = 0.0;
    double mLinearWindSpeed = 0.0;
    bool mUseAllAngles = false;

    // Simulator Input : Eruption Parameters
    EruptionVent mEruptionVent = EruptionVent::TaalMainCrater;
    bool mEruptOnPlay = true;
    double mInitialPlumeSpeed = 150.0;
    double mInitialPlumeDensity = 200.0;
    double mVentRadius = 100.0;
    double mVentAltitude = 0.0;

    // Display Settings
    TerrainByYear mTerrainByYear = TerrainByYear::Year2023;
    bool mDisplayLandmarks = true;
    bool mDisplayInfoUI = true;
    bool mDisplayBillboards = true;
    bool mDisplayFreeSpheres = false;
    bool mDisplaySpheresWithSubspheres = false;
    bool mDisplayTorusLayers = false;

    // Playback
    ref<Texture> mpResetIcon;
    ref<Texture> mpPlayIcon;
    ref<Texture> mpPauseIcon;
    ref<Texture> mpStopIcon;
    SimulatorState mSimulatorState = SimulatorState::Stopped;

    // Plume Direction Tracker
    ref<Texture> mpTaalMinimap;
    bool mDisplayLayeredView = true;
    bool mDisplayWindVectors = true;

private:
    ref<Scene> mpScene;
    ref<Camera> mpCamera;
    ref<EnvMap> mpEnvMap;
    ref<ParticleSystem> mpParticles;

    ref<RenderGraph> mpRenderGraph;
    ref<RasterPass> mpRasterPass;

    ref<Program> mpRaytraceProgram;
    ref<RtProgramVars> mpRtVars;
    ref<Texture> mpRtOut;

    RenderMode mRenderMode = RenderMode::Raster;
    bool mUseDOF = false;

    uint32_t mSampleIndex = 0xdeadbeef;

    AssetResolver mAssetResolver;
};
