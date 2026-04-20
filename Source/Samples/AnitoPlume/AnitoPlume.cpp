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
#include "AnitoPlume.h"
#include "Utils/Math/FalcorMath.h"
#include "Utils/UI/TextRenderer.h"
#include "RenderGraph/RenderPassStandardFlags.h"
#include "Scene/SceneBuilder.h"

FALCOR_EXPORT_D3D12_AGILITY_SDK

static const float4 kClearColor(0.3f, 0.6f, 1.0f, 1);
static const std::string kDefaultScene = "AnitoPlume/Taal.pyscene";
static const Gui::WindowFlags kDefaultWindowFlags =
    Gui::WindowFlags::ShowTitleBar |
    Gui::WindowFlags::AllowMove |
    Gui::WindowFlags::NoResize |
    Gui::WindowFlags::CloseButton;

// GUI texture paths
static const std::string kResetIconPath = "AnitoPlume/TexturesUI/ResetIcon.png";
static const std::string kPlayIconPath = "AnitoPlume/TexturesUI/PlayIcon.png";
static const std::string kPauseIconPath = "AnitoPlume/TexturesUI/PauseIcon.png";
static const std::string kStopIconPath = "AnitoPlume/TexturesUI/StopIcon.png";
static const std::string kTaalMinimapPath = "AnitoPlume/TexturesUI/TaalMinimap.png";

const Gui::DropdownList kCameraControllerTypeList = {
    {(uint32_t)Scene::CameraControllerType::FirstPerson, "First Person"},
    {(uint32_t)Scene::CameraControllerType::Orbiter, "Orbiter"},
    {(uint32_t)Scene::CameraControllerType::SixDOF, "6-DOF"},
};

const Gui::DropdownList kEruptionVentList = {
    {(uint32_t)AnitoPlume::EruptionVent::TaalMainCrater, "Taal Main Crater"},
    {(uint32_t)AnitoPlume::EruptionVent::BinintiangMalaki, "Binintiang Malaki"},
    {(uint32_t)AnitoPlume::EruptionVent::BinintiangMunti, "Binintiang Munti"},
    {(uint32_t)AnitoPlume::EruptionVent::Pirapiraso, "Pirapiraso"},
    //{(uint32_t)AnitoPlume::EruptionVent::CaluitPoint, "Caluit Point"},
};

const Gui::DropdownList kTerrainByYearList = {
    {(uint32_t)AnitoPlume::TerrainByYear::Year2023, "2023"},
    {(uint32_t)AnitoPlume::TerrainByYear::Year2021, "2021"},
    {(uint32_t)AnitoPlume::TerrainByYear::Year2019, "2019"},
    {(uint32_t)AnitoPlume::TerrainByYear::Year2015, "2015"},
};

AnitoPlume::AnitoPlume(const SampleAppConfig& config) : SampleApp(config)
{
    //
}

AnitoPlume::~AnitoPlume()
{
    //
}

void AnitoPlume::onLoad(RenderContext* pRenderContext)
{
    if (getDevice()->isFeatureSupported(Device::SupportedFeatures::Raytracing) == false)
    {
        FALCOR_THROW("Device does not support raytracing!");
    }

    // Load all render pass plugins (PathTracer, GBuffer, etc.)
    PluginManager::instance().loadAllPlugins();
    mAssetResolver = AssetResolver::getDefaultResolver();

    // Load any .py render graph from Source/Mogwai/Data/
    std::filesystem::path scriptPath = mAssetResolver.resolvePath("AnitoPlume/scripts/PathTracer.py", AssetCategory::Scene);
    mpRenderGraph = RenderGraph::createFromFile(getDevice(), scriptPath);

    if (mpRenderGraph == nullptr)
    {
        FALCOR_THROW("Failed to load render graph from file.");
    }

    mpResetIcon = createGUITexture(kResetIconPath);
    mpPlayIcon = createGUITexture(kPlayIconPath);
    mpPauseIcon = createGUITexture(kPauseIconPath);
    mpStopIcon = createGUITexture(kStopIconPath);
    mpTaalMinimap = createGUITexture(kTaalMinimapPath);

    mpParticles = ParticleSystem::create(getDevice());

    loadScene(kDefaultScene, getTargetFbo().get());
    getDevice()->getProfiler()->setEnabled(true);
}

void AnitoPlume::onShutdown()
{
    //
}

void AnitoPlume::onResize(uint32_t width, uint32_t height)
{
    float h = (float)height;
    float widget = (float)width;

    if (mpCamera)
    {
        mpCamera->setFocalLength(18);
        float aspectRatio = (widget / h);
        mpCamera->setAspectRatio(aspectRatio);
    }

    mpRenderGraph->onResize(getTargetFbo().get());

    mpRtOut = getDevice()->createTexture2D(
        width, height, ResourceFormat::RGBA16Float, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );

    if (mpScene)
        mpScene->setCameraAspectRatio((float)width / (float)height);
}

void AnitoPlume::onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    pRenderContext->clearFbo(pTargetFbo.get(), kClearColor, 1.0f, 0, FboAttachmentType::All);

    if (mpScene)
    {
        IScene::UpdateFlags updates = mpScene->update(pRenderContext, getGlobalClock().getTime());
        if (is_set(updates, IScene::UpdateFlags::GeometryChanged))
            FALCOR_THROW("This sample does not support scene geometry changes.");
        if (is_set(updates, IScene::UpdateFlags::RecompileNeeded))
            FALCOR_THROW("This sample does not support scene changes that require shader recompilation.");

        switch (mRenderMode)
        {
        case AnitoPlume::RenderMode::Raster:
            renderRaster(pRenderContext, pTargetFbo);
            break;
        case AnitoPlume::RenderMode::RayTrace:
            renderRT(pRenderContext, pTargetFbo);
            break;
        case AnitoPlume::RenderMode::Graph:
            renderGraph(pRenderContext, pTargetFbo, updates);
            break;
        default:
            renderRaster(pRenderContext, pTargetFbo);
            break;
        }

    }

    //getTextRenderer().render(pRenderContext, getFrameRate().getMsg(), pTargetFbo, {1680, 1020});
}

void AnitoPlume::onGuiRender(Gui* pGui)
{
    renderMainMenuBar(pGui);
    renderSimulatorInput(pGui);
    renderPlayback(pGui);
    renderCameraSettings(pGui);
    renderDisplaySettings(pGui);
    renderPlumeDirectionTracker(pGui);
    renderProfiler(pGui);

    //renderGlobalUI(pGui);
}

bool AnitoPlume::onKeyEvent(const KeyboardEvent& keyEvent)
{
    if (keyEvent.key == Input::Key::Key1 && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        mRenderMode = RenderMode::Raster;
        return true;
    }

    if (keyEvent.key == Input::Key::Key2 && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        mRenderMode = RenderMode::RayTrace;
        return true;
    }

    if (keyEvent.key == Input::Key::Key3 && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        mRenderMode = RenderMode::Graph;
        return true;
    }

    if (mpScene && mpScene->onKeyEvent(keyEvent))
        return true;

    return false;
}

bool AnitoPlume::onMouseEvent(const MouseEvent& mouseEvent)
{
    return mpScene && mpScene->onMouseEvent(mouseEvent);
}

void AnitoPlume::onHotReload(HotReloadFlags reloaded)
{
    if (mpRenderGraph)
        mpRenderGraph->onHotReload(reloaded);
}

void AnitoPlume::loadScene(const std::filesystem::path& path, const Fbo* pTargetFbo)
{
    mpScene = Scene::create(getDevice(), path);
    // The sample doesn't support dynamic geometry changes, even on load.
    //mpScene->addCustomPrimitive(0, AABB(float3(-0.5f), float3(0.5f)));

    mpRenderGraph->setScene(mpScene);
    mpRenderGraph->onResize(pTargetFbo);

    mpCamera = mpScene->getCamera();
    mpEnvMap = mpScene->getEnvMap();

    // Update the controllers
    float radius = mpScene->getSceneBounds().radius();
    mpScene->setCameraSpeed(radius * 0.25f);
    float nearZ = std::max(0.1f, radius / 750.0f);
    float farZ = radius * 10;
    mpCamera->setDepthRange(nearZ, farZ);
    mpCamera->setAspectRatio((float)pTargetFbo->getWidth() / (float)pTargetFbo->getHeight());

    // Get shader modules and type conformances for types used by the scene.
    // These need to be set on the program in order to use Falcor's material system.
    auto shaderModules = mpScene->getShaderModules();
    auto typeConformances = mpScene->getTypeConformances();

    // Get scene defines. These need to be set on any program using the scene.
    auto defines = mpScene->getSceneDefines();

    // Create raster pass.
    // This utility wraps the creation of the program and vars, and sets the necessary scene defines.
    ProgramDesc rasterProgDesc;
    rasterProgDesc.addShaderModules(shaderModules);
    rasterProgDesc.addShaderLibrary("Samples/AnitoPlume/AnitoPlume.3d.slang").vsEntry("vsMain").psEntry("psMain");
    rasterProgDesc.addTypeConformances(typeConformances);

    mpRasterPass = RasterPass::create(getDevice(), rasterProgDesc, defines);

    // We'll now create a raytracing program. To do that we need to setup two things:
    // - A program description (ProgramDesc). This holds all shader entry points, compiler flags, macro defintions,
    // etc.
    // - A binding table (RtBindingTable). This maps shaders to geometries in the scene, and sets the ray generation and
    // miss shaders.
    //
    // After setting up these, we can create the Program and associated RtProgramVars that holds the variable/resource
    // bindings. The Program can be reused for different scenes, but RtProgramVars needs to binding table which is
    // Scene-specific and needs to be re-created when switching scene. In this example, we re-create both the program
    // and vars when a scene is loaded.

    ProgramDesc rtProgDesc;
    rtProgDesc.addShaderModules(shaderModules);
    rtProgDesc.addShaderLibrary("Samples/AnitoPlume/AnitoPlume.rt.slang");
    rtProgDesc.addTypeConformances(typeConformances);
    rtProgDesc.setMaxTraceRecursionDepth(3); // 1 for calling TraceRay from RayGen, 1 for calling it from the
                                             // primary-ray ClosestHit shader for reflections, 1 for reflection ray
                                             // tracing a shadow ray
    rtProgDesc.setMaxPayloadSize(24);        // The largest ray payload struct (PrimaryRayData) is 24 bytes. The payload size
                                             // should be set as small as possible for maximum performance.

    ref<RtBindingTable> sbt = RtBindingTable::create(2, 2, mpScene->getGeometryCount());
    sbt->setRayGen(rtProgDesc.addRayGen("rayGen"));
    sbt->setMiss(0, rtProgDesc.addMiss("primaryMiss"));
    sbt->setMiss(1, rtProgDesc.addMiss("shadowMiss"));
    auto primary = rtProgDesc.addHitGroup("primaryClosestHit", "primaryAnyHit");
    auto shadow = rtProgDesc.addHitGroup("", "shadowAnyHit");
    sbt->setHitGroup(0, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), primary);
    sbt->setHitGroup(1, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), shadow);

    mpRaytraceProgram = Program::create(getDevice(), rtProgDesc, defines);
    mpRtVars = RtProgramVars::create(getDevice(), mpRaytraceProgram, sbt);
}

void AnitoPlume::setPerFrameVars(const Fbo* pTargetFbo)
{
    auto var = mpRtVars->getRootVar();
    var["PerFrameCB"]["invView"] = inverse(mpCamera->getViewMatrix());
    var["PerFrameCB"]["viewportDims"] = float2(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    float fovY = focalLengthToFovY(mpCamera->getFocalLength(), Camera::kDefaultFrameHeight);
    var["PerFrameCB"]["tanHalfFovY"] = std::tan(fovY * 0.5f);
    var["PerFrameCB"]["sampleIndex"] = mSampleIndex++;
    var["PerFrameCB"]["useDOF"] = mUseDOF;
    var["gOutput"] = mpRtOut;
}

void AnitoPlume::renderRaster(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    FALCOR_ASSERT(mpScene);
    FALCOR_PROFILE(pRenderContext, "renderRaster");

    mpRasterPass->getState()->setFbo(pTargetFbo);
    mpScene->rasterize(pRenderContext, mpRasterPass->getState().get(), mpRasterPass->getVars().get());
    //mpParticles->simulate(pRenderContext, getGlobalClock().getDelta());
    //mpParticles->render(pRenderContext,pTargetFbo, mpCamera);
}

void AnitoPlume::renderRT(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    FALCOR_ASSERT(mpScene);
    FALCOR_PROFILE(pRenderContext, "renderRT");

    setPerFrameVars(pTargetFbo.get());

    pRenderContext->clearUAV(mpRtOut->getUAV().get(), kClearColor);
    mpScene->raytrace(pRenderContext, mpRaytraceProgram.get(), mpRtVars, uint3(pTargetFbo->getWidth(), pTargetFbo->getHeight(), 1));
    pRenderContext->blit(mpRtOut->getSRV(), pTargetFbo->getRenderTargetView(0));
}

void AnitoPlume::renderGraph(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo, IScene::UpdateFlags updates)
{
    FALCOR_ASSERT(mpScene);
    FALCOR_PROFILE(pRenderContext, "renderGraph");

    mpRenderGraph->compile(pRenderContext);

    // Notify active graph of any scene updates.
    mpRenderGraph->onSceneUpdates(pRenderContext, updates);

    // Execute graph.
    mpRenderGraph->getPassesDictionary()[kRenderPassRefreshFlags] = RenderPassRefreshFlags::None;
    mpRenderGraph->execute(pRenderContext);

    // Blit main graph output to frame buffer.
    ref<Texture> pOutTex = mpRenderGraph->getOutput(mpRenderGraph->getOutputName(0))->asTexture();
    FALCOR_ASSERT(pOutTex);
    pRenderContext->blit(pOutTex->getSRV(), pTargetFbo->getRenderTargetView(0));

}

ref<Texture> AnitoPlume::createGUITexture(const std::filesystem::path& path)
{
    std::filesystem::path resolvedPath = mAssetResolver.resolvePath(path, AssetCategory::Scene);
    return Texture::createFromFile(getDevice(), resolvedPath.string(), true, false);
}

#pragma region GUI

void AnitoPlume::renderMainMenuBar(Gui* pGui)
{
    Gui::MainMenu mainMenu(pGui);
    mainMenu.dropdown("File");
    mainMenu.item("New");
    mainMenu.dropdown("Edit");
    mainMenu.dropdown("View");
    mainMenu.dropdown("Window");
    mainMenu.dropdown("Help");
    mainMenu.dropdown("About");
}

void AnitoPlume::renderSimulatorInput(Gui* pGui)
{
    Gui::Window widget(pGui, "Simulator Input", {400, 530}, {10, 20}, kDefaultWindowFlags);

    // TODO: Implement simulator input
    if (auto windSettings = widget.group("Wind Settings", true))
    {
        widget.rect({480, 338});
        widget.dummy("##WindSettingsStart", {0, 4});
        widget.indent(10);

        widget.graph("##Intensity", AnitoPlume::windIntensityGraphCallback, this, 5, 0, 0.0f, 200.0f);
        widget.graph("##Angle", AnitoPlume::windIntensityGraphCallback, this, 5, 0, 0.0f, 360.0f);
        widget.slider("Altitude", mAltitude, 0.0, 10000.0, false, "%.0f m");
        widget.button("No wind");
        widget.button("Linear wind", true);
        widget.button("Max intensity", true);
        widget.checkbox("Use all angles", mUseAllAngles, true);
        widget.slider("Linear wind speed", mLinearWindSpeed, 0.0, 80.0, false, "%.2f m/s");
        widget.button("Set 2020 eruption winds");

        widget.indent(-10);
        widget.dummy("##WindSettingsEnd", {0, 8});
    }

    if (auto eruptionParameters = widget.group("Eruption Parameters", true))
    {
        widget.rect({480, 220});
        widget.dummy("##EruptionParametersStart", {0, 4});
        widget.indent(10);

        widget.button("Enable all vents");
        widget.button("Enable selected vent only", true);
        EruptionVent vent = mEruptionVent;
        if (widget.dropdown("Eruption Vent", kEruptionVentList, reinterpret_cast<uint32_t&>(vent)))
            mEruptionVent = vent;
        widget.separator();
        widget.checkbox("Erupt on play", mEruptOnPlay);
        widget.button("Reset to default", true);
        widget.slider("Initial Plume Speed", mInitialPlumeSpeed, 0.0, 200.0, false, "%.2f m/s");
        widget.slider("Initial Plume Density", mInitialPlumeDensity, 0.0, 400.0, false, "%.2f kg/m3");
        widget.slider("Vent Radius", mVentRadius, 0.0, 1000.0, false, "%.2f m");
        widget.slider("Vent Altitude", mVentAltitude, 0.0, 1000.0, false, "%.2f m");

        widget.indent(-10);
        widget.dummy("##EruptionParametersEnd", {0, 4});
    }

}

void AnitoPlume::renderPlayback(Gui* pGui)
{
    Gui::Window widget(pGui, "Playback", {300, 100}, {600, 20}, kDefaultWindowFlags);

    double timeScale = getGlobalClock().getTimeScale();
    widget.indent(40);
    if (widget.slider("Time scale", timeScale, 0.1, 10.0, false, "%.2f"))
        getGlobalClock().setTimeScale(timeScale);

    widget.indent(50);
    Falcor::float2 buttonSize = {40, 40};
    if (mpResetIcon != nullptr && widget.imageButton("Reset", mpResetIcon.get(), buttonSize, true, false))
    {
        // TODO: Implement reset functionality
    }

    if (mSimulatorState != SimulatorState::Playing &&
        mpPlayIcon != nullptr &&
        widget.imageButton("Play", mpPlayIcon.get(), buttonSize, true, true))
    {
        mSimulatorState = SimulatorState::Playing;
        // TODO: Implement play functionality
    }

    if (mSimulatorState == SimulatorState::Playing &&
        mpPauseIcon != nullptr &&
        widget.imageButton("Pause", mpPauseIcon.get(), buttonSize, true, true))
    {
        mSimulatorState = SimulatorState::Paused;
        // TODO: Implement pause functionality
    }

    if (mSimulatorState != SimulatorState::Stopped &&
        mpStopIcon != nullptr &&
        widget.imageButton("Stop", mpStopIcon.get(), buttonSize, true, true))
    {
        mSimulatorState = SimulatorState::Stopped;
        // TODO: Implement stop functionality
    }
}

void AnitoPlume::renderCameraSettings(Gui* pGui)
{
    Gui::Window widget(pGui, "Camera Settings", {300, 150}, {1230, 20}, kDefaultWindowFlags);

    auto camera = mpScene->getCamera();

    auto cameraControllerType = mpScene->getCameraControllerType();
    if (widget.dropdown("Camera Controller", kCameraControllerTypeList, reinterpret_cast<uint32_t&>(cameraControllerType)))
        mpScene->setCameraController(cameraControllerType);

    float mCameraSpeed = mpScene->getCameraSpeed();
    if (widget.var("Camera Speed", mCameraSpeed, 0.f, std::numeric_limits<float>::max(), 0.01f, false, "%.2f"))
        mpScene->setCameraSpeed(mCameraSpeed);

    float3 pos = camera->getPosition();
    if (widget.var("Position", pos, -FLT_MAX, FLT_MAX, 0.001f, false, "%.2f"))
        camera->setPosition(pos);

    float3 target = camera->getTarget();
    if (widget.var("Target", target, -FLT_MAX, FLT_MAX, 0.001f, false, "%.2f"))
        camera->setTarget(target);

    if (auto cameraGroup = widget.group("More Settings"))
    {
        float focalLength = camera->getFocalLength();
        if (widget.var("Focal Length", focalLength, 0.0f, FLT_MAX, 0.25f))
            camera->setFocalLength(focalLength);

        float aspectRatio = camera->getAspectRatio();
        if (widget.var("Aspect Ratio", aspectRatio, 0.f, FLT_MAX, 0.001f))
            camera->setAspectRatio(aspectRatio);

        float focalDistance = camera->getFocalDistance();
        if (widget.var("Focal Distance", focalDistance, 0.f, FLT_MAX, 0.05f))
            camera->setFocalDistance(focalDistance);

        float apertureRadius = camera->getApertureRadius();
        if (widget.var("Aperture Radius", apertureRadius, 0.f, FLT_MAX, 0.001f))
            camera->setApertureRadius(apertureRadius);

        float shutterSpeed = camera->getShutterSpeed();
        if (widget.var("Shutter Speed", shutterSpeed, 0.f, FLT_MAX, 0.001f))
            camera->setShutterSpeed(shutterSpeed);

        float ISOSpeed = camera->getISOSpeed();
        if (widget.var("ISO Speed", ISOSpeed, 0.8f, FLT_MAX, 0.25f))
            camera->setISOSpeed(ISOSpeed);
        
        float2 depth = float2(camera->getNearPlane(), camera->getFarPlane());
        if (widget.var("Depth Range", depth, 0.f, FLT_MAX, 0.1f))
            camera->setDepthRange(depth.x, depth.y);

        float3 up = camera->getUpVector();
        if (widget.var("Up", up, -FLT_MAX, FLT_MAX, 0.001f, false, "%.4f"))
            camera->setUpVector(up);
    }

}

void AnitoPlume::renderDisplaySettings(Gui* pGui)
{
    Gui::Window widget(pGui, "Display Settings", {300, 200}, {1230, 200}, kDefaultWindowFlags);

    // TODO: Implement functionality
    auto terrainByYear = mTerrainByYear;
    if (widget.dropdown("Terrain By Year", kTerrainByYearList, reinterpret_cast<uint32_t&>(terrainByYear)))
    {
        mTerrainByYear = terrainByYear;
    }

    if (widget.checkbox("Display landmarks", mDisplayLandmarks)) {};
    if (widget.checkbox("Display info UI", mDisplayInfoUI)) {};
    if (widget.checkbox("Display billboards", mDisplayBillboards)) {};
    if (widget.checkbox("Display free spheres", mDisplayFreeSpheres)) {};
    if (widget.checkbox("Display spheres with subspheres", mDisplaySpheresWithSubspheres)) {};
    if (widget.checkbox("Display torus layers", mDisplayTorusLayers)) {};
}

void AnitoPlume::renderPlumeDirectionTracker(Gui* pGui)
{
    Gui::Window widget(pGui, "Plume Direction Tracker", {500, 300}, {1030, 530}, kDefaultWindowFlags);

    if (mpTaalMinimap)
        widget.image("##Taal Minimap", mpTaalMinimap.get(), {300, 300});

    widget.rect({290, 300}, {1.0f, 1.0f, 1.0f, 1.0f}, false, true);
    widget.text("Affected Areas");

    if (widget.checkbox("Display layered view", mDisplayLayeredView))
    {

    }

    if (widget.checkbox("Display wind vectors", mDisplayWindVectors, true))
    {

    }
}

void AnitoPlume::renderProfiler(Gui* pGui)
{
    Gui::Window widget(pGui, "Profiler", {500, 200}, {10, 620}, kDefaultWindowFlags);

    // TODO: Implement  the profiler
    widget.text(getFrameRate().getMsg());
}

float AnitoPlume::windIntensityGraphCallback(void*, int32_t index)
{

    return 0.0f;
}

float AnitoPlume::windAngleGraphCallback(void*, int32_t index)
{
    return 0.0f;
}

#pragma endregion

int runMain(int argc, char** argv)
{
    SampleAppConfig config;
    config.windowDesc.title = "AnitoPlume V2";
    config.windowDesc.resizableWindow = true;

    AnitoPlume project(config);
    return project.run();
}

int main(int argc, char** argv)
{
    return catchAndReportAllExceptions([&]() { return runMain(argc, argv); });
}
