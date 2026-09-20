// MIT License
//
// Copyright(c) 2022 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include "factories.h"
#include "interfaces.h"
#include "log.h"
#include "eye_target_tracker.h"

namespace {

    using namespace toolkit;
    using namespace toolkit::config;
    using namespace toolkit::log;
    using namespace toolkit::graphics;
    using namespace toolkit::utilities;

    class FrameAnalyzer : public IFrameAnalyzer {
      public:
        FrameAnalyzer(std::shared_ptr<IConfigManager> configManager,
                      std::shared_ptr<IDevice> graphicsDevice,
                      uint32_t displayWidth,
                      uint32_t displayHeight,
                      FrameAnalyzerHeuristic heuristic)
            : m_configManager(configManager), m_device(graphicsDevice), m_displayWidth(displayWidth),
              m_displayHeight(displayHeight), m_forceHeuristic(heuristic) {
            if (m_device->getApi() == Api::D3D11) {
                m_lmuMode = std::clamp(m_configManager->getValue("lmu_eye_target_mode"), 0, 2);
            }
            if (m_lmuMode) {
                Log("LMU eye-target experiment v1: mode=%d (1=observe, 2=apply); restart to change mode\n", m_lmuMode);
            }
        }

        void registerColorSwapchainImage(XrSwapchain swapchain,
                                         std::shared_ptr<ITexture> source,
                                         Eye eye,
                                         bool wholeEye) override {
            m_eyeSwapchain[(int)eye].insert(swapchain);
            m_eyeSwapchainImages[(int)eye].insert(source->getNativePtr());
            if (m_lmuMode && wholeEye && source->getInfo().arraySize == 1) {
                m_lmuRoots[swapchain].emplace_back(source, eye);
                m_lmuTracker.registerEye(resourceId(source), toOwnership(eye));
                Log("LMU eye root: texture=%p eye=%d size=%ux%u samples=%u\n",
                    source->getNativePtr(), (int)eye, source->getInfo().width, source->getInfo().height,
                    source->getInfo().sampleCount);
            }
        }

        void unregisterColorSwapchain(XrSwapchain swapchain) override {
            if (!m_lmuMode) {
                return;
            }
            const auto found = m_lmuRoots.find(swapchain);
            if (found != m_lmuRoots.end()) {
                for (const auto& image : found->second) {
                    m_eyeSwapchainImages[(int)image.second].erase(image.first->getNativePtr());
                }
                m_lmuRoots.erase(found);
            }
            for (auto& swapchains : m_eyeSwapchain) {
                swapchains.erase(swapchain);
            }
            // Release retained images and discard all learned native identities on recreation.
            m_lmuCurrentTextures.clear();
            m_lmuPreviousTextures.clear();
            m_lmuTracker = EyeTargetTracker{};
            m_lmuHint.reset();
            for (const auto& entry : m_lmuRoots) {
                for (const auto& image : entry.second) {
                    m_lmuTracker.registerEye(resourceId(image.first), toOwnership(image.second));
                }
            }
        }

        bool requiresKnownEye() const override {
            return m_lmuMode == 2;
        }

        void resetForFrame() override {
            if (m_lmuMode) {
                m_lmuTracker.beginFrame();
                m_lmuCurrentTextures.clear();
                m_lmuHint.reset();
                m_lmuHits = {};
                m_lmuResolves = 0;
                ++m_lmuFrame;
            }
            m_hasSeenLeftEye = m_hasSeenRightEye = false;
            m_hasCopiedLeftEye = m_hasCopiedRightEye = false;

            m_eyePrediction = m_firstEye;
            m_isPredictionValid = m_shouldPredictEye;

            if (m_fallbackDelay) {
                m_fallbackDelay--;
            }
        }

        void prepareForEndFrame() override {
            if (m_lmuMode) {
                m_lmuTracker.endFrame();
                // Keep the resources behind every learned native pointer alive until the
                // next completed frame. This prevents pointer reuse from misidentifying an eye.
                m_lmuPreviousTextures = std::move(m_lmuCurrentTextures);
                if (m_lmuFrame <= 6 || (m_lmuFrame % 300) == 0) {
                    Log("LMU eye frame=%llu mode=%d copies=%zu resolves=%u learned=%zu bindL=%u bindR=%u unknown=%u overflow=%d\n",
                        (unsigned long long)m_lmuFrame, m_lmuMode, m_lmuTracker.transferCount(), m_lmuResolves,
                        m_lmuTracker.learnedCount(), m_lmuHits[0], m_lmuHits[1], m_lmuHits[2],
                        (int)m_lmuTracker.overflowed());
                }
            }
            if (m_heuristic == FrameAnalyzerHeuristic::Unknown) {
                if (m_hasSeenLeftEye && m_hasSeenRightEye &&
                    (m_forceHeuristic == FrameAnalyzerHeuristic::ForwardRender ||
                     m_forceHeuristic == FrameAnalyzerHeuristic::Unknown)) {
                    Log("Detected forward rendering\n");
                    m_heuristic = FrameAnalyzerHeuristic::ForwardRender;
                    m_firstEye = Eye::Left;
                } else if (m_hasCopiedLeftEye && m_hasCopiedRightEye &&
                           (m_forceHeuristic == FrameAnalyzerHeuristic::DeferredCopy ||
                            m_forceHeuristic == FrameAnalyzerHeuristic::Unknown)) {
                    Log("Detected deferred rendering with copy\n");
                    m_heuristic = FrameAnalyzerHeuristic::DeferredCopy;
                    m_firstEye = m_firstEyeCopy;
                } else if (!m_fallbackDelay && (m_forceHeuristic == FrameAnalyzerHeuristic::Fallback ||
                                                m_forceHeuristic == FrameAnalyzerHeuristic::Unknown)) {
                    Log("Fallback to swapchain acquisition\n");
                    m_heuristic = FrameAnalyzerHeuristic::Fallback;
                    m_firstEye = Eye::Left;
                }

                TraceLoggingWrite(
                    g_traceProvider, "FrameAnalyzer_TrySetHeuristic", TLArg((uint32_t)m_heuristic, "Heuristic"));

                m_shouldPredictEye = m_heuristic != FrameAnalyzerHeuristic::Unknown;
            }
        }

        void onSetRenderTarget(std::shared_ptr<graphics::IContext> context,
                               std::shared_ptr<ITexture> renderTarget) override {
            if (m_lmuMode) {
                m_lmuHint.reset();
                if (renderTarget->getInfo().arraySize == 1 && renderTarget->getInfo().mipCount == 1 &&
                    context->getAs<D3D11>()->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) {
                    const auto ownership = m_lmuTracker.eyeFor(resourceId(renderTarget));
                    if (ownership == EyeTargetTracker::Left || ownership == EyeTargetTracker::Right) {
                        m_lmuHint = ownership == EyeTargetTracker::Left ? Eye::Left : Eye::Right;
                    }
                }
                ++m_lmuHits[m_lmuHint ? (int)*m_lmuHint : 2];
                TraceLoggingWrite(g_traceProvider, "LMU_EyeTargetBind",
                                  TLPArg(renderTarget->getNativePtr(), "Texture"),
                                  TLArg(m_lmuHint ? (int)*m_lmuHint : -1, "LearnedEye"));
                if (m_lmuHint && m_lmuBindLogBudget) {
                    --m_lmuBindLogBudget;
                    Log("LMU eye bind: texture=%p eye=%d size=%ux%u samples=%u\n",
                        renderTarget->getNativePtr(), (int)*m_lmuHint, renderTarget->getInfo().width,
                        renderTarget->getInfo().height, renderTarget->getInfo().sampleCount);
                }
            }
            const auto& info = renderTarget->getInfo();
            if (info.arraySize != 1) {
                return;
            }

            const void* const nativePtr = renderTarget->getNativePtr();

            // Handle when the application uses the swapchain image directly.
            if (m_eyeSwapchainImages[0].find(nativePtr) != m_eyeSwapchainImages[0].cend()) {
                TraceLoggingWrite(g_traceProvider, "FrameAnalyzer_DetectedLeftEyeForwardRender");
                m_eyePrediction = Eye::Left;
                m_hasSeenLeftEye = true;
            } else if (m_eyeSwapchainImages[1].find(nativePtr) != m_eyeSwapchainImages[1].cend()) {
                TraceLoggingWrite(g_traceProvider, "FrameAnalyzer_DetectedRightEyeForwardRender");
                m_eyePrediction = Eye::Right;
                m_hasSeenRightEye = true;
            }
        }

        void onUnsetRenderTarget(std::shared_ptr<graphics::IContext> context) override {
        }

        void onCopyTexture(std::shared_ptr<ITexture> source,
                           std::shared_ptr<ITexture> destination,
                           int sourceSlice = -1,
                           int destinationSlice = -1,
                           bool wholeImage = false,
                           bool resolve = false) override {
            if (m_lmuMode) {
                m_lmuResolves += resolve;
                TraceLoggingWrite(g_traceProvider, "LMU_EyeTargetTransfer",
                                  TLPArg(source->getNativePtr(), "Source"),
                                  TLPArg(destination->getNativePtr(), "Destination"),
                                  TLArg(wholeImage, "WholeImage"), TLArg(resolve, "Resolve"));
                if (m_lmuTracker.transfer(resourceId(source), resourceId(destination), wholeImage)) {
                    m_lmuCurrentTextures[resourceId(source)] = source;
                    m_lmuCurrentTextures[resourceId(destination)] = destination;
                }
                if (m_lmuTransferLogBudget) {
                    --m_lmuTransferLogBudget;
                    Log("LMU eye transfer: %s source=%p destination=%p whole=%d src=%ux%u/%u dst=%ux%u/%u sub=%d:%d\n",
                        resolve ? "resolve" : "copy", source->getNativePtr(), destination->getNativePtr(),
                        (int)wholeImage, source->getInfo().width, source->getInfo().height, source->getInfo().sampleCount,
                        destination->getInfo().width, destination->getInfo().height,
                        destination->getInfo().sampleCount, sourceSlice, destinationSlice);
                }
            }
            // Observing resolves must not change the stock copy-order heuristic in mode 1.
            if (resolve) {
                return;
            }
            if (destination->getInfo().arraySize != 1) {
                return;
            }

            const void* const nativePtr = destination->getNativePtr();

            // Handle when the application copies the texture to the swapchain image mid-pass. This is what FS2020 does.
            if (m_eyeSwapchainImages[0].find(nativePtr) != m_eyeSwapchainImages[0].cend()) {
                TraceLoggingWrite(g_traceProvider, "FrameAnalyzer_DetectedLeftEyeCopyOut");

                if (!m_hasCopiedLeftEye && !m_hasCopiedRightEye) {
                    m_firstEyeCopy = Eye::Left;
                }

                // Switch to right eye now.
                m_eyePrediction = Eye::Right;
                m_hasCopiedLeftEye = true;
            } else if (m_eyeSwapchainImages[1].find(nativePtr) != m_eyeSwapchainImages[1].cend()) {
                TraceLoggingWrite(g_traceProvider, "FrameAnalyzer_DetectedRightEyeCopyOut");

                if (!m_hasCopiedLeftEye && !m_hasCopiedRightEye) {
                    m_firstEyeCopy = Eye::Right;
                }

                // Switch to left eye now.
                m_eyePrediction = Eye::Left;
                m_hasCopiedRightEye = true;
            }
        }

        void onAcquireSwapchain(XrSwapchain swapchain) override {
            // If we don't have a better heuristic, just use the swapchain acquisition order.
            if (m_eyeSwapchain[0].find(swapchain) != m_eyeSwapchain[0].cend()) {
                TraceLoggingWrite(g_traceProvider, "FrameAnalyzer_DetectedLeftEyeSwapchainAcquisition");
                if (m_heuristic == FrameAnalyzerHeuristic::Fallback) {
                    m_eyePrediction = Eye::Left;
                }
            } else if (m_eyeSwapchain[1].find(swapchain) != m_eyeSwapchain[1].cend()) {
                TraceLoggingWrite(g_traceProvider, "FrameAnalyzer_DetectedRightEyeSwapchainAcquisition");
                if (m_heuristic == FrameAnalyzerHeuristic::Fallback) {
                    m_eyePrediction = Eye::Right;
                }
            }
        }

        void onReleaseSwapchain(XrSwapchain swapchain) override {
            // If we don't have a better heuristic, just use the swapchain acquisition order.
            // Switch eye once a swapchain is released.
            if (m_eyeSwapchain[0].find(swapchain) != m_eyeSwapchain[0].cend()) {
                TraceLoggingWrite(g_traceProvider, "FrameAnalyzer_DetectedLeftEyeSwapchainRelease");
                if (m_heuristic == FrameAnalyzerHeuristic::Fallback) {
                    m_eyePrediction = Eye::Right;
                }
            } else if (m_eyeSwapchain[1].find(swapchain) != m_eyeSwapchain[1].cend()) {
                TraceLoggingWrite(g_traceProvider, "FrameAnalyzer_DetectedRightEyeSwapchainRelease");
                if (m_heuristic == FrameAnalyzerHeuristic::Fallback) {
                    m_eyePrediction = Eye::Left;
                }
            }
        }

        std::optional<Eye> getEyeHint() const override {
            if (requiresKnownEye()) {
                return m_lmuHint;
            }
            if (!m_isPredictionValid) {
                return std::nullopt;
            }
            return m_eyePrediction;
        }

        FrameAnalyzerHeuristic getCurrentHeuristic() const override {
            return m_heuristic;
        }

      private:
        static EyeTargetTracker::Resource resourceId(const std::shared_ptr<ITexture>& texture) {
            return reinterpret_cast<EyeTargetTracker::Resource>(texture->getNativePtr());
        }

        static EyeTargetTracker::Ownership toOwnership(Eye eye) {
            return eye == Eye::Left ? EyeTargetTracker::Left : EyeTargetTracker::Right;
        }

        int m_lmuMode{0};
        EyeTargetTracker m_lmuTracker;
        std::optional<Eye> m_lmuHint;
        std::map<XrSwapchain, std::vector<std::pair<std::shared_ptr<ITexture>, Eye>>> m_lmuRoots;
        std::map<EyeTargetTracker::Resource, std::shared_ptr<ITexture>> m_lmuCurrentTextures;
        std::map<EyeTargetTracker::Resource, std::shared_ptr<ITexture>> m_lmuPreviousTextures;
        uint64_t m_lmuFrame{0};
        std::array<uint32_t, 3> m_lmuHits{};
        uint32_t m_lmuResolves{0};
        uint32_t m_lmuTransferLogBudget{80};
        uint32_t m_lmuBindLogBudget{40};

        const std::shared_ptr<IConfigManager> m_configManager;
        const std::shared_ptr<IDevice> m_device;
        const uint32_t m_displayWidth;
        const uint32_t m_displayHeight;
        const FrameAnalyzerHeuristic m_forceHeuristic;

        std::set<const void*> m_eyeSwapchainImages[ViewCount];
        std::set<XrSwapchain> m_eyeSwapchain[ViewCount];

        bool m_hasSeenLeftEye{false};
        bool m_hasSeenRightEye{false};
        bool m_hasCopiedLeftEye{false};
        bool m_hasCopiedRightEye{false};
        Eye m_firstEyeCopy;
        FrameAnalyzerHeuristic m_heuristic{FrameAnalyzerHeuristic::Unknown};

        bool m_shouldPredictEye{false};
        bool m_isPredictionValid{false};
        Eye m_eyePrediction;
        Eye m_firstEye{Eye::Left};

        uint32_t m_fallbackDelay{100};
    };

} // namespace

namespace toolkit::graphics {
    std::shared_ptr<IFrameAnalyzer> CreateFrameAnalyzer(std::shared_ptr<IConfigManager> configManager,
                                                        std::shared_ptr<IDevice> graphicsDevice,
                                                        uint32_t displayWidth,
                                                        uint32_t displayHeight,
                                                        FrameAnalyzerHeuristic heuristic) {
        return std::make_shared<FrameAnalyzer>(configManager, graphicsDevice, displayWidth, displayHeight, heuristic);
    }

} // namespace toolkit::graphics
