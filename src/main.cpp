#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <fmt/format.h>
#include <deque>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr const char* CONTAINER_ID = "kola.cps-counter/labels";

    struct CPSState {
        std::deque<int64_t> clickTimesMs;
        int attemptClicks = 0;
        int levelClicks = 0;
        double maxAttemptCps = 0.0;
        double maxLevelCps = 0.0;

        static CPSState& get() {
            static CPSState inst;
            return inst;
        }

        void resetAttempt() {
            clickTimesMs.clear();
            attemptClicks = 0;
            maxAttemptCps = 0.0;
        }

        void resetLevel() {
            resetAttempt();
            levelClicks = 0;
            maxLevelCps = 0.0;
        }
    };

    int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    bool shouldCount(int button) {
        if (button <= 0 || button > 3) return false;
        if (Mod::get()->getSettingValue<std::string>("count-source") == "Jump only") {
            return button == 1;
        }
        return true;
    }

    double realtimeCps() {
        auto& state = CPSState::get();
        int64_t windowMs = Mod::get()->getSettingValue<int64_t>("window-ms");
        int64_t cutoff = nowMs() - windowMs;
        auto& deque = state.clickTimesMs;
        while (!deque.empty() && deque.front() < cutoff) deque.pop_front();
        return static_cast<double>(deque.size()) * 1000.0 / static_cast<double>(windowMs);
    }
}

class $modify(CPSPlayLayer, PlayLayer) {
    struct Fields {
        CCNode* container = nullptr;
        CCLabelBMFont* cpsLabel = nullptr;
        CCLabelBMFont* maxLabel = nullptr;
        CCLabelBMFont* totalLabel = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        CPSState::get().resetLevel();

        auto container = CCNode::create();
        container->setID(CONTAINER_ID);
        this->addChild(container, 1000);
        m_fields->container = container;

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        CPSState::get().resetAttempt();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& state = CPSState::get();
        double cps = realtimeCps();
        if (cps > state.maxAttemptCps) state.maxAttemptCps = cps;
        if (cps > state.maxLevelCps) state.maxLevelCps = cps;

        auto container = m_fields->container;
        if (!container) return;

        bool showRealtime = Mod::get()->getSettingValue<bool>("show-realtime");
        bool showMax = Mod::get()->getSettingValue<bool>("show-max");
        bool showTotal = Mod::get()->getSettingValue<bool>("show-total");
        bool totalPerAttempt = Mod::get()->getSettingValue<std::string>("total-mode") == "Attempt";
        bool maxPerAttempt = Mod::get()->getSettingValue<std::string>("max-mode") == "Attempt";

        double maxCps = maxPerAttempt ? state.maxAttemptCps : state.maxLevelCps;
        int totalClicks = totalPerAttempt ? state.attemptClicks : state.levelClicks;

        struct Line { std::string text; ccColor3B color; };
        std::vector<Line> lines;
        if (showRealtime) lines.push_back({fmt::format("CPS: {:.1f}", cps), {255, 255, 255}});
        if (showMax) lines.push_back({fmt::format("Max: {:.1f}", maxCps), {255, 160, 60}});
        if (showTotal) lines.push_back({fmt::format("Clicks: {}", totalClicks), {130, 255, 130}});

        float scale = static_cast<float>(Mod::get()->getSettingValue<double>("scale"));
        float offsetX = static_cast<float>(Mod::get()->getSettingValue<int64_t>("offset-x"));
        float offsetY = static_cast<float>(Mod::get()->getSettingValue<int64_t>("offset-y"));
        GLubyte opacity = static_cast<GLubyte>(Mod::get()->getSettingValue<int64_t>("opacity"));

        auto winSize = CCDirector::get()->getWinSize();
        container->setPosition({offsetX, winSize.height - offsetY});
        container->setScale(scale);

        const float lineH = 26.0f;
        for (size_t i = 0; i < lines.size(); ++i) {
            auto& fieldLabel = (i == 0) ? m_fields->cpsLabel
                             : (i == 1) ? m_fields->maxLabel
                                        : m_fields->totalLabel;
            auto label = fieldLabel;
            if (!label) {
                label = CCLabelBMFont::create("", "bigFont.fnt");
                label->setAnchorPoint({0.0f, 1.0f});
                container->addChild(label, 0, static_cast<int>(i));
                fieldLabel = label;
            }
            label->setString(lines[i].text.c_str());
            label->setColor(lines[i].color);
            label->setOpacity(opacity);
            label->setPosition({0.0f, -lineH * static_cast<float>(i)});
            label->setVisible(true);
        }
        // hide leftover labels when fewer lines are enabled
        if (lines.size() < 3 && m_fields->totalLabel) m_fields->totalLabel->setVisible(lines.size() > 2);
        if (lines.size() < 2 && m_fields->maxLabel) m_fields->maxLabel->setVisible(false);
        if (lines.empty() && m_fields->cpsLabel) m_fields->cpsLabel->setVisible(false);
    }
};

class $modify(CPSBaseGameLayer, GJBaseGameLayer) {
    void queueButton(int button, bool push, bool isPlayer2, double timestamp) {
        GJBaseGameLayer::queueButton(button, push, isPlayer2, timestamp);
        if (!push || !shouldCount(button)) return;

        auto& state = CPSState::get();
        state.clickTimesMs.push_back(nowMs());
        state.attemptClicks += 1;
        state.levelClicks += 1;
    }
};
