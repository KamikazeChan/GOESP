#include "ESP.h"

#include "imgui/imgui.h"

#include "Config.h"
#include "Interfaces.h"
#include "Memory.h"
#include "SDK/Engine.h"
#include "SDK/Entity.h"
#include "SDK/EntityList.h"
#include "SDK/GlobalVars.h"
#include "SDK/Utils.h"
#include "SDK/Vector.h"

static constexpr bool worldToScreen(const Vector& in, ImVec2& out) noexcept
{
    const auto matrix = memory.viewMatrix;
    
    float w = matrix->_41 * in.x + matrix->_42 * in.y + matrix->_43 * in.z + matrix->_44;

    if (w > 0.001f) {
        const auto [width, height] = interfaces.engine->getScreenSize();
        out.x = width / 2 * (1 + (matrix->_11 * in.x + matrix->_12 * in.y + matrix->_13 * in.z + matrix->_14) / w);
        out.y = height / 2 * (1 - (matrix->_21 * in.x + matrix->_22 * in.y + matrix->_23 * in.z + matrix->_24) / w);
        return true;
    }
    return false;
}

struct BoundingBox {
    ImVec2 min, max;
    ImVec2 vertices[8];
};

static auto boundingBox(Entity* entity, BoundingBox& out) noexcept
{
    const auto [width, height] = interfaces.engine->getScreenSize();

    out.min.x = static_cast<float>(width * 2);
    out.min.y = static_cast<float>(height * 2);
    out.max.x = -static_cast<float>(width * 2);
    out.max.y = -static_cast<float>(height * 2);

    const auto mins = entity->getCollideable()->obbMins();
    const auto maxs = entity->getCollideable()->obbMaxs();

    for (int i = 0; i < 8; i++) {
        const Vector point{ i & 1 ? maxs.x : mins.x,
                            i & 2 ? maxs.y : mins.y,
                            i & 4 ? maxs.z : mins.z };

        if (!worldToScreen(point.transform(entity->coordinateFrame()), out.vertices[i]))
            return false;

        if (out.min.x > out.vertices[i].x)
            out.min.x = out.vertices[i].x;

        if (out.min.y > out.vertices[i].y)
            out.min.y = out.vertices[i].y;

        if (out.max.x < out.vertices[i].x)
            out.max.x = out.vertices[i].x;

        if (out.max.y < out.vertices[i].y)
            out.max.y = out.vertices[i].y;
    }
    return true;
}

static void renderBox(ImDrawList* drawList, Entity* entity, const BoundingBox& bbox, const Config::Shared& config) noexcept
{
    if (config.box.enabled) {
        ImU32 color = config.box.rainbow ? ImGui::ColorConvertFloat4ToU32(rainbowColor(memory.globalVars->realtime, config.box.rainbowSpeed)) : ImGui::ColorConvertFloat4ToU32(config.box.color);

        switch (config.boxType) {
        case 0:
            drawList->AddRect(bbox.min, bbox.max, color);
            break;
           
        case 1:
            drawList->AddLine(bbox.min, { bbox.min.x, bbox.min.y + (bbox.max.y - bbox.min.y) * 0.25f }, color);
            drawList->AddLine(bbox.min, { bbox.min.x + (bbox.max.x - bbox.min.x) * 0.25f, bbox.min.y }, color);

            drawList->AddLine({ bbox.max.x, bbox.min.y }, { bbox.max.x - (bbox.max.x - bbox.min.x) * 0.25f, bbox.min.y }, color);
            drawList->AddLine({ bbox.max.x, bbox.min.y }, { bbox.max.x, bbox.min.y + (bbox.max.y - bbox.min.y) * 0.25f }, color);

            drawList->AddLine({ bbox.min.x, bbox.max.y }, { bbox.min.x, bbox.max.y - (bbox.max.y - bbox.min.y) * 0.25f }, color);
            drawList->AddLine({ bbox.min.x, bbox.max.y }, { bbox.min.x + (bbox.max.x - bbox.min.x) * 0.25f, bbox.max.y}, color);

            drawList->AddLine(bbox.max, { bbox.max.x - (bbox.max.x - bbox.min.x) * 0.25f, bbox.max.y }, color);
            drawList->AddLine(bbox.max, { bbox.max.x, bbox.max.y - (bbox.max.y - bbox.min.y) * 0.25f }, color);
            break;
        case 2:
            for (int i = 0; i < 8; i++) {
                for (int j = 1; j <= 4; j <<= 1) {
                    if (!(i & j))
                        drawList->AddLine(bbox.vertices[i], bbox.vertices[i + j], color);
                }
            }
            break;
        case 3:
            for (int i = 0; i < 8; i++) {
                for (int j = 1; j <= 4; j <<= 1) {
                    if (!(i & j)) {
                        drawList->AddLine(bbox.vertices[i], { bbox.vertices[i].x + (bbox.vertices[i + j].x - bbox.vertices[i].x) * 0.25f, bbox.vertices[i].y + (bbox.vertices[i + j].y - bbox.vertices[i].y) * 0.25f }, color);
                        drawList->AddLine({ bbox.vertices[i].x + (bbox.vertices[i + j].x - bbox.vertices[i].x) * 0.75f, bbox.vertices[i].y + (bbox.vertices[i + j].y - bbox.vertices[i].y) * 0.75f }, bbox.vertices[i + j], color);
                    }
                }
            }
            break;
        }
    }
}

static void renderPlayerBox(ImDrawList* drawList, Entity* entity, const Config::Player& config) noexcept
{
    if (BoundingBox bbox; boundingBox(entity, bbox)) {
        renderBox(drawList, entity, bbox, config);
    }
}

enum EspId {
    ALLIES_ALL = 0,
    ALLIES_VISIBLE,
    ALLIES_OCCLUDED,

    ENEMIES_ALL,
    ENEMIES_VISIBLE,
    ENEMIES_OCCLUDED
};

static constexpr bool renderPlayerEsp(ImDrawList* drawList, Entity* entity, EspId id) noexcept
{
    if (config.players[id].enabled) {
        renderPlayerBox(drawList, entity, config.players[id]);
    }
    return config.players[id].enabled;
}

void ESP::render(ImDrawList* drawList) noexcept
{
    if (interfaces.engine->isInGame()) {
        const auto localPlayer = interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer());

        if (!localPlayer)
            return;

        for (int i = 1; i <= interfaces.engine->getMaxClients(); i++) {
            const auto entity = interfaces.entityList->getEntity(i);
            if (!entity || entity == localPlayer || entity->isDormant() || !entity->isAlive())
                continue;

            if (!entity->isEnemy()) {
                if (!renderPlayerEsp(drawList, entity, ALLIES_ALL)) {
                    if (entity->isVisible())
                        renderPlayerEsp(drawList, entity, ALLIES_VISIBLE);
                    else
                        renderPlayerEsp(drawList, entity, ALLIES_OCCLUDED);
                }
            } else if (!renderPlayerEsp(drawList, entity, ENEMIES_ALL)) {
                if (entity->isVisible())
                    renderPlayerEsp(drawList, entity, ENEMIES_VISIBLE);
                else
                    renderPlayerEsp(drawList, entity, ENEMIES_OCCLUDED);
            }
        }
    }
}
