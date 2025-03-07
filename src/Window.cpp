#include "Window.hpp"
#include "Compositor.hpp"
#include "render/decorations/CHyprDropShadowDecoration.hpp"
#include "render/decorations/CHyprGroupBarDecoration.hpp"
#include "render/decorations/CHyprBorderDecoration.hpp"

CWindow::CWindow() {
    m_vRealPosition.create(AVARTYPE_VECTOR, g_pConfigManager->getAnimationPropertyConfig("windowsIn"), (void*)this, AVARDAMAGE_ENTIRE);
    m_vRealSize.create(AVARTYPE_VECTOR, g_pConfigManager->getAnimationPropertyConfig("windowsIn"), (void*)this, AVARDAMAGE_ENTIRE);
    m_fBorderFadeAnimationProgress.create(AVARTYPE_FLOAT, g_pConfigManager->getAnimationPropertyConfig("border"), (void*)this, AVARDAMAGE_BORDER);
    m_fBorderAngleAnimationProgress.create(AVARTYPE_FLOAT, g_pConfigManager->getAnimationPropertyConfig("borderangle"), (void*)this, AVARDAMAGE_BORDER);
    m_fAlpha.create(AVARTYPE_FLOAT, g_pConfigManager->getAnimationPropertyConfig("fadeIn"), (void*)this, AVARDAMAGE_ENTIRE);
    m_fActiveInactiveAlpha.create(AVARTYPE_FLOAT, g_pConfigManager->getAnimationPropertyConfig("fadeSwitch"), (void*)this, AVARDAMAGE_ENTIRE);
    m_cRealShadowColor.create(AVARTYPE_COLOR, g_pConfigManager->getAnimationPropertyConfig("fadeShadow"), (void*)this, AVARDAMAGE_SHADOW);
    m_fDimPercent.create(AVARTYPE_FLOAT, g_pConfigManager->getAnimationPropertyConfig("fadeDim"), (void*)this, AVARDAMAGE_ENTIRE);

    addWindowDeco(std::make_unique<CHyprDropShadowDecoration>(this));
    addWindowDeco(std::make_unique<CHyprBorderDecoration>(this));
}

CWindow::~CWindow() {
    if (g_pCompositor->isWindowActive(this)) {
        g_pCompositor->m_pLastFocus  = nullptr;
        g_pCompositor->m_pLastWindow = nullptr;
    }

    if (!g_pHyprOpenGL)
        return;

    g_pHyprRenderer->makeEGLCurrent();
    std::erase_if(g_pHyprOpenGL->m_mWindowFramebuffers, [&](const auto& other) { return other.first == this; });
}

SWindowDecorationExtents CWindow::getFullWindowExtents() {
    if (m_bFadingOut)
        return m_eOriginalClosedExtents;

    const int BORDERSIZE = getRealBorderSize();

    if (m_sAdditionalConfigData.dimAround) {
        const auto PMONITOR = g_pCompositor->getMonitorFromID(m_iMonitorID);
        return {{m_vRealPosition.vec().x - PMONITOR->vecPosition.x, m_vRealPosition.vec().y - PMONITOR->vecPosition.y},
                {PMONITOR->vecSize.x - (m_vRealPosition.vec().x - PMONITOR->vecPosition.x), PMONITOR->vecSize.y - (m_vRealPosition.vec().y - PMONITOR->vecPosition.y)}};
    }

    SWindowDecorationExtents maxExtents = {{BORDERSIZE + 2, BORDERSIZE + 2}, {BORDERSIZE + 2, BORDERSIZE + 2}};

    const auto               EXTENTS = g_pDecorationPositioner->getWindowDecorationExtents(this);

    if (EXTENTS.topLeft.x > maxExtents.topLeft.x)
        maxExtents.topLeft.x = EXTENTS.topLeft.x;

    if (EXTENTS.topLeft.y > maxExtents.topLeft.y)
        maxExtents.topLeft.y = EXTENTS.topLeft.y;

    if (EXTENTS.bottomRight.x > maxExtents.bottomRight.x)
        maxExtents.bottomRight.x = EXTENTS.bottomRight.x;

    if (EXTENTS.bottomRight.y > maxExtents.bottomRight.y)
        maxExtents.bottomRight.y = EXTENTS.bottomRight.y;

    if (m_pWLSurface.exists() && !m_bIsX11) {
        CBox surfaceExtents = {0, 0, 0, 0};
        // TODO: this could be better, perhaps make a getFullWindowRegion?
        wlr_xdg_surface_for_each_popup_surface(
            m_uSurface.xdg,
            [](wlr_surface* surf, int sx, int sy, void* data) {
                CBox* pSurfaceExtents = (CBox*)data;
                if (sx < pSurfaceExtents->x)
                    pSurfaceExtents->x = sx;
                if (sy < pSurfaceExtents->y)
                    pSurfaceExtents->y = sy;
                if (sx + surf->current.width > pSurfaceExtents->width)
                    pSurfaceExtents->width = sx + surf->current.width - pSurfaceExtents->x;
                if (sy + surf->current.height > pSurfaceExtents->height)
                    pSurfaceExtents->height = sy + surf->current.height - pSurfaceExtents->y;
            },
            &surfaceExtents);

        if (-surfaceExtents.x > maxExtents.topLeft.x)
            maxExtents.topLeft.x = -surfaceExtents.x;

        if (-surfaceExtents.y > maxExtents.topLeft.y)
            maxExtents.topLeft.y = -surfaceExtents.y;

        if (surfaceExtents.x + surfaceExtents.width > m_pWLSurface.wlr()->current.width + maxExtents.bottomRight.x)
            maxExtents.bottomRight.x = surfaceExtents.x + surfaceExtents.width - m_pWLSurface.wlr()->current.width;

        if (surfaceExtents.y + surfaceExtents.height > m_pWLSurface.wlr()->current.height + maxExtents.bottomRight.y)
            maxExtents.bottomRight.y = surfaceExtents.y + surfaceExtents.height - m_pWLSurface.wlr()->current.height;
    }

    return maxExtents;
}

CBox CWindow::getFullWindowBoundingBox() {
    if (m_sAdditionalConfigData.dimAround) {
        const auto PMONITOR = g_pCompositor->getMonitorFromID(m_iMonitorID);
        return {PMONITOR->vecPosition.x, PMONITOR->vecPosition.y, PMONITOR->vecSize.x, PMONITOR->vecSize.y};
    }

    auto maxExtents = getFullWindowExtents();

    CBox finalBox = {m_vRealPosition.vec().x - maxExtents.topLeft.x, m_vRealPosition.vec().y - maxExtents.topLeft.y,
                     m_vRealSize.vec().x + maxExtents.topLeft.x + maxExtents.bottomRight.x, m_vRealSize.vec().y + maxExtents.topLeft.y + maxExtents.bottomRight.y};

    return finalBox;
}

CBox CWindow::getWindowIdealBoundingBoxIgnoreReserved() {

    const auto PMONITOR = g_pCompositor->getMonitorFromID(m_iMonitorID);

    auto       POS  = m_vPosition;
    auto       SIZE = m_vSize;

    if (m_bIsFullscreen) {
        POS  = PMONITOR->vecPosition;
        SIZE = PMONITOR->vecSize;

        return CBox{(int)POS.x, (int)POS.y, (int)SIZE.x, (int)SIZE.y};
    }

    if (DELTALESSTHAN(POS.y - PMONITOR->vecPosition.y, PMONITOR->vecReservedTopLeft.y, 1)) {
        POS.y = PMONITOR->vecPosition.y;
        SIZE.y += PMONITOR->vecReservedTopLeft.y;
    }
    if (DELTALESSTHAN(POS.x - PMONITOR->vecPosition.x, PMONITOR->vecReservedTopLeft.x, 1)) {
        POS.x = PMONITOR->vecPosition.x;
        SIZE.x += PMONITOR->vecReservedTopLeft.x;
    }
    if (DELTALESSTHAN(POS.x + SIZE.x - PMONITOR->vecPosition.x, PMONITOR->vecSize.x - PMONITOR->vecReservedBottomRight.x, 1)) {
        SIZE.x += PMONITOR->vecReservedBottomRight.x;
    }
    if (DELTALESSTHAN(POS.y + SIZE.y - PMONITOR->vecPosition.y, PMONITOR->vecSize.y - PMONITOR->vecReservedBottomRight.y, 1)) {
        SIZE.y += PMONITOR->vecReservedBottomRight.y;
    }

    return CBox{(int)POS.x, (int)POS.y, (int)SIZE.x, (int)SIZE.y};
}

CBox CWindow::getWindowInputBox() {
    const int BORDERSIZE = getRealBorderSize();

    if (m_sAdditionalConfigData.dimAround) {
        const auto PMONITOR = g_pCompositor->getMonitorFromID(m_iMonitorID);
        return {PMONITOR->vecPosition.x, PMONITOR->vecPosition.y, PMONITOR->vecSize.x, PMONITOR->vecSize.y};
    }

    SWindowDecorationExtents maxExtents = {{BORDERSIZE + 2, BORDERSIZE + 2}, {BORDERSIZE + 2, BORDERSIZE + 2}};

    const auto               EXTENTS = g_pDecorationPositioner->getWindowDecorationExtents(this, true);

    if (EXTENTS.topLeft.x > maxExtents.topLeft.x)
        maxExtents.topLeft.x = EXTENTS.topLeft.x;

    if (EXTENTS.topLeft.y > maxExtents.topLeft.y)
        maxExtents.topLeft.y = EXTENTS.topLeft.y;

    if (EXTENTS.bottomRight.x > maxExtents.bottomRight.x)
        maxExtents.bottomRight.x = EXTENTS.bottomRight.x;

    if (EXTENTS.bottomRight.y > maxExtents.bottomRight.y)
        maxExtents.bottomRight.y = EXTENTS.bottomRight.y;

    // Add extents to the real base BB and return
    CBox finalBox = {m_vRealPosition.vec().x - maxExtents.topLeft.x, m_vRealPosition.vec().y - maxExtents.topLeft.y,
                     m_vRealSize.vec().x + maxExtents.topLeft.x + maxExtents.bottomRight.x, m_vRealSize.vec().y + maxExtents.topLeft.y + maxExtents.bottomRight.y};

    return finalBox;
}

CBox CWindow::getWindowMainSurfaceBox() {
    return {m_vRealPosition.vec().x, m_vRealPosition.vec().y, m_vRealSize.vec().x, m_vRealSize.vec().y};
}

SWindowDecorationExtents CWindow::getFullWindowReservedArea() {
    return g_pDecorationPositioner->getWindowDecorationReserved(this);
}

void CWindow::updateWindowDecos() {
    bool recalc = false;

    if (!m_bIsMapped || isHidden())
        return;

    for (auto& wd : m_vDecosToRemove) {
        for (auto it = m_dWindowDecorations.begin(); it != m_dWindowDecorations.end(); it++) {
            if (it->get() == wd) {
                g_pDecorationPositioner->uncacheDecoration(it->get());
                it     = m_dWindowDecorations.erase(it);
                recalc = true;
                if (it == m_dWindowDecorations.end())
                    break;
            }
        }
    }

    g_pDecorationPositioner->onWindowUpdate(this);

    if (recalc)
        g_pLayoutManager->getCurrentLayout()->recalculateWindow(this);

    m_vDecosToRemove.clear();

    for (auto& wd : m_dWindowDecorations) {
        wd->updateWindow(this);
    }
}

void CWindow::addWindowDeco(std::unique_ptr<IHyprWindowDecoration> deco) {
    m_dWindowDecorations.emplace_back(std::move(deco));
    g_pDecorationPositioner->forceRecalcFor(this);
    updateWindowDecos();
    g_pLayoutManager->getCurrentLayout()->recalculateWindow(this);
}

void CWindow::removeWindowDeco(IHyprWindowDecoration* deco) {
    m_vDecosToRemove.push_back(deco);
    g_pDecorationPositioner->forceRecalcFor(this);
    updateWindowDecos();
    g_pLayoutManager->getCurrentLayout()->recalculateWindow(this);
}

void CWindow::uncacheWindowDecos() {
    for (auto& wd : m_dWindowDecorations) {
        g_pDecorationPositioner->uncacheDecoration(wd.get());
    }
}

bool CWindow::checkInputOnDecos(const eInputType type, const Vector2D& mouseCoords, std::any data) {
    if (type != INPUT_TYPE_DRAG_END && hasPopupAt(mouseCoords))
        return false;

    for (auto& wd : m_dWindowDecorations) {
        if (!(wd->getDecorationFlags() & DECORATION_ALLOWS_MOUSE_INPUT))
            continue;

        if (!g_pDecorationPositioner->getWindowDecorationBox(wd.get()).containsPoint(mouseCoords))
            continue;

        if (wd->onInputOnDeco(type, mouseCoords, data))
            return true;
    }

    return false;
}

pid_t CWindow::getPID() {
    pid_t PID = -1;
    if (!m_bIsX11) {

        if (!m_bIsMapped)
            return -1;

        wl_client_get_credentials(wl_resource_get_client(m_uSurface.xdg->resource), &PID, nullptr, nullptr);
    } else {
        if (!m_bIsMapped)
            return -1;

        PID = m_uSurface.xwayland->pid;
    }

    return PID;
}

IHyprWindowDecoration* CWindow::getDecorationByType(eDecorationType type) {
    for (auto& wd : m_dWindowDecorations) {
        if (wd->getDecorationType() == type)
            return wd.get();
    }

    return nullptr;
}

void CWindow::createToplevelHandle() {
    if (m_bIsX11 && (m_bX11DoesntWantBorders || m_iX11Type == 2))
        return; // don't create a toplevel

    m_phForeignToplevel = wlr_foreign_toplevel_handle_v1_create(g_pCompositor->m_sWLRToplevelMgr);

    wlr_foreign_toplevel_handle_v1_set_app_id(m_phForeignToplevel, g_pXWaylandManager->getAppIDClass(this).c_str());
    wlr_foreign_toplevel_handle_v1_output_enter(m_phForeignToplevel, g_pCompositor->getMonitorFromID(m_iMonitorID)->output);
    wlr_foreign_toplevel_handle_v1_set_title(m_phForeignToplevel, m_szTitle.c_str());
    wlr_foreign_toplevel_handle_v1_set_maximized(m_phForeignToplevel, false);
    wlr_foreign_toplevel_handle_v1_set_minimized(m_phForeignToplevel, false);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(m_phForeignToplevel, false);

    // handle events
    hyprListener_toplevelActivate.initCallback(
        &m_phForeignToplevel->events.request_activate, [&](void* owner, void* data) { g_pLayoutManager->getCurrentLayout()->requestFocusForWindow(this); }, this, "Toplevel");

    hyprListener_toplevelFullscreen.initCallback(
        &m_phForeignToplevel->events.request_fullscreen,
        [&](void* owner, void* data) {
            const auto EV = (wlr_foreign_toplevel_handle_v1_fullscreen_event*)data;

            g_pCompositor->setWindowFullscreen(this, EV->fullscreen, FULLSCREEN_FULL);
        },
        this, "Toplevel");

    hyprListener_toplevelClose.initCallback(
        &m_phForeignToplevel->events.request_close, [&](void* owner, void* data) { g_pCompositor->closeWindow(this); }, this, "Toplevel");

    m_iLastToplevelMonitorID = m_iMonitorID;
}

void CWindow::destroyToplevelHandle() {
    if (!m_phForeignToplevel)
        return;

    hyprListener_toplevelActivate.removeCallback();
    hyprListener_toplevelClose.removeCallback();
    hyprListener_toplevelFullscreen.removeCallback();

    wlr_foreign_toplevel_handle_v1_destroy(m_phForeignToplevel);
    m_phForeignToplevel = nullptr;
}

void CWindow::updateToplevel() {
    updateSurfaceOutputs();

    if (!m_phForeignToplevel)
        return;

    wlr_foreign_toplevel_handle_v1_set_title(m_phForeignToplevel, m_szTitle.c_str());
    wlr_foreign_toplevel_handle_v1_set_fullscreen(m_phForeignToplevel, m_bIsFullscreen);

    if (m_iLastToplevelMonitorID != m_iMonitorID) {
        if (const auto PMONITOR = g_pCompositor->getMonitorFromID(m_iLastToplevelMonitorID); PMONITOR && PMONITOR->m_bEnabled)
            wlr_foreign_toplevel_handle_v1_output_leave(m_phForeignToplevel, PMONITOR->output);
        wlr_foreign_toplevel_handle_v1_output_enter(m_phForeignToplevel, g_pCompositor->getMonitorFromID(m_iMonitorID)->output);

        m_iLastToplevelMonitorID = m_iMonitorID;
    }
}

void sendEnterIter(wlr_surface* pSurface, int x, int y, void* data) {
    const auto OUTPUT = (wlr_output*)data;
    wlr_surface_send_enter(pSurface, OUTPUT);
}

void sendLeaveIter(wlr_surface* pSurface, int x, int y, void* data) {
    const auto OUTPUT = (wlr_output*)data;
    wlr_surface_send_leave(pSurface, OUTPUT);
}

void CWindow::updateSurfaceOutputs() {
    if (m_iLastSurfaceMonitorID == m_iMonitorID || !m_bIsMapped || m_bHidden)
        return;

    const auto PLASTMONITOR = g_pCompositor->getMonitorFromID(m_iLastSurfaceMonitorID);

    m_iLastSurfaceMonitorID = m_iMonitorID;

    const auto PNEWMONITOR = g_pCompositor->getMonitorFromID(m_iMonitorID);

    if (PLASTMONITOR && PLASTMONITOR->m_bEnabled)
        wlr_surface_for_each_surface(m_pWLSurface.wlr(), sendLeaveIter, PLASTMONITOR->output);

    wlr_surface_for_each_surface(m_pWLSurface.wlr(), sendEnterIter, PNEWMONITOR->output);

    wlr_surface_for_each_surface(
        m_pWLSurface.wlr(),
        [](wlr_surface* surf, int x, int y, void* data) {
            const auto PMONITOR = g_pCompositor->getMonitorFromID(((CWindow*)data)->m_iMonitorID);
            g_pCompositor->setPreferredScaleForSurface(surf, PMONITOR ? PMONITOR->scale : 1.f);
            g_pCompositor->setPreferredTransformForSurface(surf, PMONITOR->transform);
        },
        this);
}

void CWindow::moveToWorkspace(int workspaceID) {
    if (m_iWorkspaceID == workspaceID)
        return;

    static auto* const PCLOSEONLASTSPECIAL = &g_pConfigManager->getConfigValuePtr("misc:close_special_on_empty")->intValue;

    const int          OLDWORKSPACE = m_iWorkspaceID;

    m_iWorkspaceID = workspaceID;

    const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(m_iWorkspaceID);

    updateSpecialRenderData();

    if (PWORKSPACE) {
        g_pEventManager->postEvent(SHyprIPCEvent{"movewindow", std::format("{:x},{}", (uintptr_t)this, PWORKSPACE->m_szName)});
        EMIT_HOOK_EVENT("moveWindow", (std::vector<void*>{this, PWORKSPACE}));
    }

    if (m_pSwallowed) {
        m_pSwallowed->moveToWorkspace(workspaceID);
        m_pSwallowed->m_iMonitorID = m_iMonitorID;
    }

    // update xwayland coords
    g_pXWaylandManager->setWindowSize(this, m_vRealSize.vec());

    if (g_pCompositor->isWorkspaceSpecial(OLDWORKSPACE) && g_pCompositor->getWindowsOnWorkspace(OLDWORKSPACE) == 0 && *PCLOSEONLASTSPECIAL) {
        const auto PWS = g_pCompositor->getWorkspaceByID(OLDWORKSPACE);

        if (PWS) {
            if (const auto PMONITOR = g_pCompositor->getMonitorFromID(PWS->m_iMonitorID); PMONITOR)
                PMONITOR->setSpecialWorkspace(nullptr);
        }
    }
}

CWindow* CWindow::X11TransientFor() {
    if (!m_bIsX11)
        return nullptr;

    if (!m_uSurface.xwayland->parent)
        return nullptr;

    auto PPARENT = g_pCompositor->getWindowFromSurface(m_uSurface.xwayland->parent->surface);

    while (g_pCompositor->windowValidMapped(PPARENT) && PPARENT->m_uSurface.xwayland->parent) {
        PPARENT = g_pCompositor->getWindowFromSurface(PPARENT->m_uSurface.xwayland->parent->surface);
    }

    if (!g_pCompositor->windowValidMapped(PPARENT))
        return nullptr;

    return PPARENT;
}

void CWindow::removeDecorationByType(eDecorationType type) {
    for (auto& wd : m_dWindowDecorations) {
        if (wd->getDecorationType() == type)
            m_vDecosToRemove.push_back(wd.get());
    }

    updateWindowDecos();
}

void unregisterVar(void* ptr) {
    ((CAnimatedVariable*)ptr)->unregister();
}

void CWindow::onUnmap() {
    static auto* const PCLOSEONLASTSPECIAL = &g_pConfigManager->getConfigValuePtr("misc:close_special_on_empty")->intValue;

    if (g_pCompositor->m_pLastWindow == this)
        g_pCompositor->m_pLastWindow = nullptr;

    m_vRealPosition.setCallbackOnEnd(unregisterVar);
    m_vRealSize.setCallbackOnEnd(unregisterVar);
    m_fBorderFadeAnimationProgress.setCallbackOnEnd(unregisterVar);
    m_fBorderAngleAnimationProgress.setCallbackOnEnd(unregisterVar);
    m_fActiveInactiveAlpha.setCallbackOnEnd(unregisterVar);
    m_fAlpha.setCallbackOnEnd(unregisterVar);
    m_cRealShadowColor.setCallbackOnEnd(unregisterVar);
    m_fDimPercent.setCallbackOnEnd(unregisterVar);

    m_vRealSize.setCallbackOnBegin(nullptr);

    std::erase_if(g_pCompositor->m_vWindowFocusHistory, [&](const auto& other) { return other == this; });

    m_pWLSurface.unassign();

    hyprListener_unmapWindow.removeCallback();

    if (*PCLOSEONLASTSPECIAL && g_pCompositor->getWindowsOnWorkspace(m_iWorkspaceID) == 0 && g_pCompositor->isWorkspaceSpecial(m_iWorkspaceID)) {
        const auto PMONITOR = g_pCompositor->getMonitorFromID(m_iMonitorID);
        if (PMONITOR && PMONITOR->specialWorkspaceID == m_iWorkspaceID)
            PMONITOR->setSpecialWorkspace(nullptr);
    }

    const auto PMONITOR = g_pCompositor->getMonitorFromID(m_iMonitorID);

    if (PMONITOR && PMONITOR->solitaryClient == this)
        PMONITOR->solitaryClient = nullptr;

    g_pCompositor->updateWorkspaceWindows(m_iWorkspaceID);
}

void CWindow::onMap() {

    m_pWLSurface.assign(g_pXWaylandManager->getWindowSurface(this));
    m_pWLSurface.m_pOwner = this;

    // JIC, reset the callbacks. If any are set, we'll make sure they are cleared so we don't accidentally unset them. (In case a window got remapped)
    m_vRealPosition.resetAllCallbacks();
    m_vRealSize.resetAllCallbacks();
    m_fBorderFadeAnimationProgress.resetAllCallbacks();
    m_fBorderAngleAnimationProgress.resetAllCallbacks();
    m_fActiveInactiveAlpha.resetAllCallbacks();
    m_fAlpha.resetAllCallbacks();
    m_cRealShadowColor.resetAllCallbacks();
    m_fDimPercent.resetAllCallbacks();

    m_vRealPosition.registerVar();
    m_vRealSize.registerVar();
    m_fBorderFadeAnimationProgress.registerVar();
    m_fBorderAngleAnimationProgress.registerVar();
    m_fActiveInactiveAlpha.registerVar();
    m_fAlpha.registerVar();
    m_cRealShadowColor.registerVar();
    m_fDimPercent.registerVar();

    m_fBorderAngleAnimationProgress.setCallbackOnEnd([&](void* ptr) { onBorderAngleAnimEnd(ptr); }, false);

    m_fBorderAngleAnimationProgress.setValueAndWarp(0.f);
    m_fBorderAngleAnimationProgress = 1.f;

    g_pCompositor->m_vWindowFocusHistory.push_back(this);

    hyprListener_unmapWindow.initCallback(m_bIsX11 ? &m_uSurface.xwayland->surface->events.unmap : &m_uSurface.xdg->surface->events.unmap, &Events::listener_unmapWindow, this,
                                          "CWindow");

    m_vReportedSize = m_vPendingReportedSize;
}

void CWindow::onBorderAngleAnimEnd(void* ptr) {
    const auto        PANIMVAR = (CAnimatedVariable*)ptr;

    const std::string STYLE = PANIMVAR->getConfig()->pValues->internalStyle;

    if (STYLE != "loop" || !PANIMVAR->getConfig()->pValues->internalEnabled)
        return;

    PANIMVAR->setCallbackOnEnd(nullptr); // we remove the callback here because otherwise setvalueandwarp will recurse this

    PANIMVAR->setValueAndWarp(0);
    *PANIMVAR = 1.f;

    PANIMVAR->setCallbackOnEnd([&](void* ptr) { onBorderAngleAnimEnd(ptr); }, false);
}

void CWindow::setHidden(bool hidden) {
    m_bHidden = hidden;

    if (hidden && g_pCompositor->m_pLastWindow == this) {
        g_pCompositor->m_pLastWindow = nullptr;
    }

    setSuspended(hidden);
}

bool CWindow::isHidden() {
    return m_bHidden;
}

void CWindow::applyDynamicRule(const SWindowRule& r) {
    if (r.szRule == "noblur") {
        m_sAdditionalConfigData.forceNoBlur = true;
    } else if (r.szRule == "noborder") {
        m_sAdditionalConfigData.forceNoBorder = true;
    } else if (r.szRule == "noshadow") {
        m_sAdditionalConfigData.forceNoShadow = true;
    } else if (r.szRule == "nodim") {
        m_sAdditionalConfigData.forceNoDim = true;
    } else if (r.szRule == "forcergbx") {
        m_sAdditionalConfigData.forceRGBX = true;
    } else if (r.szRule == "opaque") {
        if (!m_sAdditionalConfigData.forceOpaqueOverridden)
            m_sAdditionalConfigData.forceOpaque = true;
    } else if (r.szRule == "immediate") {
        m_sAdditionalConfigData.forceTearing = true;
    } else if (r.szRule == "nearestneighbor") {
        m_sAdditionalConfigData.nearestNeighbor = true;
    } else if (r.szRule.starts_with("rounding")) {
        try {
            m_sAdditionalConfigData.rounding = std::stoi(r.szRule.substr(r.szRule.find_first_of(' ') + 1));
        } catch (std::exception& e) { Debug::log(ERR, "Rounding rule \"{}\" failed with: {}", r.szRule, e.what()); }
    } else if (r.szRule.starts_with("bordersize")) {
        try {
            m_sAdditionalConfigData.borderSize = std::stoi(r.szRule.substr(r.szRule.find_first_of(' ') + 1));
        } catch (std::exception& e) { Debug::log(ERR, "Bordersize rule \"{}\" failed with: {}", r.szRule, e.what()); }
    } else if (r.szRule.starts_with("opacity")) {
        try {
            CVarList vars(r.szRule, 0, ' ');

            int      opacityIDX = 0;

            for (auto& r : vars) {
                if (r == "opacity")
                    continue;

                if (r == "override") {
                    if (opacityIDX == 1) {
                        m_sSpecialRenderData.alphaOverride         = true;
                        m_sSpecialRenderData.alphaInactiveOverride = true;
                    } else if (opacityIDX == 2)
                        m_sSpecialRenderData.alphaInactiveOverride = true;
                } else {
                    if (opacityIDX == 0) {
                        m_sSpecialRenderData.alpha         = std::stof(r);
                        m_sSpecialRenderData.alphaInactive = std::stof(r);
                    } else if (opacityIDX == 1) {
                        m_sSpecialRenderData.alphaInactive         = std::stof(r);
                        m_sSpecialRenderData.alphaInactiveOverride = false;
                    } else {
                        throw std::runtime_error("more than 2 alpha values");
                    }

                    opacityIDX++;
                }
            }
        } catch (std::exception& e) { Debug::log(ERR, "Opacity rule \"{}\" failed with: {}", r.szRule, e.what()); }
    } else if (r.szRule == "noanim") {
        m_sAdditionalConfigData.forceNoAnims = true;
    } else if (r.szRule.starts_with("animation")) {
        auto STYLE                             = r.szRule.substr(r.szRule.find_first_of(' ') + 1);
        m_sAdditionalConfigData.animationStyle = STYLE;
    } else if (r.szRule.starts_with("bordercolor")) {
        try {
            std::string colorPart = removeBeginEndSpacesTabs(r.szRule.substr(r.szRule.find_first_of(' ') + 1));

            if (colorPart.contains(' ')) {
                // we have a space, 2 values
                m_sSpecialRenderData.activeBorderColor   = configStringToInt(colorPart.substr(0, colorPart.find_first_of(' ')));
                m_sSpecialRenderData.inactiveBorderColor = configStringToInt(colorPart.substr(colorPart.find_first_of(' ') + 1));
            } else {
                m_sSpecialRenderData.activeBorderColor = configStringToInt(colorPart);
            }
        } catch (std::exception& e) { Debug::log(ERR, "BorderColor rule \"{}\" failed with: {}", r.szRule, e.what()); }
    } else if (r.szRule == "dimaround") {
        m_sAdditionalConfigData.dimAround = true;
    } else if (r.szRule == "keepaspectratio") {
        m_sAdditionalConfigData.keepAspectRatio = true;
    } else if (r.szRule.starts_with("xray")) {
        CVarList vars(r.szRule, 0, ' ');

        try {
            m_sAdditionalConfigData.xray = configStringToInt(vars[1]);
        } catch (...) {}
    } else if (r.szRule.starts_with("idleinhibit")) {
        auto IDLERULE = r.szRule.substr(r.szRule.find_first_of(' ') + 1);

        if (IDLERULE == "none")
            m_eIdleInhibitMode = IDLEINHIBIT_NONE;
        else if (IDLERULE == "always")
            m_eIdleInhibitMode = IDLEINHIBIT_ALWAYS;
        else if (IDLERULE == "focus")
            m_eIdleInhibitMode = IDLEINHIBIT_FOCUS;
        else if (IDLERULE == "fullscreen")
            m_eIdleInhibitMode = IDLEINHIBIT_FULLSCREEN;
        else
            Debug::log(ERR, "Rule idleinhibit: unknown mode {}", IDLERULE);
    }
}

void CWindow::updateDynamicRules() {
    m_sSpecialRenderData.activeBorderColor   = -1;
    m_sSpecialRenderData.inactiveBorderColor = -1;
    m_sSpecialRenderData.alpha               = 1.f;
    m_sSpecialRenderData.alphaInactive       = -1.f;
    m_sAdditionalConfigData.forceNoBlur      = false;
    m_sAdditionalConfigData.forceNoBorder    = false;
    m_sAdditionalConfigData.forceNoShadow    = false;
    m_sAdditionalConfigData.forceNoDim       = false;
    if (!m_sAdditionalConfigData.forceOpaqueOverridden)
        m_sAdditionalConfigData.forceOpaque = false;
    m_sAdditionalConfigData.forceNoAnims    = false;
    m_sAdditionalConfigData.animationStyle  = std::string("");
    m_sAdditionalConfigData.rounding        = -1;
    m_sAdditionalConfigData.dimAround       = false;
    m_sAdditionalConfigData.forceRGBX       = false;
    m_sAdditionalConfigData.borderSize      = -1;
    m_sAdditionalConfigData.keepAspectRatio = false;
    m_sAdditionalConfigData.xray            = -1;
    m_sAdditionalConfigData.forceTearing    = false;
    m_sAdditionalConfigData.nearestNeighbor = false;
    m_eIdleInhibitMode                      = IDLEINHIBIT_NONE;

    const auto WINDOWRULES = g_pConfigManager->getMatchingRules(this);
    for (auto& r : WINDOWRULES) {
        applyDynamicRule(r);
    }

    g_pLayoutManager->getCurrentLayout()->recalculateMonitor(m_iMonitorID);
}

// check if the point is "hidden" under a rounded corner of the window
// it is assumed that the point is within the real window box (m_vRealPosition, m_vRealSize)
// otherwise behaviour is undefined
bool CWindow::isInCurvedCorner(double x, double y) {
    const int ROUNDING = rounding();
    if (getRealBorderSize() >= ROUNDING)
        return false;

    // (x0, y0), (x0, y1), ... are the center point of rounding at each corner
    double x0 = m_vRealPosition.vec().x + ROUNDING;
    double y0 = m_vRealPosition.vec().y + ROUNDING;
    double x1 = m_vRealPosition.vec().x + m_vRealSize.vec().x - ROUNDING;
    double y1 = m_vRealPosition.vec().y + m_vRealSize.vec().y - ROUNDING;

    if (x < x0 && y < y0) {
        return Vector2D{x0, y0}.distance(Vector2D{x, y}) > (double)ROUNDING;
    }
    if (x > x1 && y < y0) {
        return Vector2D{x1, y0}.distance(Vector2D{x, y}) > (double)ROUNDING;
    }
    if (x < x0 && y > y1) {
        return Vector2D{x0, y1}.distance(Vector2D{x, y}) > (double)ROUNDING;
    }
    if (x > x1 && y > y1) {
        return Vector2D{x1, y1}.distance(Vector2D{x, y}) > (double)ROUNDING;
    }

    return false;
}

void findExtensionForVector2D(wlr_surface* surface, int x, int y, void* data) {
    const auto DATA = (SExtensionFindingData*)data;

    CBox       box = {DATA->origin.x + x, DATA->origin.y + y, surface->current.width, surface->current.height};

    if (box.containsPoint(DATA->vec))
        *DATA->found = surface;
}

// checks if the wayland window has a popup at pos
bool CWindow::hasPopupAt(const Vector2D& pos) {
    if (m_bIsX11)
        return false;

    wlr_surface*          resultSurf = nullptr;
    Vector2D              origin     = m_vRealPosition.vec();
    SExtensionFindingData data       = {origin, pos, &resultSurf};
    wlr_xdg_surface_for_each_popup_surface(m_uSurface.xdg, findExtensionForVector2D, &data);

    return resultSurf;
}

void CWindow::applyGroupRules() {
    if ((m_eGroupRules & GROUP_SET && m_bFirstMap) || m_eGroupRules & GROUP_SET_ALWAYS)
        createGroup();

    if (m_sGroupData.pNextWindow && ((m_eGroupRules & GROUP_LOCK && m_bFirstMap) || m_eGroupRules & GROUP_LOCK_ALWAYS))
        getGroupHead()->m_sGroupData.locked = true;
}

void CWindow::createGroup() {
    if (m_sGroupData.deny) {
        Debug::log(LOG, "createGroup: window:{:x},title:{} is denied as a group, ignored", (uintptr_t)this, this->m_szTitle);
        return;
    }

    if (!m_sGroupData.pNextWindow) {
        m_sGroupData.pNextWindow = this;
        m_sGroupData.head        = true;
        m_sGroupData.locked      = false;
        m_sGroupData.deny        = false;

        addWindowDeco(std::make_unique<CHyprGroupBarDecoration>(this));

        g_pLayoutManager->getCurrentLayout()->recalculateWindow(this);
        g_pCompositor->updateAllWindowsAnimatedDecorationValues();
    }
}

void CWindow::destroyGroup() {
    if (m_sGroupData.pNextWindow == this) {
        if (m_eGroupRules & GROUP_SET_ALWAYS) {
            Debug::log(LOG, "destoryGroup: window:{:x},title:{} has rule [group set always], ignored", (uintptr_t)this, this->m_szTitle);
            return;
        }
        m_sGroupData.pNextWindow = nullptr;
        updateWindowDecos();
        return;
    }

    CWindow*              curr = this;
    std::vector<CWindow*> members;
    do {
        const auto PLASTWIN                = curr;
        curr                               = curr->m_sGroupData.pNextWindow;
        PLASTWIN->m_sGroupData.pNextWindow = nullptr;
        curr->setHidden(false);
        members.push_back(curr);
    } while (curr != this);

    for (auto& w : members) {
        if (w->m_sGroupData.head)
            g_pLayoutManager->getCurrentLayout()->onWindowRemoved(curr);
        w->m_sGroupData.head = false;
    }

    const bool GROUPSLOCKEDPREV        = g_pKeybindManager->m_bGroupsLocked;
    g_pKeybindManager->m_bGroupsLocked = true;
    for (auto& w : members) {
        g_pLayoutManager->getCurrentLayout()->onWindowCreated(w);
        w->updateWindowDecos();
    }
    g_pKeybindManager->m_bGroupsLocked = GROUPSLOCKEDPREV;
}

CWindow* CWindow::getGroupHead() {
    CWindow* curr = this;
    while (!curr->m_sGroupData.head)
        curr = curr->m_sGroupData.pNextWindow;
    return curr;
}

CWindow* CWindow::getGroupTail() {
    CWindow* curr = this;
    while (!curr->m_sGroupData.pNextWindow->m_sGroupData.head)
        curr = curr->m_sGroupData.pNextWindow;
    return curr;
}

CWindow* CWindow::getGroupCurrent() {
    CWindow* curr = this;
    while (curr->isHidden())
        curr = curr->m_sGroupData.pNextWindow;
    return curr;
}

int CWindow::getGroupSize() {
    int      size = 1;
    CWindow* curr = this;
    while (curr->m_sGroupData.pNextWindow != this) {
        curr = curr->m_sGroupData.pNextWindow;
        size++;
    }
    return size;
}

bool CWindow::canBeGroupedInto(CWindow* pWindow) {
    return !g_pKeybindManager->m_bGroupsLocked                                          // global group lock disengaged
        && ((m_eGroupRules & GROUP_INVADE && m_bFirstMap)                               // window ignore local group locks, or
            || (!pWindow->getGroupHead()->m_sGroupData.locked                           //      target unlocked
                && !(m_sGroupData.pNextWindow && getGroupHead()->m_sGroupData.locked))) //      source unlocked or isn't group
        && !m_sGroupData.deny                                                           // source is not denied entry
        && !(m_eGroupRules & GROUP_BARRED && m_bFirstMap);                              // group rule doesn't prevent adding window
}

CWindow* CWindow::getGroupWindowByIndex(int index) {
    const int SIZE = getGroupSize();
    index          = ((index % SIZE) + SIZE) % SIZE;
    CWindow* curr  = getGroupHead();
    while (index > 0) {
        curr = curr->m_sGroupData.pNextWindow;
        index--;
    }
    return curr;
}

void CWindow::setGroupCurrent(CWindow* pWindow) {
    CWindow* curr     = this->m_sGroupData.pNextWindow;
    bool     isMember = false;
    while (curr != this) {
        if (curr == pWindow) {
            isMember = true;
            break;
        }
        curr = curr->m_sGroupData.pNextWindow;
    }

    if (!isMember && pWindow != this)
        return;

    const auto PCURRENT   = getGroupCurrent();
    const bool FULLSCREEN = PCURRENT->m_bIsFullscreen;
    const auto WORKSPACE  = g_pCompositor->getWorkspaceByID(PCURRENT->m_iWorkspaceID);

    const auto PWINDOWSIZE = PCURRENT->m_vRealSize.goalv();
    const auto PWINDOWPOS  = PCURRENT->m_vRealPosition.goalv();

    const auto CURRENTISFOCUS = PCURRENT == g_pCompositor->m_pLastWindow;

    if (FULLSCREEN)
        g_pCompositor->setWindowFullscreen(PCURRENT, false, WORKSPACE->m_efFullscreenMode);

    PCURRENT->setHidden(true);
    pWindow->setHidden(false); // can remove m_pLastWindow

    g_pLayoutManager->getCurrentLayout()->replaceWindowDataWith(PCURRENT, pWindow);

    if (PCURRENT->m_bIsFloating) {
        pWindow->m_vRealPosition.setValueAndWarp(PWINDOWPOS);
        pWindow->m_vRealSize.setValueAndWarp(PWINDOWSIZE);
    }

    g_pCompositor->updateAllWindowsAnimatedDecorationValues();

    if (CURRENTISFOCUS)
        g_pCompositor->focusWindow(pWindow);

    if (FULLSCREEN)
        g_pCompositor->setWindowFullscreen(pWindow, true, WORKSPACE->m_efFullscreenMode);

    g_pHyprRenderer->damageWindow(pWindow);

    pWindow->updateWindowDecos();
}

void CWindow::insertWindowToGroup(CWindow* pWindow) {
    const auto BEGINAT = this;
    const auto ENDAT   = m_sGroupData.pNextWindow;

    if (!pWindow->getDecorationByType(DECORATION_GROUPBAR))
        pWindow->addWindowDeco(std::make_unique<CHyprGroupBarDecoration>(pWindow));

    if (!pWindow->m_sGroupData.pNextWindow) {
        BEGINAT->m_sGroupData.pNextWindow = pWindow;
        pWindow->m_sGroupData.pNextWindow = ENDAT;
        pWindow->m_sGroupData.head        = false;
        return;
    }

    const auto SHEAD = pWindow->getGroupHead();
    const auto STAIL = pWindow->getGroupTail();

    SHEAD->m_sGroupData.head          = false;
    BEGINAT->m_sGroupData.pNextWindow = SHEAD;
    STAIL->m_sGroupData.pNextWindow   = ENDAT;
}

CWindow* CWindow::getGroupPrevious() {
    CWindow* curr = m_sGroupData.pNextWindow;

    while (curr != this && curr->m_sGroupData.pNextWindow != this)
        curr = curr->m_sGroupData.pNextWindow;

    return curr;
}

void CWindow::switchWithWindowInGroup(CWindow* pWindow) {
    if (!m_sGroupData.pNextWindow || !pWindow->m_sGroupData.pNextWindow)
        return;

    if (m_sGroupData.pNextWindow == pWindow) { // A -> this -> pWindow -> B >> A -> pWindow -> this -> B
        getGroupPrevious()->m_sGroupData.pNextWindow = pWindow;
        m_sGroupData.pNextWindow                     = pWindow->m_sGroupData.pNextWindow;
        pWindow->m_sGroupData.pNextWindow            = this;

    } else if (pWindow->m_sGroupData.pNextWindow == this) { // A -> pWindow -> this -> B >> A -> this -> pWindow -> B
        pWindow->getGroupPrevious()->m_sGroupData.pNextWindow = this;
        pWindow->m_sGroupData.pNextWindow                     = m_sGroupData.pNextWindow;
        m_sGroupData.pNextWindow                              = pWindow;

    } else { // A -> this -> B | C -> pWindow -> D >> A -> pWindow -> B | C -> this -> D
        std::swap(m_sGroupData.pNextWindow, pWindow->m_sGroupData.pNextWindow);
        std::swap(getGroupPrevious()->m_sGroupData.pNextWindow, pWindow->getGroupPrevious()->m_sGroupData.pNextWindow);
    }

    std::swap(m_sGroupData.head, pWindow->m_sGroupData.head);
    std::swap(m_sGroupData.locked, pWindow->m_sGroupData.locked);
}

void CWindow::updateGroupOutputs() {
    if (!m_sGroupData.pNextWindow)
        return;

    CWindow* curr = m_sGroupData.pNextWindow;

    while (curr != this) {
        curr->m_iMonitorID = m_iMonitorID;
        curr->moveToWorkspace(m_iWorkspaceID);

        curr->m_vRealPosition = m_vRealPosition.goalv();
        curr->m_vRealSize     = m_vRealSize.goalv();

        curr = curr->m_sGroupData.pNextWindow;
    }
}

Vector2D CWindow::middle() {
    return m_vRealPosition.goalv() + m_vRealSize.goalv() / 2.f;
}

bool CWindow::opaque() {
    if (m_fAlpha.fl() != 1.f || m_fActiveInactiveAlpha.fl() != 1.f)
        return false;

    const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(m_iWorkspaceID);

    if (m_pWLSurface.small() && !m_pWLSurface.m_bFillIgnoreSmall)
        return false;

    if (PWORKSPACE->m_fAlpha.fl() != 1.f)
        return false;

    if (m_bIsX11)
        return !m_uSurface.xwayland->has_alpha;

    if (m_uSurface.xdg->surface->opaque)
        return true;

    const auto EXTENTS = pixman_region32_extents(&m_uSurface.xdg->surface->opaque_region);
    if (EXTENTS->x2 - EXTENTS->x1 >= m_uSurface.xdg->surface->current.buffer_width && EXTENTS->y2 - EXTENTS->y1 >= m_uSurface.xdg->surface->current.buffer_height)
        return true;

    return false;
}

float CWindow::rounding() {
    static auto* const PROUNDING = &g_pConfigManager->getConfigValuePtr("decoration:rounding")->intValue;

    float              rounding = m_sAdditionalConfigData.rounding.toUnderlying() == -1 ? *PROUNDING : m_sAdditionalConfigData.rounding.toUnderlying();

    return m_sSpecialRenderData.rounding ? rounding : 0;
}

void CWindow::updateSpecialRenderData() {
    const auto PWORKSPACE    = g_pCompositor->getWorkspaceByID(m_iWorkspaceID);
    const auto WORKSPACERULE = PWORKSPACE ? g_pConfigManager->getWorkspaceRuleFor(PWORKSPACE) : SWorkspaceRule{};
    bool       border        = true;

    if (m_bIsFloating && g_pConfigManager->getConfigValuePtr("general:no_border_on_floating")->intValue == 1)
        border = false;

    m_sSpecialRenderData.border     = WORKSPACERULE.border.value_or(border);
    m_sSpecialRenderData.borderSize = WORKSPACERULE.borderSize.value_or(-1);
    m_sSpecialRenderData.decorate   = WORKSPACERULE.decorate.value_or(true);
    m_sSpecialRenderData.rounding   = WORKSPACERULE.rounding.value_or(true);
    m_sSpecialRenderData.shadow     = WORKSPACERULE.shadow.value_or(true);
}

int CWindow::getRealBorderSize() {
    if (!m_sSpecialRenderData.border || m_sAdditionalConfigData.forceNoBorder)
        return 0;

    if (m_sAdditionalConfigData.borderSize.toUnderlying() != -1)
        return m_sAdditionalConfigData.borderSize.toUnderlying();

    if (m_sSpecialRenderData.borderSize.toUnderlying() != -1)
        return m_sSpecialRenderData.borderSize.toUnderlying();

    return g_pConfigManager->getConfigValuePtr("general:border_size")->intValue;
}

bool CWindow::canBeTorn() {
    return (m_sAdditionalConfigData.forceTearing.toUnderlying() || m_bTearingHint) && g_pHyprRenderer->m_bTearingEnvSatisfied;
}

bool CWindow::shouldSendFullscreenState() {
    const auto MODE = g_pCompositor->getWorkspaceByID(m_iWorkspaceID)->m_efFullscreenMode;
    return m_bFakeFullscreenState || (m_bIsFullscreen && (MODE == FULLSCREEN_FULL));
}

void CWindow::setSuspended(bool suspend) {
    if (suspend == m_bSuspended)
        return;

    if (m_bIsX11)
        return;

    wlr_xdg_toplevel_set_suspended(m_uSurface.xdg->toplevel, suspend);
    m_bSuspended = suspend;
}
