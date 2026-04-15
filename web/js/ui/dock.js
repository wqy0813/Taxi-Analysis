import { state } from "../core/state.js";
import { hideSelectionBox, cancelRegionSelection } from "../map/regionSelection.js";
import { clearRegionOverlay } from "../map/map.js";
import { stopAllTaxiMode } from "../features/trajectory/trajectoryService.js";
import { resetDensityState } from "../features/density/densityStore.js";
import { renderInfoPanel, updateRegionStatus, updateModeStatus } from "../core/utils.js";

function setDockPanelState(name, open) {
    hideSelectionBox();
    if (state.selectingRegion) {
        cancelRegionSelection();
    }

    if (name !== "query") {
        clearRegionOverlay();
    }

    document.querySelectorAll(".tool-btn[data-panel]").forEach((button) => {
        const isActive = name !== null && button.dataset.panel === name;
        const isOpen = open !== null && button.dataset.panel === open;
        button.classList.toggle("active", isActive);
        button.setAttribute("aria-expanded", String(isOpen));
    });

    document.querySelectorAll(".tool-panel[data-panel]").forEach((panel) => {
        panel.classList.toggle("is-open", open !== null && panel.dataset.panel === open);
    });

    state.activeDockPanel = name;
    state.openDockPanel = open;

}

function activateDockPanel(name) {
    if (state.activeDockPanel === name) {
        const nextOpen = state.openDockPanel === name ? null : name;
        setDockPanelState(name, nextOpen);
        return;
    }

    setDockPanelState(name, name);
}

function clearDockSelection() {
    hideSelectionBox();
    if (state.selectingRegion) {
        cancelRegionSelection();
    }
    stopAllTaxiMode(true);
    state.region = null;
    clearRegionOverlay();
    resetDensityState();
    updateRegionStatus();
    updateModeStatus("地图");
    renderInfoPanel("trajectory-info", [], "等待查询");
    renderInfoPanel("region-query-info", [], "等待查询");
    renderInfoPanel("density-info", [], "等待查询");
    state.activeDockPanel = null;
    state.openDockPanel = null;
    document.querySelectorAll(".tool-btn[data-panel]").forEach((button) => {
        button.classList.remove("active");
        button.setAttribute("aria-expanded", "false");
    });
    document.querySelectorAll(".tool-panel[data-panel]").forEach((panel) => {
        panel.classList.remove("is-open");
    });
}

export {
    setDockPanelState,
    activateDockPanel,
    clearDockSelection
};
